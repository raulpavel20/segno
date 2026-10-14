#include "Sampler.h"

#include "fd/device/Library.h"
#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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

// What a loaded file is, for checking: kept with a project, never in a preset, which holds only
// the sound's path (it must be the same whether the Library is there or not).
constexpr std::string_view kSessionKeys[] = {"sample.rate", "sample.frames", "sample.zones"};

constexpr DeviceInfo kInfo{
    .id = "sampler",
    .name = "Sampler",
    .category = "Instruments",
    .kind = DeviceKind::Instrument,
    .buses = {.inputChannels = 0, .outputChannels = 2, .acceptsMidi = true},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
    .sessionExtraKeys = kSessionKeys,
};

dsp::Adsr::Settings settings(const ParamStore& p, size_t attack, double sustainOverride = -1.0)
{
    return {p.get(attack) * 0.001, p.get(attack + 1) * 0.001,
            sustainOverride >= 0.0 ? sustainOverride : p.get(attack + 2) * 0.01, p.get(attack + 3) * 0.001, false};
}

/// A decaying tone with harmonics at `hz`, two channels.
std::vector<std::vector<float>> testTone(double hz, double seconds, double rate)
{
    std::vector<std::vector<float>> ch(2, std::vector<float>(static_cast<size_t>(seconds * rate)));
    for (size_t i = 0; i < ch[0].size(); ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double env = std::exp(-t * 8.0) * std::min(1.0, t * 2000.0);
        const double x = 0.5 * std::sin(dsp::twoPi * hz * t) +
                         0.25 * std::sin(dsp::twoPi * 2.0 * hz * t + 0.3) +
                         0.1 * std::sin(dsp::twoPi * 9.0 * hz * t);
        ch[0][i] = static_cast<float>(env * x);
        ch[1][i] = static_cast<float>(env * (0.8 * x + 0.05 * std::sin(dsp::twoPi * 15.0 * hz * t)));
    }
    return ch;
}

/// A generated multisample for the conformance suite and snapshots (ADR 0017's testSetup): an SFZ
/// of three 16-bit tones, A2 below C3 and a round robin of two above it, written once per process
/// to the temp directory and removed at exit.
const std::string& testMapPath()
{
    struct Folder
    {
        std::filesystem::path path;
        ~Folder()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
    static const Folder folder = []
    {
        std::random_device rd;
        Folder f{std::filesystem::temp_directory_path() / ("fd-sampler-test-" + std::to_string(rd()))};
        std::filesystem::create_directories(f.path);
        constexpr double rate = 48000.0;
        audiofile::writeWav((f.path / "Low A2.wav").string(), testTone(110.0, 1.0, rate), rate, 45, 16);
        audiofile::writeWav((f.path / "High A3 1.wav").string(), testTone(220.0, 1.0, rate), rate, 57, 16);
        audiofile::writeWav((f.path / "High A3 2.wav").string(), testTone(220.0 * 1.003, 1.0, rate), rate, 57,
                            16);
        std::ofstream((f.path / "Test.sfz").string())
            << "<global> ampeg_release=0.05\n"
               "<region> sample=Low A2.wav lokey=0 hikey=59 pitch_keycenter=a2\n"
               "<group> lokey=60 hikey=127 pitch_keycenter=57 seq_length=2\n"
               "<region> sample=High A3 1.wav seq_position=1\n"
               "<region> sample=High A3 2.wav seq_position=2\n";
        return f;
    }();
    static const std::string path = (folder.path / "Test.sfz").string();
    return path;
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
    r.loopEnd = r.end;
    // The crossfade blends the loop's end with the material just before its start.
    r.crossfade = std::min({p.get(LoopXfade) * 0.01 * r.loopLength(), r.loopStart, r.loopLength() * 0.5});
    return r;
}

Sampler::Region Sampler::regionFor(const ParamStore& p, const SampleMap::Zone& zone) noexcept
{
    const int frames = zone.frames();
    if (zone.start == 0 && zone.playEnd() == frames && !zone.loops)
        return regionFor(p, frames); // one sample, or a zone that plays its whole file
    Region r;
    const double from = std::clamp(zone.start, 0, std::max(0, frames - 1));
    const double to = std::max(from + 1.0, static_cast<double>(zone.playEnd()));
    if (frames <= 0)
        return r;
    const double span = to - from;
    double a = from + p.get(Start) * 0.01 * span, b = from + p.get(End) * 0.01 * span;
    if (b < a)
        std::swap(a, b);
    r.start = std::clamp(a, from, to - 1.0);
    r.end = std::clamp(b, r.start + 1.0, to);
    const double len = r.end - r.start;
    const double minLoop = std::min(16.0, len);
    if (zone.loops)
    {
        // Its own loop, within what plays.
        r.loopEnd =
            std::clamp(static_cast<double>(zone.loopEnd), r.start + minLoop, static_cast<double>(frames));
        r.loopStart = std::clamp(static_cast<double>(zone.loopStart), r.start, r.loopEnd - minLoop);
    }
    else
    {
        r.loopStart = std::min(r.start + p.get(LoopStart) * 0.01 * len, r.end - minLoop);
        r.loopEnd = r.end;
    }
    r.crossfade = std::min({p.get(LoopXfade) * 0.01 * r.loopLength(), r.loopStart, r.loopLength() * 0.5});
    return r;
}

std::vector<std::pair<size_t, float>> Sampler::settingsFor(const SampleMap& map)
{
    std::vector<std::pair<size_t, float>> out;
    if (map.single)
    {
        if (!map.zones.empty() && map.zones[0].sample != nullptr && map.zones[0].sample->rootKey)
            out.emplace_back(Root, static_cast<float>(*map.zones[0].sample->rootKey));
        return out;
    }
    const auto& s = map.settings;
    const auto ms = [](double seconds)
    { return static_cast<float>(std::clamp(seconds * 1000.0, 0.5, 20000.0)); };
    if (s.oneShot)
        out.emplace_back(Mode, 1.0f);
    if (s.loop)
        out.emplace_back(Loop, 1.0f);
    if (s.velocityToVolume)
        out.emplace_back(VelAmp, static_cast<float>(std::clamp(*s.velocityToVolume, 0.0, 100.0)));
    if (s.ampAttack)
        out.emplace_back(AmpA, ms(*s.ampAttack));
    if (s.ampDecay)
        out.emplace_back(AmpD, ms(*s.ampDecay));
    if (s.ampSustain)
        out.emplace_back(AmpS, static_cast<float>(std::clamp(*s.ampSustain, 0.0, 100.0)));
    if (s.ampRelease)
        out.emplace_back(AmpR, ms(*s.ampRelease));
    return out;
}

int Sampler::playableKey(const Device& device, int key)
{
    const auto* sampler = dynamic_cast<const Sampler*>(&device);
    const auto map = sampler != nullptr ? sampler->map() : nullptr;
    if (map == nullptr || map->single || map->zones.empty())
        return key;
    const int low = map->lowestKey(), high = map->highestKey();
    while (key < low && key + 12 <= 127)
        key += 12;
    while (key > high && key - 12 >= 0)
        key -= 12;
    key = std::clamp(key, low, high);
    // A key no zone plays (a kit's gaps): the nearest one that does.
    const auto plays = [&map](int k)
    {
        return std::any_of(map->zones.begin(), map->zones.end(), [k](const SampleMap::Zone& z)
                           { return z.sample != nullptr && k >= z.loKey && k <= z.hiKey; });
    };
    for (int d = 0; d <= 127; ++d)
    {
        if (key - d >= 0 && plays(key - d))
            return key - d;
        if (key + d <= 127 && plays(key + d))
            return key + d;
    }
    return key;
}

void Sampler::prepare(const ProcessSpec& spec)
{
    (void)dsp::SincKernels::instance(); // built here, never on the audio thread
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
        v.map = {};
        v.layers = {};
        v.layerCount = 0;
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
        v.cutoffFrom = v.cutoffTo = 10.0;
        v.gainFrom = v.gainTo = 0.0f;
        v.fadeRemaining = 0;
        v.pending = {};
        v.sounding = false;
    }
    if (old_.isSet())
    {
        maps_.retire(old_);
        old_ = {};
    }
    bend_ = {};
    lastPitch_ = 60.0;
    choice_.reset(); // the same choices after every reset (SPEC, S5)
    lastZone_ = -1;
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
    if (const auto& m = maps_.latest())
    {
        if (!m->single)
            extra["sample.zones"] = m->zones.size();
        else if (!m->zones.empty() && m->zones[0].sample != nullptr)
        {
            extra["sample.rate"] = m->zones[0].sample->sampleRate;
            extra["sample.frames"] = m->zones[0].sample->frames;
        }
    }
}

void Sampler::readExtraState(const nlohmann::json& extra)
{
    std::string path;
    if (extra.contains("sample.path") && extra["sample.path"].is_string())
        path = extra["sample.path"].get<std::string>();

    const std::scoped_lock lock(loaderMutex_);
    if (path == path_ && error_.empty() && path.empty() == (maps_.latest() == nullptr))
        return; // already loaded
    path_ = path;
    error_.clear();
    std::shared_ptr<const SampleMap> map;
    // A sound in the Library is stored by its place in it (ADR 0055), found on each machine; one
    // that isn't there says so.
    std::error_code ec;
    if (const auto file = library::resolve(path);
        library::isReference(path) && (file.empty() || !std::filesystem::is_regular_file(file, ec)))
        error_ = library::missingMessage(path);
    else if (SampleMap::isSfz(path))
        map = SampleMap::loadSfz(file.string(), &error_);
    else if (!path.empty())
        if (auto data = SampleData::load(file.string(), &error_))
            map = SampleMap::one(std::move(data));
    sampleSeconds_ = map != nullptr ? map->longestSeconds() : 0.0;
    maps_.publish(std::move(map));
}

std::shared_ptr<const SampleMap> Sampler::map() const
{
    const std::scoped_lock lock(loaderMutex_);
    return maps_.latest();
}

std::shared_ptr<const SampleData> Sampler::sample() const
{
    const std::scoped_lock lock(loaderMutex_);
    const auto& m = maps_.latest();
    return m != nullptr && m->single && !m->zones.empty() ? m->zones[0].sample : nullptr;
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

void Sampler::chooseZones(Voice& v, int key, float velocity) noexcept
{
    v.layerCount = 0;
    const auto* m = v.map.get();
    if (m == nullptr)
        return;
    const int vel = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
    const auto turn = choice_.nextTurn(key);
    const auto pick = choice_.nextPick();
    std::array<int, SampleMap::maxChosen> chosen{};
    const int count = m->choose(key, vel, turn, pick, chosen);
    for (int n = 0; n < count; ++n)
    {
        const auto& z = m->zones[static_cast<size_t>(chosen[static_cast<size_t>(n)])];
        auto& l = v.layers[static_cast<size_t>(v.layerCount++)];
        l = {};
        l.zone = &z;
        l.zoneIndex = chosen[static_cast<size_t>(n)];
        l.pos = regionFor(params(), z).start;
        l.active = true;
        l.table = &dsp::SincKernels::instance().table(1.0);
    }
    if (v.layerCount > 0)
        lastZone_ = v.layers[0].zoneIndex;
}

void Sampler::startNote(Voice& v, int key, float velocity, int channel, bool released) noexcept
{
    const bool mono = voices_.mode() != VoiceManager::Mode::Poly;
    const double from = mono && v.sounding ? v.glidePitch : lastPitch_;
    v.map = current_;
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

    chooseZones(v, key, velocity);
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
    v.map = {};
    v.layerCount = 0;
    voices_.voiceFinished(static_cast<int>(&v - voice_.data()));
    return false;
}

void Sampler::updateControls(Voice& v, bool snap) noexcept
{
    const auto& p = params();
    v.cutoffFrom = v.cutoffTo;
    v.gainFrom = v.gainTo;
    const auto* m = v.map.get();
    if (m == nullptr)
        return;

    const double pitch = v.glidePitch + bend_[static_cast<size_t>(v.channel)] * p.get(VoiceBend);
    v.cutoffTo = std::log2(p.get(FilterCutoff)) + p.get(FilterKey) * 0.01 * (pitch - 60.0) / 12.0;
    v.gainTo = dsp::dbToGain(p.get(Gain), -70.0f) * (1.0f - p.get(VelAmp) * 0.01f * (1.0f - v.velocity));
    if (snap)
    {
        v.cutoffFrom = v.cutoffTo;
        v.gainFrom = v.gainTo;
    }
    for (int i = 0; i < v.layerCount; ++i)
    {
        auto& l = v.layers[static_cast<size_t>(i)];
        const auto* s = l.zone->sample.get();
        // One sample plays at Root Key; a zone at its own root (SPEC, S3).
        const double root = m->single ? p.get(Root) : l.zone->root;
        const double shift =
            std::clamp(pitch + p.get(Transpose) + p.get(Detune) * 0.01 + l.zone->tune * 0.01 - root,
                       -maxShiftSemitones, maxShiftSemitones);
        l.ratioFrom = l.ratioTo;
        l.ratioTo = std::exp2(shift / 12.0) * s->sampleRate / sampleRate_;
        if (snap)
            l.ratioFrom = l.ratioTo;
        // The smallest level that never reads more than twice its rate; the kernel stretches
        // for the rest (dsp::SincKernels).
        const double fastest = std::max(l.ratioFrom, l.ratioTo);
        const int levels = static_cast<int>(s->levels.size());
        int level = 0;
        while (level + 1 < levels && fastest / std::exp2(level) > 2.0)
            ++level;
        l.level = level;
        l.table = &dsp::SincKernels::instance().table(fastest / std::exp2(level));
    }
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
    voices_.setVoiceCount(p.getInt(VoiceCount), *this); // releases what a smaller count leaves out
    voices_.setMode(static_cast<VoiceManager::Mode>(p.getInt(VoiceMode)), *this);

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

void Sampler::takeNewMap() noexcept
{
    if (old_.isSet())
    {
        const bool used = std::any_of(voice_.begin(), voice_.end(),
                                      [&](const Voice& v) { return v.sounding && v.map == old_; });
        if (used)
            return; // the previous swap is still fading out
        maps_.retire(old_);
        old_ = {};
    }
    const auto fresh = maps_.takeNew();
    if (!fresh.isSet())
        return;
    old_ = current_;
    current_ = fresh;
    if (old_.isSet())
        for (auto& v : voice_)
            if (v.sounding && v.map == old_ && v.fadeRemaining == 0)
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

    // Each layer's region, refreshed when a pending note starts mid-segment.
    std::array<Region, maxLayers> region{};
    std::array<bool, maxLayers> looping{};
    std::array<double, maxLayers> scale{};
    auto refresh = [&]
    {
        for (int i = 0; i < v.layerCount; ++i)
        {
            const auto& l = v.layers[static_cast<size_t>(i)];
            region[static_cast<size_t>(i)] = regionFor(p, *l.zone);
            looping[static_cast<size_t>(i)] = loopOn && region[static_cast<size_t>(i)].loopLength() >= 2.0;
            scale[static_cast<size_t>(i)] = 1.0 / std::exp2(l.level);
        }
    };
    refresh();

    for (int i = from; i < to; ++i)
    {
        const bool playing = std::any_of(v.layers.begin(), v.layers.begin() + v.layerCount,
                                         [](const Layer& l) { return l.active; });
        if (!playing)
        {
            if (!endSound(v))
                return;
            refresh();
            continue;
        }
        const double t = static_cast<double>(phase + (i - from) + 1) / interval;
        float gain = v.gainFrom + static_cast<float>(t) * (v.gainTo - v.gainFrom);

        // The layers, summed; one layer's end fades on the voice's gain, as one sample's always has.
        float xl = 0.0f, xr = 0.0f;
        float endFade = 1.0f;
        for (int n = 0; n < v.layerCount; ++n)
        {
            auto& l = v.layers[static_cast<size_t>(n)];
            if (!l.active)
                continue;
            const auto& r = region[static_cast<size_t>(n)];
            const bool loops = looping[static_cast<size_t>(n)];
            const double sc = scale[static_cast<size_t>(n)];
            const auto& s = *l.zone->sample;
            const double ratio = l.ratioFrom + t * (l.ratioTo - l.ratioFrom);
            const auto& table = *l.table;
            float yl = s.read(table, l.level, 0, l.pos * sc);
            float yr = s.numChannels > 1 ? s.read(table, l.level, 1, l.pos * sc) : yl;
            if (loops && r.crossfade > 0.0 && l.pos >= r.loopEnd - r.crossfade)
            {
                // Equal-power blend of the loop's end with the material before the loop start.
                const double u = (l.pos - (r.loopEnd - r.crossfade)) / r.crossfade;
                const double back = (l.pos - r.loopLength()) * sc;
                const float bl = s.read(table, l.level, 0, back);
                const float br = s.numChannels > 1 ? s.read(table, l.level, 1, back) : bl;
                const auto a = static_cast<float>(std::cos(u * dsp::pi * 0.5));
                const auto b = static_cast<float>(std::sin(u * dsp::pi * 0.5));
                yl = a * yl + b * bl;
                yr = a * yr + b * br;
            }
            float layerGain = l.zone->gain;
            if (!loops && fadeOut > 0.0)
            {
                const auto fade = static_cast<float>(std::clamp((r.end - l.pos) / ratio / fadeOut, 0.0, 1.0));
                if (v.layerCount == 1)
                    endFade = fade;
                else
                    layerGain *= fade;
            }
            xl += yl * layerGain;
            xr += yr * layerGain;

            l.pos += ratio;
            if (loops && l.pos >= r.loopEnd)
                l.pos -= r.loopLength();
            else if (!loops && l.pos >= r.end)
                l.active = false;
        }

        gain *= v.amp.next();
        const float fenv = v.fenv.next();
        if (v.elapsed < fadeIn)
            gain *= static_cast<float>((v.elapsed + 1) / fadeIn);
        gain *= endFade;
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
        ++v.elapsed;

        bool ended = false;
        if (v.fadeRemaining > 0 && --v.fadeRemaining == 0)
            ended = true; // stolen or swapped: the faded sound is over
        else if (!v.amp.isActive())
            ended = true;
        else
            ended = std::none_of(v.layers.begin(), v.layers.begin() + v.layerCount,
                                 [](const Layer& l) { return l.active; });
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

    takeNewMap();

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
        if (v.sounding && v.layerCount > 0 && v.layers[0].active && slot < numPlayheads)
        {
            const auto& l = v.layers[0];
            analysis().publish(FirstPlayhead + slot,
                               static_cast<float>(l.pos / std::max(1, l.zone->frames())));
            analysis().publish(FirstPlayheadZone + slot++, static_cast<float>(l.zoneIndex));
        }
    for (; slot < numPlayheads; ++slot)
    {
        analysis().publish(FirstPlayhead + slot, -1.0f);
        analysis().publish(FirstPlayheadZone + slot, -1.0f);
    }
    analysis().publish(LastZone, static_cast<float>(lastZone_));
}

const DeviceFactory& samplerFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Sampler>(); },
        .testSetup = [](Device& d) { d.readExtraState({{"sample.path", testMapPath()}}); },
    };
    return factory;
}
} // namespace fd::devices
