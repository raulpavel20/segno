// MIDI learn for assistants (M3 C8, docs/design/midi-learn.md §9): the project's mappings and the
// app's transport ones, read, made and removed by message. Assistants can't learn (there's no hand
// on the hardware): they map by message.

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
constexpr std::pair<const char*, const char*> kTransport[] = {
    {"play", "transport.play"}, {"stop", "transport.stop"},           {"record", "transport.record"},
    {"loop", "transport.loop"}, {"metronome", "transport.metronome"}, {"capture", "transport.capture"}};

Json targetSchema()
{
    auto j = paramRefSchema();
    j["button"] = {{"type", "string"},
                   {"enum", Json::array({"mute", "solo", "arm"})},
                   {"description", "A channel's button, with track (arm: tracks only)"}};
    j["transport"] = {{"type", "string"},
                      {"enum", Json::array({"play", "stop", "record", "loop", "metronome", "capture"})},
                      {"description", "A transport button (kept with the app, for every project)"}};
    return j;
}

/// What the arguments map: a parameter (module and parameter, or track and volume, pan or send),
/// a channel's button (track and button) or a transport button.
model::MidiTarget targetArg(ToolEnv& env, const Json& args)
{
    if (args.contains("transport"))
    {
        for (const auto& [name, key] : kTransport)
            if (args["transport"] == name)
                return {0, key};
        throw Problem{"transport is play, stop, record, loop, metronome or capture."};
    }
    if (args.contains("button"))
    {
        if (!args.contains("track"))
            throw Problem{"Give the track whose " + args["button"].get<std::string>() + " button it is."};
        const auto t = channel(env, args["track"]);
        const model::MidiTarget target{t.id, args["button"].get<std::string>()};
        if (!env.session.canMapMidi(target))
            throw Problem{t.name + " has no " + target.key +
                          " button (only tracks arm; the master has none)."};
        return target;
    }
    const auto p = paramRef(env, args);
    return {p.target.channel, p.target.key};
}

Json targetJson(ToolEnv& env, const model::MidiTarget& t)
{
    Json j;
    if (t.isTransport())
        j = {{"transport", t.key.substr(std::string("transport.").size())}};
    else if (t.key == "mute" || t.key == "solo" || t.key == "arm")
        j = {{"track", formatId(IdKind::Track, t.channel)}, {"button", t.key}};
    else
        j = paramRefJson(env, {t.channel, t.key});
    j["name"] = env.session.midiTargetName(t);
    return j;
}

Json messageSchema()
{
    return object({{"type",
                    {{"type", "string"},
                     {"enum", Json::array({"cc", "cc14", "note", "pitchBend"})},
                     {"description",
                      "A controller, a 14-bit pair (number the coarse one, 0-31), a note, or pitch bend"}}},
                   {"number",
                    {{"type", {"integer", "string"}},
                     {"description", "The controller (0-127), or the note (60 or \"C3\")"}}},
                   {"channel", integer("1-16 (default 1)", 1, 16)},
                   {"device", string("The MIDI input's name, as get_settings lists them")}},
                  {"type", "device"});
}

model::MidiSource sourceArg(const Json& m)
{
    model::MidiSource s;
    const auto kind = model::kindFromKey(m.value("type", std::string()));
    if (!kind)
        throw Problem{"message.type is cc, cc14, note or pitchBend."};
    s.kind = *kind;
    s.device = m.value("device", std::string());
    if (s.device.empty())
        throw Problem{"Name the MIDI input (message.device), as get_settings lists them."};
    s.channel = m.value("channel", 1);
    if (s.kind != model::MidiSource::Kind::PitchBend)
    {
        if (!m.contains("number"))
            throw Problem{"Give message.number: the controller, or the note."};
        if (s.kind == model::MidiSource::Kind::Note)
        {
            const auto pitch = parsePitch(m["number"]);
            if (!pitch)
                throw Problem{m["number"].dump() + " isn't a note: give 0-127 or a name such as \"C3\"."};
            s.number = *pitch;
        }
        else if (m["number"].is_number_integer())
            s.number = m["number"].get<int>();
        else
            throw Problem{"A controller's number is 0-127."};
        const int top = s.kind == model::MidiSource::Kind::Cc14 ? 31 : 127;
        if (s.number < 0 || s.number > top)
            throw Problem{s.kind == model::MidiSource::Kind::Cc14
                              ? "A 14-bit pair's coarse controller is 0-31."
                              : "That's outside 0-127."};
    }
    return s;
}

Json messageJson(const model::MidiSource& s)
{
    Json j{{"type", model::kindKey(s.kind)},
           {"channel", s.channel},
           {"device", s.device},
           {"label", model::sourceLabel(s)}};
    if (s.kind != model::MidiSource::Kind::PitchBend)
        j["number"] = s.number;
    return j;
}

/// A Min or Max: the parameter's unit, text, or true/false for a button.
float valueArg(ToolEnv& env, const model::MidiTarget& t, const Json& v)
{
    if (t.isSwitchOnly() && v.is_boolean())
        return v.get<bool>() ? 1.0f : 0.0f;
    std::optional<float> travel;
    if (v.is_boolean())
        travel = v.get<bool>() ? 1.0f : 0.0f;
    else if (v.is_number() && !t.isSwitchOnly())
        travel = env.session.paramFromPlain({t.channel, t.key}, v.get<double>());
    else if (v.is_string())
        travel = env.session.midiValueFromText(t, v.get<std::string>());
    if (!travel)
        throw Problem{v.dump() + " isn't a value of " + env.session.midiTargetName(t) +
                      ": give it in its unit or as text (" + env.session.midiValueText(t, 0.5f) + ")."};
    return *travel;
}

Json mappingJson(ToolEnv& env, const model::MidiMapping& m)
{
    return {{"target", targetJson(env, m.target)},
            {"message", messageJson(m.source)},
            {"min", env.session.midiValueText(m.target, m.min)},
            {"max", env.session.midiValueText(m.target, m.max)},
            {"mode", model::modeKey(m.mode)},
            {"keptWith", m.target.isTransport() ? "app" : "project"}};
}

Json mappingResult()
{
    return result(
        {{"target", result({{"name", string("As a mapping's row names it: \"Filter · Freq\"")}})},
         {"message", result({{"type", string("")}, {"device", string("")}, {"channel", integer("")}})},
         {"min", string("Where the hardware's bottom takes it")},
         {"max", string("Where its top takes it (below min: the other way)")},
         {"mode", string("absolute, toggle, momentary, relativeTwos, relativeOffset or relativeSigned")}},
        {"target", "message", "min", "max", "mode"});
}
} // namespace

void addMidiMapTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    ToolAnnotations changes;
    changes.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"get_midi_mappings", "List MIDI mappings",
         "The MIDI mappings: the project's (a controller's knob, fader, button or pad moving a parameter, a "
         "channel's mute, solo or arm) and the app's (transport buttons, for every project). Each: its "
         "target, "
         "message (type, number, channel, input), min and max as the control shows them, and mode.",
         object({}), result({{"mappings", arrayOf(mappingResult(), "", 0)}}), reading, false},
        [&env](const Json&, CallContext&)
        {
            env.session.flushSnapshot();
            Json list = Json::array();
            for (const auto& m : env.session.midiMappings())
                list.push_back(mappingJson(env, m));
            return ToolResult::ok({{"mappings", std::move(list)}},
                                  std::to_string(list.size()) + " mappings.");
        }));

    registry.tools.push_back(make(
        env,
        {"map_midi", "Map a MIDI control",
         "Maps a controller's message to a target: a module's parameter (module, parameter), a channel's "
         "volume, "
         "pan or send (track, parameter), a channel's button (track, button: mute, solo, arm) or a transport "
         "button (transport). One message moves one target: mapping it again moves it, and a target mapped "
         "again leaves its old message. min and max (the parameter's unit or text; default its whole range; "
         "max below min turns it the other way). mode: absolute (a knob or fader), toggle or momentary "
         "(buttons; a note's default), relativeTwos, relativeOffset or relativeSigned (encoders). One undo "
         "step "
         "(a transport mapping is the app's, kept for every project). Assistants map by message; people can "
         "also learn by moving the hardware (a control's right-click > Learn MIDI, Options > MIDI map mode).",
         object(
             [&]
             {
                 auto j = targetSchema();
                 j["message"] = messageSchema();
                 j["min"] = {{"type", {"number", "string", "boolean"}},
                             {"description", "Where the hardware's bottom takes it"}};
                 j["max"] = {{"type", {"number", "string", "boolean"}},
                             {"description", "Where its top takes it"}};
                 j["mode"] = {{"type", "string"},
                              {"enum", Json::array({"absolute", "toggle", "momentary", "relativeTwos",
                                                    "relativeOffset", "relativeSigned"})},
                              {"description", ""}};
                 return j;
             }(),
             {"message"}),
         mappingResult(), changes},
        [&env](const Json& args, CallContext&)
        {
            auto& s = env.session;
            const auto target = targetArg(env, args);
            const auto source = sourceArg(args["message"]);
            model::MidiMapping m{.source = source, .target = target, .mode = model::learnedMode(source, 0)};
            if (args.contains("mode"))
            {
                const auto mode = model::modeFromKey(args["mode"].get<std::string>());
                const auto modes = model::modesFor(source.kind);
                if (!mode || std::find(modes.begin(), modes.end(), *mode) == modes.end())
                {
                    std::string list;
                    for (const auto md : modes)
                        list += (list.empty() ? "" : ", ") + model::modeKey(md);
                    throw Problem{"A " + model::kindKey(source.kind) + " message's modes are " + list + "."};
                }
                m.mode = *mode;
            }
            if (args.contains("min"))
                m.min = valueArg(env, target, args["min"]);
            if (args.contains("max"))
                m.max = valueArg(env, target, args["max"]);
            if (!s.mapMidi(m))
                throw Problem{s.midiTargetName(target) + " can't be mapped."};
            s.flushSnapshot();
            return ToolResult::ok(mappingJson(env, m), model::sourceText(source) + " (" + source.device +
                                                           ") moves " + s.midiTargetName(target) + ".");
        }));

    registry.tools.push_back(make(env,
                                  {"remove_midi_mapping", "Remove a MIDI mapping",
                                   "Removes the mapping of a target (as map_midi names it), or of a message "
                                   "(message). One undo step (a "
                                   "transport mapping is the app's).",
                                   object(
                                       [&]
                                       {
                                           auto j = targetSchema();
                                           j["message"] = messageSchema();
                                           return j;
                                       }()),
                                   result({{"removed", mappingResult()}}), changes},
                                  [&env](const Json& args, CallContext&)
                                  {
                                      auto& s = env.session;
                                      std::optional<model::MidiMapping> found;
                                      if (args.contains("message"))
                                      {
                                          const auto source = sourceArg(args["message"]);
                                          for (const auto& m : s.midiMappings())
                                              if (m.source == source)
                                                  found = m;
                                          if (!found)
                                              throw Problem{"No mapping has " + model::sourceText(source) +
                                                            " from " + source.device + "."};
                                      }
                                      else
                                      {
                                          const auto target = targetArg(env, args);
                                          found = s.midiMappingOf(target);
                                          if (!found)
                                              throw Problem{s.midiTargetName(target) +
                                                            " has no MIDI mapping."};
                                      }
                                      auto removed = mappingJson(env, *found);
                                      s.removeMidiMapping(found->target);
                                      s.flushSnapshot();
                                      return ToolResult::ok({{"removed", std::move(removed)}}, "Removed.");
                                  }));
}
} // namespace fd::mcp
