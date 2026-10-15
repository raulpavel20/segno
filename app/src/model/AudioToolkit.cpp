#include "model/AudioToolkit.h"

#include <algorithm>

namespace fd::model
{
namespace
{
std::vector<ToolSpec> makeTools()
{
    using K = ToolKind;
    return {
        {"quantise",
         "Quantise",
         K::Transform,
         "Moves the clip's transients towards the grid through warp markers, in the range or the whole clip; "
         "Warp goes on with it",
         {{.id = "strength",
           .label = "Strength",
           .min = 0.0,
           .max = 100.0,
           .step = 1.0,
           .initial = 100.0,
           .unit = "%"},
          {.id = "swing",
           .label = "Swing",
           .min = 0.0,
           .max = 100.0,
           .step = 1.0,
           .initial = 0.0,
           .unit = "%"},
          // The clip's own (§7.4): shown here, and set on the clip as it's dragged.
          {.id = "sensitivity",
           .label = "Sensitivity",
           .min = 0.0,
           .max = 100.0,
           .step = 1.0,
           .initial = 50.0,
           .unit = "%"}}},
        {"tempo",
         "Tempo",
         K::Transform,
         "Finds the clip's tempo and first downbeat, to correct by halving or doubling; Apply warps the clip "
         "to them, or sets the song's tempo from it",
         {{.id = "tempo",
           .label = "Tempo",
           .min = kMinClipTempo,
           .max = kMaxClipTempo,
           .step = 0.01,
           .initial = 120.0,
           .unit = "BPM",
           .halveDouble = true},
          {.id = "downbeat",
           .label = "Downbeat",
           .min = 0.0,
           .max = 36000.0,
           .step = 0.001,
           .initial = 0.0,
           .unit = "s"},
          {.id = "then",
           .label = "Then",
           .choices = {"Warp on at this tempo", "Song tempo from it"},
           .words = {"warp", "song"},
           .max = 0.0,
           .menu = true}}},
        {"normalise",
         "Normalise",
         K::Transform,
         "Sets the selected clips' gain so each reaches a peak or a loudness, or all together keeping their "
         "balance",
         {{.id = "to",
           .label = "To",
           .choices = {"Peak", "Loudness"},
           .words = {"peak", "loudness"},
           .max = 0.0},
          {.id = "peak",
           .label = "Peak",
           .min = -30.0,
           .max = 0.0,
           .step = 0.1,
           .initial = -1.0,
           .unit = "dBFS",
           .when = "to",
           .whenIs = 0},
          {.id = "loudness",
           .label = "Loudness",
           .min = -36.0,
           .max = -5.0,
           .step = 0.5,
           .initial = -14.0,
           .unit = "LUFS",
           .when = "to",
           .whenIs = 1},
          {.id = "clips",
           .label = "Clips",
           .choices = {"Each", "Together"},
           .words = {"each", "together"},
           .max = 0.0}}},
        {"slice",
         "Slice",
         K::Transform,
         "Cuts the clip into clips on its track at its transients, its warp markers, every step of the grid "
         "or every bar, in the range or the whole clip; they play on as before, and move apart",
         {{.id = "at",
           .label = "At",
           .choices = {"Transients", "Warp markers", "Grid", "Bars"},
           .words = {"transients", "markers", "grid", "bars"},
           .max = 0.0,
           .menu = true},
          // The clip's own (§7.4), as Quantise's.
          {.id = "sensitivity",
           .label = "Sensitivity",
           .min = 0.0,
           .max = 100.0,
           .step = 1.0,
           .initial = 50.0,
           .unit = "%",
           .when = "at",
           .whenIs = 0}}},
    };
}
} // namespace

const std::vector<ToolSpec>& audioTools()
{
    static const auto tools = makeTools();
    return tools;
}

const ToolSpec* findAudioTool(std::string_view id) noexcept
{
    for (const auto& t : audioTools())
        if (t.id == id)
            return &t;
    return nullptr;
}

NormaliseChoice normaliseChoice(const ToolValues& values)
{
    const auto v = toolValues(*findAudioTool("normalise"), values);
    const bool loudness = v.at("to") == 1.0;
    return {.loudness = loudness,
            .targetDb = loudness ? v.at("loudness") : v.at("peak"),
            .together = v.at("clips") == 1.0};
}
WarpQuantise warpQuantiseChoice(const ToolValues& values, double gridBeats)
{
    const auto v = toolValues(*findAudioTool("quantise"), values);
    return {.gridBeats = gridBeats, .strength = v.at("strength") / 100.0, .swing = v.at("swing") / 100.0};
}

double quantiseSensitivity(const ToolValues& values)
{
    return toolValues(*findAudioTool("quantise"), values).at("sensitivity") / 100.0;
}

SliceChoice sliceChoice(const ToolValues& values)
{
    const auto v = toolValues(*findAudioTool("slice"), values);
    const auto at = static_cast<SliceAt>(std::clamp(static_cast<int>(v.at("at")), 0, 3));
    return {.at = at, .sensitivity = v.at("sensitivity") / 100.0};
}

TempoChoice tempoChoice(const ToolValues& values)
{
    const auto v = toolValues(*findAudioTool("tempo"), values);
    return {.bpm = v.at("tempo"), .downbeat = v.at("downbeat"), .songTempo = v.at("then") == 1.0};
}
} // namespace fd::model
