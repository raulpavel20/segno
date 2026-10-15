#include "model/AudioToolkit.h"

namespace fd::model
{
namespace
{
std::vector<ToolSpec> makeTools()
{
    using K = ToolKind;
    return {
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
} // namespace fd::model
