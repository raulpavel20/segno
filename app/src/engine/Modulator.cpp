#include "Modulator.h"

#include "DevicePlugin.h"
#include "SpecParameter.h"

#include <algorithm>
#include <cmath>

namespace fd::engine
{
using namespace model::modulator;
using model::ModulatorKind;
namespace mod = dsp::mod;

const juce::Identifier Modulator::stateType{"FDMODULATOR"};
const juce::Identifier Modulator::copyId{"fdCopy"};
const juce::Identifier Modulator::placeId{"fdBefore"};
const juce::Identifier Modulator::keptRollId{"fdKeptRoll"};
const juce::Identifier Modulator::listenId{"fdListen"};
const juce::Identifier Modulator::rackChainId{"fdRackChain"};
namespace
{
const juce::Identifier kCurveId{"fdCurve"}; // the LFO's drawn shape: {"shape": …} or {"points": […]}
} // namespace

namespace
{
const juce::Identifier kTypeId{"fdType"};
const juce::Identifier kKeptLapId{"fdKeptLap"};
const juce::Identifier kStepsId{"fdSteps"};
const juce::Identifier kChancesId{"fdChances"};

juce::Identifier propertyOf(const ParamSpec& spec)
{
    return juce::String(std::string(spec.id));
}

std::vector<float> floatsOf(const juce::var& v, size_t count, float fallback)
{
    std::vector<float> out(count, fallback);
    juce::StringArray parts;
    parts.addTokens(v.toString(), " ", "");
    for (int i = 0; i < parts.size() && static_cast<size_t>(i) < count; ++i)
        out[static_cast<size_t>(i)] = parts[i].getFloatValue();
    return out;
}

juce::String textOf(const std::vector<float>& values)
{
    juce::StringArray parts;
    for (const float v : values)
        parts.add(juce::String(v, 4));
    return parts.joinIntoString(" ");
}

/// The default Steps pattern: a gentle rise and fall, every step sure.
float defaultStep(int i)
{
    return static_cast<float>(std::sin(i * 0.39) * 0.6);
}

std::uint64_t freshRoll()
{
    return static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64());
}
} // namespace

// ---- the type ---------------------------------------------------------------------------------

void Modulator::registerType()
{
    te::ModifierList::isCustomType = [](const juce::Identifier& type) { return type == stateType; };
    te::ModifierList::createCustom = [](te::Edit& edit, const juce::ValueTree& v) -> te::Modifier*
    { return kindOf(v).has_value() ? new Modulator(edit, v) : nullptr; };
}

juce::ValueTree Modulator::createState(ModulatorKind kind)
{
    juce::ValueTree v(stateType);
    v.setProperty(kTypeId, juce::String(std::string(model::modulatorInfo(kind).id)), nullptr);
    return v;
}

std::optional<ModulatorKind> Modulator::kindOf(const juce::ValueTree& v)
{
    if (!v.hasType(stateType))
        return std::nullopt;
    return model::modulatorKind(v[kTypeId].toString().toStdString());
}

// ---- the clock --------------------------------------------------------------------------------

/// Before each block (tracktion's modifier timers): the song's clock for the block, the
/// modulator's own parameters following their automation and modulation, the loop's laps.
struct Modulator::Timer final : te::ModifierTimer
{
    explicit Timer(Modulator& m) : owner(m) {}
    void updateStreamTime(te::TimePosition editTime, int numSamples) override
    {
        tempo.set(editTime);
        owner.blockBeats_.store(tempo.getBeats().inBeats(), std::memory_order_relaxed);
        owner.beatsPerSecond_.store(tempo.getTempo() / 60.0, std::memory_order_relaxed);
        const auto sig = tempo.getTimeSignature();
        owner.beatsPerBar_.store(sig.denominator > 0 ? sig.numerator * 4.0 / sig.denominator : 4.0,
                                 std::memory_order_relaxed);
        owner.tick(editTime, numSamples);
    }
    Modulator& owner;
    te::tempo::Sequence::Position tempo{te::createPosition(owner.edit.tempoSequence)};
};

// ---- life -------------------------------------------------------------------------------------

Modulator::Modulator(te::Edit& e, const juce::ValueTree& v)
    : te::Modifier(e, v), kind_(kindOf(v).value_or(ModulatorKind::Lfo))
{
    auto* um = &edit.getUndoManager();
    for (const auto& spec : info().params)
    {
        // Every value stored explicitly (its default too), so undo restores a value rather than
        // removing one, as a module's (DevicePlugin).
        const auto id = propertyOf(spec);
        if (!state.hasProperty(id))
            state.setProperty(id, spec.toNormalized(spec.def), nullptr);
        auto value = std::make_unique<juce::CachedValue<float>>();
        value->referTo(state, id, um, spec.toNormalized(spec.def));
        auto* param = new SpecParameter(spec, *this);
        addAutomatableParameter(*param);
        param->attachToCurrentValue(*value);
        params_.push_back(param);
        values_.push_back(std::move(value));
    }
    for (auto& f : fired_)
        f.store(-1.0);
    for (auto& r : released_)
        r.store(-1.0);
    roll_.store(freshRoll());
    refreshFromState();
    state.addListener(this);
}

Modulator::~Modulator()
{
    state.removeListener(this);
    notifyListenersOfDeletion();
    if (timer_ != nullptr)
        edit.removeModifierTimer(*timer_);
    for (auto* p : getAutomatableParameters())
        p->detachFromCurrentValue();
    deleteAutomatableParameters();
}

void Modulator::initialise()
{
    // Here, not in the constructor: the audio code may use the parameters before it has finished.
    timer_ = std::make_unique<Timer>(*this);
    edit.addModifierTimer(*timer_);
    restoreChangedParametersFromState();
}

void Modulator::initialise(double sampleRate, int)
{
    sampleRate_.store(sampleRate > 0.0 ? sampleRate : 48000.0);
    for (auto& f : lowCut_)
        f.reset();
    for (auto& f : highCut_)
        f.reset();
    lastLow_ = lastHigh_ = -1.0f;
    followed_ = 0.0;
}

juce::String Modulator::getName() const
{
    return juce::String(std::string(model::modulatorInfo(kind_).name));
}

te::AutomatableParameter& Modulator::parameter(size_t index)
{
    return *params_[index];
}

float Modulator::plain(size_t index) const
{
    return info().params[index].fromNormalized(values_[index]->get());
}

float Modulator::value(size_t index) const noexcept
{
    return info().params[index].fromNormalized(params_[index]->getCurrentValue());
}

void Modulator::followStoredValues()
{
    for (size_t i = 0; i < params_.size(); ++i)
        if (params_[i]->getCurrentBaseValue() != values_[i]->get())
            params_[i]->updateFromAttachedValue();
}

// ---- place, routes, rolls, steps ------------------------------------------------------------

te::EditItemID Modulator::place() const
{
    return te::EditItemID::fromVar(state[placeId]);
}

void Modulator::setPlace(te::EditItemID plugin, juce::UndoManager* um)
{
    if (plugin.isValid())
        state.setProperty(placeId, plugin.toVar(), um);
    else
        state.removeProperty(placeId, um);
}

te::EditItemID Modulator::getPluginToProcessBefore()
{
    return place();
}

te::EditItemID Modulator::getSidechainSourceID() const
{
    return kind_ == ModulatorKind::Follower ? te::EditItemID::fromVar(state[listenId]) : te::EditItemID();
}

void Modulator::setRouteCount(int routes) noexcept
{
    routes_.store(std::clamp(routes, 1, maxCopies), std::memory_order_relaxed);
}

void Modulator::startPass(std::uint64_t roll) noexcept
{
    roll_.store(roll, std::memory_order_relaxed);
}

std::uint64_t Modulator::roll() const noexcept
{
    return kept_.load(std::memory_order_relaxed) ? keptRoll_.load(std::memory_order_relaxed)
                                                 : roll_.load(std::memory_order_relaxed);
}

bool Modulator::kept() const
{
    return state.hasProperty(keptRollId);
}

void Modulator::setKept(bool keep, juce::UndoManager* um)
{
    if (keep)
    {
        // The roll playing now, and the lap the loop is on: what you heard.
        state.setProperty(keptRollId, juce::String(static_cast<juce::int64>(roll_.load())), um);
        state.setProperty(kKeptLapId, lap_.load(), um);
    }
    else
    {
        state.removeProperty(keptRollId, um);
        state.removeProperty(kKeptLapId, um);
    }
}

std::vector<float> Modulator::stepValues() const
{
    std::vector<float> out(Steps::maxSteps);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = steps_[i].load(std::memory_order_relaxed);
    return out;
}

std::vector<float> Modulator::stepChances() const
{
    std::vector<float> out(Steps::maxSteps);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = chances_[i].load(std::memory_order_relaxed);
    return out;
}

void Modulator::setStep(int step, float value, float chance, juce::UndoManager* um)
{
    if (step < 0 || step >= Steps::maxSteps)
        return;
    auto values = stepValues();
    auto chances = stepChances();
    values[static_cast<size_t>(step)] = std::clamp(value, -1.0f, 1.0f);
    chances[static_cast<size_t>(step)] = std::clamp(chance, 0.0f, 1.0f);
    state.setProperty(kStepsId, textOf(values), um);
    state.setProperty(kChancesId, textOf(chances), um);
}

dsp::DrawnCurve Modulator::drawnCurve() const
{
    dsp::DrawnCurve c;
    const auto json = nlohmann::json::parse(state[kCurveId].toString().toStdString(), nullptr, false);
    if (!json.is_object())
        return c;
    if (const auto it = json.find("points"); it != json.end() && it->is_array())
        for (const auto& p : *it)
            if (p.is_array() && p.size() == 3)
                c.points.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
    c.shape = json.value("shape", std::string("sine"));
    return c;
}

void Modulator::setDrawnCurve(const dsp::DrawnCurve& curve, juce::UndoManager* um)
{
    if (curve.points.empty() && curve.shape == "sine")
    {
        state.removeProperty(kCurveId, um); // as it starts
        return;
    }
    nlohmann::json json = nlohmann::json::object();
    if (curve.points.empty())
        json["shape"] = curve.shape;
    else
        json["points"] = curve.points;
    state.setProperty(kCurveId, juce::String(json.dump()), um);
}

bool Modulator::patternSet() const
{
    return state.hasProperty(kStepsId) || state.hasProperty(kChancesId);
}

void Modulator::setPattern(const std::vector<float>& values, const std::vector<float>& chances,
                           juce::UndoManager* um)
{
    if (values.empty())
    {
        state.removeProperty(kStepsId, um);
        state.removeProperty(kChancesId, um);
        return;
    }
    std::vector<float> v(Steps::maxSteps, 0.0f), c(Steps::maxSteps, 1.0f);
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i < values.size())
            v[i] = std::clamp(values[i], -1.0f, 1.0f);
        if (i < chances.size())
            c[i] = std::clamp(chances[i], 0.0f, 1.0f);
    }
    state.setProperty(kStepsId, textOf(v), um);
    state.setProperty(kChancesId, textOf(c), um);
}

void Modulator::refreshFromState()
{
    // The drawn shape, into the table not shown, then shown (§12.1).
    if (kind_ == ModulatorKind::Lfo)
    {
        const int next = 1 - drawnShown_.load(std::memory_order_relaxed);
        drawn_[static_cast<size_t>(next)] = drawnCurve().table();
        drawnShown_.store(next, std::memory_order_release);
    }
    kept_.store(state.hasProperty(keptRollId));
    keptRoll_.store(static_cast<std::uint64_t>(state[keptRollId].toString().getLargeIntValue()));
    keptLap_.store(static_cast<int>(state.getProperty(kKeptLapId, 0)));
    std::vector<float> values(Steps::maxSteps), chances(Steps::maxSteps, 1.0f);
    for (int i = 0; i < Steps::maxSteps; ++i)
        values[static_cast<size_t>(i)] = defaultStep(i);
    if (state.hasProperty(kStepsId))
        values = floatsOf(state[kStepsId], Steps::maxSteps, 0.0f);
    if (state.hasProperty(kChancesId))
        chances = floatsOf(state[kChancesId], Steps::maxSteps, 1.0f);
    for (size_t i = 0; i < values.size(); ++i)
    {
        steps_[i].store(values[i], std::memory_order_relaxed);
        chances_[i].store(chances[i], std::memory_order_relaxed);
    }
}

void Modulator::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == state &&
        (id == keptRollId || id == kKeptLapId || id == kStepsId || id == kChancesId || id == kCurveId))
        refreshFromState();
}

// ---- the panel's view -------------------------------------------------------------------------

std::vector<double> Modulator::playing() const
{
    const auto shape = envelopeShape();
    const double now = blockTime_.load(std::memory_order_relaxed);
    std::vector<double> out;
    for (int c = 0; c < routeCount(); ++c)
    {
        const double f = fired_[static_cast<size_t>(c)].load(std::memory_order_relaxed);
        const double r = released_[static_cast<size_t>(c)].load(std::memory_order_relaxed);
        const double ms = (now - f) * 1000.0;
        out.push_back(f < 0.0 || mod::envelopeDone(shape, ms, r < 0.0 ? -1.0 : (r - f) * 1000.0) ? -1.0 : ms);
    }
    return out;
}

std::vector<float> Modulator::heard(double seconds) const
{
    return getValues(te::TimeDuration::fromSeconds(seconds));
}

// ---- values -----------------------------------------------------------------------------------

double Modulator::beatsAt(double seconds) const noexcept
{
    return blockBeats_.load(std::memory_order_relaxed) +
           (seconds - blockTime_.load(std::memory_order_relaxed)) *
               beatsPerSecond_.load(std::memory_order_relaxed);
}

double Modulator::lfoCycles(double seconds) const noexcept
{
    const int mode = static_cast<int>(value(Lfo::Mode));
    if (mode == 0) // Free: locked to the song when synced; restarted with playback when not
        return stepsAt(seconds, Lfo::Rate, Lfo::Sync, passStart_.load(std::memory_order_relaxed), 0.0);
    const int sync = static_cast<int>(value(Lfo::Sync));
    const double since = sync > 0
                             ? (beatsAt(seconds) - restartBeats_.load(std::memory_order_relaxed)) /
                                   mod::syncBeats(sync, beatsPerBar_.load(std::memory_order_relaxed))
                             : (seconds - restartTime_.load(std::memory_order_relaxed)) * value(Lfo::Rate);
    return mode == 2 ? std::clamp(since, 0.0, 0.999999) : since; // Envelope: once per note
}

double Modulator::stepsAt(double seconds, size_t rateParam, size_t syncParam, double originSeconds,
                          double originBeats) const noexcept
{
    // Synced: steps of a note value, counted on the song's beats from `originBeats`; free: steps
    // at Rate since `originSeconds` while playing, on the free clock while stopped.
    if (const int sync = static_cast<int>(value(syncParam)); sync > 0)
        return (beatsAt(seconds) - originBeats) /
               mod::syncBeats(sync, beatsPerBar_.load(std::memory_order_relaxed));
    const double rate = value(rateParam);
    return playing_.load(std::memory_order_relaxed) ? (seconds - originSeconds) * rate
                                                    : stoppedClock_.load(std::memory_order_relaxed) * rate;
}

std::uint64_t Modulator::seed() const noexcept
{
    // The roll, this modulator, and the loop's lap (a kept roll keeps its lap).
    const bool keep = kept_.load(std::memory_order_relaxed);
    const auto lap = static_cast<std::uint64_t>(keep ? keptLap_.load(std::memory_order_relaxed)
                                                     : lap_.load(std::memory_order_relaxed));
    return roll() ^ (itemID.getRawID() * 0x9E3779B97F4A7C15ull) ^ (lap * 0xC2B2AE3D27D4EB4Full);
}

dsp::mod::EnvelopeShape Modulator::envelopeShape() const noexcept
{
    if (kind_ != ModulatorKind::Envelope)
        return {};
    return {.delayMs = value(Envelope::Delay),
            .attackMs = value(Envelope::Attack),
            .holdMs = value(Envelope::Hold),
            .decayMs = value(Envelope::Decay),
            .sustain = value(Envelope::Sustain) * 0.01,
            .releaseMs = value(Envelope::Release),
            .attackCurve = value(Envelope::AttackCurve) * 0.01,
            .decayCurve = value(Envelope::DecayCurve) * 0.01,
            .releaseCurve = value(Envelope::ReleaseCurve) * 0.01,
            .mode = static_cast<mod::EnvelopeShape::Mode>(static_cast<int>(value(Envelope::Mode))),
            .repeats = static_cast<int>(value(Envelope::Repeat))};
}

double Modulator::signal(int copy, double seconds) const noexcept
{
    const auto perTarget = model::perTargetParam(kind_);
    const int c = perTarget && value(*perTarget) >= 0.5f ? std::clamp(copy, 0, maxCopies - 1) : 0;
    switch (kind_)
    {
    case ModulatorKind::Lfo:
    {
        const double cycles = lfoCycles(seconds) + value(Lfo::Phase) / 360.0 + mod::spread(c, routeCount());
        return mod::lfo(static_cast<mod::Shape>(static_cast<int>(value(Lfo::Shape))), cycles,
                        value(Lfo::Slant) * 0.01, value(Lfo::Smooth) * 0.01,
                        &drawn_[static_cast<size_t>(drawnShown_.load(std::memory_order_acquire))]);
    }
    case ModulatorKind::Random:
    {
        const double frozen = frozenAt_.load(std::memory_order_relaxed);
        const double x = frozen >= 0.0 ? frozen
                                       : stepsAt(seconds, Random::Rate, Random::Sync,
                                                 passStart_.load(std::memory_order_relaxed), 0.0);
        return mod::random(seed(), static_cast<std::uint64_t>(c), x,
                           {.smooth = value(Random::Smooth) * 0.01,
                            .slant = value(Random::Slant) * 0.01,
                            .cyclic = value(Random::Cyclic) * 0.01,
                            .jitter = value(Random::Jitter) * 0.01});
    }
    case ModulatorKind::Steps:
    {
        const int length = std::clamp(static_cast<int>(value(Steps::Length)), 1, Steps::maxSteps);
        std::array<float, Steps::maxSteps> values{}, chances{};
        for (size_t i = 0; i < static_cast<size_t>(length); ++i)
        {
            values[i] = steps_[i].load(std::memory_order_relaxed);
            chances[i] = chances_[i].load(std::memory_order_relaxed);
        }
        const bool byTime = static_cast<int>(value(Steps::Advance)) == 0;
        const double x =
            byTime ? stepsAt(seconds, Steps::Rate, Steps::Sync, restartTime_.load(std::memory_order_relaxed),
                             restartBeats_.load(std::memory_order_relaxed))
                   : static_cast<double>(advances_.load(std::memory_order_relaxed)) + 0.5;
        return mod::steps(std::span<const float>(values.data(), static_cast<size_t>(length)),
                          std::span<const float>(chances.data(), static_cast<size_t>(length)), seed(),
                          static_cast<std::uint64_t>(c), x, byTime ? value(Steps::Smooth) * 0.01 : 0.0,
                          byTime ? value(Steps::Jitter) * 0.01 : 0.0);
    }
    case ModulatorKind::Envelope:
    {
        const double f = fired_[static_cast<size_t>(c)].load(std::memory_order_relaxed);
        if (f < 0.0)
            return 0.0;
        const double r = released_[static_cast<size_t>(c)].load(std::memory_order_relaxed);
        return mod::envelope(envelopeShape(), (seconds - f) * 1000.0, r < 0.0 ? -1.0 : (r - f) * 1000.0);
    }
    case ModulatorKind::Follower:
        return level_.load(std::memory_order_relaxed);
    }
    return 0.0;
}

float Modulator::output(int copy, double seconds) const noexcept
{
    const double depth = value(model::depthParam(kind_)) * 0.01;
    double s = signal(copy, seconds);
    if (kind_ == ModulatorKind::Lfo && static_cast<int>(value(Lfo::Mode)) != 0)
        if (const double fade = value(Lfo::Fade) * 0.001; fade > 0.0) // Fade In, after each restart
            s *= std::clamp((seconds - restartTime_.load(std::memory_order_relaxed)) / fade, 0.0, 1.0);
    // Both ways: around the knob, half its range each way at an amount of 100 %; one way: up,
    // its whole range at 100 % (a negative amount turns it down).
    const auto both = model::bothWaysParam(kind_);
    if (both && value(*both) >= 0.5f)
        return static_cast<float>(0.5 * depth * s);
    if (both)
        return static_cast<float>(depth * 0.5 * (s + 1.0));
    return static_cast<float>(depth * std::clamp(s, 0.0, 1.0));
}

float Modulator::getCurrentValue()
{
    return static_cast<float>(signal(0, blockTime_.load(std::memory_order_relaxed)));
}

float Modulator::getValueFor(const te::AutomatableParameter::ModifierAssignment& assignment,
                             te::TimePosition time)
{
    const auto* mine = dynamic_cast<const Assignment*>(&assignment);
    return output(mine != nullptr ? mine->copy.load(std::memory_order_relaxed) : 0, time.inSeconds());
}

// ---- routes -----------------------------------------------------------------------------------

Modulator::Assignment::Assignment(const juce::ValueTree& v, const Modulator& m)
    : te::AutomatableParameter::ModifierAssignment(m.edit, v), modulator(m.itemID)
{
    copy.store(static_cast<int>(v.getProperty(copyId, 0)));
}

bool Modulator::Assignment::isForModifierSource(const ModifierSource& source) const
{
    const auto* m = dynamic_cast<const Modulator*>(&source);
    return m != nullptr && m->itemID == modulator;
}

te::AutomatableParameter::ModifierAssignment* Modulator::createAssignment(const juce::ValueTree& v)
{
    return new Assignment(v, *this);
}

// ---- in time ----------------------------------------------------------------------------------

void Modulator::fire(double seconds) noexcept
{
    // Each copy fires on its own chance (Per Target), from the roll: the same firing, the same fate.
    const auto index = firings_.fetch_add(1, std::memory_order_relaxed);
    const auto perTarget = model::perTargetParam(kind_);
    const int copies = perTarget && value(*perTarget) >= 0.5f ? routeCount() : 1;
    const double chance = value(Envelope::Chance) * 0.01;
    for (int c = 0; c < copies; ++c)
        if (mod::stepPlays(seed(), static_cast<std::uint64_t>(c), index, chance))
        {
            fired_[static_cast<size_t>(c)].store(seconds, std::memory_order_relaxed);
            released_[static_cast<size_t>(c)].store(-1.0, std::memory_order_relaxed);
        }
}

void Modulator::release(double seconds) noexcept
{
    for (size_t c = 0; c < fired_.size(); ++c)
        if (fired_[c].load(std::memory_order_relaxed) >= 0.0 &&
            released_[c].load(std::memory_order_relaxed) < 0.0)
            released_[c].store(seconds, std::memory_order_relaxed);
}

void Modulator::tick(te::TimePosition editTime, int numSamples) noexcept
{
    const double seconds = editTime.inSeconds();
    blockTime_.store(seconds, std::memory_order_relaxed);
    updateParameterStreams(editTime); // its own parameters follow their automation and modulation
    const bool playing = playing_.load(std::memory_order_relaxed);
    if (!playing) // stopped: the free clock runs on, so what it moves keeps moving
        stoppedClock_.store(stoppedClock_.load(std::memory_order_relaxed) +
                                numSamples / sampleRate_.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    // The loop came round (or the song jumped back): another lap, fresh dice (ADR 0062).
    if (playing && seconds < lastTime_.load(std::memory_order_relaxed) - 1.0e-3)
        lap_.fetch_add(1, std::memory_order_relaxed);
    lastTime_.store(seconds, std::memory_order_relaxed);

    // Fire: pressed (or moved past half by anything that moves parameters).
    if (kind_ == ModulatorKind::Envelope || kind_ == ModulatorKind::Steps)
    {
        const size_t fireParam =
            kind_ == ModulatorKind::Envelope ? size_t{Envelope::Fire} : size_t{Steps::Fire};
        const bool on = value(fireParam) >= 0.5f;
        if (on && !fireWas_)
        {
            if (kind_ == ModulatorKind::Envelope)
                fire(seconds);
            else if (static_cast<int>(value(Steps::Advance)) == 2)
                advances_.fetch_add(1, std::memory_order_relaxed);
        }
        fireWas_ = on;
    }
    // Freeze: Random stops where it is.
    if (kind_ == ModulatorKind::Random)
    {
        const bool frozen = value(Random::Freeze) >= 0.5f;
        if (frozen && frozenAt_.load(std::memory_order_relaxed) < 0.0)
            frozenAt_.store(
                stepsAt(seconds, Random::Rate, Random::Sync, passStart_.load(std::memory_order_relaxed), 0.0),
                std::memory_order_relaxed);
        else if (!frozen)
            frozenAt_.store(-1.0, std::memory_order_relaxed);
    }
    // Where it is, for the panel.
    switch (kind_)
    {
    case ModulatorKind::Lfo:
        position_.store(lfoCycles(seconds) + value(Lfo::Phase) / 360.0, std::memory_order_relaxed);
        break;
    case ModulatorKind::Random:
        position_.store(frozenAt_.load() >= 0.0 ? frozenAt_.load()
                                                : stepsAt(seconds, Random::Rate, Random::Sync,
                                                          passStart_.load(std::memory_order_relaxed), 0.0),
                        std::memory_order_relaxed);
        break;
    case ModulatorKind::Steps:
        position_.store(static_cast<int>(value(Steps::Advance)) == 0
                            ? stepsAt(seconds, Steps::Rate, Steps::Sync,
                                      restartTime_.load(std::memory_order_relaxed),
                                      restartBeats_.load(std::memory_order_relaxed))
                            : static_cast<double>(advances_.load(std::memory_order_relaxed)),
                        std::memory_order_relaxed);
        break;
    case ModulatorKind::Envelope:
    {
        const double f = fired_[0].load(std::memory_order_relaxed);
        position_.store(f < 0.0 ? -1.0 : (seconds - f) * 1000.0, std::memory_order_relaxed);
        break;
    }
    case ModulatorKind::Follower:
        break;
    }
}

void Modulator::applyToBuffer(const te::PluginRenderContext& context)
{
    const double t0 = context.editTime.getStart().inSeconds();
    setEditTime(context.editTime.getStart());
    const double rate = sampleRate_.load(std::memory_order_relaxed);
    const double end = t0 + context.bufferNumSamples / rate;

    // Playback starting: this pass starts here (free modulators restart at their Phase); stopping
    // releases what was held.
    const bool wasPlaying = playing_.load(std::memory_order_relaxed);
    if (context.isPlaying && !wasPlaying)
    {
        passStart_.store(t0, std::memory_order_relaxed);
        restartTime_.store(t0, std::memory_order_relaxed);
        restartBeats_.store(beatsAt(t0), std::memory_order_relaxed);
        advances_.store(0, std::memory_order_relaxed);
        lap_.store(0, std::memory_order_relaxed);
        lastTime_.store(t0, std::memory_order_relaxed);
        notesHeld_ = 0;
        for (auto& f : fired_)
            f.store(-1.0, std::memory_order_relaxed);
        firings_.store(0, std::memory_order_relaxed);
    }
    if (!context.isPlaying && wasPlaying)
        release(t0);
    playing_.store(context.isPlaying, std::memory_order_relaxed);

    // The notes played where it sits.
    if (context.bufferForMidiMessages != nullptr)
        for (auto& m : *context.bufferForMidiMessages)
        {
            const double t = t0 + m.getTimeStamp();
            if (m.isNoteOn())
            {
                const bool first = notesHeld_ == 0;
                ++notesHeld_;
                switch (kind_)
                {
                case ModulatorKind::Lfo:
                    if (static_cast<int>(value(Lfo::Mode)) != 0)
                    {
                        restartTime_.store(t, std::memory_order_relaxed);
                        restartBeats_.store(beatsAt(t), std::memory_order_relaxed);
                    }
                    break;
                case ModulatorKind::Envelope:
                    if (static_cast<int>(value(Envelope::Trigger)) == 0 &&
                        (first || value(Envelope::Legato) < 0.5f))
                        fire(t);
                    break;
                case ModulatorKind::Steps:
                    if (static_cast<int>(value(Steps::Restart)) == 1)
                    {
                        restartTime_.store(t, std::memory_order_relaxed);
                        restartBeats_.store(beatsAt(t), std::memory_order_relaxed);
                    }
                    if (static_cast<int>(value(Steps::Advance)) == 1)
                        advances_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case ModulatorKind::Random:
                case ModulatorKind::Follower:
                    break;
                }
            }
            else if (m.isNoteOff())
            {
                notesHeld_ = std::max(0, notesHeld_ - 1);
                if (notesHeld_ == 0 && kind_ == ModulatorKind::Envelope)
                    release(t);
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                notesHeld_ = 0;
                if (kind_ == ModulatorKind::Envelope)
                    release(t);
            }
        }

    // The beat: the Envelope fires every so often; Steps restart each bar.
    if (context.isPlaying)
    {
        const double b0 = beatsAt(t0), b1 = beatsAt(end);
        const double bps = std::max(1.0e-6, beatsPerSecond_.load(std::memory_order_relaxed));
        const auto crossing = [&](double every) -> std::optional<double>
        {
            if (every <= 0.0)
                return std::nullopt;
            const double at = std::ceil(b0 / every - 1.0e-9) * every; // the first boundary from b0
            return at < b1 ? std::optional<double>(t0 + (at - b0) / bps) : std::nullopt;
        };
        if (kind_ == ModulatorKind::Envelope && static_cast<int>(value(Envelope::Trigger)) == 1)
            if (const auto t = crossing(mod::syncBeats(static_cast<int>(value(Envelope::Every)) + 1,
                                                       beatsPerBar_.load(std::memory_order_relaxed))))
                fire(*t);
        if (kind_ == ModulatorKind::Steps && static_cast<int>(value(Steps::Restart)) == 2)
            if (const auto t = crossing(beatsPerBar_.load(std::memory_order_relaxed)))
            {
                restartTime_.store(*t, std::memory_order_relaxed);
                restartBeats_.store(beatsAt(*t), std::memory_order_relaxed);
            }
    }

    // The Follower: how loud the sound is where it sits, or on the track it listens to (its two
    // channels after the track's own, the modifier-sidechain patch); its band only, while Band is
    // on. It listens; it never changes the sound passing through.
    if (kind_ == ModulatorKind::Follower && context.destBuffer != nullptr)
    {
        const bool band = value(Follower::Band) >= 0.5f;
        const float low = value(Follower::Low), high = value(Follower::High);
        if (band && (low != lastLow_ || high != lastHigh_))
        {
            for (auto& f : lowCut_)
                f.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, low));
            for (auto& f : highCut_)
                f.setCoefficients(juce::IIRCoefficients::makeLowPass(rate, high));
            lastLow_ = low;
            lastHigh_ = high;
        }
        const double gain = juce::Decibels::decibelsToGain(static_cast<double>(value(Follower::Gain)));
        const double attack =
            1.0 - std::exp(-1.0 / (rate * std::max(1.0e-4, value(Follower::Attack) * 0.001)));
        const double releaseCoef =
            1.0 - std::exp(-1.0 / (rate * std::max(1.0e-4, value(Follower::Release) * 0.001)));
        const auto& buffer = *context.destBuffer;
        const int first = buffer.getNumChannels() >= 4 ? 2 : 0; // elsewhere, or here
        const int channels = std::min(2, buffer.getNumChannels() - first);
        for (int i = 0; i < context.bufferNumSamples; ++i)
        {
            double x = 0.0;
            for (int ch = 0; ch < channels; ++ch)
            {
                float s = buffer.getSample(first + ch, context.bufferStartSample + i);
                if (band)
                    s = highCut_[ch].processSingleSampleRaw(lowCut_[ch].processSingleSampleRaw(s));
                x = std::max(x, static_cast<double>(std::abs(s)));
            }
            x *= gain;
            followed_ += (x > followed_ ? attack : releaseCoef) * (x - followed_);
        }
        level_.store(static_cast<float>(std::clamp(followed_, 0.0, 1.0)), std::memory_order_relaxed);
    }
}
bool movedByModulators(const te::AutomatableParameter& param)
{
    for (auto a : param.getAssignments())
        if (dynamic_cast<const Modulator::Assignment*>(a) != nullptr)
            return true;
    return false;
}
} // namespace fd::engine
