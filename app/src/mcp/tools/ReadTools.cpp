// Read tools (mcp.md §5): the resources by another route, for clients that prefer tools.

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

namespace tools
{
Json clipResultSchema()
{
    return result({{"id", string("The clip's id")},
                   {"kind", string("\"midi\" or \"audio\"")},
                   {"track", string("The track it's on")},
                   {"start", positionResult()},
                   {"end", positionResult()},
                   {"length", number("Beats")},
                   {"muted", boolean("Muted: it keeps its place and plays nothing")},
                   {"loop", {{"type", {"object", "null"}},
                             {"description", "What it plays again and again: start (in its content) and length, in beats; "
                                             "for audio also in seconds. Null when it doesn't loop"}}}},
                  {"id", "kind", "start", "end", "length"});
}

Json projectResultSchema()
{
    return result({{"name", string("The project's name")},
                   {"file", {{"type", {"string", "null"}}, {"description", "Its file, or null before it's saved"}}},
                   {"unsaved", boolean("Changes since it was opened or saved")},
                   {"tempo", number("Beats per minute")},
                   {"edgeFades", boolean("Cut edges of audio clips fade 4 ms (set_edge_fades)")},
                   {"timeSignature", result({{"numerator", integer("")}, {"denominator", integer("")}})},
                   {"tracks", arrayOf(result({{"id", string("")}, {"name", string("")}}), "Every track", std::nullopt)}},
                  {"name", "file", "unsaved", "tempo", "timeSignature", "tracks"});
}

Json moduleResultSchema()
{
    return result(
        {{"id", string("The module's id")},
         {"name", string("")},
         {"type", string("Its type (\"plugin\" for third-party plugins)")},
         {"on", boolean("")},
         {"latencyMs",
          number("How late it plays its track (ms): the DAW keeps the tracks in time, but what's played "
                 "live through it is heard that late. 0 for most modules; a linear-phase or spectral EQ "
                 "about 50 to 220")},
         {"parameters", arrayOf(result({{"id", string("")}, {"name", string("")}}), "", std::nullopt)}},
        {"id", "name", "type", "on", "latencyMs", "parameters"});
}
} // namespace tools

void addReadTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"get_project", "Get the project",
         "The open project: name, file, unsaved changes, tempo, time signature, loop, undo, and every track with its "
         "clips (without notes) and its module chain. The same as the segno://project resource.",
         object({}), projectResultSchema(), reading},
        [&env](const Json&, CallContext&) { return ToolResult::ok(projectJson(env)); }));

    registry.tools.push_back(make(
        env,
        {"get_clip", "Get a clip",
         "One clip: kind, position, length, its loop and, for MIDI clips, every note (pitch, start and length in beats "
         "from the clip's start, or from its loop's start while it loops; velocity 1-127).",
         object({{"clip", id(IdKind::Clip, "The clip")}}, {"clip"}), clipResultSchema(), reading},
        [&env](const Json& args, CallContext&) { return ToolResult::ok(clipJson(env, clip(env, args["clip"]), true)); }));

    registry.tools.push_back(make(env,
                                  {"get_module", "Get a module",
                                   "One module in a chain: type, on/off, the delay it adds (latencyMs), "
                                   "every parameter (id, name, value, text, "
                                   "unit, range, default), its panel sections and whether they're collapsed, "
                                   "and its other state (such as the "
                                   "Sampler's sample).",
                                   object({{"module", id(IdKind::Module, "The module")}}, {"module"}),
                                   moduleResultSchema(), reading},
                                  [&env](const Json& args, CallContext&)
                                  {
                                      const auto [slot, owner] = module(env, args["module"]);
                                      return ToolResult::ok(moduleJson(env, slot, owner));
                                  }));

    registry.tools.push_back(make(
        env,
        {"list_module_types", "List module types",
         "Every module that can be added to a chain: our instruments and effects, and scanned third-party plugins. "
         "Use a type with add_module; read segno://module-type/{type} for its parameters.",
         object({{"kind", {{"enum", {"instrument", "effect"}}, {"description", "Only instruments or only effects"}}}}),
         result({{"types", arrayOf(result({{"type", string("")}, {"name", string("")}, {"kind", string("")}}), "",
                                   std::nullopt)}},
                {"types"}),
         reading},
        [&env](const Json& args, CallContext&) {
            return ToolResult::ok({{"types", moduleTypesJson(env, args.value("kind", std::string()))}});
        }));
}
} // namespace fd::mcp
