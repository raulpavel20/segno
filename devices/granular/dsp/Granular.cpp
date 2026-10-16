#include "Granular.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fd::devices
{
namespace
{
using G = Granular;

constexpr std::string_view kInputs[] = {"Sample", "Live"};
constexpr std::string_view kPick[] = {"Random", "Turns", "Both"};
constexpr std::string_view kLiveModes[] = {"Rolling", "Hold", "Frozen"};
constexpr std::string_view kSprayShapes[] = {"Even", "Near", "Ahead"};
constexpr std::string_view kSnaps[] = {"Off", "Transients", "Beats", "Zero Crossings"};
constexpr std::string_view kScanSync[] = {"Off",   "1/4T",   "1/4",    "1/4D",   "1/2T",    "1/2",    "1/2D",
                                          "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars"};
constexpr std::string_view kScanEnds[] = {"Wrap", "Bounce", "Stop"};
constexpr std::string_view kPlayheads[] = {"Per Note", "Shared", "Song"};
constexpr std::string_view kModes[] = {"Classic", "Loop", "Cloud", "Pitched", "Rhythm", "Spectral"};
constexpr std::string_view kSizeSync[] = {"Off",   "1/128", "1/64T", "1/64",  "1/32T", "1/32", "1/16T",
                                          "1/32D", "1/16",  "1/8T",  "1/16D", "1/8",   "1/4T", "1/8D",
                                          "1/4",   "1/2T",  "1/4D",  "1/2",   "1/2D",  "1 bar"};
constexpr std::string_view kRates[] = {"1/128", "1/64T", "1/64",  "1/32T", "1/32",  "1/16T",  "1/32D",
                                       "1/16",  "1/8T",  "1/16D", "1/8",   "1/4T",  "1/8D",   "1/4",
                                       "1/2T",  "1/4D",  "1/2",   "1/2D",  "1 bar", "2 bars", "4 bars"};
constexpr std::string_view kWindows[] = {"Hann",      "Gaussian", "Triangle", "Sharp Attack",
                                         "Sharp End", "Square",   "Drawn"};
constexpr std::string_view kDirections[] = {"Both", "Up", "Down"};
constexpr std::string_view kQuantize[] = {"Off", "Semitones", "Song Key", "Octaves", "Fifths", "Chord"};
constexpr std::string_view kChords[] = {"Major",      "Minor",   "Sus 2",   "Sus 4", "Major 7",  "Minor 7",
                                        "Dominant 7", "Major 9", "Minor 9", "Power", "Key Chord"};
constexpr std::string_view kPanModes[] = {"Random", "Alternate", "By Pitch"};
constexpr std::string_view kGrainFilterTypes[] = {"LP", "BP", "HP", "Resonator"};
constexpr std::string_view kFilterTypes[] = {"LP 12",  "LP 24",  "Ladder",  "Diode", "HP 12",
                                             "HP 24",  "BP 12",  "BP 24",   "Notch", "Peak",
                                             "Comb +", "Comb −", "Formant", "Phaser"};
constexpr std::string_view kEnvModes[] = {"Normal", "Loop", "One-shot"};
constexpr std::string_view kSync[] = {"Off",   "1/64", "1/32T", "1/32",   "1/16T",  "1/16",  "1/8T",
                                      "1/16D", "1/8",  "1/4T",  "1/8D",   "1/4",    "1/2T",  "1/4D",
                                      "1/2",   "1/2D", "1 bar", "2 bars", "4 bars", "8 bars"};
constexpr std::string_view kLfoModes[] = {"Free", "Retrigger", "Envelope"};
constexpr std::string_view kRandomModes[] = {"Steps", "Smooth", "Chaos", "Per Note", "Per Grain"};
constexpr std::string_view kVoiceModes[] = {"Poly", "Mono", "Legato"};
constexpr std::string_view kGlideModes[] = {"Always", "Legato"};
constexpr std::string_view kQualities[] = {"Eco", "Normal", "High"};
constexpr std::string_view kResolutions[] = {"1 024", "2 048", "4 096", "8 192", "16 384"};

constexpr auto kAutoInf = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin;

/// The parameter table, generated once (families like "src2.start" need stored strings).
struct Tables
{
    std::array<std::string, G::NumParams> ids, names, shortNames;
    std::array<std::string_view, G::NumParams> descriptions{};
    std::array<ParamSpec, G::NumParams> specs{};
    std::vector<std::string> sourceNames, targetNames;
    std::vector<std::string_view> sourceViews, targetViews;
    std::array<MeterSpec, 1> meters{
        MeterSpec{G::GrainsPlaying, "grains", "Grains playing", MeterKind::Count}};
    DeviceInfo info{};

    void set(size_t i, std::string id, std::string name, std::string shortName, std::string_view description,
             ParamSpec spec)
    {
        ids[i] = std::move(id);
        names[i] = std::move(name);
        shortNames[i] = std::move(shortName);
        descriptions[i] = description;
        specs[i] = spec;
    }

    static ParamSpec percent(float def, float min = 0.0f, float max = 100.0f)
    {
        return {.min = min, .max = max, .def = def, .unit = Unit::Percent, .decimals = 0};
    }
    static ParamSpec level(float def, float max)
    {
        return {.min = -70.0f,
                .max = max,
                .def = def,
                .skewCentre = -12.0f,
                .unit = Unit::Decibels,
                .flags = kAutoInf};
    }
    static ParamSpec choice(std::span<const std::string_view> choices, float def,
                            std::uint32_t flags = ParamFlags::Automatable)
    {
        return {.kind = ParamKind::Choice,
                .min = 0.0f,
                .max = static_cast<float>(choices.size() - 1),
                .def = def,
                .choices = choices,
                .flags = flags};
    }
    static ParamSpec discrete(float min, float max, float def, Unit unit = Unit::None)
    {
        return {.kind = ParamKind::Discrete, .min = min, .max = max, .def = def, .unit = unit};
    }
    static ParamSpec toggle(float def)
    {
        return {.kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f, .def = def};
    }
    static ParamSpec ms(float min, float max, float def, float centre)
    {
        return {.min = min,
                .max = max,
                .def = def,
                .skewCentre = centre,
                .unit = Unit::Milliseconds,
                .decimals = 0};
    }
    static ParamSpec semitones(float range, float def = 0.0f)
    {
        return {.min = -range, .max = range, .def = def, .unit = Unit::Semitones, .decimals = 1};
    }
    static ParamSpec cents()
    {
        return {.min = -100.0f, .max = 100.0f, .def = 0.0f, .unit = Unit::Cents, .decimals = 0};
    }
    static ParamSpec hertz(float def)
    {
        return {.min = 20.0f,
                .max = 20000.0f,
                .def = def,
                .skewCentre = 632.0f,
                .unit = Unit::Hertz,
                .smoothing = {SmoothingKind::Multiplicative, 20.0f},
                .decimals = 0};
    }
    static ParamSpec rate(float def)
    {
        return {.min = 0.01f,
                .max = 100.0f,
                .def = def,
                .skewCentre = 1.0f,
                .unit = Unit::Hertz,
                .smoothing = {SmoothingKind::Multiplicative, 20.0f},
                .decimals = 2};
    }

    void modulationLists()
    {
        sourceNames = {"None", "Amp Env", "Env 2", "Env 3"};
        for (int n = 1; n <= G::numLfos; ++n)
            sourceNames.push_back("LFO " + std::to_string(n));
        for (int n = 1; n <= G::numRandoms; ++n)
            sourceNames.push_back("Random " + std::to_string(n));
        for (int n = 1; n <= G::numMacros; ++n)
            sourceNames.push_back("Macro " + std::to_string(n));
        for (const char* s : {"Velocity", "Note", "Pressure", "Slide", "Pitch Bend", "Mod Wheel",
                              "Grain Random", "Grain Alternate", "Scan", "Input Level"})
            sourceNames.emplace_back(s);
        targetNames = {"None",       "Pitch",       "Position",  "Position Spray", "Scan",        "Size",
                       "Size Spray", "Overlap",     "Density",   "Periods",        "Formant",     "Crossfade",
                       "Jitter",     "Chance",      "Swing",     "Fire",           "Skew",        "Flatness",
                       "Reverse",    "Level Spray", "Even",      "Pitch Spray",    "Grain Glide", "Pan",
                       "Pan Spray",  "Width",       "Source Mix"};
        for (int n = 1; n <= G::numSources; ++n)
            for (const char* t : {"Start", "End", "Gain", "Tune"})
                targetNames.push_back("Source " + std::to_string(n) + " " + t);
        for (const char* t :
             {"Grain Cutoff", "Grain Resonance", "Cutoff Spray", "Crush", "Ring", "Ring Ratio"})
            targetNames.emplace_back(t);
        for (int n = 1; n <= G::numFilters; ++n)
            for (const char* t : {"Cutoff", "Resonance", "Drive", "Shape"})
                targetNames.push_back("Filter " + std::to_string(n) + " " + t);
        for (const char* t : {"Filter Routing", "Amp", "Volume", "Live Gain", "Feedback"})
            targetNames.emplace_back(t);
        for (int n = 1; n <= G::numLfos; ++n)
            targetNames.push_back("LFO " + std::to_string(n) + " Rate");
        for (int n = 1; n <= G::numRandoms; ++n)
            targetNames.push_back("Random " + std::to_string(n) + " Rate");
        for (const char* t : {"Smear", "Harmonics", "Clean"})
            targetNames.emplace_back(t);
        for (const auto& s : sourceNames)
            sourceViews.emplace_back(s);
        for (const auto& t : targetNames)
            targetViews.emplace_back(t);
    }

    Tables()
    {
        modulationLists();

        // Sources.
        for (int n = 0; n < G::numSources; ++n)
        {
            const std::string id = "src" + std::to_string(n + 1) + ".";
            const std::string name = "Source " + std::to_string(n + 1) + " ";
            const auto at = [n](G::SourceParam p) { return G::source(n, p); };
            set(at(G::SrcOn), id + "on", name + "On", "On",
                "Turns the source on or off; with one off, every grain comes from the other.",
                toggle(n == 0 ? 1.0f : 0.0f));
            set(at(G::SrcInput), id + "input", name + "Input", "Input",
                "Sample plays the source's audio; Live plays what the input buffer holds.",
                choice(kInputs, 0.0f));
            set(at(G::SrcStart), id + "start", name + "Start", "Start",
                "Where the part of the source that grains come from begins.", percent(0.0f));
            set(at(G::SrcEnd), id + "end", name + "End", "End", "Where that part ends.", percent(100.0f));
            set(at(G::SrcGain), id + "gain", name + "Gain", "Gain",
                "The source's level, after it's brought to full scale on loading.", level(0.0f, 12.0f));
            set(at(G::SrcTune), id + "tune", name + "Tune", "Tune",
                "Shifts the source by semitones, from the root it was found or set at.",
                discrete(-48.0f, 48.0f, 0.0f, Unit::Semitones));
            set(at(G::SrcFine), id + "fine", name + "Fine", "Fine", "Tunes the source slightly.", cents());
        }
        set(G::SrcMix, "src.mix", "Source Mix", "Mix",
            "How many grains come from Source 2: none at 0 %, all at 100 %.", percent(50.0f));
        set(G::SrcPick, "src.pick", "Source Pick", "Pick",
            "Random picks each grain's source by the mix; Turns alternates them; Both plays every grain from "
            "each.",
            choice(kPick, 0.0f));

        // The live input.
        set(G::LiveMode, "live.mode", "Live Mode", "Mode",
            "Rolling keeps recording; Hold stops while notes play, so they play what came before; Frozen "
            "stops "
            "recording.",
            choice(kLiveModes, 0.0f));
        set(G::LiveLength, "live.length", "Live Length", "Length",
            "How many seconds of the input the buffer keeps.",
            {.min = 0.5f,
             .max = 30.0f,
             .def = 8.0f,
             .skewCentre = 4.0f,
             .unit = Unit::Seconds,
             .decimals = 1});
        set(G::LiveGain, "live.gain", "Live Gain", "Gain", "The input's level into the buffer.",
            level(0.0f, 24.0f));
        set(G::LiveFeedback, "live.feedback", "Feedback", "Feedback",
            "Sends the module's own output back into the buffer, for trails that build on themselves.",
            percent(0.0f));

        // Position and scan.
        set(G::Pos, "pos", "Position", "Position",
            "Where grains come from: across the source's part, or how far back in the live buffer.",
            percent(0.0f));
        set(G::PosSpray, "pos.spray", "Position Spray", "Spray",
            "How far each grain's start strays from the position, at random.",
            {.min = 0.0f,
             .max = 100.0f,
             .def = 0.0f,
             .skewCentre = 10.0f,
             .unit = Unit::Percent,
             .decimals = 1});
        set(G::PosDist, "pos.dist", "Spray Shape", "Spray Shape",
            "Even spreads grains over the spray; Near keeps most close to the position; Ahead strays only "
            "forward.",
            choice(kSprayShapes, 0.0f));
        set(G::PosKey, "pos.key", "Position Key", "Pos Key",
            "How far each note moves the position, so notes play different places in the source.",
            percent(0.0f, -100.0f));
        set(G::PosSnap, "pos.snap", "Snap", "Snap",
            "Starts grains on the source's transients, its beats or its zero crossings.",
            choice(kSnaps, 0.0f));
        set(G::ScanRate, "scan.rate", "Scan", "Scan",
            "How fast the position moves through the source: 100 % at the recording's own speed, 0 holds "
            "still, "
            "below zero backwards.",
            percent(0.0f, -400.0f, 400.0f));
        set(G::ScanSync, "scan.sync", "Scan Sync", "Scan Sync",
            "Locks the scan to the song: one pass through the source's part in this many note values.",
            choice(kScanSync, 0.0f));
        set(G::ScanEnd, "scan.end", "Scan End", "Scan End",
            "At the end of the part, Wrap starts over, Bounce turns back, Stop stays there.",
            choice(kScanEnds, 0.0f));
        set(G::ScanFrom, "scan.from", "Playhead", "Playhead",
            "Per Note scans from the position on each note; Shared runs one scan for every note; Song "
            "follows the "
            "song's position.",
            choice(kPlayheads, 0.0f));

        // Grains.
        set(G::GrainMode, "grain.mode", "Mode", "Mode",
            "How grains start: overlapping (Classic), as loops, scattered (Cloud), at the note's pitch, on "
            "the "
            "beat, or as spectra.",
            choice(kModes, 0.0f));
        set(G::GrainSize, "grain.size", "Size", "Size",
            "How long each grain lasts; in Loop, how long each loop is.", ms(1.0f, 4000.0f, 100.0f, 100.0f));
        set(G::GrainSizeSync, "grain.sizesync", "Size Sync", "Size Sync",
            "Sets each grain's length in note values instead.", choice(kSizeSync, 0.0f));
        set(G::GrainOverlap, "grain.overlap", "Overlap", "Overlap",
            "In Classic, how many grains sound over each other.",
            {.min = 1.0f, .max = 16.0f, .def = 2.0f, .skewCentre = 4.0f, .decimals = 1});
        set(G::GrainDensity, "grain.density", "Density", "Density",
            "In Cloud, how many grains start each second.",
            {.min = 0.5f,
             .max = 2000.0f,
             .def = 40.0f,
             .skewCentre = 40.0f,
             .unit = Unit::PerSecond,
             .decimals = 1});
        set(G::GrainRate, "grain.rate", "Rate", "Rate", "In Rhythm, how often grains start, in note values.",
            choice(kRates, 7.0f));
        set(G::GrainPeriods, "grain.periods", "Periods", "Periods",
            "In Pitched, how long each grain lasts, in cycles of the note.",
            {.min = 1.0f, .max = 64.0f, .def = 4.0f, .skewCentre = 8.0f, .decimals = 1});
        set(G::GrainFormant, "grain.formant", "Formant", "Formant",
            "In Pitched and Spectral, moves the sound's character up or down without changing the note.",
            semitones(24.0f));
        set(G::GrainXfade, "grain.xfade", "Crossfade", "Xfade",
            "In Loop, how much of each loop fades into the next.", percent(10.0f, 0.0f, 50.0f));
        set(G::GrainJitter, "grain.jitter", "Jitter", "Jitter",
            "Moves each grain's start in time a little, at random, so a steady stream loosens.",
            percent(0.0f));
        set(G::GrainChance, "grain.chance", "Chance", "Chance",
            "How likely each grain is to play: lower leaves gaps.", percent(100.0f));
        set(G::GrainRepeats, "grain.repeats", "Repeats", "Repeats",
            "Plays each grain this many times in a row, for stutters.", discrete(1.0f, 16.0f, 1.0f));
        set(G::GrainSwing, "grain.swing", "Swing", "Swing", "In Rhythm, delays every second grain.",
            percent(0.0f, 0.0f, 75.0f));
        set(G::GrainFire, "grain.fire", "Fire", "Fire",
            "Starts a grain the moment it rises past half, in any mode: route a modulator to it for grains "
            "on your "
            "own rhythm.",
            {.min = 0.0f,
             .max = 100.0f,
             .def = 0.0f,
             .unit = Unit::Percent,
             .smoothing = {SmoothingKind::None, 0.0f},
             .decimals = 0});
        set(G::GrainWindow, "grain.window", "Window", "Window", "The shape each grain fades in and out with.",
            choice(kWindows, 0.0f));
        set(G::GrainSkew, "grain.skew", "Skew", "Skew", "Moves the window's peak earlier or later.",
            percent(0.0f, -100.0f));
        set(G::GrainFlat, "grain.flat", "Flatness", "Flat", "How much of each grain plays at full level.",
            percent(0.0f));
        set(G::GrainReverse, "grain.reverse", "Reverse", "Reverse",
            "How likely each grain is to play backwards.", percent(0.0f));
        set(G::GrainSizeSpray, "grain.sizespray", "Size Spray", "Size",
            "How much each grain's length varies, at random.", percent(0.0f));
        set(G::GrainLevelSpray, "grain.levelspray", "Level Spray", "Level",
            "How much each grain's level varies, at random.", percent(0.0f));
        set(G::GrainEven, "grain.even", "Even", "Even",
            "Raises grains from quiet parts of the source, so a dynamic recording makes a steady texture.",
            percent(0.0f));
        set(G::GrainComp, "grain.comp", "Density Gain", "Density Gain",
            "Keeps the level steady as more grains overlap.", toggle(1.0f));
        set(G::GrainMax, "grain.max", "Max Grains", "Max",
            "The most grains a note plays at once: fewer saves CPU. Not automatable.",
            {.kind = ParamKind::Discrete,
             .min = 8.0f,
             .max = 256.0f,
             .def = 64.0f,
             .skewCentre = 48.0f,
             .flags = ParamFlags::None});

        // Pitch.
        set(G::PitchTranspose, "pitch.transpose", "Transpose", "Semi", "Shifts every grain by semitones.",
            discrete(-48.0f, 48.0f, 0.0f, Unit::Semitones));
        set(G::PitchFine, "pitch.fine", "Fine", "Fine", "Tunes every grain slightly.", cents());
        set(G::PitchKey, "pitch.key", "Key Track", "Key",
            "How much the note played sets the pitch: at 0 every note plays the source as it is.",
            percent(100.0f));
        set(G::PitchSpray, "pitch.spray", "Pitch Spray", "Spray",
            "How far each grain's pitch strays, at random.",
            {.min = 0.0f,
             .max = 48.0f,
             .def = 0.0f,
             .skewCentre = 12.0f,
             .unit = Unit::Semitones,
             .decimals = 1});
        set(G::PitchDir, "pitch.dir", "Spray Direction", "Direction",
            "Strays both ways, only up, or only down.", choice(kDirections, 0.0f));
        set(G::PitchQuant, "pitch.quant", "Quantize", "Quantize",
            "Lands each grain's pitch on semitones, the song's key, octaves, octaves and fifths, or a chord.",
            choice(kQuantize, 0.0f));
        set(G::PitchChord, "pitch.chord", "Chord", "Chord",
            "The chord grains land on when Quantize is Chord.", choice(kChords, 0.0f));
        set(G::PitchGlide, "pitch.glide", "Grain Glide", "Glide",
            "Slides each grain's pitch by this much over its life.", semitones(24.0f));

        // Stereo.
        set(G::PanSpray, "pan.spray", "Pan Spray", "Pan", "How far grains spread across the stereo field.",
            percent(30.0f));
        set(G::PanMode, "pan.mode", "Pan Mode", "Pan Mode",
            "Random places grains anywhere in the spray; Alternate swings them left and right; By Pitch puts "
            "higher grains to the right.",
            choice(kPanModes, 0.0f));
        set(G::OutWidth, "out.width", "Width", "Width",
            "The width of the whole sound: 0 % is mono, 200 % wider than the source.",
            percent(100.0f, 0.0f, 200.0f));

        // The grain filter and colour.
        set(G::GFilterOn, "gfilter.on", "Grain Filter", "On", "Gives each grain a filter of its own.",
            toggle(0.0f));
        set(G::GFilterType, "gfilter.type", "Grain Filter Type", "Type",
            "Low, band or high pass, or a resonator that rings at the cutoff.",
            choice(kGrainFilterTypes, 0.0f));
        set(G::GFilterCutoff, "gfilter.cutoff", "Grain Cutoff", "Cutoff", "Where each grain's filter acts.",
            hertz(2000.0f));
        set(G::GFilterRes, "gfilter.res", "Grain Resonance", "Res", "Emphasis at each grain's cutoff.",
            percent(20.0f));
        set(G::GFilterSpray, "gfilter.spray", "Cutoff Spray", "Spray",
            "How far each grain's cutoff strays, at random.",
            {.min = 0.0f, .max = 6.0f, .def = 0.0f, .unit = Unit::Octaves, .decimals = 1});
        set(G::GFilterKey, "gfilter.key", "Cutoff Pitch Track", "Track",
            "How much each grain's cutoff follows that grain's pitch.", percent(0.0f));
        set(G::GFilterSnap, "gfilter.snap", "Cutoff to Key", "In Key",
            "Lands each grain's cutoff on a note of the song's key, for tuned resonances.", toggle(0.0f));
        set(G::GColorCrush, "gcolor.crush", "Crush", "Crush",
            "Lowers each grain's resolution and rate, for grit.", percent(0.0f));
        set(G::GColorCrushSpray, "gcolor.crushspray", "Crush Spray", "Spray",
            "How much the crush varies from grain to grain.", percent(0.0f));
        set(G::GColorRing, "gcolor.ring", "Ring", "Ring",
            "Ring-modulates each grain, for metallic and bell-like tones.", percent(0.0f));
        set(G::GColorRatio, "gcolor.ratio", "Ring Ratio", "Ratio",
            "The ring's frequency against each grain's pitch.",
            {.min = 0.25f,
             .max = 16.0f,
             .def = 2.0f,
             .skewCentre = 2.0f,
             .unit = Unit::Times,
             .decimals = 2});
        set(G::GColorRingSpray, "gcolor.ringspray", "Ring Spray", "Spray",
            "How much the ring's frequency varies from grain to grain.", percent(0.0f));

        // The voice filters, as the Wavetable's.
        for (int n = 0; n < G::numFilters; ++n)
        {
            const std::string id = "filter" + std::to_string(n + 1) + ".";
            const std::string name = "Filter " + std::to_string(n + 1) + " ";
            const auto at = [n](G::FilterParam p) { return G::filter(n, p); };
            set(at(G::FilterOn), id + "on", name + "On", "On", "Turns the filter on or off.",
                toggle(n == 0 ? 1.0f : 0.0f));
            set(at(G::FilterType), id + "type", name + "Type", "Type",
                "The filter's kind: low, high or band pass, notch, peak, ladder or diode, comb, formant or "
                "phaser.",
                choice(kFilterTypes, n == 0 ? 1.0f : 4.0f));
            set(at(G::FilterCutoff), id + "cutoff", name + "Cutoff", "Cutoff",
                "Where the filter acts: for a low pass, lower is darker.", hertz(n == 0 ? 20000.0f : 100.0f));
            set(at(G::FilterRes), id + "res", name + "Resonance", "Res",
                "Emphasis at the cutoff; near the top the ladder and diode filters start to sing on their "
                "own.",
                percent(20.0f));
            set(at(G::FilterDrive), id + "drive", name + "Drive", "Drive",
                "Pushes the filter harder, for warmth and grit.", percent(0.0f));
            set(at(G::FilterShape), id + "shape", name + "Shape", "Shape",
                "The type's own setting: the formant's vowel, the comb's damping, the phaser's spread.",
                percent(50.0f));
            set(at(G::FilterKey), id + "key", name + "Key Track", "Key",
                "How much the cutoff follows the note played: at 100 % it rises an octave per octave.",
                percent(50.0f));
            set(at(G::FilterEnv), id + "env", name + "Env", "Env",
                "How far Env 2 moves the cutoff, up or down.", percent(0.0f, -100.0f));
        }
        set(G::FiltersRouting, "filters.routing", "Filter Routing", "Routing",
            "Filter 1 into filter 2 at 0 %, the two side by side at 100 %, or anywhere between.",
            percent(0.0f));
        set(G::AmpVelocity, "amp.vel", "Velocity", "Velocity", "How much harder playing makes notes louder.",
            {.min = 0.0f,
             .max = 100.0f,
             .def = 50.0f,
             .unit = Unit::Percent,
             .smoothing = {SmoothingKind::None, 0.0f},
             .decimals = 0});

        // Envelopes: the amp's suits a pad.
        constexpr const char* envNames[G::numEnvs] = {"Amp ", "Env 2 ", "Env 3 "};
        for (int n = 0; n < G::numEnvs; ++n)
        {
            const std::string id = "env" + std::to_string(n + 1) + ".";
            const std::string name = envNames[n];
            const auto at = [n](G::EnvParam p) { return G::env(n, p); };
            set(at(G::EnvAttack), id + "a", name + "Attack", "Attack", "How long the envelope takes to rise.",
                ms(0.0f, 20000.0f, n == 0 ? 50.0f : 2.0f, 50.0f));
            set(at(G::EnvHold), id + "h", name + "Hold", "Hold",
                "How long the envelope stays at its peak before it falls.", ms(0.0f, 10000.0f, 0.0f, 100.0f));
            set(at(G::EnvDecay), id + "d", name + "Decay", "Decay",
                "How long the envelope takes to fall to its sustain level.",
                ms(1.0f, 20000.0f, n == 0 ? 1000.0f : (n == 1 ? 400.0f : 300.0f), 300.0f));
            set(at(G::EnvSustain), id + "s", name + "Sustain", "Sustain",
                "The envelope's level while the key stays down.", percent(n == 0 ? 100.0f : 0.0f));
            set(at(G::EnvRelease), id + "r", name + "Release", "Release",
                "How long the envelope takes to fall after the key is released.",
                ms(1.0f, 20000.0f, n == 0 ? 800.0f : 300.0f, 300.0f));
            set(at(G::EnvAttackCurve), id + "acurve", name + "Attack Curve", "A Curve",
                "How the attack rises: below zero it starts slowly, above zero quickly.",
                percent(30.0f, -100.0f));
            set(at(G::EnvDecayCurve), id + "dcurve", name + "Decay Curve", "D Curve",
                "How the decay falls: below zero it starts slowly, above zero quickly.",
                percent(60.0f, -100.0f));
            set(at(G::EnvReleaseCurve), id + "rcurve", name + "Release Curve", "R Curve",
                "How the release falls: below zero it starts slowly, above zero quickly.",
                percent(60.0f, -100.0f));
            set(at(G::EnvMode), id + "mode", name + "Mode", "Mode",
                "Normal; Loop repeats attack to decay while the key is held; One-shot plays through and "
                "ignores the "
                "release.",
                choice(kEnvModes, 0.0f));
        }

        // LFOs and random sources.
        for (int n = 0; n < G::numLfos; ++n)
        {
            const std::string id = "lfo" + std::to_string(n + 1) + ".";
            const std::string name = "LFO " + std::to_string(n + 1) + " ";
            const auto at = [n](G::LfoParam p) { return G::lfo(n, p); };
            set(at(G::LfoRate), id + "rate", name + "Rate", "Rate",
                "How fast the LFO moves when it isn't synced to the tempo.", rate(2.0f));
            set(at(G::LfoSync), id + "sync", name + "Sync", "Sync",
                "Locks the LFO to the song's tempo, in note values.", choice(kSync, 0.0f));
            set(at(G::LfoMode), id + "mode", name + "Mode", "Mode",
                "Free keeps running; Retrigger restarts it with each note; Envelope plays its shape once per "
                "note.",
                choice(kLfoModes, 1.0f));
            set(at(G::LfoPhase), id + "phase", name + "Phase", "Phase", "Where in its shape the LFO starts.",
                {.min = 0.0f, .max = 360.0f, .def = 0.0f, .unit = Unit::Degrees, .decimals = 0});
            set(at(G::LfoSmooth), id + "smooth", name + "Smooth", "Smooth",
                "Rounds off the shape's corners, so steps glide.", percent(0.0f));
            set(at(G::LfoFade), id + "fade", name + "Fade In", "Fade In",
                "How long the LFO takes to fade in after each note starts.",
                ms(0.0f, 10000.0f, 0.0f, 500.0f));
            set(at(G::LfoPoly), id + "poly", name + "Per Note", "Per Note",
                "On, each note has its own LFO; off, one LFO moves every note together.", toggle(1.0f));
        }
        for (int n = 0; n < G::numRandoms; ++n)
        {
            const std::string id = "rnd" + std::to_string(n + 1) + ".";
            const std::string name = "Random " + std::to_string(n + 1) + " ";
            const auto at = [n](G::RandomParam p) { return G::random(n, p); };
            set(at(G::RandomMode), id + "mode", name + "Mode", "Mode",
                "Steps holds, Smooth glides, Chaos wanders; Per Note picks a value for each note, Per Grain "
                "one for "
                "each grain.",
                choice(kRandomModes, 1.0f));
            set(at(G::RandomRate), id + "rate", name + "Rate", "Rate",
                "How often a new value comes when it isn't synced to the tempo.", rate(4.0f));
            set(at(G::RandomSync), id + "sync", name + "Sync", "Sync",
                "Locks the new values to the song's tempo, in note values.", choice(kSync, 0.0f));
            set(at(G::RandomPoly), id + "poly", name + "Per Note", "Per Note",
                "On, each note has its own values; off, every note follows the same ones.", toggle(1.0f));
        }
        for (int n = 0; n < G::numMacros; ++n)
            set(G::macro(n), "macro" + std::to_string(n + 1), "Macro " + std::to_string(n + 1),
                "Macro " + std::to_string(n + 1),
                "A knob of your own: route it to any settings to move them together.", percent(0.0f));
        for (int n = 0; n < G::numRoutes; ++n)
        {
            const std::string id = "mod" + std::to_string(n + 1) + ".";
            const std::string name = "Mod " + std::to_string(n + 1) + " ";
            set(G::route(n, G::RouteSource), id + "src", name + "Source", "Source",
                "What drives this route: an envelope, an LFO, a macro, how the note is played, or the grain "
                "itself.",
                choice(sourceViews, 0.0f));
            set(G::route(n, G::RouteTarget), id + "dst", name + "Target", "Target",
                "What this route changes.", choice(targetViews, 0.0f));
            set(G::route(n, G::RouteAmount), id + "amt", name + "Amount", "Amount",
                "How much, and in which direction, this route moves its target.", percent(0.0f, -100.0f));
            set(G::route(n, G::RouteCurve), id + "curve", name + "Curve", "Curve",
                "Bends the route's response: below zero small moves of the source count for less, above zero "
                "for "
                "more.",
                percent(0.0f, -100.0f));
            set(G::route(n, G::RouteScale), id + "aux", name + "Scale", "Scale",
                "Another source that scales this route's amount, such as the mod wheel or velocity.",
                choice(sourceViews, 0.0f));
        }

        // Voice and output.
        set(G::VoiceMode, "voice.mode", "Voice Mode", "Mode",
            "Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the "
            "envelopes when "
            "notes overlap.",
            choice(kVoiceModes, 0.0f));
        set(G::VoiceCount, "voice.count", "Voices", "Voices", "How many notes can sound at once.",
            discrete(1.0f, 16.0f, 8.0f));
        set(G::VoiceGlide, "voice.glide", "Glide", "Glide",
            "How long the pitch takes to slide from one note to the next.", ms(0.0f, 2000.0f, 0.0f, 200.0f));
        set(G::VoiceGlideMode, "voice.glidemode", "Glide Mode", "Glide Mode",
            "Always slides; Legato slides only between overlapping notes.", choice(kGlideModes, 0.0f));
        set(G::VoiceBend, "voice.bend", "Bend Range", "Bend",
            "How far the pitch bend wheel bends, in semitones.",
            discrete(0.0f, 48.0f, 2.0f, Unit::Semitones));
        set(G::VoiceLatch, "voice.latch", "Latch", "Latch",
            "Keeps notes sounding after you let go, until you play the next; for drones.", toggle(0.0f));
        set(G::VoiceTails, "voice.tails", "Grain Tails", "Grain Tails",
            "When a note ends, its grains finish playing; off, they fade with the amp envelope.",
            toggle(1.0f));
        set(G::OutVolume, "out.volume", "Volume", "Volume", "The instrument's output level.",
            level(-6.0f, 6.0f));
        set(G::Quality, "quality", "Quality", "Quality",
            "Eco saves CPU; High is cleanest for grains pitched far from the source. Not automatable.",
            choice(kQualities, 1.0f, ParamFlags::None));

        // Spectral (step 3).
        set(G::SpecFft, "spec.fft", "Resolution", "Resolution",
            "How finely the spectrum is cut: higher sounds smoother, lower keeps attacks sharper. Not "
            "automatable.",
            choice(kResolutions, 2.0f, ParamFlags::None));
        set(G::SpecSmear, "spec.smear", "Smear", "Smear",
            "Blurs each partial's phase, for washes and extreme stretches.", percent(0.0f));
        set(G::SpecHarm, "spec.harm", "Harmonics", "Harm", "Pulls the partials onto the note's harmonics.",
            percent(0.0f));
        set(G::SpecClean, "spec.clean", "Clean", "Clean",
            "Keeps only the pitched partials, leaving the noise behind.", percent(0.0f));

        for (size_t i = 0; i < specs.size(); ++i)
        {
            specs[i].id = ids[i];
            specs[i].name = names[i];
            specs[i].shortName = shortNames[i];
            specs[i].description = descriptions[i];
        }
        info = DeviceInfo{
            .id = "granular",
            .name = "Granular",
            .category = "Instruments",
            .kind = DeviceKind::Instrument,
            .buses = {.inputChannels = 0, .outputChannels = 2, .sidechainChannels = 2, .acceptsMidi = true},
            .params = specs,
            .stateVersion = 1,
            .meters = meters};
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}
} // namespace

// ---- the modulation lists --------------------------------------------------------------------

std::span<const std::string_view> Granular::namedShapes() noexcept
{
    return dsp::namedCurveShapes();
}

std::optional<size_t> Granular::targetParam(int target) noexcept
{
    if (target >= TargetSource1 && target < TargetGrainCutoff)
    {
        const int n = (target - TargetSource1) / 4;
        constexpr SourceParam params[] = {SrcStart, SrcEnd, SrcGain, SrcTune};
        return source(n, params[(target - TargetSource1) % 4]);
    }
    if (target >= TargetFilter1 && target < TargetRouting)
    {
        const int n = (target - TargetFilter1) / 4;
        constexpr FilterParam params[] = {FilterCutoff, FilterRes, FilterDrive, FilterShape};
        return filter(n, params[(target - TargetFilter1) % 4]);
    }
    if (target >= TargetLfoRate && target < TargetRandomRate)
        return lfo(target - TargetLfoRate, LfoRate);
    if (target >= TargetRandomRate && target < TargetSmear)
        return random(target - TargetRandomRate, RandomRate);
    switch (target)
    {
    case TargetPosition:
        return Pos;
    case TargetPositionSpray:
        return PosSpray;
    case TargetScan:
        return ScanRate;
    case TargetSize:
        return GrainSize;
    case TargetSizeSpray:
        return GrainSizeSpray;
    case TargetOverlap:
        return GrainOverlap;
    case TargetDensity:
        return GrainDensity;
    case TargetPeriods:
        return GrainPeriods;
    case TargetFormant:
        return GrainFormant;
    case TargetCrossfade:
        return GrainXfade;
    case TargetJitter:
        return GrainJitter;
    case TargetChance:
        return GrainChance;
    case TargetSwing:
        return GrainSwing;
    case TargetFire:
        return GrainFire;
    case TargetSkew:
        return GrainSkew;
    case TargetFlatness:
        return GrainFlat;
    case TargetReverse:
        return GrainReverse;
    case TargetLevelSpray:
        return GrainLevelSpray;
    case TargetEven:
        return GrainEven;
    case TargetPitchSpray:
        return PitchSpray;
    case TargetGlide:
        return PitchGlide;
    case TargetPanSpray:
        return PanSpray;
    case TargetWidth:
        return OutWidth;
    case TargetSourceMix:
        return SrcMix;
    case TargetGrainCutoff:
        return GFilterCutoff;
    case TargetGrainRes:
        return GFilterRes;
    case TargetCutoffSpray:
        return GFilterSpray;
    case TargetCrush:
        return GColorCrush;
    case TargetRing:
        return GColorRing;
    case TargetRingRatio:
        return GColorRatio;
    case TargetRouting:
        return FiltersRouting;
    case TargetVolume:
        return OutVolume;
    case TargetLiveGain:
        return LiveGain;
    case TargetFeedback:
        return LiveFeedback;
    case TargetSmear:
        return SpecSmear;
    case TargetHarmonics:
        return SpecHarm;
    case TargetClean:
        return SpecClean;
    default:
        return std::nullopt; // None, Pitch, Pan and Amp move no parameter
    }
}

std::optional<int> Granular::targetOf(size_t param) noexcept
{
    for (int t = TargetNone + 1; t < NumTargets; ++t)
        if (targetParam(t) == param)
            return t;
    return std::nullopt;
}

// ---- the device --------------------------------------------------------------------------------

Granular::Granular() : Device(tables().info)
{
    for (auto& shape : lfoShapes_)
        shape.shape = "sine";
    window_.shape = "sine";
}

void Granular::prepare(const ProcessSpec&) {}

void Granular::reset() noexcept {}

void Granular::process(ProcessContext& context) noexcept
{
    // The grain engine comes with step 1 (SPEC, "Stages"); until then, silence.
    for (int c = 0; c < context.output.numChannels; ++c)
        std::fill_n(context.output.channel(c), context.numSamples, 0.0f);
    analysis().publish(ActiveVoices, 0.0f);
    analysis().publish(CpuLoad, 0.0f);
    analysis().publish(GrainsPlaying, 0.0f);
    for (int n = 0; n < numSources; ++n)
        analysis().publish(Scan1 + n, -1.0f);
}

double Granular::tailSeconds() const noexcept
{
    return params().get(env(0, EnvRelease)) * 0.001 + params().get(GrainSize) * 0.001;
}

std::shared_ptr<const GrainSource> Granular::sourceData(int n) const
{
    return sources_[static_cast<size_t>(std::clamp(n, 0, numSources - 1))];
}

void Granular::setSourceData(int n, std::shared_ptr<const GrainSource> data)
{
    sources_[static_cast<size_t>(std::clamp(n, 0, numSources - 1))] = std::move(data);
}

std::string Granular::macroName(int n) const
{
    const auto& name = macroNames_[static_cast<size_t>(std::clamp(n, 0, numMacros - 1))];
    return name.empty() ? "Macro " + std::to_string(n + 1) : name;
}

void Granular::publishGrains(std::span<const GrainMark> marks) noexcept
{
    auto& back = markBuffers_[static_cast<size_t>(backMarks_)];
    back.count = static_cast<int>(std::min(marks.size(), back.marks.size()));
    std::copy_n(marks.begin(), back.count, back.marks.begin());
    backMarks_ = middleMarks_.exchange(backMarks_ | kNewMarks, std::memory_order_acq_rel) & ~kNewMarks;
}

std::vector<Granular::GrainMark> Granular::grains() const
{
    if ((middleMarks_.load(std::memory_order_acquire) & kNewMarks) != 0)
        frontMarks_ = middleMarks_.exchange(frontMarks_, std::memory_order_acq_rel) & ~kNewMarks;
    const auto& front = markBuffers_[static_cast<size_t>(frontMarks_)];
    return {front.marks.begin(), front.marks.begin() + front.count};
}

// ---- state that isn't a parameter (SPEC, "State that isn't a parameter") ----------------------

void Granular::writeExtraState(nlohmann::json& extra) const
{
    const auto writeCurve = [&extra](const std::string& key, const Curve& c, std::string_view standard)
    {
        if (!c.points.empty())
            extra[key + ".points"] = c.points;
        else if (c.shape != standard)
            extra[key + ".shape"] = c.shape;
    };
    for (int n = 0; n < numSources; ++n)
        if (const auto& s = sources_[static_cast<size_t>(n)])
        {
            const std::string key = "src" + std::to_string(n + 1) + ".";
            extra[key + "path"] = s->path;
            extra[key + "name"] = s->name;
            if (s->root && !s->rootFound)
                extra[key + "root"] = *s->root;
        }
    for (int n = 0; n < numLfos; ++n)
        writeCurve("lfo" + std::to_string(n + 1), lfoShapes_[static_cast<size_t>(n)], "sine");
    writeCurve("window", window_, "sine");
    for (int n = 0; n < numMacros; ++n)
        if (!macroNames_[static_cast<size_t>(n)].empty())
            extra["macro" + std::to_string(n + 1) + ".name"] = macroNames_[static_cast<size_t>(n)];
}

void Granular::readExtraState(const nlohmann::json& extra)
{
    const auto text = [&extra](const std::string& key) -> std::string
    {
        const auto it = extra.find(key);
        return it != extra.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const auto readCurve = [&extra, &text](const std::string& key, std::string_view standard)
    {
        Curve c;
        if (const auto it = extra.find(key + ".points"); it != extra.end() && it->is_array())
            for (const auto& p : *it)
                if (p.is_array() && p.size() == 3)
                    c.points.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
        const auto shape = text(key + ".shape");
        c.shape = shape.empty() ? std::string(standard) : shape;
        std::sort(c.points.begin(), c.points.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
        return c;
    };
    for (int n = 0; n < numLfos; ++n)
        lfoShapes_[static_cast<size_t>(n)] = readCurve("lfo" + std::to_string(n + 1), "sine");
    window_ = readCurve("window", "sine");
    for (int n = 0; n < numMacros; ++n)
        macroNames_[static_cast<size_t>(n)] = text("macro" + std::to_string(n + 1) + ".name");
    // The sources' files are read by the host's loader (step 1), which hands them over with
    // setSourceData().
}

const DeviceFactory& granularFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return tables().info; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Granular>(); },
        // Something to play: the generated chord in Source 1 (the engine plays it from step 1).
        .testSetup =
            [](Device& d)
        {
            static const auto chord = GrainSource::build(GrainSource::chord(6.0, 48000.0), "Chord", {});
            dynamic_cast<Granular&>(d).setSourceData(0, chord);
        },
    };
    return factory;
}
} // namespace fd::devices
