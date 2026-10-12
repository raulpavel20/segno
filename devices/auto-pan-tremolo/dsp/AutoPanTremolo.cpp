#include "AutoPanTremolo.h"

#include "fd/dsp/Math.h"
#include "fd/dsp/Waveshapers.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
constexpr std::string_view kModes[] = {"Panning", "Tremolo"};
constexpr std::string_view kShapes[] = {"Sine",   "Triangle", "Saw Up", "Saw Down",
                                        "Square", "Random",   "Drift"};
constexpr std::string_view kStereo[] = {"Offset", "Spin"};

constexpr std::array<ParamSpec, AutoPanTremolo::NumParams> kSpecs{{
    {.id = "mode",
     .name = "Mode",
     .shortName = "Mode",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kModes},
    {.id = "rate",
     .name = "Rate",
     .shortName = "Rate",
     .min = 0.01f,
     .max = 40.0f,
     .def = 1.0f,
     .skewCentre = 2.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 2},
    {.id = "sync",
     .name = "Sync",
     .shortName = "Sync",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "div",
     .name = "Sync Rate",
     .shortName = "Rate",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 16.0f,
     .def = 8.0f,
     .choices = dsp::lfoNoteValues},
    {.id = "shape",
     .name = "Shape",
     .shortName = "Shape",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 6.0f,
     .def = 0.0f,
     .choices = kShapes},
    {.id = "amount",
     .name = "Amount",
     .shortName = "Amount",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "edge",
     .name = "Edge",
     .shortName = "Edge",
     .min = -100.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "phase",
     .name = "Phase",
     .shortName = "Phase",
     .min = 0.0f,
     .max = 360.0f,
     .def = 0.0f,
     .unit = Unit::Degrees,
     .decimals = 0},
    {.id = "invert",
     .name = "Invert",
     .shortName = "Invert",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "attack",
     .name = "Attack",
     .shortName = "Attack",
     .min = 0.0f,
     .max = 1000.0f,
     .def = 0.0f,
     .skewCentre = 100.0f,
     .unit = Unit::Milliseconds,
     .decimals = 0},
    {.id = "stereo",
     .name = "Stereo",
     .shortName = "Stereo",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kStereo},
    {.id = "offset",
     .name = "Offset",
     .shortName = "Offset",
     .min = 0.0f,
     .max = 180.0f,
     .def = 0.0f,
     .unit = Unit::Degrees,
     .decimals = 0},
    {.id = "spin",
     .name = "Spin",
     .shortName = "Spin",
     .min = 0.0f,
     .max = 100.0f,
     .def = 20.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "harmonic",
     .name = "Harmonic",
     .shortName = "Harmonic",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "vintage",
     .name = "Vintage",
     .shortName = "Vintage",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "lowcut",
     .name = "Low Cut",
     .shortName = "Low Cut",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "lowcut.freq",
     .name = "Low Cut Freq",
     .shortName = "Freq",
     .min = 20.0f,
     .max = 2000.0f,
     .def = 120.0f,
     .skewCentre = 200.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "output",
     .name = "Output",
     .shortName = "Output",
     .min = -24.0f,
     .max = 12.0f,
     .def = 0.0f,
     .unit = Unit::Decibels},
}};
// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"mode",
     "The kind of movement: Panning moves the sound from side to side, Tremolo makes its level pulse."},
    {"rate", "How fast the movement goes, in cycles per second."},
    {"sync", "Moves in time with the song: the rate becomes a note value."},
    {"div", "How fast the movement goes as a note value, one cycle per note, while Sync is on."},
    {"shape", "The movement's wave: smooth, even, rising, falling, jumping, stepping at random or drifting."},
    {"amount", "How far it moves: how far to each side, or how deep the level dips."},
    {"edge", "Pushes the wave to its extremes for a harder, choppier movement, or (negative) eases it for a "
             "softer one."},
    {"phase", "Where the cycle starts against the beat, while Sync is on: 180° starts half a cycle late."},
    {"invert", "Turns the wave upside down: a pulse that dips becomes one that swells, a pan that starts "
               "left starts right."},
    {"attack", "Lets each new note through untouched, the movement fading in over this time after it."},
    {"stereo", "How the two sides pulse: Offset moves them apart in step, Spin at different speeds."},
    {"offset", "How far apart the two sides pulse: together at 0°, in turn at 180°."},
    {"spin", "How much faster the right side pulses than the left, so the two drift in and out of step."},
    {"harmonic", "Splits the tremolo at 600 Hz so the lows and highs pulse in turn, as the harmonic tremolos "
                 "of old amps did."},
    {"vintage", "Makes the tremolo behave like an old optical one: it dips sooner, comes back slower and "
                "warms the sound a little."},
    {"lowcut", "Keeps the lows out of the effect, so they stay solid and centred."},
    {"lowcut.freq", "Below this frequency, the sound passes through untouched."},
    {"output", "The level after the effect."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr MeterSpec kMeters[] = {
    {.slot = AutoPanTremolo::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = AutoPanTremolo::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = AutoPanTremolo::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = AutoPanTremolo::PeakOutR,
     .id = "output_right",
     .name = "Output, right",
     .kind = MeterKind::Level},
};

constexpr DeviceInfo kInfo{
    .id = "auto-pan-tremolo",
    .name = "Auto Pan & Tremolo",
    .category = "Modulation",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

constexpr dsp::ModulationLfo::Shape kLfoShapes[] = {
    dsp::ModulationLfo::Shape::Sine,   dsp::ModulationLfo::Shape::Triangle,
    dsp::ModulationLfo::Shape::SawUp,  dsp::ModulationLfo::Shape::SawDown,
    dsp::ModulationLfo::Shape::Square, dsp::ModulationLfo::Shape::SampleAndHold,
    dsp::ModulationLfo::Shape::Wander};

// The movement (SPEC: DSP design). Starting points, tuned by ear.
constexpr double kEdgeOctaves = 3.0; // Edge's power runs from 2^-3 to 2^3
constexpr double kGainMs = 1.0;      // no level or place changes faster than this
constexpr double kVintageFallMs = 3.0, kVintageRiseMs = 30.0;
constexpr float kVintageDrive = 1.3f; // the soft saturation: the Chorus's Warmth at 30 %
constexpr double kFastAttackMs = 0.5, kFastReleaseMs = 20.0, kSlowAttackMs = 30.0, kSlowReleaseMs = 300.0;
constexpr float kOnsetRatio = 2.0f;     // the fast follower 6 dB over the slow one: a new note
constexpr float kOnsetFloor = 0.00316f; // -50 dBFS
constexpr float kSilence = 0.001f;      // -60 dBFS: the ramp rests at nothing
constexpr double kHoldoffMs = 50.0, kGlideMs = 12.0;

float coefFor(double ms, double sampleRate)
{
    return static_cast<float>(1.0 - std::exp(-1.0 / (ms * 0.001 * sampleRate)));
}
} // namespace

AutoPanTremolo::AutoPanTremolo() : Device(kInfo) {}

// ---- pure functions ------------------------------------------------------------------------

double AutoPanTremolo::edged(double v, double edge) noexcept
{
    const double k = std::exp2(-kEdgeOctaves * std::clamp(edge, -1.0, 1.0));
    return std::copysign(std::pow(std::abs(v), k), v);
}

double AutoPanTremolo::wave(const Wave& w, double position) noexcept
{
    const auto shape = kLfoShapes[std::clamp(w.shape, 0, 6)];
    const double v = edged(dsp::ModulationLfo::value(shape, position + w.phase), w.edge);
    return w.invert ? -v : v;
}

double AutoPanTremolo::level(double amount, double w, bool vintage) noexcept
{
    const double l = 1.0 - std::clamp(amount, 0.0, 1.0) * (1.0 - w) * 0.5;
    return vintage ? l * l : l;
}

double AutoPanTremolo::mirrorLevel(double amount, double w, bool vintage) noexcept
{
    return level(amount, -w, vintage);
}

std::pair<double, double> AutoPanTremolo::panGains(double place) noexcept
{
    const double angle = (1.0 + std::clamp(place, -1.0, 1.0)) * dsp::pi * 0.25;
    return {std::sqrt(2.0) * std::cos(angle), std::sqrt(2.0) * std::sin(angle)};
}

// ---- the engine ----------------------------------------------------------------------------

void AutoPanTremolo::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    modeSamples_ = dsp::msToSamples(50.0, sampleRate_);
    holdoffSamples_ = dsp::msToSamples(kHoldoffMs, sampleRate_);
    gainCoef_ = coefFor(kGainMs, sampleRate_);
    fallCoef_ = coefFor(kVintageFallMs, sampleRate_);
    riseCoef_ = coefFor(kVintageRiseMs, sampleRate_);
    fastAttack_ = coefFor(kFastAttackMs, sampleRate_);
    fastRelease_ = coefFor(kFastReleaseMs, sampleRate_);
    slowAttack_ = coefFor(kSlowAttackMs, sampleRate_);
    slowRelease_ = coefFor(kSlowReleaseMs, sampleRate_);
    for (auto* g : {&phase_, &offset_, &rightJump_})
        g->setTime(kGlideMs, sampleRate_);
    for (auto& c : ch_)
    {
        c.lows.prepare(sampleRate_);
        c.harmonic.prepare(sampleRate_);
        c.harmonic.setFrequency(harmonicHz);
    }
    lfo_.prepare(sampleRate_, kGlideMs);
    detector_.prepare(sampleRate_);
    analysis().setSampleRate(sampleRate_);
    reset();
}

void AutoPanTremolo::reset() noexcept
{
    grid_.reset();
    for (auto& c : ch_)
    {
        c.lows.reset();
        c.harmonic.reset();
        c.high = c.mid = c.vintage = c.vintageMid = 1.0f;
    }
    lfo_.reset();
    detector_.reset();
    fast_ = slow_ = 0.0f;
    holdoff_ = 0;
    snap();
    ramp_ = params().get(Attack) > 0.0f ? 0.0f : 1.0f;
    fresh_ = true;
}

void AutoPanTremolo::snap() noexcept
{
    const auto& p = params();
    mode_ = previous_ = static_cast<ModeKind>(p.getInt(Mode));
    modeFade_.reset(1.0f);
    stereo_ = static_cast<StereoKind>(p.getInt(Stereo));
    shape_ = p.getInt(Shape);
    updateTargets(/*immediate*/ true);
    for (auto* g : {&phase_, &offset_})
        g->snap(g->target());
    rightJump_.snap(0.0);
    // Locked to the song at once: nothing has played to glide from.
    updateSync(/*snap*/ true);
    rightPosition_ = lfo_.position() + offset_.current();
}

void AutoPanTremolo::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    amount_.setTarget(p.get(Amount) * 0.01f, ramp);
    edge_.setTarget(p.get(Edge) * 0.01f, ramp);
    invert_.setTarget(p.getBool(Invert) ? 1.0f : 0.0f, ramp);
    spin_.setTarget(p.get(Spin) * 0.01f, ramp);
    harmonic_.setTarget(p.getBool(Harmonic) ? 1.0f : 0.0f, ramp);
    vintage_.setTarget(p.getBool(Vintage) ? 1.0f : 0.0f, ramp);
    lowOn_.setTarget(p.getBool(LowCut) ? 1.0f : 0.0f, ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    rate_.setTarget(p.get(Rate), ramp);
    lowFreq_.setTarget(p.get(LowCutFreq), ramp);
    offset_.setTarget(p.get(Offset) / 360.0);
    // Phase: the nearer way round (0° and 360° are the same place).
    const double phase = p.get(Phase) / 360.0, step = phase - phase_.target();
    phase_.setTarget(phase_.target() + step - std::round(step));
    if (immediate)
    {
        for (auto* s : {&amount_, &edge_, &invert_, &spin_, &harmonic_, &vintage_, &lowOn_, &output_})
            s->reset(s->target());
        rate_.reset(rate_.target());
        lowFreq_.reset(lowFreq_.target());
        phase_.snap(phase);
    }

    // A new mode: the one it leaves and the new one crossfade.
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    if (mode != mode_ && !immediate)
    {
        previous_ = mode_;
        mode_ = mode;
        modeFade_.reset(0.0f);
        modeFade_.setTarget(1.0f, modeSamples_);
    }
    // Back from Spin to Offset: the right side glides to its place.
    const auto stereo = static_cast<StereoKind>(p.getInt(Stereo));
    if (stereo != stereo_ && !immediate && stereo == StereoKind::Offset)
    {
        const double jump = lfo_.position() + offset_.current() - rightPosition_;
        rightJump_.snap(-(jump - std::round(jump)));
        rightJump_.setTarget(0.0);
    }
    stereo_ = stereo;
    shape_ = p.getInt(Shape);

    // Attack: the ramp's step per sample (0 ms: none).
    const double attackMs = p.get(Attack);
    rampStep_ = attackMs > 0.0 ? static_cast<float>(1.0 / (attackMs * 0.001 * sampleRate_)) : 1.0f;
    for (auto& c : ch_)
        c.lows.setFrequency(lowFreq_.current());
}

void AutoPanTremolo::updateSync(bool snap) noexcept
{
    const auto& p = params();
    // A synced cycle at most maxCycleSeconds long, at a slow tempo.
    const double beats = p.getBool(Sync) ? std::min(dsp::lfoNoteBeats(p.getInt(Div), beatsPerBar_),
                                                    maxCycleSeconds * tempo_ / 60.0)
                                         : 0.0;
    lfo_.set(rate_.current(), beats, snap);
}

void AutoPanTremolo::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int outChannels = std::min(ctx.output.numChannels, maxChannels);
    tempo_ = ctx.transport.tempoBpm > 0.0 ? ctx.transport.tempoBpm : 120.0;
    beatsPerBar_ = ctx.transport.timeSigDenominator > 0
                       ? ctx.transport.timeSigNumerator * 4.0 / ctx.transport.timeSigDenominator
                       : 4.0;
    lfo_.beginBlock(ctx.transport.playing, ctx.transport.ppqPosition, tempo_);
    if (fresh_)
    {
        snap();
        fresh_ = false;
    }

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {};
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            if (isTick)
            {
                updateTargets(false);
                updateSync(/*snap*/ false);
            }
            for (int i = 0; i < length; ++i)
            {
                const float amount = amount_.next(), edge = edge_.next(), invert = invert_.next(),
                            spin = spin_.next(), harmonic = harmonic_.next(), vintage = vintage_.next(),
                            lowOn = lowOn_.next(), out = output_.next(), fade = modeFade_.next();
                rate_.next();
                lowFreq_.next();
                const double phase = phase_.next(), stereoOffset = offset_.next(),
                             rightJump = rightJump_.next();

                // Each side's position: the left's; the right's by Offset, or its own by Spin.
                const double left = lfo_.next();
                if (stereo_ == StereoKind::Spin)
                    rightPosition_ += lfo_.rateHz() * (1.0 + spin) / sampleRate_;
                else
                    rightPosition_ = left + stereoOffset + rightJump;

                const int at = offset + i;
                const float in[maxChannels] = {ctx.input.channel(0)[at], inChannels > 1
                                                                             ? ctx.input.channel(1)[at]
                                                                             : ctx.input.channel(0)[at]};

                // Attack: a new note (or the end of silence) starts the movement again from nothing.
                const float peak = detector_.process(std::max(std::abs(in[0]), std::abs(in[1]))).peak;
                fast_ += (peak > fast_ ? fastAttack_ : fastRelease_) * (peak - fast_);
                slow_ += (peak > slow_ ? slowAttack_ : slowRelease_) * (peak - slow_);
                holdoff_ = std::max(0, holdoff_ - 1);
                ramp_ = std::min(1.0f, ramp_ + rampStep_);
                if (rampStep_ < 1.0f)
                {
                    if (slow_ < kSilence)
                        ramp_ = 0.0f;
                    else if (holdoff_ == 0 && fast_ > kOnsetFloor && fast_ > kOnsetRatio * slow_)
                    {
                        ramp_ = 0.0f;
                        holdoff_ = holdoffSamples_;
                    }
                }
                const double depth = static_cast<double>(amount) * ramp_;

                // The wave on each side, flipped as Invert fades in.
                const Wave w{.shape = shape_, .edge = edge, .phase = phase};
                const double flip = 1.0 - 2.0 * invert;
                const double waves[maxChannels] = {flip * wave(w, left), flip * wave(w, rightPosition_)};
                const auto pan = panGains(depth * waves[0]);
                const double panning[maxChannels] = {pan.first, pan.second};

                // How much each mode sounds: the one it's in, and the one it's leaving.
                float weight[2] = {};
                weight[static_cast<int>(mode_)] += fade;
                weight[static_cast<int>(previous_)] += 1.0f - fade;
                const float wPan = weight[0], wTremolo = weight[1];

                for (int c = 0; c < maxChannels; ++c)
                {
                    auto& h = ch_[static_cast<size_t>(c)];
                    peakIn[c] = std::max(peakIn[c], std::abs(in[c]));

                    // The tremolo's levels, and Vintage's: squared, quick to dip and slow to recover.
                    const auto plain = static_cast<float>(level(depth, waves[c], false));
                    const auto mirror = static_cast<float>(mirrorLevel(depth, waves[c], false));
                    const auto squared = static_cast<float>(level(depth, waves[c], true));
                    const auto squaredMirror = static_cast<float>(mirrorLevel(depth, waves[c], true));
                    h.vintage += (squared < h.vintage ? fallCoef_ : riseCoef_) * (squared - h.vintage);
                    h.vintageMid += (squaredMirror < h.vintageMid ? fallCoef_ : riseCoef_) *
                                    (squaredMirror - h.vintageMid);
                    const float high = plain + vintage * (h.vintage - plain);
                    const float mid = mirror + vintage * (h.vintageMid - mirror);

                    // The gains, the mode's, never faster than 1 ms: the level (or the side's share of
                    // the balance), and Harmonic's lows' mirror.
                    const float p = static_cast<float>(panning[c]);
                    h.high += gainCoef_ * (wPan * p + wTremolo * high - h.high);
                    h.mid += gainCoef_ * (wPan * p + wTremolo * mid - h.mid);

                    // Below Low Cut the sound passes as it was; above, it moves whole, or with Harmonic
                    // its two bands at 600 Hz move in turn. With Low Cut off the input moves whole.
                    const auto bands = h.lows.process(in[c]);
                    const float above = in[c] + lowOn * (bands.high - in[c]);
                    const auto split = h.harmonic.process(above);
                    const float banded = harmonic * wTremolo;
                    float y = (1.0f - banded) * h.high * above +
                              banded * (h.high * split.high + h.mid * split.low) + lowOn * bands.low;
                    const float warmth = vintage * wTremolo;
                    if (warmth > 0.0f)
                    {
                        const float sat =
                            static_cast<float>(
                                dsp::shape(dsp::ShaperCurve::Soft, static_cast<double>(kVintageDrive * y))) /
                            kVintageDrive;
                        y += warmth * (sat - y);
                    }
                    y *= out;
                    if (c < outChannels)
                    {
                        ctx.output.channel(c)[at] = y;
                        peakOut[c] = std::max(peakOut[c], std::abs(y));
                    }
                }
            }
        });

    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, peakIn[1]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, peakOut[1]);
    const double position = lfo_.position();
    analysis().publish(LfoPosition, static_cast<float>(position - std::floor(position)));
    analysis().publish(RateHz, static_cast<float>(lfo_.rateHz()));
    analysis().publish(AttackRamp, ramp_);
}

const DeviceFactory& autoPanTremoloFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<AutoPanTremolo>(); },
    };
    return factory;
}
} // namespace fd::devices
