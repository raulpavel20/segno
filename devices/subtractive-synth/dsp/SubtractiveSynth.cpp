#include "SubtractiveSynth.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
using P = SubtractiveSynth::Param;

constexpr std::string_view kWaves[] = {"Saw", "Square", "Triangle", "Sine"};
constexpr std::string_view kFilterTypes[] = {"LP 12", "LP 24", "HP 12", "BP 12"};
constexpr std::string_view kLfoWaves[] = {"Sine", "Triangle", "Saw", "Square", "S&H"};
constexpr std::string_view kSync[] = {"Off", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars"};
constexpr double kSyncBeats[] = {0.0, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
constexpr std::string_view kSources[] = {"Mod Env", "LFO", "Velocity", "Mod Wheel", "Pressure", "Key", "Slide"};
constexpr std::string_view kTargets[] = {"Pitch", "Cutoff", "Resonance", "Osc 1 Shape", "Osc 1 Width", "Osc 2 Level", "Amp", "Pan"};
constexpr std::string_view kModes[] = {"Poly", "Mono", "Legato"};

constexpr auto kAutoInf = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin;

constexpr ParamSpec time(std::string_view id, std::string_view name, std::string_view shortName, float defMs)
{
    return {.id = id, .name = name, .shortName = shortName, .min = 0.5f, .max = 20000.0f, .def = defMs,
            .skewCentre = 300.0f, .unit = Unit::Milliseconds, .decimals = 1};
}
constexpr ParamSpec percent(std::string_view id, std::string_view name, std::string_view shortName, float def,
                            float min = 0.0f)
{
    return {.id = id, .name = name, .shortName = shortName, .min = min, .max = 100.0f, .def = def,
            .unit = Unit::Percent, .decimals = 0};
}
constexpr ParamSpec level(std::string_view id, std::string_view name, std::string_view shortName, float def,
                          float max = 0.0f)
{
    return {.id = id, .name = name, .shortName = shortName, .min = -70.0f, .max = max, .def = def,
            .skewCentre = -12.0f, .unit = Unit::Decibels, .flags = kAutoInf};
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

constexpr std::array<ParamSpec, P::NumParams> kParams{{
    percent("osc1.shape", "Osc 1 Shape", "Shape", 0.0f),
    discrete("osc1.oct", "Osc 1 Octave", "Oct", -3.0f, 3.0f, 0.0f),
    {.id = "osc1.pw", .name = "Osc 1 Width", .shortName = "Width", .min = 5.0f, .max = 95.0f, .def = 50.0f,
     .unit = Unit::Percent, .decimals = 0},
    choice("osc2.wave", "Osc 2 Wave", "Wave", kWaves, 0.0f),
    discrete("osc2.oct", "Osc 2 Octave", "Oct", -3.0f, 3.0f, 0.0f),
    discrete("osc2.semi", "Osc 2 Semi", "Semi", -12.0f, 12.0f, 0.0f, Unit::Semitones),
    {.id = "osc2.detune", .name = "Osc 2 Detune", .shortName = "Detune", .min = -50.0f, .max = 50.0f, .def = 7.0f,
     .unit = Unit::Cents, .decimals = 0},
    level("mix.osc1", "Osc 1 Level", "Osc 1", 0.0f),
    level("mix.osc2", "Osc 2 Level", "Osc 2", -6.0f),
    level("mix.noise", "Noise Level", "Noise", -70.0f),
    choice("filter.type", "Filter Type", "Type", kFilterTypes, 1.0f),
    {.id = "filter.cutoff", .name = "Cutoff", .shortName = "Cutoff", .min = 20.0f, .max = 20000.0f, .def = 2000.0f,
     .skewCentre = 632.0f, .unit = Unit::Hertz, .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 0},
    percent("filter.res", "Resonance", "Res", 20.0f),
    percent("filter.key", "Key Track", "Key", 50.0f),
    percent("filter.env", "Env Amount", "Env", 30.0f, -100.0f),
    time("amp.a", "Amp Attack", "A", 2.0f),
    time("amp.d", "Amp Decay", "D", 300.0f),
    percent("amp.s", "Amp Sustain", "S", 80.0f),
    time("amp.r", "Amp Release", "R", 200.0f),
    time("mod.a", "Mod Attack", "A", 2.0f),
    time("mod.d", "Mod Decay", "D", 400.0f),
    percent("mod.s", "Mod Sustain", "S", 0.0f),
    time("mod.r", "Mod Release", "R", 300.0f),
    {.id = "mod.cycle", .name = "Mod Cycle", .shortName = "Cycle", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f},
    choice("lfo.wave", "LFO Wave", "Wave", kLfoWaves, 0.0f),
    {.id = "lfo.rate", .name = "LFO Rate", .shortName = "Rate", .min = 0.01f, .max = 40.0f, .def = 2.0f,
     .skewCentre = 2.0f, .unit = Unit::Hertz, .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 2},
    choice("lfo.sync", "LFO Sync", "Sync", kSync, 0.0f),
    choice("m1.src", "Mod 1 Source", "Src", kSources, 1.0f),
    choice("m1.dst", "Mod 1 Target", "Dst", kTargets, 0.0f),
    percent("m1.amt", "Mod 1 Amount", "Amt", 0.0f, -100.0f),
    choice("m2.src", "Mod 2 Source", "Src", kSources, 2.0f),
    choice("m2.dst", "Mod 2 Target", "Dst", kTargets, 1.0f),
    percent("m2.amt", "Mod 2 Amount", "Amt", 0.0f, -100.0f),
    choice("m3.src", "Mod 3 Source", "Src", kSources, 3.0f),
    choice("m3.dst", "Mod 3 Target", "Dst", kTargets, 1.0f),
    percent("m3.amt", "Mod 3 Amount", "Amt", 0.0f, -100.0f),
    choice("voice.mode", "Voice Mode", "Mode", kModes, 0.0f),
    discrete("voice.count", "Voices", "Voices", 1.0f, 16.0f, 8.0f),
    {.id = "voice.glide", .name = "Glide", .shortName = "Glide", .min = 0.0f, .max = 2000.0f, .def = 0.0f,
     .skewCentre = 200.0f, .unit = Unit::Milliseconds, .decimals = 0},
    percent("voice.drift", "Drift", "Drift", 15.0f),
    discrete("voice.bend", "Bend Range", "Bend", 0.0f, 24.0f, 2.0f, Unit::Semitones),
    level("out.volume", "Volume", "Volume", -6.0f, 6.0f),
}};

constexpr DeviceInfo kInfo{
    .id = "subtractive-synth",
    .name = "Subtractive Synth",
    .category = "Instruments",
    .kind = DeviceKind::Instrument,
    .buses = {.inputChannels = 0, .outputChannels = 2, .acceptsMidi = true},
    .params = kParams,
    .stateVersion = 1,
};

double hzForPitch(double semitones)
{
    return 440.0 * std::exp2((semitones - 69.0) / 12.0);
}
} // namespace

dsp::Oscillator::Mix SubtractiveSynth::osc1Mix(double shapePercent, double widthPercent) noexcept
{
    return dsp::Oscillator::morph(static_cast<float>(shapePercent * 0.01), static_cast<float>(widthPercent * 0.01));
}

dsp::Oscillator::Mix SubtractiveSynth::osc2Mix(int wave) noexcept
{
    dsp::Oscillator::Mix m{0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    switch (wave)
    {
    case 0: m.saw = 1.0f; break;
    case 1: m.square = 1.0f; break;
    case 2: m.triangle = 1.0f; break;
    default: m.sine = 1.0f; break;
    }
    return m;
}

dsp::Adsr::Settings SubtractiveSynth::envelopeSettings(double attackMs, double decayMs, double sustainPercent,
                                                       double releaseMs, bool cycle) noexcept
{
    return {attackMs * 0.001, decayMs * 0.001, sustainPercent * 0.01, releaseMs * 0.001, cycle};
}

SubtractiveSynth::SubtractiveSynth() : Device(kInfo) {}

void SubtractiveSynth::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    for (size_t i = 0; i < voice_.size(); ++i)
    {
        auto& v = voice_[i];
        v.osc1.prepare(sampleRate_);
        v.osc2.prepare(sampleRate_);
        v.amp.setSampleRate(sampleRate_);
        v.mod.setSampleRate(sampleRate_);
    }
    reset();
}

void SubtractiveSynth::reset() noexcept
{
    grid_.reset();
    voices_.reset();
    for (size_t i = 0; i < voice_.size(); ++i)
    {
        auto& v = voice_[i];
        v.osc1.reset();
        v.osc2.reset();
        v.filter.reset();
        v.amp.reset();
        v.mod.reset();
        v.noise.seed(static_cast<std::uint32_t>(i) * 7919u + 1u);
        v.driftPitch.seed(static_cast<std::uint32_t>(i) * 2u + 11u);
        v.driftCutoff.seed(static_cast<std::uint32_t>(i) * 2u + 12u);
        v.key = 60;
        v.channel = 0;
        v.velocity = 0.8f;
        v.glidePitch = v.glideTarget = 60.0;
        v.glideRate = 0.0;
        v.driftP = v.driftC = 0.0f;
        v.from = {};
        v.to = {};
        v.sounding = false;
    }
    lfo_.reset();
    lfoValue_ = 0.0f;
    bend_ = {};
    pressure_ = {};
    slide_ = {};
    modWheel_ = 0.0f;
    lastPitch_ = 60.0;
    volume_.reset(dsp::dbToGain(params().get(OutVolume), -70.0f));
    voices_.setVoiceCount(params().getInt(VoiceCount));
    voices_.setMode(static_cast<VoiceManager::Mode>(params().getInt(VoiceMode)));
}

double SubtractiveSynth::tailSeconds() const noexcept
{
    // Release is specified to -60 dB; a voice ends at -120 dB, i.e. after twice that.
    return 2.0 * params().get(AmpR) * 0.001 + 0.05;
}

void SubtractiveSynth::computeControls(Voice& v, std::array<double, NumControls>& out) const noexcept
{
    const auto& p = params();
    std::array<float, 8> mod{};
    for (int slot = 0; slot < 3; ++slot)
    {
        const auto base = static_cast<size_t>(M1Src + slot * 3);
        const float amt = p.get(base + 2) * 0.01f;
        if (amt == 0.0f)
            continue;
        float src = 0.0f;
        switch (static_cast<Source>(p.getInt(base)))
        {
        case Source::ModEnv: src = static_cast<float>(v.mod.level()); break;
        case Source::Lfo: src = lfoValue_; break;
        case Source::Velocity: src = v.velocity; break;
        case Source::ModWheel: src = modWheel_; break;
        case Source::Pressure: src = pressure_[static_cast<size_t>(v.channel)]; break;
        case Source::Key: src = static_cast<float>(v.key - 60) / 60.0f; break;
        case Source::Slide: src = slide_[static_cast<size_t>(v.channel)]; break;
        }
        mod[static_cast<size_t>(p.getInt(base + 1))] += amt * src;
    }
    const float drift = p.get(VoiceDrift) * 0.01f;
    const double pitch = v.glidePitch + bend_[static_cast<size_t>(v.channel)] * p.get(VoiceBend) +
                         v.driftP * drift * 0.15 + mod[static_cast<size_t>(Target::Pitch)] * 12.0;
    out[CPitch] = pitch;
    out[CCutoffOct] = std::log2(p.get(FilterCutoff)) + p.get(FilterKey) * 0.01 * (pitch - 60.0) / 12.0 +
                      mod[static_cast<size_t>(Target::Cutoff)] * 5.0 + v.driftC * drift * 0.3;
    out[CRes] = std::clamp(p.get(FilterRes) * 0.01f + mod[static_cast<size_t>(Target::Resonance)], 0.0f, 1.0f);
    out[CShape] = std::clamp(p.get(Osc1Shape) * 0.01f + mod[static_cast<size_t>(Target::Osc1Shape)], 0.0f, 1.0f);
    out[CWidth] = std::clamp(p.get(Osc1Pw) * 0.01f + mod[static_cast<size_t>(Target::Osc1Width)] * 0.45f, 0.05f, 0.95f);
    out[COsc1] = dsp::dbToGain(p.get(MixOsc1), -70.0f);
    out[COsc2] = dsp::dbToGain(p.get(MixOsc2), -70.0f) *
                 std::clamp(1.0f + mod[static_cast<size_t>(Target::Osc2Level)], 0.0f, 2.0f);
    out[CNoise] = dsp::dbToGain(p.get(MixNoise), -70.0f);
    out[CAmp] = std::clamp(1.0f + mod[static_cast<size_t>(Target::Amp)], 0.0f, 2.0f);
    out[CPan] = std::clamp(mod[static_cast<size_t>(Target::Pan)], -1.0f, 1.0f);
}

void SubtractiveSynth::startGlide(Voice& v, double fromPitch) noexcept
{
    const double glideSec = params().get(VoiceGlide) * 0.001;
    v.glideTarget = v.key;
    if (glideSec <= 0.0)
    {
        v.glidePitch = v.glideTarget;
        v.glideRate = 0.0;
        return;
    }
    v.glidePitch = fromPitch;
    v.glideRate = std::abs(v.glideTarget - fromPitch) / (glideSec * sampleRate_);
}

void SubtractiveSynth::voiceStart(int index, int key, float velocity, int channel, std::int32_t, bool retrigger)
{
    auto& v = voice_[static_cast<size_t>(index)];
    const bool mono = voices_.mode() != VoiceManager::Mode::Poly;
    const double from = (retrigger || mono) && v.sounding ? v.glidePitch : lastPitch_;
    v.key = key;
    v.velocity = velocity;
    v.channel = channel & 15;
    if (!v.sounding)
    {
        // A fresh voice starts from a clean state; a stolen or retriggered voice keeps
        // oscillator phase, filter state and envelope level (click-free).
        v.osc1.reset();
        v.osc2.reset();
        v.filter.reset();
    }
    startGlide(v, from);
    lastPitch_ = key;
    v.amp.noteOn();
    v.mod.noteOn();
    v.sounding = true;
    computeControls(v, v.to);
    v.from = v.to;
}

void SubtractiveSynth::voiceLegato(int index, int key, float velocity)
{
    auto& v = voice_[static_cast<size_t>(index)];
    v.key = key;
    v.velocity = velocity;
    startGlide(v, v.glidePitch);
    lastPitch_ = key;
}

void SubtractiveSynth::voiceRelease(int index)
{
    auto& v = voice_[static_cast<size_t>(index)];
    v.amp.noteOff();
    v.mod.noteOff();
}

void SubtractiveSynth::handleEvent(const Event& e) noexcept
{
    const auto ch = static_cast<size_t>(e.channel & 15);
    switch (e.type)
    {
    case EventType::NoteOn:
    case EventType::NoteOff:
    case EventType::AllNotesOff:
        voices_.handle(e, *this);
        break;
    case EventType::Controller:
        if (e.key == 1)
            modWheel_ = e.value;
        else if (e.key == 74)
            slide_[ch] = e.value;
        else
            voices_.handle(e, *this); // sustain pedal
        break;
    case EventType::PitchBend:
        bend_[ch] = e.value;
        break;
    case EventType::ChannelPressure:
    case EventType::PolyPressure:
        pressure_[ch] = e.value;
        break;
    }
}

void SubtractiveSynth::tick(const TransportInfo& transport) noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();
    volume_.setTarget(dsp::dbToGain(p.get(OutVolume), -70.0f), dsp::msToSamples(20.0, sampleRate_));

    const int count = p.getInt(VoiceCount);
    if (count < voices_.voiceCount())
        for (int i = count; i < voices_.voiceCount(); ++i)
            if (voice_[static_cast<size_t>(i)].sounding)
                voiceRelease(i);
    voices_.setVoiceCount(count);
    voices_.setMode(static_cast<VoiceManager::Mode>(p.getInt(VoiceMode)));

    const int sync = p.getInt(LfoSync);
    const double rate = sync > 0 ? (transport.tempoBpm / 60.0) / kSyncBeats[sync] : p.get(LfoRate);
    lfoValue_ = lfo_.advance(rate, sampleRate_, interval, static_cast<dsp::Lfo::Wave>(p.getInt(LfoWave)));

    const auto ampSettings = envelopeSettings(p.get(AmpA), p.get(AmpD), p.get(AmpS), p.get(AmpR), false);
    const auto modSettings = envelopeSettings(p.get(ModA), p.get(ModD), p.get(ModS), p.get(ModR), p.getBool(ModCycle));
    for (auto& v : voice_)
    {
        v.amp.set(ampSettings);
        v.mod.set(modSettings);
        if (!v.sounding)
            continue;
        // Glide moves in whole grid intervals; the pitch is then ramped per sample.
        if (v.glideRate > 0.0)
        {
            const double step = v.glideRate * interval;
            v.glidePitch = v.glidePitch < v.glideTarget ? std::min(v.glideTarget, v.glidePitch + step)
                                                        : std::max(v.glideTarget, v.glidePitch - step);
        }
        v.driftP = v.driftPitch.advance(interval, sampleRate_);
        v.driftC = v.driftCutoff.advance(interval, sampleRate_);
        v.from = v.to;
        computeControls(v, v.to);
    }
}

void SubtractiveSynth::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    float* outL = ctx.output.channel(0);
    float* outR = ctx.output.numChannels > 1 ? ctx.output.channel(1) : nullptr;
    std::fill(outL, outL + n, 0.0f);
    if (outR != nullptr)
        std::fill(outR, outR + n, 0.0f);

    const auto& p = params();
    const auto filterType = static_cast<dsp::SynthFilter::Type>(p.getInt(FilterType));
    const float envAmount = p.get(FilterEnv) * 0.01f * 5.0f; // octaves at full mod envelope
    const double osc1Offset = 12.0 * p.get(Osc1Oct);
    const double osc2Offset = 12.0 * p.get(Osc2Oct) + p.get(Osc2Semi) + p.get(Osc2Detune) * 0.01;
    const auto mix2 = osc2Mix(p.getInt(Osc2Wave));
    const double nyquistOct = std::log2(0.45 * sampleRate_);

    const EventList* events = ctx.events;
    size_t nextEvent = 0;
    const int interval = grid_.interval();
    float peakOut = 0.0f;

    auto render = [&](int from, int to, int phase) noexcept {
        for (auto& v : voice_)
        {
            if (!v.sounding)
                continue;
            for (int s = from; s < to; ++s)
            {
                const double t = static_cast<double>(phase + (s - from) + 1) / interval;
                std::array<double, NumControls> c;
                for (size_t k = 0; k < c.size(); ++k)
                    c[k] = v.from[k] + t * (v.to[k] - v.from[k]);
                const float ampEnv = v.amp.next();
                const float modEnv = v.mod.next();

                const double f1 = hzForPitch(c[CPitch] + osc1Offset) / sampleRate_;
                const double f2 = hzForPitch(c[CPitch] + osc2Offset) / sampleRate_;
                float x = 0.0f;
                if (c[COsc1] > 0.0)
                    x += static_cast<float>(c[COsc1]) *
                         v.osc1.process(f1, dsp::Oscillator::morph(static_cast<float>(c[CShape]), static_cast<float>(c[CWidth])));
                if (c[COsc2] > 0.0)
                    x += static_cast<float>(c[COsc2]) * v.osc2.process(f2, mix2);
                if (c[CNoise] > 0.0)
                    x += static_cast<float>(c[CNoise]) * v.noise.next();

                const double cutoff = std::exp2(std::min(nyquistOct, c[CCutoffOct] + envAmount * modEnv));
                float y = v.filter.process(x, filterType, cutoff, c[CRes], sampleRate_);
                y *= ampEnv * static_cast<float>(c[CAmp]);
                const float angle = static_cast<float>((c[CPan] + 1.0) * 0.25 * dsp::pi);
                outL[s] += y * (outR != nullptr ? std::cos(angle) : 1.0f);
                if (outR != nullptr)
                    outR[s] += y * std::sin(angle);
            }
            if (!v.amp.isActive())
            {
                v.sounding = false;
                voices_.voiceFinished(static_cast<int>(&v - voice_.data()));
            }
        }
    };

    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        const int phase0 = static_cast<int>(grid_.position() % interval);
        if (isTick)
            tick(ctx.transport);
        int i = offset;
        const int end = offset + length;
        while (i < end)
        {
            while (events != nullptr && nextEvent < events->size() && (*events)[nextEvent].sampleOffset <= i)
                handleEvent((*events)[nextEvent++]);
            int stop = end;
            if (events != nullptr && nextEvent < events->size() && (*events)[nextEvent].sampleOffset < end)
                stop = std::max(i + 1, (*events)[nextEvent].sampleOffset);
            render(i, stop, phase0 + (i - offset));
            i = stop;
        }
    });
    while (events != nullptr && nextEvent < events->size())
        handleEvent((*events)[nextEvent++]);

    // Output volume (smoothed) and analysis.
    for (int s = 0; s < n; ++s)
    {
        const float g = volume_.next();
        outL[s] *= g;
        if (outR != nullptr)
            outR[s] *= g;
        peakOut = std::max({peakOut, std::abs(outL[s]), outR != nullptr ? std::abs(outR[s]) : 0.0f});
    }
    analysis().publishPeak(PeakOut, peakOut);
    analysis().publish(ActiveVoices, static_cast<float>(voices_.activeVoices()));
}

const DeviceFactory& subtractiveSynthFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<SubtractiveSynth>(); },
    };
    return factory;
}
} // namespace fd::devices
