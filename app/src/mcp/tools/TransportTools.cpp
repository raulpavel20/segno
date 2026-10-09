// Transport, tempo and time signature (mcp.md §5).

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

void addTransportTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations transport; // changes what the DAW is doing, not the project
    transport.idempotent = true;
    const auto transportResult = result({{"playing", boolean("")}, {"position", positionResult()}}, {"playing", "position"});

    registry.tools.push_back(make(env,
                                  {"play", "Play", "Starts playback from the playhead.", object({}), transportResult,
                                   transport, false},
                                  [&env](const Json&, CallContext&) {
                                      env.session.play();
                                      return ToolResult::ok(transportJson(env));
                                  }));
    registry.tools.push_back(make(env,
                                  {"stop", "Stop", "Stops playback.", object({}), transportResult, transport, false},
                                  [&env](const Json&, CallContext&) {
                                      env.session.stop();
                                      return ToolResult::ok(transportJson(env));
                                  }));
    registry.tools.push_back(make(
        env,
        {"set_position", "Move the playhead", "Moves the playhead (playback continues from there when playing).",
         object({{"at", position("Where.")}}, {"at"}), transportResult, transport, false},
        [&env](const Json& args, CallContext&) {
            env.session.setPosition(seconds(env, positionArg(env, args["at"])));
            return ToolResult::ok(transportJson(env));
        }));
    registry.tools.push_back(make(
        env,
        {"set_loop", "Set the loop", "Sets the loop range and turns looping on (or off with \"on\": false).",
         object({{"start", position("Where the loop starts.")},
                 {"end", position("Where it ends.")},
                 {"on", boolean("Looping on (default true)")}},
                {"start", "end"}),
         result({{"loop", result({{"on", boolean("")}, {"start", positionResult()}, {"end", positionResult()}})}}, {"loop"}),
         transport, false},
        [&env](const Json& args, CallContext&) {
            const double start = positionArg(env, args["start"]), end = positionArg(env, args["end"]);
            if (!(end > start))
                throw Problem{"The loop's end must come after its start."};
            env.session.setLoopRange(seconds(env, start), seconds(env, end));
            env.session.setLooping(args.value("on", true));
            return ToolResult::ok(transportJson(env));
        }));
    registry.tools.push_back(make(env,
                                  {"set_metronome", "Metronome on or off", "Turns the metronome on or off.",
                                   object({{"on", boolean("On")}}, {"on"}), result({{"metronome", boolean("")}}, {"metronome"}),
                                   transport, false},
                                  [&env](const Json& args, CallContext&) {
                                      env.session.setMetronome(args["on"].get<bool>());
                                      return ToolResult::ok(transportJson(env));
                                  }));

    ToolAnnotations edit;
    edit.idempotent = true;
    registry.tools.push_back(make(
        env,
        {"set_tempo", "Set the tempo", "Sets the song's tempo (one undo step). Clips keep their bars and beats.",
         object({{"bpm", {{"type", "number"}, {"minimum", 20}, {"maximum", 999}, {"description", "Beats per minute"}}}},
                {"bpm"}),
         result({{"tempo", number("Beats per minute")}}, {"tempo"}), edit},
        [&env](const Json& args, CallContext&) {
            env.session.setTempo(args["bpm"].get<double>());
            env.session.flushSnapshot();
            return ToolResult::ok({{"tempo", tempo(env).bpm}});
        }));
    registry.tools.push_back(make(
        env,
        {"set_time_signature", "Set the time signature", "Sets the song's time signature (one undo step).",
         object({{"numerator", integer("Beats per bar", 1, 32)},
                 {"denominator", {{"enum", {1, 2, 4, 8, 16, 32}}, {"description", "The beat's note value"}}}},
                {"numerator", "denominator"}),
         result({{"timeSignature", result({{"numerator", integer("")}, {"denominator", integer("")}})}}, {"timeSignature"}),
         edit},
        [&env](const Json& args, CallContext&) {
            env.session.setTimeSignature(args["numerator"].get<int>(), args["denominator"].get<int>());
            env.session.flushSnapshot();
            const auto t = tempo(env);
            return ToolResult::ok({{"timeSignature", {{"numerator", t.numerator}, {"denominator", t.denominator}}}});
        }));
}
} // namespace fd::mcp
