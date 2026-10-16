#include "Granular.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <random>

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

// ---- the engine ----------------------------------------------------------------------------------

namespace
{
using granular::Grain;

// Each choice's length in beats (quarter notes); negative: bars, from the time signature.
constexpr double kSyncBeats[] = {0.0,     1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3,
                                 3.0 / 8, 1.0 / 2,  2.0 / 3,  3.0 / 4, 1.0,     4.0 / 3, 3.0 / 2,
                                 2.0,     3.0,      -1.0,     -2.0,    -4.0,    -8.0};
constexpr double kScanSyncBeats[] = {0.0,  2.0 / 3, 1.0,  1.5,  4.0 / 3, 2.0,  3.0,
                                     -1.0, -2.0,    -4.0, -8.0, -16.0,   -32.0};
constexpr double kSizeSyncBeats[] = {0.0,      1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6,
                                     3.0 / 16, 1.0 / 4,  1.0 / 3,  3.0 / 8,  1.0 / 2,  2.0 / 3, 3.0 / 4,
                                     1.0,      4.0 / 3,  1.5,      2.0,      3.0,      -1.0};
constexpr double kRateBeats[] = {1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 3.0 / 16,
                                 1.0 / 4,  1.0 / 3,  3.0 / 8,  1.0 / 2,  2.0 / 3, 3.0 / 4, 1.0,
                                 4.0 / 3,  1.5,      2.0,      3.0,      -1.0,    -2.0,    -4.0};

template <size_t N> double beatsOf(const double (&table)[N], int choice, const TransportInfo& t) noexcept
{
    const double value = table[static_cast<size_t>(std::clamp(choice, 0, static_cast<int>(N) - 1))];
    const double beatsPerBar = t.timeSigNumerator * 4.0 / std::max(1, t.timeSigDenominator);
    return value < 0.0 ? -value * beatsPerBar : value;
}

double samplesPerBeat(const TransportInfo& t, double sampleRate) noexcept
{
    return sampleRate * 60.0 / std::max(1.0, t.tempoBpm);
}

std::uint32_t hash(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x == 0 ? 1 : x;
}

/// Counter-based randomness (SPEC, "Performance": hashed, not run): a grain's values come from
/// the voice's seed and the grain's number, so a kept roll renders exactly.
struct Rng
{
    std::uint32_t state;
    float next01() noexcept
    {
        state = hash(state + 0x9E3779B9u);
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
    float bipolar() noexcept { return 2.0f * next01() - 1.0f; }
};

/// Chords as intervals from their root (Chord's choices but the last, the Key Chord).
constexpr std::array<std::array<int, 5>, 10> kChordTones{{{0, 4, 7, -1, -1},
                                                          {0, 3, 7, -1, -1},
                                                          {0, 2, 7, -1, -1},
                                                          {0, 5, 7, -1, -1},
                                                          {0, 4, 7, 11, -1},
                                                          {0, 3, 7, 10, -1},
                                                          {0, 4, 7, 10, -1},
                                                          {0, 4, 7, 11, 14},
                                                          {0, 3, 7, 10, 14},
                                                          {0, 7, -1, -1, -1}}};

/// The nearest offset to `s` (semitones) among `tones`, repeating each octave.
double nearestTone(double s, const int* tones, int count) noexcept
{
    double best = s, distance = 1e9;
    for (int octave = -5; octave <= 5; ++octave)
        for (int i = 0; i < count; ++i)
            if (tones[i] >= 0)
            {
                const double t = tones[i] + 12.0 * octave;
                if (std::abs(t - s) < distance)
                {
                    distance = std::abs(t - s);
                    best = t;
                }
            }
    return best;
}

/// The nearest note of `key` to `pitch` (MIDI), below on a tie; the pitch rounded without a key.
int nearestInKey(const KeyInfo& key, double pitch) noexcept
{
    const int at = static_cast<int>(std::lround(pitch));
    if (!key.known())
        return at;
    for (int d = 0; d < 12; ++d)
    {
        if (key.contains(at - d))
            return at - d;
        if (key.contains(at + d))
            return at + d;
    }
    return at;
}

/// The key's chord on the note's degree: thirds stacked in the key from the note (major when the
/// note is off the key, or there's no key).
std::array<int, 5> keyChord(const KeyInfo& key, int note) noexcept
{
    std::array<int, 5> tones{0, 4, 7, -1, -1};
    if (!key.known() || !key.contains(note))
        return tones;
    int at = note;
    for (size_t found = 1; found < 3; ++found)
    {
        // Two notes of the key up from the last: a third in the key.
        for (int passed = 0; passed < 2;)
            if (key.contains(++at))
                ++passed;
        tones[found] = at - note;
    }
    return tones;
}

/// Balance for a stereo source, equal power for a mono one; both at unity in the middle.
void panGains(Grain& g, float pan, float gain, int channels) noexcept
{
    pan = std::clamp(pan, -1.0f, 1.0f);
    if (channels == 2)
    {
        // Moving right takes the left channel into the right, and back.
        g.gLL = gain * (1.0f - std::max(0.0f, pan));
        g.gRL = gain * std::max(0.0f, -pan);
        g.gRR = gain * (1.0f - std::max(0.0f, -pan));
        g.gLR = gain * std::max(0.0f, pan);
    }
    else
    {
        const float theta = (pan + 1.0f) * static_cast<float>(std::numbers::pi) * 0.25f;
        g.gLL = gain * std::cos(theta) * std::numbers::sqrt2_v<float>;
        g.gLR = gain * std::sin(theta) * std::numbers::sqrt2_v<float>;
        g.gRL = g.gRR = 0.0f;
    }
}

/// A source's position bent into its part: wrapped, bounced or held at the ends.
double fold(double place, int end) noexcept
{
    switch (end)
    {
    case 0:
        return place - std::floor(place);
    case 1:
    {
        const double m = place - 2.0 * std::floor(place / 2.0);
        return m <= 1.0 ? m : 2.0 - m;
    }
    default:
        return std::clamp(place, 0.0, 1.0);
    }
}

constexpr float kSilent = 1.6e-5f;      // -96 dB: a grain this quiet isn't started
constexpr double kStealSeconds = 0.002; // a stolen grain's fade, and an old source's
constexpr double kRingSeconds = 0.02;   // the voice filters ring on after the last grain
} // namespace

Granular::Granular() : Device(tables().info)
{
    for (auto& shape : lfoShapes_)
        shape.shape = "sine";
    window_.shape = "sine";
    roll_.store(hash(kRollBase), std::memory_order_relaxed);
    publishAssets();
}

void Granular::publishAssets()
{
    auto a = std::make_shared<Assets>();
    {
        const std::scoped_lock lock(loaderMutex_);
        a->sources = sources_;
        a->roots = roots_;
    }
    for (int n = 0; n < numLfos; ++n)
        a->lfoShapes[static_cast<size_t>(n)] = lfoShapes_[static_cast<size_t>(n)].table();
    a->window = window_.table();
    assets_.publish(std::move(a));
}

void Granular::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    maxBlock_ = std::max(1, spec.maxBlockSize);
    scratch_.prepare(maxBlock_);
    // A segment a tick, and room for the notes a block starts on one voice.
    const int segments = maxBlock_ / grid_.interval() + 2 + 32;
    for (auto& v : voice_)
    {
        v.grains.assign(static_cast<size_t>(2 * maxGrainsPerVoice), Grain{});
        v.segments.assign(static_cast<size_t>(segments), Segment{});
        v.busL.assign(static_cast<size_t>(maxBlock_), 0.0f);
        v.busR.assign(static_cast<size_t>(maxBlock_), 0.0f);
        v.amp.assign(static_cast<size_t>(maxBlock_), 0.0f);
        for (auto& pair : v.filters)
            for (auto& f : pair)
                f.prepare(sampleRate_);
        for (auto& e : v.envs)
            e.prepare(sampleRate_);
    }
    spareRight_.assign(static_cast<size_t>(maxBlock_), 0.0f);
    reset();
}

void Granular::reset() noexcept
{
    grid_.reset();
    voices_.reset();
    for (auto& v : voice_)
    {
        v.sounding = false;
        v.active = 0;
        v.segmentCount = v.rendered = v.envAt = 0;
        v.dcIn = v.dcOut = {};
        for (auto& e : v.envs)
            e.reset();
        for (auto& pair : v.filters)
            for (auto& f : pair)
                f.reset();
    }
    for (auto& l : lfos_)
        l.reset();
    for (int n = 0; n < numRandoms; ++n)
        randoms_[static_cast<size_t>(n)].reset(static_cast<std::uint32_t>(1000 + n));
    lfoValue_ = {};
    randomValue_ = {};
    bend_ = {};
    pressure_ = {};
    slide_ = {};
    modWheel_ = 0.0f;
    notes_ = 0;
    newest_ = -1;
    lastPitch_ = 60.0;
    sharedTravel_ = 0.0;
    wasPlaying_ = false;
    smoothedPrimed_ = false;
    blockStart_ = blockSong_ = 0;
    sinceDisplay_ = 0;
    // Deterministic: the passes roll again from the first.
    rollStarts_ = 0;
    if (!rollKept_.load(std::memory_order_relaxed))
        roll_.store(hash(kRollBase), std::memory_order_relaxed);
    volume_.reset(dsp::dbToGain(params().get(OutVolume), -70.0f));
}

double Granular::tailSeconds() const noexcept
{
    // The release, and with Grain Tails the longest grain it may still play.
    const auto& p = params();
    const double grains = p.getBool(VoiceTails) ? 4.0 * std::max(1, p.getInt(GrainRepeats)) : 0.0;
    return p.get(env(0, EnvRelease)) * 0.001 + grains + 0.05;
}

void Granular::startPass(std::uint64_t roll) noexcept
{
    hostRoll_.store(hash(static_cast<std::uint32_t>(roll ^ (roll >> 32))), std::memory_order_relaxed);
}

dsp::CurvedEnvelope::Settings Granular::envelopeSettings(int n) const noexcept
{
    const auto& p = params();
    dsp::CurvedEnvelope::Settings s;
    s.attackMs = p.get(env(n, EnvAttack));
    s.holdMs = p.get(env(n, EnvHold));
    s.decayMs = p.get(env(n, EnvDecay));
    s.sustain = (smoothedPrimed_ ? smoothed_[env(n, EnvSustain)] : p.get(env(n, EnvSustain))) * 0.01;
    s.releaseMs = p.get(env(n, EnvRelease));
    s.attackCurve = p.get(env(n, EnvAttackCurve)) * 0.01;
    s.decayCurve = p.get(env(n, EnvDecayCurve)) * 0.01;
    s.releaseCurve = p.get(env(n, EnvReleaseCurve)) * 0.01;
    s.mode = static_cast<dsp::CurvedEnvelope::Mode>(p.getInt(env(n, EnvMode)));
    return s;
}

// ---- notes ---------------------------------------------------------------------------------------

void Granular::updateZones(Voice& v) noexcept
{
    const auto* a = current_.get();
    for (int s = 0; s < numSources; ++s)
    {
        const auto* set = a != nullptr ? a->sources[static_cast<size_t>(s)].get() : nullptr;
        v.zones[static_cast<size_t>(s)] = set != nullptr ? set->zoneFor(v.key, v.velocity127) : nullptr;
    }
}

void Granular::Listener::voiceStart(int index, int key, float velocity, int channel, std::int32_t,
                                    bool retrigger)
{
    auto& v = g.voice_[static_cast<size_t>(index)];
    const auto& p = g.params();
    const int at = g.eventAt_;
    const bool continuing = v.sounding && retrigger;
    if (continuing)
        g.advanceEnvs(v, at);
    else
    {
        // From silence: nothing playing, the block's buses empty from here.
        v.active = 0;
        v.envAt = v.rendered = at;
        std::fill(v.busL.begin() + at, v.busL.begin() + g.maxBlock_, 0.0f);
        std::fill(v.busR.begin() + at, v.busR.begin() + g.maxBlock_, 0.0f);
    }
    v.key = key;
    v.channel = channel & 15;
    v.velocity = velocity;
    v.velocity127 = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
    v.seed = hash(g.roll_.load(std::memory_order_relaxed) ^ hash(++g.notes_ * 2654435761U));
    v.grainCount = 0;
    v.heldSamples = 0.0;
    v.born = g.blockStart_ + at;
    v.nextOnset = v.nextBase = 0.0; // its first grain at once
    v.step = 0;
    v.findStep = true;
    v.travel = 0.0;
    v.direction = 1;
    v.spawning = true;
    v.released = v.frozen = false;
    v.envEnd = v.tailEnd = -1;
    for (int n = 0; n < numEnvs; ++n)
    {
        v.envs[static_cast<size_t>(n)].set(g.envelopeSettings(n));
        v.envs[static_cast<size_t>(n)].noteOn();
    }
    for (int n = 0; n < numLfos; ++n)
        v.lfos[static_cast<size_t>(n)].noteOn(p.get(lfo(n, LfoPhase)) / 360.0);
    v.lfoSmoothed = {};
    v.randomValue = {};
    for (int n = 0; n < numRandoms; ++n)
    {
        v.randoms[static_cast<size_t>(n)].reset(hash(v.seed + 500 + static_cast<std::uint32_t>(n)));
        v.randoms[static_cast<size_t>(n)].noteOn();
    }
    g.updateZones(v);
    // Glide: from the last note played, always, or only between notes held together.
    const double ms = p.get(VoiceGlide);
    const bool glide = ms > 0.0 && (p.getInt(VoiceGlideMode) == 0 || continuing);
    v.glidePitch = glide ? g.lastPitch_ : key;
    v.glideTarget = key;
    v.glideRate = glide ? std::abs(key - v.glidePitch) / std::max(1.0, ms * 0.001 * g.sampleRate_) : 0.0;
    g.lastPitch_ = key;
    v.sounding = true;
    g.newest_ = index;
    // Its controls now, held to the next tick.
    g.tickVoice(v, g.tickTransport_, -1);
    v.lastFire = g.moved(GrainFire, TargetFire, v.mod);
    v.ctrlFrom = v.ctrlTo;
    g.addSegment(v, at, g.tickPhase_ + (at - g.tickOffset_), !continuing);
}

void Granular::Listener::voiceLegato(int index, int key, float velocity)
{
    auto& v = g.voice_[static_cast<size_t>(index)];
    v.velocity = velocity;
    v.glideTarget = key;
    const double ms = g.params().get(VoiceGlide);
    v.glideRate = ms > 0.0 ? std::abs(key - v.glidePitch) / std::max(1.0, ms * 0.001 * g.sampleRate_) : 1e9;
    v.key = key;
    g.lastPitch_ = key;
    g.updateZones(v);
}

void Granular::Listener::voiceRelease(int index)
{
    // Latch holds notes until the next one (or every note is let go).
    if (g.params().getBool(VoiceLatch) && !g.allOff_)
        return;
    g.release(g.voice_[static_cast<size_t>(index)], g.eventAt_);
}

void Granular::release(Voice& v, int at) noexcept
{
    if (v.released || !v.sounding)
        return;
    advanceEnvs(v, at);
    for (auto& e : v.envs)
        e.noteOff();
    v.released = true;
    // Grain Tails: the grains playing keep their level, and those that start in the release
    // take theirs from the envelope as they start (a one-shot envelope plays out as it is).
    if (params().getBool(VoiceTails) && static_cast<dsp::CurvedEnvelope::Mode>(params().getInt(
                                            env(0, EnvMode))) != dsp::CurvedEnvelope::Mode::OneShot)
    {
        v.frozen = true;
        v.releaseLevel = std::max(1e-3f, v.envs[0].level());
    }
}

void Granular::handleEvent(const Event& e, int at) noexcept
{
    eventAt_ = at;
    const auto ch = static_cast<size_t>(e.channel & 15);
    switch (e.type)
    {
    case EventType::NoteOn:
        // Latch: a new note lets the held ones go (those whose keys are up).
        if (params().getBool(VoiceLatch))
            for (int i = 0; i < static_cast<int>(voice_.size()); ++i)
                if (!voices_.voice(i).gate)
                    release(voice_[static_cast<size_t>(i)], at);
        voices_.handle(e, listener_);
        break;
    case EventType::NoteOff:
        voices_.handle(e, listener_);
        break;
    case EventType::AllNotesOff:
        allOff_ = true;
        voices_.handle(e, listener_);
        for (auto& v : voice_)
            release(v, at);
        allOff_ = false;
        break;
    case EventType::Controller:
        if (e.key == 1)
            modWheel_ = e.value;
        else if (e.key == 74)
            slide_[ch] = e.value;
        else
            voices_.handle(e, listener_); // the sustain pedal
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

// ---- control ticks -------------------------------------------------------------------------------

float Granular::sourceValue(const Voice& v, int source) const noexcept
{
    const auto s = static_cast<Source>(source);
    if (s >= Source::AmpEnv && s <= Source::Env3)
        return v.envs[static_cast<size_t>(source - static_cast<int>(Source::AmpEnv))].level();
    if (s >= Source::Lfo1 && s < Source::Random1)
    {
        const int n = source - static_cast<int>(Source::Lfo1);
        if (!params().getBool(lfo(n, LfoPoly)))
            return lfoValue_[static_cast<size_t>(n)];
        float value = v.lfoSmoothed[static_cast<size_t>(n)];
        if (const double fade = params().get(lfo(n, LfoFade)) * 0.001 * sampleRate_; fade > 1.0)
            value *= static_cast<float>(std::min(1.0, v.heldSamples / fade));
        return value;
    }
    if (s >= Source::Random1 && s < Source::Macro1)
    {
        const int n = source - static_cast<int>(Source::Random1);
        return params().getBool(random(n, RandomPoly)) ? v.randomValue[static_cast<size_t>(n)]
                                                       : randomValue_[static_cast<size_t>(n)];
    }
    if (s >= Source::Macro1 && s < Source::Velocity)
        return smoothed_[macro(source - static_cast<int>(Source::Macro1))] * 0.01f;
    switch (s)
    {
    case Source::Velocity:
        return v.velocity;
    case Source::Note:
        return std::clamp((v.key - 60) / 60.0f, -1.0f, 1.0f);
    case Source::Pressure:
        return pressure_[static_cast<size_t>(v.channel)];
    case Source::Slide:
        return slide_[static_cast<size_t>(v.channel)];
    case Source::PitchBend:
        return bend_[static_cast<size_t>(v.channel)];
    case Source::ModWheel:
        return modWheel_;
    case Source::Scan:
    {
        const double place = v.place[0];
        return static_cast<float>(place - std::floor(place));
    }
    default:
        return 0.0f; // the grain's own come at each grain's start; Input Level with step 2
    }
}

float Granular::moved(size_t param, int target, const std::array<float, NumTargets>& mod) const noexcept
{
    const auto& spec = tables().specs[param];
    const float base = smoothedPrimed_ ? smoothed_[param] : params().get(param);
    const float m = target > 0 && target < NumTargets ? mod[static_cast<size_t>(target)] : 0.0f;
    if (m == 0.0f)
        return base;
    return spec.fromNormalized(std::clamp(spec.toNormalized(base) + m, 0.0f, 1.0f));
}

double Granular::notePitch(const Voice& v, const std::array<float, NumTargets>& mod) const noexcept
{
    return v.glidePitch + bend_[static_cast<size_t>(v.channel)] * params().get(VoiceBend) +
           params().get(PitchTranspose) + moved(PitchFine, 0, mod) * 0.01 + 24.0 * mod[TargetPitch];
}

void Granular::tick(const TransportInfo& transport, int offset) noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();
    eventAt_ = offset;
    // Envelopes up to here, before any setting changes.
    for (auto& v : voice_)
        if (v.sounding)
            advanceEnvs(v, offset);
    volume_.setTarget(dsp::dbToGain(p.get(OutVolume), -70.0f), dsp::msToSamples(20.0, sampleRate_));
    voices_.setVoiceCount(p.getInt(VoiceCount), listener_);
    voices_.setMode(static_cast<VoiceManager::Mode>(p.getInt(VoiceMode)), listener_);

    // Continuous settings glide over about 20 ms, at the control rate.
    const double k = smoothedPrimed_ ? 1.0 - std::exp(-interval / (0.02 * sampleRate_)) : 1.0;
    for (size_t i = 0; i < smoothed_.size(); ++i)
    {
        const float target = p.get(i);
        if (p.spec(i).kind != ParamKind::Continuous)
            smoothed_[i] = target;
        else
            smoothed_[i] += static_cast<float>(k * (target - smoothed_[i]));
    }
    smoothedPrimed_ = true;

    // The shared LFOs and random sources.
    const auto* a = current_.get();
    const auto cycles = [&](int sync, double hz) -> double
    {
        if (sync <= 0)
            return hz / sampleRate_;
        return std::max(1.0, transport.tempoBpm) / 60.0 / beatsOf(kSyncBeats, sync, transport) / sampleRate_;
    };
    for (int n = 0; n < numLfos && a != nullptr; ++n)
    {
        if (p.getBool(lfo(n, LfoPoly)))
            continue;
        const auto i = static_cast<size_t>(n);
        const int sync = p.getInt(lfo(n, LfoSync));
        const double song = sync > 0 && transport.playing
                                ? transport.ppqPosition / beatsOf(kSyncBeats, sync, transport) +
                                      p.get(lfo(n, LfoPhase)) / 360.0
                                : -1.0;
        const double hz = smoothed_[lfo(n, LfoRate)];
        const float raw =
            lfos_[i].advance(a->lfoShapes[i], dsp::ShapeLfo::Mode::Free, interval, cycles(sync, hz), song);
        const double tau = smoothed_[lfo(n, LfoSmooth)] * 0.01 * 0.25 / std::max(0.01, hz);
        const double follow = tau > 1e-6 ? 1.0 - std::exp(-interval / (tau * sampleRate_)) : 1.0;
        lfoValue_[i] += static_cast<float>(follow * (raw - lfoValue_[i]));
    }
    for (int n = 0; n < numRandoms; ++n)
        if (!p.getBool(random(n, RandomPoly)) && p.getInt(random(n, RandomMode)) != RandomPerGrain)
            randomValue_[static_cast<size_t>(n)] = randoms_[static_cast<size_t>(n)].advance(
                static_cast<dsp::RandomSource::Mode>(p.getInt(random(n, RandomMode))), interval,
                cycles(p.getInt(random(n, RandomSync)), smoothed_[random(n, RandomRate)]));

    for (auto& v : voice_)
        if (v.sounding)
        {
            v.ctrlFrom = v.ctrlTo;
            tickVoice(v, transport, offset);
            addSegment(v, offset, 0, false);
        }

    // The Shared playhead moves on: one pass of a four-second part a second at 100 %.
    sharedTravel_ += smoothed_[ScanRate] * 0.01 * interval / sampleRate_ / 4.0;
}

void Granular::tickVoice(Voice& v, const TransportInfo& transport, int offset) noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();

    // The matrix: every route's source, bent, scaled, into its target (in its normalised range).
    // The grain's own sources (Grain Random, Grain Alternate, a random source Per Grain) come at
    // each grain's start.
    std::array<float, NumTargets> mod{};
    for (int r = 0; r < numRoutes; ++r)
    {
        const int src = p.getInt(route(r, RouteSource)), dst = p.getInt(route(r, RouteTarget));
        const float amount = smoothed_[route(r, RouteAmount)] * 0.01f;
        if (src <= 0 || dst <= 0 || dst >= NumTargets || amount == 0.0f)
            continue;
        const auto s = static_cast<Source>(src);
        if (s == Source::GrainRandom || s == Source::GrainAlternate ||
            (s >= Source::Random1 && s < Source::Macro1 &&
             p.getInt(random(src - static_cast<int>(Source::Random1), RandomMode)) == RandomPerGrain))
            continue;
        const float value = sourceValue(v, src);
        const float bent = std::copysign(
            std::pow(std::abs(value), std::exp2(-2.0f * smoothed_[route(r, RouteCurve)] * 0.01f)), value);
        float scale = 1.0f;
        if (const int aux = p.getInt(route(r, RouteScale)); aux > 0)
        {
            const float sv = sourceValue(v, aux);
            const auto as = static_cast<Source>(aux);
            const bool bipolar =
                (as >= Source::Lfo1 && as < Source::Macro1) || as == Source::Note || as == Source::PitchBend;
            scale = bipolar ? 0.5f * (sv + 1.0f) : sv;
        }
        mod[static_cast<size_t>(dst)] += bent * amount * scale;
    }
    v.mod = mod;

    // Pitch glides at the control rate.
    if (v.glidePitch != v.glideTarget)
    {
        const double step = v.glideRate * interval;
        v.glidePitch = std::abs(v.glideTarget - v.glidePitch) <= step
                           ? v.glideTarget
                           : v.glidePitch + std::copysign(step, v.glideTarget - v.glidePitch);
    }

    // The voice stage: its filters (key tracking and Env 2 on the cutoff) and its amp.
    for (int n = 0; n < numFilters; ++n)
    {
        const auto i = static_cast<size_t>(n);
        const int t = TargetFilter1 + n * 4;
        const double octaves = smoothed_[filter(n, FilterKey)] * 0.01 * (v.glidePitch - 60.0) / 12.0 +
                               smoothed_[filter(n, FilterEnv)] * 0.01 * 5.0 * v.envs[1].level() +
                               5.0 * mod[static_cast<size_t>(t)];
        v.ctrlTo[CCutoff0 + i] = static_cast<float>(
            std::clamp(smoothed_[filter(n, FilterCutoff)] * std::exp2(octaves), 20.0, 0.45 * sampleRate_));
        v.ctrlTo[CRes0 + i] = moved(filter(n, FilterRes), t + 1, mod) * 0.01f;
        v.ctrlTo[CDrive0 + i] = moved(filter(n, FilterDrive), t + 2, mod) * 0.01f;
        v.ctrlTo[CShape0 + i] = moved(filter(n, FilterShape), t + 3, mod) * 0.01f;
    }
    v.ctrlTo[CRouting] = moved(FiltersRouting, TargetRouting, mod) * 0.01f;
    const double velocity = 1.0 - smoothed_[AmpVelocity] * 0.01 * (1.0 - v.velocity);
    const double density = p.getBool(GrainComp) ? 1.0 / std::sqrt(std::max(1.0, expectedOverlap(v))) : 1.0;
    // Volume's modulation moves this note against the instrument's Volume (which the output applies).
    const double volume = mod[TargetVolume] != 0.0f
                              ? dsp::dbToGain(moved(OutVolume, TargetVolume, mod), -70.0f) /
                                    std::max(1e-6f, dsp::dbToGain(smoothed_[OutVolume], -70.0f))
                              : 1.0;
    v.ctrlTo[CAmp] = static_cast<float>(std::max(0.0, velocity * (1.0 + mod[TargetAmp]) * density * volume));
    v.ctrlTo[CWidth] = moved(OutWidth, TargetWidth, mod) * 0.01f;

    // Envelope settings (new times from here).
    for (int n = 0; n < numEnvs; ++n)
        v.envs[static_cast<size_t>(n)].set(envelopeSettings(n));

    // Per-note LFOs and random sources, for the next tick.
    const auto* a = current_.get();
    const auto cycles = [&](int sync, double hz) -> double
    {
        if (sync <= 0)
            return hz / sampleRate_;
        return std::max(1.0, transport.tempoBpm) / 60.0 / beatsOf(kSyncBeats, sync, transport) / sampleRate_;
    };
    for (int n = 0; n < numLfos && a != nullptr; ++n)
    {
        if (!p.getBool(lfo(n, LfoPoly)))
            continue;
        const auto i = static_cast<size_t>(n);
        const double hz = moved(lfo(n, LfoRate), TargetLfoRate + n, mod);
        const float raw =
            v.lfos[i].advance(a->lfoShapes[i], static_cast<dsp::ShapeLfo::Mode>(p.getInt(lfo(n, LfoMode))),
                              interval, cycles(p.getInt(lfo(n, LfoSync)), hz));
        const double tau = smoothed_[lfo(n, LfoSmooth)] * 0.01 * 0.25 / std::max(0.01, hz);
        const double follow = tau > 1e-6 ? 1.0 - std::exp(-interval / (tau * sampleRate_)) : 1.0;
        v.lfoSmoothed[i] += static_cast<float>(follow * (raw - v.lfoSmoothed[i]));
    }
    for (int n = 0; n < numRandoms; ++n)
        if (p.getBool(random(n, RandomPoly)) && p.getInt(random(n, RandomMode)) != RandomPerGrain)
            v.randomValue[static_cast<size_t>(n)] = v.randoms[static_cast<size_t>(n)].advance(
                static_cast<dsp::RandomSource::Mode>(p.getInt(random(n, RandomMode))), interval,
                cycles(p.getInt(random(n, RandomSync)),
                       moved(random(n, RandomRate), TargetRandomRate + n, mod)));
    v.heldSamples += interval;

    // The playhead: Per Note travels from the note's start, Shared with every note, Song with the
    // song's position (one pass in Scan Sync's note value, else at Scan's speed). Measured in the
    // first source's part; the song's position in samples, so it's exact.
    const int from = p.getInt(ScanFrom);
    const int sync = p.getInt(ScanSync);
    const double rate = moved(ScanRate, TargetScan, mod) * 0.01;
    const auto* zone = v.zones[0] != nullptr ? v.zones[0] : v.zones[1];
    const int part = v.zones[0] != nullptr ? 0 : 1;
    const double partSeconds =
        zone != nullptr && zone->audio != nullptr
            ? std::max(1e-3,
                       zone->audio->seconds() *
                           std::max(1e-4, (moved(source(part, SrcEnd), TargetSource1 + part * 4 + 1, mod) -
                                           moved(source(part, SrcStart), TargetSource1 + part * 4, mod)) *
                                              0.01))
            : 4.0;
    const double passSamples =
        sync > 0 ? beatsOf(kScanSyncBeats, sync, transport) * samplesPerBeat(transport, sampleRate_) : 0.0;
    double travel = v.travel;
    if (from == 2)
        travel = sync > 0 ? static_cast<double>(transport.samplePosition) / passSamples
                          : static_cast<double>(transport.samplePosition) / sampleRate_ * rate / partSeconds;
    else if (from == 1)
        travel = sharedTravel_ * 4.0 / partSeconds;
    for (auto& place : v.place)
        place = static_cast<float>(travel);
    // Per Note moves on for the next tick (Stop holds it at the part's end).
    if (from == 0)
    {
        v.travel += sync > 0 ? interval / passSamples : rate * interval / sampleRate_ / partSeconds;
        if (p.getInt(ScanEnd) == 2)
            v.travel = std::clamp(v.travel, -1.0, 1.0);
    }

    // Fire: a grain the moment it rises past half.
    const float fire = moved(GrainFire, TargetFire, mod);
    if (offset >= 0 && v.lastFire < 50.0f && fire >= 50.0f && v.spawning)
        spawn(v, transport, offset);
    v.lastFire = fire;
}

double Granular::expectedOverlap(const Voice& v) const noexcept
{
    const auto& p = params();
    const double size = moved(GrainSize, TargetSize, v.mod) * 0.001 * sampleRate_;
    double overlap = 1.0;
    switch (static_cast<Mode>(p.getInt(GrainMode)))
    {
    case Mode::Classic:
        overlap = moved(GrainOverlap, TargetOverlap, v.mod);
        break;
    case Mode::Loop:
        overlap = 1.0 / std::max(0.5, 1.0 - moved(GrainXfade, TargetCrossfade, v.mod) * 0.01);
        break;
    case Mode::Cloud:
        overlap = moved(GrainDensity, TargetDensity, v.mod) * size / sampleRate_;
        break;
    case Mode::Pitched:
        overlap = moved(GrainPeriods, TargetPeriods, v.mod);
        break;
    case Mode::Rhythm:
    case Mode::Spectral:
        overlap = 1.0;
        break;
    }
    if (p.getInt(SrcPick) == 2 && p.getBool(source(0, SrcOn)) && p.getBool(source(1, SrcOn)))
        overlap *= 2.0;
    return std::min(overlap, static_cast<double>(grainCap()));
}

// ---- grains --------------------------------------------------------------------------------------

namespace
{
/// Where a grain ends (its repeats too), in the block's samples, as it stands after the voice's
/// grains were last rendered up to `rendered`.
std::int64_t endOf(const Grain& g, int rendered) noexcept
{
    return rendered + std::max(0, g.delay) + (g.length - g.age) +
           static_cast<std::int64_t>(g.repeatsLeft) * g.length;
}
} // namespace

int Granular::alive(const Voice& v, int at) const noexcept
{
    // Playing at `at`, not stepping aside: counted, not rendered, so a block's cut never shows.
    int count = 0;
    for (int i = 0; i < v.active; ++i)
    {
        const auto& g = v.grains[static_cast<size_t>(i)];
        count += g.fadeLength == 0 && at < endOf(g, v.rendered) ? 1 : 0;
    }
    return count;
}

int Granular::grainCap() const noexcept
{
    // A note's Max Grains, within its share of the instance's 1 024 (fixed, so a render never
    // depends on how a block is cut).
    return std::max(1, std::min({params().getInt(GrainMax), maxGrainsPerVoice,
                                 maxGrains / std::max(1, params().getInt(VoiceCount))}));
}

void Granular::schedule(Voice& v, const TransportInfo& transport, int from, int to) noexcept
{
    if (!v.spawning)
        return;
    const auto& p = params();
    const auto mode = static_cast<Mode>(p.getInt(GrainMode));
    if (mode == Mode::Spectral)
        return; // step 3
    // Its note's start in this block (exact: the counts are whole samples).
    const auto bornAt = static_cast<double>(v.born - blockStart_);
    const double swing = moved(GrainSwing, TargetSwing, v.mod) * 0.01 * 0.5; // of a step, every second
    if (mode == Mode::Rhythm && transport.playing)
    {
        // On the song's grid: step k at k steps from the song's start, every second one late.
        const double stepLength =
            beatsOf(kRateBeats, p.getInt(GrainRate), transport) * samplesPerBeat(transport, sampleRate_);
        const auto at = [&](std::int64_t k)
        {
            return static_cast<double>(k) * stepLength + ((k & 1) != 0 ? swing * stepLength : 0.0) -
                   static_cast<double>(blockSong_);
        };
        if (v.findStep || at(v.step) < from - stepLength || at(v.step) > to + 2.0 * stepLength)
        {
            v.step =
                static_cast<std::int64_t>(std::floor((static_cast<double>(blockSong_) + from) / stepLength));
            while (at(v.step) < from)
                ++v.step;
            v.findStep = false;
        }
        for (double t = at(v.step); t < to && v.spawning; t = at(++v.step))
            spawn(v, transport, std::max(static_cast<double>(from), t));
        v.nextOnset = v.nextBase = to - bornAt; // when the song stops, the free grid goes on from here
        return;
    }
    v.findStep = true;
    if (bornAt + v.nextOnset < from) // behind (a mode just changed): from here, not a burst
        v.nextOnset = v.nextBase = from - bornAt;
    const double size = std::max(1.0, moved(GrainSize, TargetSize, v.mod) * 0.001 * sampleRate_);
    for (double at = bornAt + v.nextOnset; at < to && v.spawning; at = bornAt + v.nextOnset)
    {
        spawn(v, transport, std::max(static_cast<double>(from), at));
        // When the next starts.
        Rng rng{hash(v.seed ^ 0xA5A5A5A5u ^ hash(v.grainCount + 7u))};
        double interval = size;
        switch (mode)
        {
        case Mode::Classic:
            interval = size / std::max(1.0f, moved(GrainOverlap, TargetOverlap, v.mod));
            break;
        case Mode::Loop:
            interval = size * std::max(0.5, 1.0 - moved(GrainXfade, TargetCrossfade, v.mod) * 0.01);
            break;
        case Mode::Cloud:
            interval = -std::log(std::max(1e-6f, rng.next01())) * sampleRate_ /
                       std::max(0.5f, moved(GrainDensity, TargetDensity, v.mod));
            break;
        case Mode::Pitched:
            interval = sampleRate_ / (440.0 * std::exp2((notePitch(v, v.mod) - 69.0) / 12.0));
            break;
        case Mode::Rhythm:
            // Stopped: the grid from the note's start.
            interval =
                beatsOf(kRateBeats, p.getInt(GrainRate), transport) * samplesPerBeat(transport, sampleRate_);
            break;
        case Mode::Spectral:
            break;
        }
        interval = std::max(1.0, interval);
        v.nextBase += interval;
        if (mode == Mode::Rhythm)
            v.nextOnset = v.nextBase + ((++v.step & 1) != 0 ? swing * interval : 0.0);
        else if (mode == Mode::Cloud)
            v.nextOnset = v.nextBase;
        else
            v.nextOnset = v.nextBase + static_cast<double>(rng.bipolar()) * 0.5 *
                                           moved(GrainJitter, TargetJitter, v.mod) * 0.01 * interval;
    }
}

void Granular::spawn(Voice& v, const TransportInfo& transport, double at) noexcept
{
    const auto& p = params();
    const auto* assets = current_.get();
    if (assets == nullptr)
        return;
    // Its envelope up to its start first; the grains before it only when their room is full.
    const int startSample = static_cast<int>(std::ceil(at));
    advanceEnvs(v, startSample);
    if (v.active == static_cast<int>(v.grains.size()))
        renderGrains(v, startSample);
    if (!v.envs[0].isActive())
    {
        v.spawning = false; // the amp has finished: none after it
        return;
    }
    const auto mode = static_cast<Mode>(p.getInt(GrainMode));
    const std::uint32_t number = ++v.grainCount;
    Rng rng{hash(v.seed ^ hash(number))};

    // The grain's own sources: Grain Random, Grain Alternate, a random source Per Grain.
    std::array<float, NumTargets> mod = v.mod;
    const float grainRandom = rng.bipolar(), alternate = (number % 2 == 0) ? 1.0f : -1.0f;
    const std::array<float, numRandoms> perGrain{rng.bipolar(), rng.bipolar()};
    for (int r = 0; r < numRoutes; ++r)
    {
        const int src = p.getInt(route(r, RouteSource)), dst = p.getInt(route(r, RouteTarget));
        if (src <= 0 || dst <= 0 || dst >= NumTargets)
            continue;
        const auto s = static_cast<Source>(src);
        float value = 0.0f;
        if (s == Source::GrainRandom)
            value = grainRandom;
        else if (s == Source::GrainAlternate)
            value = alternate;
        else if (s >= Source::Random1 && s < Source::Macro1 &&
                 p.getInt(random(src - static_cast<int>(Source::Random1), RandomMode)) == RandomPerGrain)
            value = perGrain[static_cast<size_t>(src - static_cast<int>(Source::Random1))];
        else
            continue;
        const float bent = std::copysign(
            std::pow(std::abs(value), std::exp2(-2.0f * smoothed_[route(r, RouteCurve)] * 0.01f)), value);
        mod[static_cast<size_t>(dst)] += bent * smoothed_[route(r, RouteAmount)] * 0.01f;
    }

    if (rng.next01() * 100.0f >= moved(GrainChance, TargetChance, mod))
        return;

    // Which source plays it (or both).
    const bool on1 = p.getBool(source(0, SrcOn)) && v.zones[0] != nullptr;
    const bool on2 = p.getBool(source(1, SrcOn)) && v.zones[1] != nullptr;
    if (!on1 && !on2)
        return;
    std::array<bool, numSources> from{on1, on2};
    if (on1 && on2)
    {
        if (const int pick = p.getInt(SrcPick); pick == 0)
        {
            const bool second = rng.next01() * 100.0f < moved(SrcMix, TargetSourceMix, mod);
            from = {!second, second};
        }
        else if (pick == 1)
            from = {number % 2 == 1, number % 2 == 0};
    }

    const float sizeMs = [&]
    {
        if (const int sync = p.getInt(GrainSizeSync); sync > 0 && mode != Mode::Pitched)
            return static_cast<float>(beatsOf(kSizeSyncBeats, sync, transport) * 60000.0 /
                                      std::max(1.0, transport.tempoBpm));
        return moved(GrainSize, TargetSize, mod);
    }();
    const auto quality = static_cast<granular::Interp>(std::clamp(p.getInt(Quality), 0, 2));
    const auto& key = transport.key;
    const int cap = grainCap();
    const double note = notePitch(v, mod);

    for (int s = 0; s < numSources; ++s)
    {
        if (!from[static_cast<size_t>(s)])
            continue;
        const auto* zone = v.zones[static_cast<size_t>(s)];
        if (zone->audio == nullptr || zone->audio->levels.empty() || zone->audio->frames < 4)
            continue;
        const auto& audio = *zone->audio;
        const int t = TargetSource1 + s * 4;

        // Where: the position, the note's key, the playhead's travel, the spray; in the part.
        const double partStart = std::clamp(moved(source(s, SrcStart), t, mod) * 0.01, 0.0, 1.0);
        const double partEnd = std::clamp(moved(source(s, SrcEnd), t + 1, mod) * 0.01, partStart + 1e-4, 1.0);
        double place = moved(Pos, TargetPosition, mod) * 0.01 +
                       moved(PosKey, 0, mod) * 0.01 * (v.key - 60) / 48.0 + v.place[static_cast<size_t>(s)];
        const double spray = moved(PosSpray, TargetPositionSpray, mod) * 0.01;
        switch (p.getInt(PosDist))
        {
        case 0:
            place += spray * 0.5 * rng.bipolar();
            break; // Even
        case 1:
            place += spray * 0.5 * (rng.next01() + rng.next01() - 1.0);
            break; // Near
        default:
            place += spray * rng.next01();
            break; // Ahead
        }
        double frame = (partStart + fold(place, p.getInt(ScanEnd)) * (partEnd - partStart)) * audio.frames;
        switch (p.getInt(PosSnap))
        {
        case 1:
            frame = GrainSource::nearest(audio.transients, frame / audio.sampleRate) * audio.sampleRate;
            break;
        case 2:
            frame = GrainSource::nearest(audio.beats, frame / audio.sampleRate) * audio.sampleRate;
            break;
        case 3:
            frame = audio.nearestZeroCrossing(frame);
            break;
        default:
            break;
        }
        if (mode == Mode::Pitched)
            frame = audio.nearestZeroCrossing(frame); // on the source's periods

        // How high: the note against the root (Key Track), the tunings, the spray, quantised.
        const double root =
            assets->roots[static_cast<size_t>(s)] ? *assets->roots[static_cast<size_t>(s)] : zone->root;
        double sprayed = moved(PitchSpray, TargetPitchSpray, mod) * rng.bipolar();
        if (const int dir = p.getInt(PitchDir); dir == 1)
            sprayed = std::abs(sprayed);
        else if (dir == 2)
            sprayed = -std::abs(sprayed);
        switch (p.getInt(PitchQuant))
        {
        case 1:
            sprayed = std::round(sprayed);
            break;
        case 2:
            sprayed = nearestInKey(key, note + sprayed) - note;
            break;
        case 3:
            sprayed = 12.0 * std::round(sprayed / 12.0);
            break;
        case 4:
        {
            constexpr int fifths[] = {0, 7};
            sprayed = nearestTone(sprayed, fifths, 2);
            break;
        }
        case 5:
        {
            const int chord = std::clamp(p.getInt(PitchChord), 0, 10);
            const auto tones = chord == 10 ? keyChord(key, static_cast<int>(std::lround(note)))
                                           : kChordTones[static_cast<size_t>(chord)];
            sprayed = nearestTone(sprayed, tones.data(), static_cast<int>(tones.size()));
            break;
        }
        default:
            break;
        }
        // The note played (with its bend) tracks against the root; Transpose, Fine and the
        // modulation on top, then the source's tuning and the spray.
        const double tune = moved(source(s, SrcTune), t + 3, mod) + p.get(source(s, SrcFine)) * 0.01;
        const double played = v.glidePitch + bend_[static_cast<size_t>(v.channel)] * p.get(VoiceBend);
        const double semis =
            (played - root) * moved(PitchKey, 0, mod) * 0.01 + (note - played) + tune + sprayed;
        const double rateRatio = audio.sampleRate / sampleRate_;
        double speed = std::exp2(semis / 12.0) * rateRatio; // file frames an output sample
        double lengthSamples =
            std::max(sampleRate_ * 0.001, static_cast<double>(sizeMs) * 0.001 * sampleRate_);
        if (mode == Mode::Pitched)
        {
            // The emission sets the pitch; the read speed only the formant.
            const double period = sampleRate_ / (440.0 * std::exp2((note - 69.0) / 12.0));
            lengthSamples = std::max(4.0, moved(GrainPeriods, TargetPeriods, mod) * period);
            speed = std::exp2((moved(GrainFormant, TargetFormant, mod) + tune + sprayed) / 12.0) * rateRatio;
        }
        lengthSamples *= 1.0 + moved(GrainSizeSpray, TargetSizeSpray, mod) * 0.01 * 0.5 * rng.bipolar();
        lengthSamples = std::max(4.0, lengthSamples);
        const bool reverse = rng.next01() * 100.0f < moved(GrainReverse, TargetReverse, mod);
        const double glide = moved(PitchGlide, TargetGlide, mod);

        // The level it reads: where it moves at most two samples (one, plain) an output sample.
        const int lastLevel = static_cast<int>(audio.levels.size()) - 1;
        const int levelIndex =
            std::clamp(speed > 1.0 ? static_cast<int>(std::ceil(std::log2(speed) - 1e-9)) : 0, 0, lastLevel);
        const auto& level = audio.levels[static_cast<size_t>(levelIndex)];
        const double inc = speed * level.scale;
        const double endInc = inc * std::exp2(glide / 12.0);
        const auto interp = audio.compact ? (quality == granular::Interp::Linear ? granular::Interp::Linear
                                                                                 : granular::Interp::Sinc8)
                                          : quality;
        const double lowest = granular::tapsBefore(interp);
        const double highest = level.frames - 1 - granular::tapsAfter(interp);
        if (highest <= lowest + 4.0)
            continue;
        // The whole read inside the level: shortened to fit, then moved back.
        const double mean = 0.5 * (inc + endInc);
        if (mean * lengthSamples >= highest - lowest - 1.0)
            lengthSamples = std::max(4.0, (highest - lowest - 2.0) / mean);
        const double reach = mean * lengthSamples;
        const double begin = reverse ? std::clamp(frame * level.scale, lowest + reach, highest)
                                     : std::clamp(frame * level.scale, lowest, highest - reach);

        // How loud: the source's gain, the zone's, the spray, Even; with Grain Tails in the
        // release, the envelope as it starts.
        float gain = dsp::dbToGain(moved(source(s, SrcGain), t + 2, mod), -70.0f) * zone->gain;
        gain *= 1.0f - moved(GrainLevelSpray, TargetLevelSpray, mod) * 0.01f * rng.next01();
        const float here = audio.levelAt(begin / level.scale);
        if (const float even = moved(GrainEven, TargetEven, mod) * 0.01f; even > 0.0f)
            gain *= std::pow(std::clamp(0.3f / std::max(here, 1e-4f), 1.0f, 16.0f), even);
        if (v.frozen)
            gain *= std::min(1.0f, v.envs[0].level() / v.releaseLevel);
        if (here * gain < kSilent)
            continue; // inaudible: not started

        // Its pan.
        const float panSpray = moved(PanSpray, TargetPanSpray, mod) * 0.01f;
        float pan = mod[TargetPan];
        switch (p.getInt(PanMode))
        {
        case 0:
            pan += panSpray * rng.bipolar();
            break;
        case 1:
            pan += panSpray * alternate;
            break;
        default:
            pan += panSpray * static_cast<float>(std::clamp(sprayed / 24.0, -1.0, 1.0));
            break;
        }

        // The budget (SPEC, "Performance"): past the note's share, the grain nearest its end steps
        // aside over 2 ms; with none to step aside, or no room at all, this one doesn't start.
        int living = alive(v, startSample);
        if (living >= cap)
        {
            int steal = -1;
            std::int64_t least = std::numeric_limits<std::int64_t>::max();
            for (int i = 0; i < v.active; ++i)
            {
                const auto& o = v.grains[static_cast<size_t>(i)];
                const std::int64_t left = endOf(o, v.rendered) - startSample;
                if (o.fadeLength == 0 && left > 0 && v.rendered + std::max(0, o.delay) <= startSample &&
                    left < least)
                {
                    least = left;
                    steal = i;
                }
            }
            if (steal < 0)
                return;
            // It fades from here, its fade counted from where its next render starts (through a
            // repeat's restart if one comes first).
            auto& o = v.grains[static_cast<size_t>(steal)];
            o.fadeLength = std::max(1, static_cast<int>(kStealSeconds * sampleRate_));
            o.fadeLeft = o.fadeLength + (startSample - v.rendered - std::max(0, o.delay));
            --living;
        }
        if (v.active >= static_cast<int>(v.grains.size()))
            return;

        Grain g;
        g.channels = static_cast<std::int8_t>(audio.numChannels);
        if (audio.compact)
            g.i16 = level.i16At(0, audio.numChannels);
        else
            g.f32 = level.f32At(0, audio.numChannels);
        // At the budget, the cheapest reads too (as Clouds does under load).
        g.interp = living + 1 >= cap ? granular::Interp::Linear : interp;
        g.length = static_cast<int>(std::lround(lengthSamples));
        // It starts between samples: read on from where it would be by the next whole one.
        const double late = startSample - at;
        const double signedInc = reverse ? -inc : inc, signedEnd = reverse ? -endInc : endInc;
        const double dinc = (signedEnd - signedInc) / std::max(1.0, lengthSamples);
        g.pos0 = static_cast<std::int64_t>(std::llround((begin + late * signedInc) * 4294967296.0));
        g.inc0 = static_cast<std::int64_t>(std::llround(signedInc * 4294967296.0));
        g.dinc = static_cast<std::int64_t>(std::llround(dinc * 4294967296.0));
        g.pos = g.pos0;
        g.inc = g.inc0;
        g.delay = startSample - v.rendered;
        g.window.shape = static_cast<granular::Window>(p.getInt(GrainWindow));
        g.window.skew = moved(GrainSkew, TargetSkew, mod) * 0.01f;
        g.window.flatness =
            mode == Mode::Loop ? std::max(0.0f, 1.0f - 2.0f * moved(GrainXfade, TargetCrossfade, mod) * 0.01f)
                               : moved(GrainFlat, TargetFlatness, mod) * 0.01f;
        g.window.fade = std::clamp(static_cast<float>(0.001 * sampleRate_ / g.length), 0.002f, 0.5f);
        g.window.drawn = &assets->window;
        g.repeatsLeft = std::max(1, p.getInt(GrainRepeats)) - 1;
        panGains(g, pan, gain, audio.numChannels);
        // Colour: crush and ring.
        if (const float crush = std::clamp(moved(GColorCrush, TargetCrush, mod) * 0.01f *
                                               (1.0f + p.get(GColorCrushSpray) * 0.01f * rng.bipolar()),
                                           0.0f, 1.0f);
            crush > 0.0f)
        {
            g.crushHold = 1 + static_cast<int>(crush * crush * 23.0f);
            g.crushStep = 2.0f / std::exp2(16.0f - 13.0f * crush);
        }
        if (const float ring = moved(GColorRing, TargetRing, mod) * 0.01f; ring > 0.0f)
        {
            const double hz = 440.0 * std::exp2((root + semis - 69.0) / 12.0);
            const double ratio = moved(GColorRatio, TargetRingRatio, mod) *
                                 std::exp2(p.get(GColorRingSpray) * 0.01 * rng.bipolar());
            g.ringMix = ring;
            g.ringInc = static_cast<float>(ratio * hz / sampleRate_);
        }
        // The grain filter: its cutoff sprayed, following the grain's pitch, on the key.
        if (p.getBool(GFilterOn))
        {
            double cutoff = moved(GFilterCutoff, TargetGrainCutoff, mod) *
                            std::exp2(moved(GFilterSpray, TargetCutoffSpray, mod) * 0.5 * rng.bipolar() +
                                      p.get(GFilterKey) * 0.01 * semis / 12.0);
            if (p.getBool(GFilterSnap) && key.known())
                cutoff =
                    440.0 *
                    std::exp2(
                        (nearestInKey(key, 69.0 + 12.0 * std::log2(std::max(1.0, cutoff) / 440.0)) - 69.0) /
                        12.0);
            granular::setGrainFilter(g, p.getInt(GFilterType) + 1, cutoff,
                                     moved(GFilterRes, TargetGrainRes, mod) * 0.01, sampleRate_);
        }
        g.shownPitch = static_cast<float>(semis);
        g.shownLevel = std::min(1.0f, gain);
        g.shownPan = std::clamp(pan, -1.0f, 1.0f);
        g.levelFrames = level.frames;
        g.source = static_cast<std::uint8_t>(s);
        g.owner = assets->sources[static_cast<size_t>(s)].get();
        v.grains[static_cast<size_t>(v.active++)] = g;
    }
}

void Granular::renderGrains(Voice& v, int to) noexcept
{
    if (to <= v.rendered)
        return;
    granular::renderGrains({v.grains.data(), static_cast<size_t>(v.active)}, v.busL.data() + v.rendered,
                           v.busR.data() + v.rendered, to - v.rendered, scratch_);
    // Those finished go; the rest keep the order they started in.
    int kept = 0;
    for (int i = 0; i < v.active; ++i)
        if (const auto& g = v.grains[static_cast<size_t>(i)]; !g.finished())
        {
            if (kept != i)
                v.grains[static_cast<size_t>(kept)] = g;
            ++kept;
        }
    v.active = kept;
    v.rendered = to;
}

void Granular::advanceEnvs(Voice& v, int to) noexcept
{
    for (int s = v.envAt; s < to; ++s)
    {
        const float a = v.envs[0].next();
        v.envs[1].next();
        v.envs[2].next();
        v.amp[static_cast<size_t>(s)] = v.frozen ? v.releaseLevel : a;
        if (v.envEnd < 0 && !v.envs[0].isActive())
            v.envEnd = blockStart_ + s + 1;
    }
    v.envAt = std::max(v.envAt, to);
}

void Granular::addSegment(Voice& v, int from, int phase0, bool reset, bool silent) noexcept
{
    // A segment starting where the last does replaces it (and so does one past the last room).
    auto count = static_cast<size_t>(v.segmentCount);
    if (count > 0 && (v.segments[count - 1].from >= from || count == v.segments.size()))
        --count;
    auto& s = v.segments[count];
    const bool resetBefore = count < static_cast<size_t>(v.segmentCount) && s.reset;
    s = {from, phase0, v.ctrlFrom, v.ctrlTo, reset || resetBefore, silent};
    v.segmentCount = static_cast<int>(count) + 1;
}

void Granular::finishVoice(int index, int at) noexcept
{
    auto& v = voice_[static_cast<size_t>(index)];
    renderGrains(v, at); // (with Grain Tails, they've all finished by here)
    v.active = 0;
    v.sounding = false;
    addSegment(v, at, 0, false, true);
    voices_.voiceFinished(index);
}

// ---- audio ---------------------------------------------------------------------------------------

void Granular::renderVoiceStage(Voice& v, float* outL, float* outR, int n) noexcept
{
    const auto& p = params();
    const auto interval = static_cast<float>(grid_.interval());
    const bool on1 = p.getBool(filter(0, FilterOn)), on2 = p.getBool(filter(1, FilterOn));
    const auto t1 = static_cast<dsp::VoiceFilter::Type>(p.getInt(filter(0, FilterType)));
    const auto t2 = static_cast<dsp::VoiceFilter::Type>(p.getInt(filter(1, FilterType)));
    // A DC blocker at 5 Hz first: a grain shorter than its source's period (Pitched, a formant
    // below the note) carries an offset.
    const auto blocker = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 5.0 / sampleRate_));
    for (int k = 0; k < v.segmentCount; ++k)
    {
        const auto& seg = v.segments[static_cast<size_t>(k)];
        const int end = k + 1 < v.segmentCount ? v.segments[static_cast<size_t>(k + 1)].from : n;
        if (seg.reset)
        {
            for (auto& pair : v.filters)
                for (auto& f : pair)
                    f.reset();
            v.dcIn = v.dcOut = {};
        }
        if (seg.silent)
            continue;
        for (int s = seg.from; s < std::min(end, n); ++s)
        {
            const float u = static_cast<float>(seg.phase0 + (s - seg.from)) / interval;
            const auto c = [&](size_t i) { return seg.a[i] + (seg.b[i] - seg.a[i]) * u; };
            float l = v.busL[static_cast<size_t>(s)], r = v.busR[static_cast<size_t>(s)];
            for (int ch = 0; ch < 2; ++ch)
            {
                float& x = ch == 0 ? l : r;
                const auto i = static_cast<size_t>(ch);
                const float y = x - v.dcIn[i] + blocker * v.dcOut[i];
                v.dcIn[i] = x;
                v.dcOut[i] = y;
                x = y;
            }
            if (on1 || on2)
            {
                const auto run = [&](int f, dsp::VoiceFilter::Type type, float x, int ch)
                {
                    const auto i = static_cast<size_t>(f);
                    return v.filters[i][static_cast<size_t>(ch)].process(
                        x, type, c(CCutoff0 + i), c(CRes0 + i), c(CDrive0 + i), c(CShape0 + i));
                };
                const float routing = c(CRouting);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& x = ch == 0 ? l : r;
                    if (on1 && on2)
                    {
                        // Filter 1 into filter 2 at 0 %, the two side by side at 100 %.
                        const float y = run(0, t1, x, ch);
                        x = run(1, t2, (1.0f - routing) * y + routing * x, ch) + routing * y;
                    }
                    else
                        x = on1 ? run(0, t1, x, ch) : run(1, t2, x, ch);
                }
            }
            const float amp = c(CAmp) * v.amp[static_cast<size_t>(s)];
            // Its width: the side scaled (0 mono, 1 as it is, 2 twice as wide).
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * c(CWidth);
            outL[s] += (mid + side) * amp;
            outR[s] += (mid - side) * amp;
        }
    }
}

void Granular::publishDisplay(int n) noexcept
{
    sinceDisplay_ += n;
    if (sinceDisplay_ < static_cast<int>(0.03 * sampleRate_))
        return;
    sinceDisplay_ = 0;
    size_t count = 0;
    for (const auto& v : voice_)
        for (int i = 0; i < v.active && count < shownMarks_.size(); ++i)
        {
            const auto& g = v.grains[static_cast<size_t>(i)];
            if (g.delay > 0)
                continue;
            const float phase =
                g.length > 1 ? static_cast<float>(g.age) / static_cast<float>(g.length - 1) : 0.0f;
            shownMarks_[count++] = {
                static_cast<float>(
                    std::clamp(static_cast<double>(g.pos >> 32) / std::max(1.0, g.levelFrames), 0.0, 1.0)),
                g.shownPitch, g.shownLevel * g.window.at(std::min(phase, 1.0f)), g.shownPan, g.source};
        }
    publishGrains({shownMarks_.data(), count});
}

void Granular::process(ProcessContext& ctx) noexcept
{
    const auto started = std::chrono::steady_clock::now();
    const int n = std::min(ctx.numSamples, maxBlock_);
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    for (int c = 0; c < ctx.output.numChannels; ++c)
        std::fill_n(ctx.output.channel(c), ctx.numSamples, 0.0f);
    float* outL = ctx.output.channel(0);
    float* outR = ctx.output.numChannels > 1 ? ctx.output.channel(1) : nullptr;
    // A mono output plays the left channel: the right goes to a spare.
    if (outR == nullptr)
    {
        std::fill(spareRight_.begin(), spareRight_.begin() + n, 0.0f);
        outR = spareRight_.data();
    }
    const bool stereo = ctx.output.numChannels > 1;

    // New sources and curves, at the block's start. Grains of a source that changed fade out
    // over 2 ms, still reading the old one, which is held until they have.
    if (previous_.isSet() && previousLeft_ <= 0)
    {
        assets_.retire(previous_);
        previous_ = {};
    }
    if (auto fresh = assets_.takeNew(); fresh.isSet())
    {
        const auto* now = fresh.get();
        if (previous_.isSet())
        {
            // A second swap within the fade: what still reads the oldest stops at once.
            for (auto& v : voice_)
            {
                int kept = 0;
                for (int i = 0; i < v.active; ++i)
                {
                    const auto& g = v.grains[static_cast<size_t>(i)];
                    bool old = false;
                    for (const auto& set : previous_->sources)
                        old = old || (set != nullptr && g.owner == set.get() &&
                                      std::none_of(now->sources.begin(), now->sources.end(),
                                                   [&](const auto& s) { return s.get() == g.owner; }));
                    if (!old)
                        v.grains[static_cast<size_t>(kept++)] = g;
                }
                v.active = kept;
            }
            assets_.retire(previous_);
            previous_ = {};
        }
        if (current_.isSet())
        {
            previous_ = current_;
            previousLeft_ = static_cast<int>(kStealSeconds * sampleRate_) + 1;
        }
        current_ = fresh;
        for (auto& v : voice_)
        {
            for (int i = 0; i < v.active; ++i)
            {
                auto& g = v.grains[static_cast<size_t>(i)];
                g.window.drawn = &now->window;
                if (std::none_of(now->sources.begin(), now->sources.end(),
                                 [&](const auto& s) { return s.get() == g.owner; }) &&
                    g.fadeLength == 0)
                {
                    g.fadeLength = g.fadeLeft = static_cast<int>(kStealSeconds * sampleRate_);
                    g.repeatsLeft = 0;
                }
            }
            updateZones(v);
        }
    }

    // A new roll each time the song starts (ADR 0062), unless one is kept: the host's for this
    // pass, or one of its own.
    if (ctx.transport.playing && !wasPlaying_)
    {
        const auto host = hostRoll_.exchange(0, std::memory_order_relaxed);
        ++rollStarts_;
        if (!rollKept_.load(std::memory_order_relaxed))
            roll_.store(host != 0 ? host : hash(kRollBase + rollStarts_ * 2654435761U),
                        std::memory_order_relaxed);
        notes_ = 0;
    }
    wasPlaying_ = ctx.transport.playing;
    blockSong_ = ctx.transport.samplePosition;

    for (auto& v : voice_)
    {
        v.segmentCount = v.rendered = v.envAt = 0;
        if (v.sounding)
        {
            std::fill_n(v.busL.begin(), n, 0.0f);
            std::fill_n(v.busR.begin(), n, 0.0f);
        }
    }

    const EventList* events = ctx.events;
    size_t nextEvent = 0;
    const int interval = grid_.interval();
    const bool tails = params().getBool(VoiceTails);
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            const int phase0 = static_cast<int>(grid_.position() % interval);
            // The song's position at this tick, not the block's start: block sizes never show.
            TransportInfo at = ctx.transport;
            at.samplePosition += offset;
            at.ppqPosition += offset * ctx.transport.tempoBpm / (60.0 * sampleRate_);
            if (isTick)
            {
                tickTransport_ = at;
                tickOffset_ = offset;
                tickPhase_ = 0;
                tick(at, offset);
            }
            else if (offset == 0)
            {
                // The block starts inside a tick: its controls carry on.
                tickOffset_ = 0;
                tickPhase_ = phase0;
                for (auto& v : voice_)
                    if (v.sounding)
                        addSegment(v, 0, phase0, false);
            }
            int i = offset;
            const int end = offset + length;
            while (i < end)
            {
                while (events != nullptr && nextEvent < events->size() &&
                       (*events)[nextEvent].sampleOffset <= i)
                    handleEvent((*events)[nextEvent++], i);
                int stop = end;
                if (events != nullptr && nextEvent < events->size() &&
                    (*events)[nextEvent].sampleOffset < end)
                    stop = std::max(i + 1, (*events)[nextEvent].sampleOffset);
                for (int k = 0; k < static_cast<int>(voice_.size()); ++k)
                {
                    auto& v = voice_[static_cast<size_t>(k)];
                    if (!v.sounding)
                        continue;
                    TransportInfo here = at;
                    here.samplePosition += i - offset;
                    schedule(v, here, i, stop);
                    advanceEnvs(v, stop);
                    if (v.envs[0].isActive())
                        continue;
                    // The amp has finished: no more grains. The voice ends now, or with Grain
                    // Tails once its last grain has played.
                    v.spawning = false;
                    if (!tails)
                    {
                        finishVoice(k, stop);
                        continue;
                    }
                    if (v.tailEnd < 0)
                    {
                        // The later of the amp's end and the last grain's, and the filters' ring:
                        // exact, so it's the same however the block is cut.
                        std::int64_t last = v.envEnd;
                        for (int g = 0; g < v.active; ++g)
                        {
                            const auto& grain = v.grains[static_cast<size_t>(g)];
                            auto ends = endOf(grain, v.rendered);
                            if (grain.fadeLength > 0)
                                ends = std::min<std::int64_t>(ends, v.rendered + std::max(0, grain.delay) +
                                                                        grain.fadeLeft);
                            last = std::max(last, blockStart_ + ends);
                        }
                        v.tailEnd = last + static_cast<std::int64_t>(kRingSeconds * sampleRate_);
                    }
                    if (blockStart_ + stop >= v.tailEnd)
                        finishVoice(k, static_cast<int>(v.tailEnd - blockStart_));
                }
                i = stop;
            }
        });
    eventAt_ = n;
    while (events != nullptr && nextEvent < events->size())
        handleEvent((*events)[nextEvent++], n);

    // Each voice's grains to the block's end, then its filters and amp.
    int playing = 0;
    for (auto& v : voice_)
    {
        if (v.sounding)
        {
            advanceEnvs(v, n);
            renderGrains(v, n);
            playing += v.active;
        }
        if (v.segmentCount > 0)
            renderVoiceStage(v, outL, outR, n);
    }

    // The volume (each note's width is its own, in the voice stage).
    float peak = 0.0f;
    for (int s = 0; s < n; ++s)
    {
        const float g = volume_.next();
        outL[s] *= g;
        outR[s] *= g;
        peak = std::max({peak, std::abs(outL[s]), stereo ? std::abs(outR[s]) : 0.0f});
    }
    blockStart_ += n;
    previousLeft_ -= n;

    analysis().publishPeak(PeakOut, peak);
    analysis().publish(ActiveVoices, static_cast<float>(voices_.activeVoices()));
    analysis().publish(GrainsPlaying, static_cast<float>(playing));
    if (newest_ >= 0 && voice_[static_cast<size_t>(newest_)].sounding)
    {
        const auto& v = voice_[static_cast<size_t>(newest_)];
        for (int k = 0; k < numSources; ++k)
            analysis().publish(Scan1 + k, static_cast<float>(fold(v.place[static_cast<size_t>(k)],
                                                                  params().getInt(ScanEnd))));
        for (int k = 0; k < numLfos; ++k)
            analysis().publish(Lfo1Phase + k,
                               static_cast<float>(params().getBool(lfo(k, LfoPoly))
                                                      ? v.lfos[static_cast<size_t>(k)].phase()
                                                      : lfos_[static_cast<size_t>(k)].phase()));
    }
    else
        for (int k = 0; k < numSources; ++k)
            analysis().publish(Scan1 + k, -1.0f);
    publishDisplay(n);
    // The CPU it takes: this block's time over the block's own length, eased.
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    cpuLoad_ += 0.1 * (took * sampleRate_ / n - cpuLoad_);
    analysis().publish(CpuLoad, static_cast<float>(cpuLoad_));
}

// ---- the panel and assistants ----------------------------------------------------------------------

std::shared_ptr<const GrainSource> Granular::sourceData(int n) const
{
    const auto set = sourceSet(n);
    return set != nullptr ? set->shown() : nullptr;
}

std::shared_ptr<const GrainSourceSet> Granular::sourceSet(int n) const
{
    const std::scoped_lock lock(loaderMutex_);
    return sources_[static_cast<size_t>(std::clamp(n, 0, numSources - 1))];
}

void Granular::setSourceData(int n, std::shared_ptr<const GrainSource> data)
{
    const auto path = data != nullptr ? data->path : std::string();
    setSourceSet(n, data != nullptr ? GrainSourceSet::one(std::move(data), path) : nullptr);
}

void Granular::setSourceSet(int n, std::shared_ptr<const GrainSourceSet> set)
{
    {
        const std::scoped_lock lock(loaderMutex_);
        const auto i = static_cast<size_t>(std::clamp(n, 0, numSources - 1));
        sources_[i] = std::move(set);
        paths_[i] = sources_[i] != nullptr ? sources_[i]->path : std::string();
        errors_[i].clear();
    }
    publishAssets();
}

nlohmann::json Granular::rollPatch(bool keep) const
{
    return {{"roll", keep ? nlohmann::json(roll_.load(std::memory_order_relaxed)) : nlohmann::json(nullptr)}};
}

nlohmann::json Granular::sourcePatch(int n, const std::string& stored)
{
    const std::string key = "src" + std::to_string(std::clamp(n, 0, numSources - 1) + 1) + ".";
    return {{key + "path", stored.empty() ? nlohmann::json(nullptr) : nlohmann::json(stored)},
            {key + "root", nullptr}};
}

std::vector<std::pair<size_t, float>> Granular::sourceSettings(int n)
{
    n = std::clamp(n, 0, numSources - 1);
    return {{source(n, SrcOn), 1.0f},
            {source(n, SrcInput), 0.0f},
            {source(n, SrcStart), 0.0f},
            {source(n, SrcEnd), 100.0f}};
}

nlohmann::json Granular::swappedPatch() const
{
    const std::scoped_lock lock(loaderMutex_);
    nlohmann::json patch = nlohmann::json::object();
    for (int n = 0; n < numSources; ++n)
    {
        const auto other = static_cast<size_t>(numSources - 1 - n);
        const std::string key = "src" + std::to_string(n + 1) + ".";
        patch[key + "path"] = paths_[other].empty() ? nlohmann::json(nullptr) : nlohmann::json(paths_[other]);
        patch[key + "root"] = roots_[other] ? nlohmann::json(*roots_[other]) : nlohmann::json(nullptr);
    }
    return patch;
}

std::vector<std::pair<size_t, float>> Granular::swappedSettings() const
{
    std::vector<std::pair<size_t, float>> out;
    for (size_t p = 0; p < SourceParamCount; ++p)
        for (int n = 0; n < numSources; ++n)
            out.emplace_back(source(n, static_cast<SourceParam>(p)),
                             params().get(source(numSources - 1 - n, static_cast<SourceParam>(p))));
    return out;
}

std::string Granular::loadError(int n) const
{
    const std::scoped_lock lock(loaderMutex_);
    return errors_[static_cast<size_t>(std::clamp(n, 0, numSources - 1))];
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
    {
        const std::scoped_lock lock(loaderMutex_);
        for (int n = 0; n < numSources; ++n)
        {
            const auto i = static_cast<size_t>(n);
            const std::string key = "src" + std::to_string(n + 1) + ".";
            if (!paths_[i].empty())
                extra[key + "path"] = paths_[i];
            if (roots_[i])
                extra[key + "root"] = *roots_[i];
        }
    }
    for (int n = 0; n < numLfos; ++n)
        writeCurve("lfo" + std::to_string(n + 1), lfoShapes_[static_cast<size_t>(n)], "sine");
    writeCurve("window", window_, "sine");
    for (int n = 0; n < numMacros; ++n)
        if (!macroNames_[static_cast<size_t>(n)].empty())
            extra["macro" + std::to_string(n + 1) + ".name"] = macroNames_[static_cast<size_t>(n)];
    if (rollKept_.load(std::memory_order_relaxed))
        extra["roll"] = roll_.load(std::memory_order_relaxed);
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
            for (const auto& pt : *it)
                if (pt.is_array() && pt.size() == 3)
                    c.points.push_back({pt[0].get<float>(), pt[1].get<float>(), pt[2].get<float>()});
        const auto shape = text(key + ".shape");
        c.shape = shape.empty() ? std::string(standard) : shape;
        std::sort(c.points.begin(), c.points.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
        return c;
    };
    // Each source's file, read here unless it's the one already loaded (the Library's and files
    // read once are shared: GrainSourceSet::load).
    for (int n = 0; n < numSources; ++n)
    {
        const auto i = static_cast<size_t>(n);
        const std::string key = "src" + std::to_string(n + 1) + ".";
        const auto path = text(key + "path");
        std::optional<double> root;
        if (const auto it = extra.find(key + "root"); it != extra.end() && it->is_number())
            root = it->get<double>();
        std::string loaded;
        {
            const std::scoped_lock lock(loaderMutex_);
            roots_[i] = root;
            if (path == paths_[i] && (path.empty() ? sources_[i] == nullptr : sources_[i] != nullptr))
                continue;
            loaded = path;
        }
        std::string error;
        auto set = loaded.empty() ? nullptr : GrainSourceSet::load(loaded, &error);
        const std::scoped_lock lock(loaderMutex_);
        sources_[i] = std::move(set);
        paths_[i] = loaded;
        errors_[i] = std::move(error);
    }
    for (int n = 0; n < numLfos; ++n)
        lfoShapes_[static_cast<size_t>(n)] = readCurve("lfo" + std::to_string(n + 1), "sine");
    window_ = readCurve("window", "sine");
    for (int n = 0; n < numMacros; ++n)
        macroNames_[static_cast<size_t>(n)] = text("macro" + std::to_string(n + 1) + ".name");
    if (const auto it = extra.find("roll"); it != extra.end() && it->is_number_unsigned())
    {
        roll_.store(it->get<std::uint32_t>(), std::memory_order_relaxed);
        rollKept_.store(true, std::memory_order_relaxed);
    }
    else
        rollKept_.store(false, std::memory_order_relaxed);
    publishAssets();
}

namespace
{
/// The chord of Segno's own as a file, for the conformance suite and the snapshots (ADR 0017's
/// testSetup): written once per process to the temp directory and removed at exit, so a state
/// written from it reads back as it was.
const std::string& testChordPath()
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
        Folder f{std::filesystem::temp_directory_path() / ("fd-granular-test-" + std::to_string(rd()))};
        std::filesystem::create_directories(f.path);
        const auto chord = GrainSource::chord(6.0, 48000.0);
        audiofile::writeWav((f.path / "Chord A2.wav").string(), chord.channels, chord.sampleRate, 45, 16);
        return f;
    }();
    static const std::string path = (folder.path / "Chord A2.wav").string();
    return path;
}
} // namespace

const DeviceFactory& granularFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return tables().info; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Granular>(); },
        // Something to play: the chord in Source 1, kept loaded so each instance shares it.
        .testSetup =
            [](Device& d)
        {
            static const auto kept = GrainSourceSet::load(testChordPath(), nullptr);
            d.readExtraState({{"src1.path", testChordPath()}});
        },
    };
    return factory;
}
} // namespace fd::devices
