#include "Arpeggiator.h"

#include "../../midi-shared/dsp/PassThrough.h"

#include <algorithm>
#include <cmath>
#include <fd/music/Arpeggio.h>
#include <fd/music/Following.h>
#include <nlohmann/json.hpp>

namespace fd::devices
{
namespace
{
constexpr Smoothing none{SmoothingKind::None, 0.0f};
constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
constexpr std::array<std::string_view, 10> styleChoices{
    "Up", "Down", "Up-down", "Down-up", "Converge", "Diverge", "Played", "Chord", "Random", "Shuffled"};
constexpr std::array<std::string_view, 15> rateChoices{"1/1",  "1/2",   "1/2T", "1/4D",  "1/4",
                                                       "1/4T", "1/8D",  "1/8",  "1/8T",  "1/16D",
                                                       "1/16", "1/16T", "1/32", "1/32T", "1/64"};
constexpr std::array<std::string_view, 7> retriggerChoices{"Off",    "Note",   "1 bar",  "2 bars",
                                                           "4 bars", "8 bars", "Section"};
constexpr std::array<std::string_view, 5> laneIds{"velocity.length", "gate.length", "octave.length",
                                                  "ratchet.length", "chance.length"};
constexpr std::array<std::string_view, 5> laneNames{"Velocity length", "Gate length", "Octave length",
                                                    "Ratchet length", "Chance length"};

const std::array<ParamSpec, Arpeggiator::Count>& table()
{
    static const auto t = []
    {
        using A = Arpeggiator;
        std::array<ParamSpec, A::Count> p{};
        p[A::Style] = {.id = "style",
                       .name = "Style",
                       .kind = ParamKind::Choice,
                       .max = 9.0f,
                       .choices = styleChoices,
                       .smoothing = none,
                       .description = "The order the notes held are played in."};
        p[A::Sync] = {.id = "sync",
                      .name = "Sync",
                      .kind = ParamKind::Toggle,
                      .max = 1.0f,
                      .def = 1.0f,
                      .choices = offOn,
                      .smoothing = none,
                      .description = "Plays at a note value of the song's tempo, or at a rate of its own."};
        p[A::Rate] = {.id = "rate",
                      .name = "Rate",
                      .kind = ParamKind::Choice,
                      .max = 14.0f,
                      .def = 10.0f,
                      .choices = rateChoices,
                      .smoothing = none,
                      .description = "How often a step plays, as a note value."};
        p[A::Hz] = {.id = "hz",
                    .name = "Rate (free)",
                    .shortName = "Rate",
                    .min = 0.5f,
                    .max = 50.0f,
                    .def = 8.0f,
                    .skewCentre = 6.0f,
                    .unit = Unit::Hertz,
                    .smoothing = none,
                    .description = "How many steps a second, without Sync."};
        p[A::Gate] = {.id = "gate",
                      .name = "Gate",
                      .min = 5.0f,
                      .max = 200.0f,
                      .def = 80.0f,
                      .unit = Unit::Percent,
                      .smoothing = none,
                      .decimals = 0,
                      .description =
                          "How long each step plays, as a share of the step; past 100 % steps overlap."};
        p[A::Swing] = {.id = "swing",
                       .name = "Swing",
                       .max = 75.0f,
                       .unit = Unit::Percent,
                       .smoothing = none,
                       .decimals = 0,
                       .description = "Delays every second step."};
        p[A::Steps] = {.id = "steps",
                       .name = "Steps",
                       .kind = ParamKind::Discrete,
                       .min = 1.0f,
                       .max = 8.0f,
                       .def = 1.0f,
                       .smoothing = none,
                       .decimals = 0,
                       .description = "How many times the pattern plays, each time moved by Distance."};
        p[A::Distance] = {
            .id = "distance",
            .name = "Distance",
            .shortName = "Dist",
            .kind = ParamKind::Discrete,
            .min = -24.0f,
            .max = 24.0f,
            .def = 12.0f,
            .smoothing = none,
            .decimals = 0,
            .description =
                "How far each repeat of the pattern moves: semitones, or steps of the key with In key."};
        p[A::InKey] = {.id = "inkey",
                       .name = "In key",
                       .kind = ParamKind::Toggle,
                       .max = 1.0f,
                       .choices = offOn,
                       .smoothing = none,
                       .description = "Distance and the Octave lane count steps of the key."};
        p[A::Retrigger] = {.id = "retrigger",
                           .name = "Retrigger",
                           .shortName = "Retrig",
                           .kind = ParamKind::Choice,
                           .max = 6.0f,
                           .def = 1.0f,
                           .choices = retriggerChoices,
                           .smoothing = none,
                           .description = "When the pattern starts again from its first step."};
        p[A::Hold] = {.id = "hold",
                      .name = "Hold",
                      .kind = ParamKind::Toggle,
                      .max = 1.0f,
                      .choices = offOn,
                      .smoothing = none,
                      .description =
                          "Keeps playing the notes after they're let go, until new ones are played."};
        p[A::SongChords] = {.id = "song.chords",
                            .name = "Song chords",
                            .kind = ParamKind::Toggle,
                            .max = 1.0f,
                            .choices = offOn,
                            .smoothing = none,
                            .description = "With no notes held, plays the chord sounding in the song."};
        p[A::From] = {.id = "from",
                      .name = "From",
                      .kind = ParamKind::Discrete,
                      .max = 127.0f,
                      .def = 60.0f,
                      .unit = Unit::Note,
                      .smoothing = none,
                      .description = "The note the song's chords are played from."};
        for (std::size_t i = 0; i < 5; ++i)
            p[A::VelocityLength + i] = {.id = laneIds[i],
                                        .name = laneNames[i],
                                        .kind = ParamKind::Discrete,
                                        .min = 1.0f,
                                        .max = 16.0f,
                                        .def = 16.0f,
                                        .smoothing = none,
                                        .decimals = 0,
                                        .description = "How many steps the lane has before it starts again."};
        const auto h = harmony::params();
        std::copy(h.begin(), h.end(), p.begin() + A::HarmonyFirst);
        return p;
    }();
    return t;
}

const DeviceInfo arpeggiatorInfo{.id = "arpeggiator",
                                 .name = "Arpeggiator",
                                 .category = "MIDI",
                                 .kind = DeviceKind::MidiEffect,
                                 .buses = midiEffectBuses,
                                 .params = table()};
} // namespace

float Arpeggiator::neutral(int lane) noexcept
{
    switch (lane)
    {
    case OctaveLane:
        return 0.0f;
    case RatchetLane:
        return 1.0f;
    default:
        return 100.0f; // velocity and gate: 100 % of the note's; chance: 100 %
    }
}

std::string_view Arpeggiator::laneKey(int lane) noexcept
{
    static constexpr std::array<std::string_view, LaneCount> keys{"lane.velocity", "lane.gate", "lane.octave",
                                                                  "lane.ratchet", "lane.chance"};
    return keys[static_cast<std::size_t>(std::clamp(lane, 0, LaneCount - 1))];
}

namespace
{
/// A lane's values' range (SPEC, "Non-parameter state").
std::pair<float, float> laneRange(int lane) noexcept
{
    switch (lane)
    {
    case Arpeggiator::OctaveLane:
        return {-2.0f, 2.0f};
    case Arpeggiator::RatchetLane:
        return {1.0f, 4.0f};
    case Arpeggiator::ChanceLane:
        return {0.0f, 100.0f};
    default:
        return {0.0f, 200.0f};
    }
}

/// Each Rate choice's step, in beats.
constexpr std::array<double, 15> rateBeats{4.0,       2.0,       4.0 / 3.0, 1.5,        1.0,
                                           2.0 / 3.0, 0.75,      0.5,       1.0 / 3.0,  0.375,
                                           0.25,      1.0 / 6.0, 0.125,     1.0 / 12.0, 0.0625};
constexpr double songChordVelocity = 100.0 / 127.0;
} // namespace

Arpeggiator::Arpeggiator() : NoteEffect(arpeggiatorInfo)
{
    for (int lane = 0; lane < LaneCount; ++lane)
    {
        for (auto& v : lanes_[static_cast<std::size_t>(lane)])
            v.store(neutral(lane), std::memory_order_relaxed);
        playing_[static_cast<std::size_t>(lane)].store(-1, std::memory_order_relaxed);
    }
}

void Arpeggiator::writeExtraState(nlohmann::json& extra) const
{
    for (int lane = 0; lane < LaneCount; ++lane)
    {
        auto values = nlohmann::json::array();
        for (int i = 0; i < maxSteps; ++i)
            values.push_back(step(lane, i));
        extra[std::string(laneKey(lane))] = std::move(values);
    }
}

void Arpeggiator::readExtraState(const nlohmann::json& extra)
{
    for (int lane = 0; lane < LaneCount; ++lane)
    {
        const auto key = std::string(laneKey(lane));
        const auto [lo, hi] = laneRange(lane);
        const bool given = extra.contains(key) && extra[key].is_array();
        for (int i = 0; i < maxSteps; ++i)
        {
            float v = neutral(lane);
            if (given && static_cast<std::size_t>(i) < extra[key].size() &&
                extra[key][static_cast<std::size_t>(i)].is_number())
                v = std::clamp(extra[key][static_cast<std::size_t>(i)].get<float>(), lo, hi);
            setStep(lane, i, v);
        }
    }
}

double Arpeggiator::stepBeats() const noexcept
{
    return rateBeats[static_cast<std::size_t>(std::clamp(params().getInt(Rate), 0, 14))];
}

double Arpeggiator::swingBeats(double length) const noexcept
{
    // Every second step later: at 50 % a triplet's feel, at 75 % a dotted one (half a step).
    return params().get(Swing) / 100.0 * length * 2.0 / 3.0;
}

void Arpeggiator::processNotes(ProcessContext& c, const BlockBeats& beats) noexcept
{
    follower_.begin(c, params(), HarmonyFirst);
    if (!params().getBool(Hold))
    {
        for (std::size_t i = 0; i < heldCount_;) // Hold turned off: only the notes held stay
            if (!held_[i].down)
                held_[i] = held_[--heldCount_];
            else
                ++i;
    }
    const bool synced = params().getBool(Sync);
    if (synced)
        freeRunning_ = false; // its own clock starts again when Sync is turned off
    else if (!freeRunning_ && (heldCount_ > 0 || params().getBool(SongChords)))
    {
        freeRunning_ = true;
        freeNext_ = static_cast<double>(clockAt(0));
        freeSteps_ = 0;
    }

    Moments moments(c, beats, follower_);
    int from = 0;
    for (;;)
    {
        // At one sample: steps' ends, then what comes in, then the steps' notes.
        bool off = false;
        const int scheduled = nextScheduled(c.numSamples, off);
        const int moment = moments.next();
        std::int64_t step = 0;
        const int stepAt = nextStep(beats, from, step);
        const int at = std::min({scheduled, moment, stepAt});
        if (at == INT_MAX)
            break;
        if (scheduled == at && off)
            playScheduled(c, at, true);
        else if (moment == at)
        {
            const auto m = moments.take();
            if (m.kind == Moments::Kind::Input && !passOn(c, m.event))
                play(m.event, m.offset);
        }
        else if (scheduled == at)
            playScheduled(c, at, false);
        else
        {
            playStep(c, beats, at, step);
            from = at + 1;
        }
    }
}

void Arpeggiator::play(const Event& e, int offset) noexcept
{
    if (e.type != EventType::NoteOn && e.type != EventType::NoteOff)
        return; // pressure on a held note: the arpeggio's notes aren't it
    std::size_t i = 0;
    while (i < heldCount_ && !(held_[i].key == e.key && held_[i].channel == e.channel))
        ++i;
    const bool hold = params().getBool(Hold);
    if (e.type == EventType::NoteOff)
    {
        if (i < heldCount_ && held_[i].down)
        {
            held_[i].down = false;
            down_ = std::max(0, down_ - 1);
            if (!hold)
                held_[i] = held_[--heldCount_];
        }
        return;
    }
    if (hold && down_ == 0 && heldCount_ > 0)
    {
        // New notes over notes kept by Hold: they replace them.
        heldCount_ = 0;
        i = 0;
    }
    if (i == heldCount_)
    {
        if (heldCount_ >= held_.size())
            return;
        held_[heldCount_++] = {.key = e.key, .channel = e.channel, .velocity = e.value, .order = pressed_++};
    }
    else if (held_[i].down)
        return; // pressed again while held
    held_[i].down = true;
    held_[i].velocity = e.value;
    ++down_;
    if (params().getInt(Retrigger) == 1)
        restart_ = true;
    if (!params().getBool(Sync) && !freeRunning_)
    {
        freeRunning_ = true;
        freeNext_ = static_cast<double>(clockAt(offset));
        freeSteps_ = 0;
    }
}

int Arpeggiator::nextStep(const BlockBeats& beats, int from, std::int64_t& step) const noexcept
{
    if (!params().getBool(Sync))
    {
        if (!freeRunning_)
            return INT_MAX;
        const auto at = std::max<std::int64_t>(from, offsetOf(std::llround(freeNext_)));
        step = freeSteps_;
        return at < beats.numSamples ? static_cast<int>(at) : INT_MAX;
    }
    // On the song's grid, counted from its start: step k at k steps, every odd one swung.
    const double length = stepBeats();
    const double swing = swingBeats(length);
    for (auto k = static_cast<std::int64_t>(std::floor(beats.first / length)) - 1;; ++k)
    {
        const double beat = static_cast<double>(k) * length + ((k & 1) != 0 ? swing : 0.0);
        const long at = beats.sampleOf(beat);
        if (at >= beats.numSamples)
            return INT_MAX;
        if (at >= from)
        {
            step = k;
            return static_cast<int>(at);
        }
    }
}

std::size_t Arpeggiator::notesAt(double beat, std::span<Note> out) const noexcept
{
    std::size_t n = 0;
    if (heldCount_ > 0)
    {
        std::array<Held, maxHeld> sorted{};
        std::copy_n(held_.begin(), heldCount_, sorted.begin());
        const bool played = params().getInt(Style) == static_cast<int>(music::ArpStyle::Played);
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(heldCount_),
                  [played](const Held& a, const Held& b)
                  {
                      return played ? a.order < b.order
                                    : (a.key != b.key ? a.key < b.key : a.channel < b.channel);
                  });
        for (std::size_t i = 0; i < heldCount_ && n < out.size(); ++i)
            out[n++] = {sorted[i].key, sorted[i].channel, sorted[i].velocity};
        return n;
    }
    if (!params().getBool(SongChords))
        return 0;
    // The chord sounding (Follow's), each of its notes the first at or above From.
    const auto chord = follower_.chordAt(beat);
    std::array<int, 12> tones{};
    const auto count = music::chordTones(chord.root, chord.notes, tones);
    const int from = params().getInt(From);
    for (std::size_t i = 0; i < count && n < out.size(); ++i)
    {
        int p = from + ((tones[i] - from) % 12 + 12) % 12;
        if (p > 127)
            p -= 12;
        out[n++] = {p, 0, static_cast<float>(songChordVelocity)};
    }
    std::sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n),
              [](const Note& a, const Note& b) { return a.key < b.key; });
    return n;
}

std::int64_t Arpeggiator::originFor(std::int64_t step, double beat, const ProcessContext& c) const noexcept
{
    const int retrigger = params().getInt(Retrigger);
    if (retrigger == 1)
        return noteOrigin_;
    if (retrigger == 0 || !params().getBool(Sync))
        return 0;
    const double length = stepBeats();
    const auto firstStepFrom = [&](double start)
    { return std::min(step, static_cast<std::int64_t>(std::ceil(start / length - 1e-9))); };
    if (retrigger == 6) // each section
    {
        if (c.song != nullptr)
            if (const auto* s = c.song->sectionAt(beat))
                return firstStepFrom(s->start);
        return 0;
    }
    // Every 1, 2, 4 or 8 bars.
    const auto& t = c.transport;
    const double bar = t.timeSigNumerator * 4.0 / std::max(1, t.timeSigDenominator);
    const double span = bar * static_cast<double>(1 << (retrigger - 2));
    return firstStepFrom(std::floor((beat + 1e-9) / span) * span);
}

void Arpeggiator::playStep(ProcessContext& c, const BlockBeats& beats, int offset, std::int64_t step) noexcept
{
    const auto& p = params();
    const bool synced = p.getBool(Sync);
    double samples = 0.0; // the step's length
    double beat = beats.at(offset);
    if (synced)
    {
        const double length = stepBeats();
        const double swing = swingBeats(length);
        beat = static_cast<double>(step) * length; // the grid's beat, swing aside
        samples = (length + ((step & 1) != 0 ? -swing : swing)) / beats.perSample;
    }
    else
    {
        samples = sampleRate() / std::max(0.5, static_cast<double>(p.get(Hz)));
        freeNext_ += samples;
        ++freeSteps_;
    }

    std::array<Note, maxHeld> notes{};
    const auto n = notesAt(beat, notes);
    if (n == 0)
    {
        stopped();
        if (!synced)
            freeRunning_ = false;
        return;
    }
    if (restart_)
    {
        noteOrigin_ = step;
        restart_ = false;
    }
    const std::int64_t index = std::max<std::int64_t>(0, step - originFor(step, beat, c));
    std::array<float, LaneCount> lane{};
    for (int l = 0; l < LaneCount; ++l)
    {
        const auto length = std::max<std::int64_t>(1, p.getInt(VelocityLength + static_cast<std::size_t>(l)));
        const auto at = static_cast<int>(index % length);
        setPlayingStep(l, at);
        lane[static_cast<std::size_t>(l)] = this->step(l, at);
    }
    // Chance: the step's own roll (ADR 0062), the same whenever it plays.
    if (music::unit(music::mix(roll(), static_cast<std::uint64_t>(step), 0xC4A7CE)) * 100.0 >=
        lane[ChanceLane])
        return;

    const int steps = std::max(1, p.getInt(Steps));
    const int distance = p.getInt(Distance);
    const bool inKey = p.getBool(InKey);
    const int octave = static_cast<int>(std::lround(lane[OctaveLane]));
    const auto moved = [&](int key, int repeat)
    {
        const int by = (repeat + octave) * distance;
        return inKey ? music::stepBy(key, follower_.key(), by) : key + by;
    };
    const auto ratchets = std::clamp(static_cast<int>(std::lround(lane[RatchetLane])), 1, 4);
    const double hit = samples / ratchets;
    const double gate = hit * p.get(Gate) / 100.0 * lane[GateLane] / 100.0;
    const float velocity = lane[VelocityLane] / 100.0f;
    const auto start = clockAt(offset);
    const auto sound = [&](const Note& note, int key)
    {
        if (key < 0 || key > 127 || gate < 1.0)
            return;
        for (int r = 0; r < ratchets; ++r)
        {
            // A hit plays only if its end can wait for it: never a note left sounding.
            if (schedule_.room() < (r == 0 ? 1u : 2u))
                return;
            const auto on = start + static_cast<std::int64_t>(std::llround(hit * r));
            const auto off = on + std::max<std::int64_t>(1, std::llround(gate));
            const Event e{.sampleOffset = offset,
                          .channel = static_cast<std::uint8_t>(note.channel),
                          .key = static_cast<std::uint8_t>(key),
                          .value = note.velocity * velocity};
            if (r == 0)
                startNote(c, e);
            else
                schedule_.add(on, e);
            Event end = e;
            end.type = EventType::NoteOff;
            schedule_.add(off, end);
        }
    };

    const auto style = static_cast<music::ArpStyle>(std::clamp(p.getInt(Style), 0, 9));
    if (style == music::ArpStyle::Chord)
    {
        const int repeat = static_cast<int>(index % steps);
        for (std::size_t i = 0; i < n; ++i)
            sound(notes[i], moved(notes[i].key, repeat));
        return;
    }
    // Shuffled keeps its order while the notes stay the same: the roll mixed with them.
    std::uint64_t dice = roll();
    if (style == music::ArpStyle::Shuffled)
        for (std::size_t i = 0; i < n; ++i)
            dice = music::mix(dice, static_cast<std::uint64_t>(notes[i].key));
    const int total = static_cast<int>(n) * steps;
    const int i = music::arpIndex(style, total, index, dice);
    if (i < 0)
        return;
    const auto& note = notes[static_cast<std::size_t>(i) % n];
    sound(note, moved(note.key, i / static_cast<int>(n)));
}

void Arpeggiator::stopped() noexcept
{
    for (int l = 0; l < LaneCount; ++l)
        setPlayingStep(l, -1);
}

void Arpeggiator::clearNotes() noexcept
{
    heldCount_ = 0;
    down_ = 0;
    pressed_ = 0;
    restart_ = false;
    noteOrigin_ = 0;
    freeRunning_ = false;
    freeNext_ = 0.0;
    freeSteps_ = 0;
    follower_.reset();
    stopped();
}

const DeviceFactory& arpeggiatorFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return arpeggiatorInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Arpeggiator>(); }};
    return f;
}
} // namespace fd::devices
