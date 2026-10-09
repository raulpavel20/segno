#include "Sampler.h"

#include "fd/dsp/Math.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

namespace fd::devices
{
namespace
{
using P = Sampler::Param;

constexpr std::string_view kModes[] = {"Classic", "One-shot"};
constexpr std::string_view kFilterTypes[] = {"LP 12", "LP 24", "HP 12", "BP 12"};
constexpr std::string_view kVoiceModes[] = {"Poly", "Mono", "Legato"};

constexpr auto kAutoInf = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin;

constexpr ParamSpec time(std::string_view id, std::string_view name, std::string_view shortName, float defMs)
{
    return {.id = id, .name = name, .shortName = shortName, .min = 0.5f, .max = 20000.0f, .def = defMs,
            .skewCentre = 300.0f, .unit = Unit::Milliseconds, .decimals = 1};
}
constexpr ParamSpec percent(std::string_view id, std::string_view name, std::string_view shortName, float def,
                            float min = 0.0f, float max = 100.0f, std::uint8_t decimals = 0)
{
    return {.id = id, .name = name, .shortName = shortName, .min = min, .max = max, .def = def,
            .unit = Unit::Percent, .decimals = decimals};
}
constexpr ParamSpec toggle(std::string_view id, std::string_view name, std::string_view shortName)
{
    return {.id = id, .name = name, .shortName = shortName, .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f};
}
constexpr ParamSpec choice(std::string_view id, std::string_view name, std::string_view shortName,
                           std::span<const std::string_view> choices, float def)
{
    return {.id = id, .name = name, .shortName = shortName, .kind = ParamKind::Choice, .min = 0.0f,
            .max = static_cast<float>(choices.size() - 1), .def = def, .choices = choices};
}
constexpr ParamSpec discrete(std::string_view id, std::string_view name, std::string_view shortName, float min,
                             float max, float def, Unit unit = Unit::None)
{
    return {.id = id, .name = name, .shortName = shortName, .kind = ParamKind::Discrete, .min = min, .max = max,
            .def = def, .unit = unit};
}
constexpr ParamSpec fadeTime(std::string_view id, std::string_view name, std::string_view shortName, float defMs)
{
    return {.id = id, .name = name, .shortName = shortName, .min = 0.0f, .max = 100.0f, .def = defMs,
            .skewCentre = 10.0f, .unit = Unit::Milliseconds, .decimals = 1};
}

constexpr std::array<ParamSpec, P::NumParams> kSpecs{{
    choice("mode", "Mode", "Mode", kModes, 0.0f),
    percent("start", "Start", "Start", 0.0f, 0.0f, 100.0f, 1),
    percent("end", "End", "End", 100.0f, 0.0f, 100.0f, 1),
    toggle("loop", "Loop", "Loop"),
    percent("loop.start", "Loop Start", "L Start", 0.0f, 0.0f, 100.0f, 1),
    percent("loop.xfade", "Loop Crossfade", "X-Fade", 2.0f, 0.0f, 50.0f, 1),
    discrete("root", "Root Key", "Root", 0.0f, 127.0f, 60.0f, Unit::Note),
    discrete("transpose", "Transpose", "Transp", -48.0f, 48.0f, 0.0f, Unit::Semitones),
    {.id = "detune", .name = "Detune", .shortName = "Detune", .min = -50.0f, .max = 50.0f, .def = 0.0f,
     .unit = Unit::Cents, .decimals = 0},
    {.id = "gain", .name = "Gain", .shortName = "Gain", .min = -70.0f, .max = 12.0f, .def = 0.0f,
     .skewCentre = -12.0f, .unit = Unit::Decibels, .flags = kAutoInf},
    percent("vel.amp", "Velocity to Volume", "Vel", 50.0f),
    time("amp.a", "Amp Attack", "A", 0.5f),
    time("amp.d", "Amp Decay", "D", 300.0f),
    percent("amp.s", "Amp Sustain", "S", 100.0f),
    time("amp.r", "Amp Release", "R", 50.0f),
    toggle("oneshot.gate", "Gate", "Gate"),
    fadeTime("fade.in", "Fade In", "Fade In", 0.5f),
    fadeTime("fade.out", "Fade Out", "Fade Out", 2.0f),
    toggle("filter.on", "Filter", "Filter"),
    choice("filter.type", "Filter Type", "Type", kFilterTypes, 1.0f),
    {.id = "filter.cutoff", .name = "Cutoff", .shortName = "Cutoff", .min = 20.0f, .max = 20000.0f, .def = 2000.0f,
     .skewCentre = 632.0f, .unit = Unit::Hertz, .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 0},
    percent("filter.res", "Resonance", "Res", 20.0f),
    percent("filter.env", "Filter Env Amount", "Env", 0.0f, -100.0f),
    percent("filter.key", "Filter Key Track", "Key", 50.0f),
    time("fenv.a", "Filter Env Attack", "A", 2.0f),
    time("fenv.d", "Filter Env Decay", "D", 400.0f),
    percent("fenv.s", "Filter Env Sustain", "S", 0.0f),
    time("fenv.r", "Filter Env Release", "R", 300.0f),
    choice("voice.mode", "Voice Mode", "Mode", kVoiceModes, 0.0f),
    discrete("voice.count", "Voices", "Voices", 1.0f, 32.0f, 16.0f),
    {.id = "voice.glide", .name = "Glide", .shortName = "Glide", .min = 0.0f, .max = 2000.0f, .def = 0.0f,
     .skewCentre = 200.0f, .unit = Unit::Milliseconds, .decimals = 0},
    discrete("voice.bend", "Bend Range", "Bend", 0.0f, 24.0f, 2.0f, Unit::Semitones),
}};
// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"mode", "Classic plays while the key is held, with optional looping; One-shot plays to the end, as for drums."},
    {"start", "Where in the sample playback starts."},
    {"end", "Where in the sample playback ends."},
    {"loop", "Loops between Loop Start and End while the key is held (Classic only)."},
    {"loop.start", "Where the loop starts, within the start-to-end region."},
    {"loop.xfade", "Crossfades the end of the loop into its start, to hide the seam."},
    {"root", "The note at which the sample plays at its original pitch."},
    {"transpose", "Shifts the pitch in semitones."},
    {"detune", "Fine-tunes the pitch in cents."},
    {"gain", "The sample's level."},
    {"vel.amp", "How much velocity changes the volume: at 0 % every note plays equally loud."},
    {"amp.a", "How long a note takes to reach full volume."},
    {"amp.d", "How long the volume takes to fall to the sustain level."},
    {"amp.s", "The volume held while the key stays down."},
    {"amp.r", "How long a note takes to fade out after the key is released."},
    {"oneshot.gate", "In One-shot mode, releasing the key fades the sample out instead of letting it play to the end."},
    {"fade.in", "A short fade at the start of each note, against clicks."},
    {"fade.out", "A short fade at the end of each note, against clicks."},
    {"filter.on", "Turns the filter on."},
    {"filter.type", "The filter: low pass at 12 or 24 dB per octave, high pass or band pass."},
    {"filter.cutoff", "Where the filter starts to cut: lower is darker."},
    {"filter.res", "Emphasis at the cutoff; near the top the 24 dB low pass starts to whistle on its own."},
    {"filter.env", "How far the filter envelope moves the cutoff, up or down."},
    {"filter.key", "How much the cutoff follows the note played: at 100 % it rises an octave per octave."},
    {"fenv.a", "How long the filter envelope takes to rise."},
    {"fenv.d", "How long the filter envelope takes to fall to its sustain level."},
    {"fenv.s", "The filter envelope's level while the key stays down."},
    {"fenv.r", "How long the filter envelope takes to fall after the key is released."},
    {"voice.mode", "Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the envelopes when notes overlap."},
    {"voice.count", "How many notes can sound at once."},
    {"voice.glide", "How long the pitch takes to slide from one note to the next."},
    {"voice.bend", "How far the pitch bend wheel bends, in semitones."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

// The meters its panel shows, by name (MCP measure_module).
constexpr MeterSpec kMeters[] = {
    {.slot = Sampler::PeakOut, .id = "output", .name = "Output", .kind = MeterKind::Level},
    {.slot = Sampler::ActiveVoices, .id = "voices", .name = "Voices playing", .kind = MeterKind::Count},
};

constexpr DeviceInfo kInfo{
    .id = "sampler",
    .name = "Sampler",
    .category = "Instruments",
    .kind = DeviceKind::Instrument,
    .buses = {.inputChannels = 0, .outputChannels = 2, .acceptsMidi = true},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

dsp::Adsr::Settings settings(const ParamStore& p, size_t attack, double sustainOverride = -1.0)
{
    return {p.get(attack) * 0.001, p.get(attack + 1) * 0.001,
            sustainOverride >= 0.0 ? sustainOverride : p.get(attack + 2) * 0.01, p.get(attack + 3) * 0.001, false};
}

/// A short generated test sample for the conformance suite and snapshots: a decaying tone
/// (A2 with harmonics), written once per process to the temp directory and removed at exit.
const std::string& testSamplePath()
{
    struct File
    {
        std::string path;
        ~File()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };
    static const File file = [] {
        std::random_device rd;
        File f{(std::filesystem::temp_directory_path() / ("fd-sampler-test-" + std::to_string(rd()) + ".wav")).string()};
        constexpr double rate = 48000.0;
        std::vector<std::vector<float>> ch(2, std::vector<float>(12000));
        for (size_t i = 0; i < ch[0].size(); ++i)
        {
            const double t = static_cast<double>(i) / rate;
            const double env = std::exp(-t * 8.0) * std::min(1.0, t * 2000.0);
            const double x = 0.5 * std::sin(dsp::twoPi * 110.0 * t) + 0.25 * std::sin(dsp::twoPi * 220.0 * t + 0.3) +
                             0.1 * std::sin(dsp::twoPi * 990.0 * t);
            ch[0][i] = static_cast<float>(env * x);
            ch[1][i] = static_cast<float>(env * (0.8 * x + 0.05 * std::sin(dsp::twoPi * 1650.0 * t)));
        }
        audiofile::writeWav(f.path, ch, rate, 45);
        return f;
    }();
    return file.path;
}
} // namespace

Sampler::Sampler() : Device(kInfo) {}

Sampler::Region Sampler::regionFor(const ParamStore& p, int frames) noexcept
{
    Region r;
    if (frames <= 0)
        return r;
    double a = p.get(Start) * 0.01 * frames, b = p.get(End) * 0.01 * frames;
    if (b < a)
        std::swap(a, b);
    r.start = std::clamp(a, 0.0, frames - 1.0);
    r.end = std::clamp(b, r.start + 1.0, static_cast<double>(frames));
    const double len = r.end - r.start;
    const double minLoop = std::min(16.0, len);
    r.loopStart = std::min(r.start + p.get(LoopStart) * 0.01 * len, r.end - minLoop);
    // The crossfade blends the loop's end with the material just before its start.
    r.crossfade = std::min({p.get(LoopXfade) * 0.01 * r.loopLength(), r.loopStart, r.loopLength() * 0.5});
    return r;
}

void Sampler::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    fadeSamples_ = std::max(16, static_cast<int>(std::lround(0.0015 * sampleRate_)));
    for (auto& v : voice_)
    {
        v.amp.setSampleRate(sampleRate_);
        v.fenv.setSampleRate(sampleRate_);
    }
    reset();
}

void Sampler::reset() noexcept
{
    grid_.reset();
    voices_.reset();
    for (auto& v : voice_)
    {
        v.sample = {};
        v.pos = 0.0;
        v.elapsed = 0;
        v.key = 60;
        v.channel = 0;
        v.velocity = 0.8f;
        v.glidePitch = v.glideTarget = 60.0;
        v.glideRate = 0.0;
        v.amp.reset();
        v.fenv.reset();
        for (auto& f : v.filter)
            f.reset();
        v.ratioFrom = v.ratioTo = 1.0;
        v.cutoffFrom = v.cutoffTo = 10.0;
        v.gainFrom = v.gainTo = 0.0f;
        v.level = 0;
        v.table = &dsp::SincKernels::instance().table(1.0);
        v.fadeRemaining = 0;
        v.pending = {};
        v.sounding = false;
    }
    if (old_.isSet())
    {
        samples_.retire(old_);
        old_ = {};
    }
    bend_ = {};
    lastPitch_ = 60.0;
    voices_.setVoiceCount(params().getInt(VoiceCount));
    voices_.setMode(static_cast<VoiceManager::Mode>(params().getInt(VoiceMode)));
}

double Sampler::tailSeconds() const noexcept
{
    const auto& p = params();
    const double release = 2.0 * p.get(AmpR) * 0.001 + p.get(FadeOut) * 0.001 + 0.05;
    if (static_cast<PlayMode>(p.getInt(Mode)) == PlayMode::OneShot && !p.getBool(OneShotGate))
    {
        // A one-shot plays to its end however early the note-off came, at worst at the lowest
        // playback speed.
        const double slowest = std::exp2(maxShiftSemitones / 12.0);
        return std::max(release, sampleSeconds_.load() * slowest + 0.05);
    }
    return release;
}

// ---- state ------------------------------------------------------------------------------

void Sampler::writeExtraState(nlohmann::json& extra) const
{
    const std::scoped_lock lock(loaderMutex_);
    if (path_.empty())
        return;
    extra["sample.path"] = path_;
    if (const auto& s = samples_.latest())
    {
        extra["sample.rate"] = s->sampleRate;
        extra["sample.frames"] = s->frames;
    }
}

void Sampler::readExtraState(const nlohmann::json& extra)
{
    std::string path;
    if (extra.contains("sample.path") && extra["sample.path"].is_string())
        path = extra["sample.path"].get<std::string>();

    const std::scoped_lock lock(loaderMutex_);
    if (path == path_ && error_.empty() && path.empty() == (samples_.latest() == nullptr))
        return; // already loaded
    path_ = path;
    error_.clear();
    std::shared_ptr<const SampleData> data;
    if (!path.empty())
        data = SampleData::load(path, &error_);
    sampleSeconds_ = data != nullptr ? data->seconds() : 0.0;
    samples_.publish(std::move(data));
}

std::shared_ptr<const SampleData> Sampler::sample() const
{
    const std::scoped_lock lock(loaderMutex_);
    return samples_.latest();
}

std::string Sampler::loadError() const
{
    const std::scoped_lock lock(loaderMutex_);
    return error_;
}

// ---- voices -----------------------------------------------------------------------------

void Sampler::voiceStart(int index, int key, float velocity, int channel, std::int32_t, bool)
{
    auto& v = voice_[static_cast<size_t>(index)];
    if (v.sounding)
    {
        // Steal or retrigger: fade the old sound out first, then start (click-free).
        v.pending = {.active = true, .released = false, .key = key, .channel = channel, .velocity = velocity};
        if (v.fadeRemaining == 0)
            v.fadeRemaining = fadeSamples_;
        return;
    }
    startNote(v, key, velocity, channel, false);
}

void Sampler::startNote(Voice& v, int key, float velocity, int channel, bool released) noexcept
{
    const bool mono = voices_.mode() != VoiceManager::Mode::Poly;
    const double from = mono && v.sounding ? v.glidePitch : lastPitch_;
    v.sample = current_;
    v.key = key;
    v.velocity = velocity;
    v.channel = channel & 15;
    v.elapsed = 0;
    v.fadeRemaining = 0;
    v.pending = {};
    for (auto& f : v.filter)
        f.reset();
    v.amp.reset();
    v.fenv.reset();
    v.amp.noteOn();
    v.fenv.noteOn();

    const double glideSec = params().get(VoiceGlide) * 0.001;
    v.glideTarget = key;
    v.glidePitch = glideSec > 0.0 ? from : v.glideTarget;
    v.glideRate = glideSec > 0.0 ? std::abs(v.glideTarget - from) / (glideSec * sampleRate_) : 0.0;
    lastPitch_ = key;

    const auto* s = v.sample.get();
    v.pos = s != nullptr ? regionFor(params(), s->frames).start : 0.0;
    v.sounding = true;
    updateControls(v, true);
    if (released)
        releaseVoice(v);
}

void Sampler::voiceLegato(int index, int key, float velocity)
{
    auto& v = voice_[static_cast<size_t>(index)];
    if (v.pending.active)
    {
        v.pending.key = key;
        v.pending.velocity = velocity;
        return;
    }
    v.key = key;
    v.velocity = velocity;
    const double glideSec = params().get(VoiceGlide) * 0.001;
    v.glideTarget = key;
    if (glideSec <= 0.0)
        v.glidePitch = key;
    v.glideRate = glideSec > 0.0 ? std::abs(v.glideTarget - v.glidePitch) / (glideSec * sampleRate_) : 0.0;
    lastPitch_ = key;
}

void Sampler::voiceRelease(int index)
{
    auto& v = voice_[static_cast<size_t>(index)];
    if (v.pending.active)
        v.pending.released = true;
    else
        releaseVoice(v);
}

void Sampler::releaseVoice(Voice& v) noexcept
{
    const auto& p = params();
    const bool oneShot = static_cast<PlayMode>(p.getInt(Mode)) == PlayMode::OneShot;
    if (oneShot && !p.getBool(OneShotGate) && !forceRelease_)
        return; // one-shots play to the end
    v.amp.noteOff();
    v.fenv.noteOff();
}

bool Sampler::endSound(Voice& v) noexcept
{
    if (v.pending.active)
    {
        const auto n = v.pending;
        startNote(v, n.key, n.velocity, n.channel, n.released);
        return true;
    }
    v.sounding = false;
    v.fadeRemaining = 0;
    v.sample = {};
    voices_.voiceFinished(static_cast<int>(&v - voice_.data()));
    return false;
}

void Sampler::updateControls(Voice& v, bool snap) noexcept
{
    const auto& p = params();
    v.ratioFrom = v.ratioTo;
    v.cutoffFrom = v.cutoffTo;
    v.gainFrom = v.gainTo;
    const auto* s = v.sample.get();
    if (s == nullptr)
        return;

    const double pitch = v.glidePitch + bend_[static_cast<size_t>(v.channel)] * p.get(VoiceBend);
    const double shift = std::clamp(pitch + p.get(Transpose) + p.get(Detune) * 0.01 - p.get(Root),
                                    -maxShiftSemitones, maxShiftSemitones);
    v.ratioTo = std::exp2(shift / 12.0) * s->sampleRate / sampleRate_;
    v.cutoffTo = std::log2(p.get(FilterCutoff)) + p.get(FilterKey) * 0.01 * (pitch - 60.0) / 12.0;
    v.gainTo = dsp::dbToGain(p.get(Gain), -70.0f) * (1.0f - p.get(VelAmp) * 0.01f * (1.0f - v.velocity));
    if (snap)
    {
        v.ratioFrom = v.ratioTo;
        v.cutoffFrom = v.cutoffTo;
        v.gainFrom = v.gainTo;
    }

    // The smallest level that never reads more than twice its rate; the kernel stretches
    // for the rest (dsp::SincKernels).
    const double fastest = std::max(v.ratioFrom, v.ratioTo);
    const int levels = static_cast<int>(s->levels.size());
    int level = 0;
    while (level + 1 < levels && fastest / std::exp2(level) > 2.0)
        ++level;
    v.level = level;
    v.table = &dsp::SincKernels::instance().table(fastest / std::exp2(level));
}

void Sampler::handleEvent(const Event& e) noexcept
{
    switch (e.type)
    {
    case EventType::NoteOn:
    case EventType::NoteOff:
        voices_.handle(e, *this);
        break;
    case EventType::AllNotesOff:
        forceRelease_ = true;
        voices_.handle(e, *this);
        forceRelease_ = false;
        break;
    case EventType::Controller:
        if (e.key == 64)
            voices_.handle(e, *this); // sustain pedal
        break;
    case EventType::PitchBend:
        bend_[static_cast<size_t>(e.channel & 15)] = e.value;
        break;
    case EventType::ChannelPressure:
    case EventType::PolyPressure:
        break;
    }
}

void Sampler::tick() noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();
    const int count = p.getInt(VoiceCount);
    if (count < voices_.voiceCount())
        for (int i = count; i < voices_.voiceCount(); ++i)
            if (voice_[static_cast<size_t>(i)].sounding)
                voiceRelease(i);
    voices_.setVoiceCount(count);
    voices_.setMode(static_cast<VoiceManager::Mode>(p.getInt(VoiceMode)));

    const bool oneShot = static_cast<PlayMode>(p.getInt(Mode)) == PlayMode::OneShot;
    const auto ampSettings = settings(p, AmpA, oneShot ? 1.0 : -1.0); // one-shots hold full level
    const auto fenvSettings = settings(p, FenvA);
    for (auto& v : voice_)
    {
        v.amp.set(ampSettings);
        v.fenv.set(fenvSettings);
        if (!v.sounding)
            continue;
        if (v.glideRate > 0.0)
        {
            const double step = v.glideRate * interval;
            v.glidePitch = v.glidePitch < v.glideTarget ? std::min(v.glideTarget, v.glidePitch + step)
                                                        : std::max(v.glideTarget, v.glidePitch - step);
        }
        updateControls(v, false);
    }
}

void Sampler::takeNewSample() noexcept
{
    if (old_.isSet())
    {
        const bool used = std::any_of(voice_.begin(), voice_.end(),
                                      [&](const Voice& v) { return v.sounding && v.sample == old_; });
        if (used)
            return; // the previous swap is still fading out
        samples_.retire(old_);
        old_ = {};
    }
    const auto fresh = samples_.takeNew();
    if (!fresh.isSet())
        return;
    old_ = current_;
    current_ = fresh;
    if (old_.isSet())
        for (auto& v : voice_)
            if (v.sounding && v.sample == old_ && v.fadeRemaining == 0)
                v.fadeRemaining = fadeSamples_; // swapped out from under the voice: fade, then stop
}

void Sampler::renderVoice(Voice& v, float* outL, float* outR, int from, int to, int phase) noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();
    const bool oneShot = static_cast<PlayMode>(p.getInt(Mode)) == PlayMode::OneShot;
    const bool loopOn = !oneShot && p.getBool(Loop);
    const bool filterOn = p.getBool(FilterOn);
    const auto filterType = static_cast<dsp::SynthFilter::Type>(p.getInt(FilterType));
    const double res = p.get(FilterRes) * 0.01;
    const double envOct = p.get(FilterEnv) * 0.01 * 5.0;
    const double nyquistOct = std::log2(0.45 * sampleRate_);
    const double fadeIn = p.get(FadeIn) * 0.001 * sampleRate_;
    const double fadeOut = p.get(FadeOut) * 0.001 * sampleRate_;

    // Sample-dependent state, refreshed when a pending note starts mid-segment.
    const SampleData* s = nullptr;
    Region region;
    bool looping = false;
    double scale = 1.0;
    auto refresh = [&] {
        s = v.sample.get();
        if (s == nullptr)
            return;
        region = regionFor(p, s->frames);
        looping = loopOn && region.loopLength() >= 2.0;
        scale = 1.0 / std::exp2(v.level);
    };
    refresh();


    for (int i = from; i < to; ++i)
    {
        if (s == nullptr)
        {
            if (!endSound(v))
                return;
            refresh();
            continue;
        }
        const double t = static_cast<double>(phase + (i - from) + 1) / interval;
        const double ratio = v.ratioFrom + t * (v.ratioTo - v.ratioFrom);
        float gain = v.gainFrom + static_cast<float>(t) * (v.gainTo - v.gainFrom);

        const auto& table = *v.table;
        const float* d0 = s->data(v.level, 0);
        const float* d1 = s->data(v.level, 1);
        float xl = dsp::SincKernels::read(table, d0, v.pos * scale);
        float xr = s->numChannels > 1 ? dsp::SincKernels::read(table, d1, v.pos * scale) : xl;
        if (looping && region.crossfade > 0.0 && v.pos >= region.end - region.crossfade)
        {
            // Equal-power blend of the loop's end with the material before the loop start.
            const double u = (v.pos - (region.end - region.crossfade)) / region.crossfade;
            const double back = (v.pos - region.loopLength()) * scale;
            const float bl = dsp::SincKernels::read(table, d0, back);
            const float br = s->numChannels > 1 ? dsp::SincKernels::read(table, d1, back) : bl;
            const auto a = static_cast<float>(std::cos(u * dsp::pi * 0.5));
            const auto b = static_cast<float>(std::sin(u * dsp::pi * 0.5));
            xl = a * xl + b * bl;
            xr = a * xr + b * br;
        }

        gain *= v.amp.next();
        const float fenv = v.fenv.next();
        if (v.elapsed < fadeIn)
            gain *= static_cast<float>((v.elapsed + 1) / fadeIn);
        if (!looping && fadeOut > 0.0)
            gain *= static_cast<float>(std::clamp((region.end - v.pos) / ratio / fadeOut, 0.0, 1.0));
        if (v.fadeRemaining > 0)
            gain *= static_cast<float>(v.fadeRemaining) / static_cast<float>(fadeSamples_);

        if (filterOn)
        {
            const double cutoffOct = v.cutoffFrom + t * (v.cutoffTo - v.cutoffFrom) + envOct * fenv;
            const double cutoff = std::exp2(std::min(nyquistOct, cutoffOct));
            xl = v.filter[0].process(xl, filterType, cutoff, res, sampleRate_);
            xr = v.filter[1].process(xr, filterType, cutoff, res, sampleRate_);
        }
        outL[i] += xl * gain;
        if (outR != nullptr)
            outR[i] += xr * gain;

        v.pos += ratio;
        ++v.elapsed;

        bool ended = false;
        if (v.fadeRemaining > 0 && --v.fadeRemaining == 0)
            ended = true; // stolen or swapped: the faded sound is over
        else if (looping && v.pos >= region.end)
            v.pos -= region.loopLength();
        else if (!looping && v.pos >= region.end)
            ended = true;
        else if (!v.amp.isActive())
            ended = true;
        if (ended)
        {
            if (!endSound(v))
                return;
            refresh();
        }
    }
}

void Sampler::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    float* outL = ctx.output.channel(0);
    float* outR = ctx.output.numChannels > 1 ? ctx.output.channel(1) : nullptr;
    std::fill(outL, outL + n, 0.0f);
    if (outR != nullptr)
        std::fill(outR, outR + n, 0.0f);

    takeNewSample();

    const EventList* events = ctx.events;
    size_t nextEvent = 0;
    const int interval = grid_.interval();
    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        const int phase0 = static_cast<int>(grid_.position() % interval);
        if (isTick)
            tick();
        int i = offset;
        const int end = offset + length;
        while (i < end)
        {
            while (events != nullptr && nextEvent < events->size() && (*events)[nextEvent].sampleOffset <= i)
                handleEvent((*events)[nextEvent++]);
            int stop = end;
            if (events != nullptr && nextEvent < events->size() && (*events)[nextEvent].sampleOffset < end)
                stop = std::max(i + 1, (*events)[nextEvent].sampleOffset);
            for (auto& v : voice_)
                if (v.sounding)
                    renderVoice(v, outL, outR, i, stop, phase0 + (i - offset));
            i = stop;
        }
    });
    while (events != nullptr && nextEvent < events->size())
        handleEvent((*events)[nextEvent++]);

    float peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
        peakOut = std::max({peakOut, std::abs(outL[i]), outR != nullptr ? std::abs(outR[i]) : 0.0f});
    analysis().publishPeak(PeakOut, peakOut);
    analysis().publish(ActiveVoices, static_cast<float>(voices_.activeVoices()));
    int slot = 0;
    for (const auto& v : voice_)
        if (v.sounding && v.sample && slot < numPlayheads)
            analysis().publish(FirstPlayhead + slot++, static_cast<float>(v.pos / v.sample->frames));
    for (; slot < numPlayheads; ++slot)
        analysis().publish(FirstPlayhead + slot, -1.0f);
}

const DeviceFactory& samplerFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Sampler>(); },
        .testSetup = [](Device& d) { d.readExtraState({{"sample.path", testSamplePath()}}); },
    };
    return factory;
}
} // namespace fd::devices
