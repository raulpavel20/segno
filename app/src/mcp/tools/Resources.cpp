// Resources (mcp.md §4) and prompts.

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

void addResources(Registry& registry, ToolEnv& env)
{
    registry.resources = {
        {"segno://project", "project", "The project",
         "Name, file, unsaved state, tempo, time signature, loop, and every track with its clips and module chain"},
        {"segno://transport", "transport", "The transport", "Playing, position, loop and metronome (no change notifications)"},
        {"segno://modules", "modules", "Module types", "Every module type that can be added: ours and scanned plugins"},
    };
    registry.templates = {
        {"segno://track/{id}", "track", "A track", "One track: name, colour, instrument, clips and chain"},
        {"segno://clip/{id}", "clip", "A clip", "One clip, with every note of a MIDI clip"},
        {"segno://module/{id}", "module", "A module",
         "One module in a chain: every parameter, its sections and its other state"},
        {"segno://module-type/{type}", "module-type", "A module type",
         "What a module type does, its parameters and its sections"},
    };
    registry.readResource = [&env](const std::string& uri) -> std::optional<ResourceContents> {
        std::optional<ResourceContents> out;
        env.main.run([&] {
            env.session.flushSnapshot();
            const auto after = [&](const std::string& prefix) {
                return uri.starts_with(prefix) ? std::optional(uri.substr(prefix.size())) : std::nullopt;
            };
            try
            {
                Json j;
                if (uri == "segno://project")
                    j = projectJson(env);
                else if (uri == "segno://transport")
                    j = transportJson(env);
                else if (uri == "segno://modules")
                    j = {{"types", moduleTypesJson(env)}};
                else if (const auto id = after("segno://track/"))
                    j = trackJson(env, track(env, *id));
                else if (const auto c = after("segno://clip/"))
                    j = clipJson(env, clip(env, *c), true);
                else if (const auto m = after("segno://module/"))
                {
                    const auto [slot, owner] = module(env, *m);
                    j = moduleJson(env, slot, owner);
                }
                else if (const auto type = after("segno://module-type/"))
                    j = moduleTypeJson(env, *type);
                if (!j.is_null())
                    out = ResourceContents{uri, "application/json", j.dump()};
            }
            catch (const Problem&)
            {
                // names nothing: not found
            }
        });
        return out;
    };
}

void addPrompts(Registry& registry)
{
    const auto user = [](const std::string& text) {
        return Json::array({{{"role", "user"}, {"content", {{"type", "text"}, {"text", text}}}}});
    };

    registry.prompts.push_back(
        {"start-a-track",
         "Start a track",
         "Sets up starting material: a tempo, a drum pattern and a bassline.",
         {{"style", "The style or mood (optional)", false}, {"tempo", "Beats per minute (optional)", false}},
         [user](const Json& args) {
             const auto style = args.value("style", std::string());
             const auto tempo = args.value("tempo", std::string());
             return user("Start a new piece in the DAW" + (style.empty() ? std::string() : " in the style of " + style) +
                         ". Read segno://project first. Set the tempo" +
                         (tempo.empty() ? std::string(" to suit it") : " to " + tempo + " BPM") +
                         ". On one track, create a 4-bar MIDI clip with a drum pattern (C1 kick, D1 snare, F#1 "
                         "closed hat) and put a Sampler or the Synth on it; on another, a 4-bar bassline that fits. "
                         "Use list_module_types to see what's available. Then set a loop over the 4 bars, render it "
                         "and tell me how it measures. Work in a few clear steps and say what you did.");
         }});

    registry.prompts.push_back(
        {"check-the-mix",
         "Check the mix",
         "Renders the song, measures it, and suggests what to change.",
         {{"range", "The part to check, such as \"9.1.1 to 17.1.1\" (default: the whole song)", false}},
         [user](const Json& args) {
             const auto range = args.value("range", std::string());
             return user("Check my mix. Read segno://project, then render " +
                         (range.empty() ? std::string("the whole song (from 1.1.1 to the project's end)") : range) +
                         " and each track on its own. Report the levels, clipping, stereo correlation and spectral "
                         "balance, and point out problems: clipping, too much or too little low end, harshness, "
                         "tracks fighting in the same band, phase trouble. Suggest specific changes to specific "
                         "modules' parameters (get_module shows them), but don't change anything until I agree.");
         }});

    registry.prompts.push_back(
        {"explain-module",
         "Explain a module",
         "What a module in the chain does, how it's set, and what to try.",
         {{"module", "The module's id, such as \"m:7\"", true}},
         [user](const Json& args) {
             const auto m = args.value("module", std::string());
             return user("Explain module " + m + ": read it with get_module and its type with segno://module-type/"
                         "{type}. Say what it does in its track's chain, what its current settings do to the sound, "
                         "and two or three things worth trying, as set_parameters values I can approve.");
         }});
}
} // namespace fd::mcp
