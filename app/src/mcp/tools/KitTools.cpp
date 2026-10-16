// The Drum Kit (M3 P7, docs/design/drum-kit.md §13, ADR 0064): its pads, each with its sound and
// settings; samples onto pads; pads moved, swapped, copied and cleared. Through engine::Session,
// like the kit's panel (ADR 0021). Modules on pads are modules: every module tool reaches them;
// add_module and move_module take a kit and a pad.

#include "mcp/tools/Tools.h"
#include "model/DrumKit.h"

#include <cctype>
#include <cmath>

namespace fd::mcp
{
using namespace tools;

namespace tools
{
int padArg(const Json& value)
{
    if (value.is_number_integer())
    {
        const int note = value.get<int>();
        if (note < 0 || note > 127)
            throw Problem{"A pad is a note, 0 to 127 (C1 is 36)."};
        return note;
    }
    if (!value.is_string())
        throw Problem{"A pad is a note: its number (36) or its name (\"C1\", \"F#1\")."};
    // A note's name, C3 = 60 (daw-shell.md): a letter, a sharp or flat, an octave from -2.
    const auto text = value.get<std::string>();
    static const std::map<char, int> letters{{'C', 0}, {'D', 2}, {'E', 4}, {'F', 5},
                                             {'G', 7}, {'A', 9}, {'B', 11}};
    size_t i = 0;
    const auto letter = letters.find(
        static_cast<char>(std::toupper(static_cast<unsigned char>(text.empty() ? ' ' : text[0]))));
    if (letter == letters.end())
        throw Problem{"\"" + text + "\" isn't a note's name (\"C1\", \"F#1\", \"Bb2\")."};
    int pc = letter->second;
    i = 1;
    if (text.compare(i, 1, "#") == 0)
        ++pc, i += 1;
    else if (text.compare(i, 3, "\xe2\x99\xaf") == 0) // ♯
        ++pc, i += 3;
    else if (text.compare(i, 1, "b") == 0)
        --pc, i += 1;
    else if (text.compare(i, 3, "\xe2\x99\xad") == 0) // ♭
        --pc, i += 3;
    int octave = 0;
    try
    {
        size_t used = 0;
        octave = std::stoi(text.substr(i), &used);
        if (used != text.size() - i)
            throw std::invalid_argument("trailing");
    }
    catch (const std::exception&)
    {
        throw Problem{"\"" + text + "\" isn't a note's name (\"C1\", \"F#1\", \"Bb2\")."};
    }
    const int note = (octave + 2) * 12 + pc;
    if (note < 0 || note > 127)
        throw Problem{"\"" + text + "\" is outside the 128 pads (C-2 to G8)."};
    return note;
}

Json kitJson(ToolEnv& env, const model::DeviceSlot& slot, const model::TrackInfo& owner)
{
    const auto& kit = *slot.rack;
    auto pads = kit.chains;
    std::sort(pads.begin(), pads.end(), [](const auto& a, const auto& b) { return a.note < b.note; });
    Json padsJson = Json::array();
    for (const auto& p : pads)
    {
        Json modules = Json::array();
        for (const auto& d : p.devices)
            modules.push_back(moduleSummary(d));
        padsJson.push_back({{"pad", model::kit::noteName(p.note)},
                            {"note", p.note},
                            {"name", p.name},
                            {"plays", model::kit::noteName(p.plays)},
                            {"choke", p.choke},
                            {"on", p.on},
                            {"solo", p.solo},
                            {"volumeDb", db(p.volumeDb)},
                            {"pan", panText(p.pan)},
                            {"modules", modules}});
    }
    // Its macros, as a rack's are given.
    const auto rack = rackJson(env, slot, owner);
    return {{"kit", formatId(IdKind::Module, slot.id)},
            {"track", formatId(IdKind::Track, owner.id)},
            {"on", slot.enabled},
            {"pads", padsJson},
            {"selectedPad", model::kit::noteName(kit.selectedPad)},
            {"padsShown",
             {{"from", model::kit::noteName(kit.padsFrom)},
              {"to", model::kit::noteName(kit.padsFrom + model::kit::shown - 1)}}},
            {"macros", rack["macros"]}};
}
} // namespace tools

namespace
{
/// The kit an argument names, with its track.
std::pair<model::DeviceSlot, model::TrackInfo> kitArg(ToolEnv& env, const Json& value)
{
    auto [slot, owner] = module(env, value);
    if (slot.rack == nullptr || !slot.rack->kit)
        throw Problem{formatId(IdKind::Module, slot.id) +
                      " isn't a Drum Kit; add_module with type \"drum-kit\" adds one."};
    return {slot, owner};
}
} // namespace

void addKitTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations adds;
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations removes;
    removes.destructive = true;
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    const auto kitResult = result(
        {{"kit", string("The kit's id")},
         {"pads", arrayOf(result({}), "Its pads with sounds, lowest first: note, name, settings, modules",
                          std::nullopt)},
         {"selectedPad", string("The pad its panel shows")}},
        {"kit", "pads"});
    const auto now = [&env](std::uint64_t id)
    {
        env.session.flushSnapshot();
        const auto [slot, owner] = module(env, formatId(IdKind::Module, id));
        return ToolResult::ok(kitJson(env, slot, owner));
    };
    const auto pad = [](const std::string& what)
    {
        return Json{
            {"type", {"integer", "string"}},
            {"description", what + ": its note, as a number (36) or a name (\"C1\", \"F#1\"; C3 is 60)"}};
    };

    registry.tools.push_back(make(
        env,
        {"get_kit", "Read a Drum Kit",
         "A Drum Kit's pads with sounds (drum-kit.md): each pad's note, name, the note it plays into its "
         "instrument (Plays), choke group (0: none), on, solo, volume, pan and modules; the selected pad and "
         "the 16 shown; its eight macros. Modules on pads are modules: get_module and set_parameters reach "
         "them.",
         object({{"kit", id(IdKind::Module, "The kit")}}, {"kit"}), kitResult, reading, false},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = kitArg(env, args["kit"]);
            return ToolResult::ok(kitJson(env, slot, owner));
        }));

    registry.tools.push_back(make(
        env,
        {"set_pad", "Set a pad",
         "A pad's settings (drum-kit.md §5–§7), each set if given: its name, Plays (the note it plays into "
         "its instrument: a synth's pitch, a sample's transposition from C3), its choke group (0 for none, 1 "
         "to 16: a pad hit stops the others in its group), on, solo, volume (dB) and pan (-1 to 1, or "
         "\"25L\"); and selected, the pad its panel shows (view state). Each one undo step. An empty pad has "
         "no settings: load_pads or add_module gives it a sound.",
         object({{"kit", id(IdKind::Module, "The kit")},
                 {"pad", pad("The pad")},
                 {"name", string("Its name")},
                 {"plays", pad("The note it plays")},
                 {"choke", integer("Its choke group: 0 none, 1 to 16", 0, 16)},
                 {"on", boolean("On or off")},
                 {"solo", boolean("Heard alone while you set the kit up (never kept in a kit)")},
                 {"volumeDb", number("Its volume in dB (-inf to +6)")},
                 {"pan",
                  {{"type", {"number", "string"}},
                   {"description", "-1 (left) to 1 (right), or \"C\", \"25L\", \"50R\" (50: all the way)"}}},
                 {"selected", boolean("Show it in the kit's panel (true)")}},
                {"kit", "pad"}),
         kitResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [slot, owner] = kitArg(env, args["kit"]);
            const int note = padArg(args["pad"]);
            if (args.value("selected", false))
                env.session.selectPad(slot.id, note);
            engine::Session::PadEdit edit;
            if (args.contains("name"))
                edit.name = args["name"].get<std::string>();
            if (args.contains("plays"))
                edit.plays = padArg(args["plays"]);
            if (args.contains("choke"))
                edit.choke = args["choke"].get<int>();
            if (args.contains("on"))
                edit.on = args["on"].get<bool>();
            if (args.contains("solo"))
                edit.solo = args["solo"].get<bool>();
            if (args.contains("volumeDb"))
                edit.volumeDb =
                    args["volumeDb"].is_string() ? model::minusInfinityDb : args["volumeDb"].get<float>();
            if (args.contains("pan"))
            {
                const auto& v = args["pan"];
                if (v.is_number())
                    edit.pan = std::clamp(v.get<float>(), -1.0f, 1.0f);
                else
                {
                    const auto text = v.get<std::string>();
                    if (text == "C" || text == "c")
                        edit.pan = 0.0f;
                    else if (text.size() > 1 && (text.back() == 'L' || text.back() == 'R'))
                        edit.pan =
                            std::clamp(std::stof(text.substr(0, text.size() - 1)) / 50.0f, 0.0f, 1.0f) *
                            (text.back() == 'L' ? -1.0f : 1.0f);
                    else
                        throw Problem{"Pan is -1 to 1, \"C\", or like \"25L\" and \"40R\"."};
                }
            }
            const bool settings =
                edit.name || edit.plays || edit.choke || edit.on || edit.solo || edit.volumeDb || edit.pan;
            if (settings && !env.session.setPad(slot.id, note, edit))
                throw Problem{"The pad " + model::kit::noteName(note) +
                              " is empty: it has no settings until it has a sound."};
            return now(slot.id);
        }));

    registry.tools.push_back(make(
        env,
        {"load_pads", "Load samples onto pads",
         "Samples (or SFZ multisamples) onto a Drum Kit's pads from a pad up, one a pad, as a drop on a pad "
         "does (drum-kit.md §6): into the pad's Sampler, its settings kept, or a Sampler of its own in "
         "One-shot mode in place of the pad's instrument; each plays at its own pitch. A new pad is named "
         "for "
         "its sample. One undo step.",
         object({{"kit", id(IdKind::Module, "The kit")},
                 {"pad", pad("The first pad")},
                 {"paths", arrayOf(string("A sample or SFZ file"), "The files, in pad order")}},
                {"kit", "pad", "paths"}),
         kitResult, adds},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [slot, owner] = kitArg(env, args["kit"]);
            const int note = padArg(args["pad"]);
            std::vector<juce::File> files;
            for (const auto& p : args["paths"])
                files.push_back(readableFile(p));
            if (env.session.loadPads(slot.id, note, files).empty())
                throw Problem{"None of them could be loaded onto the pads."};
            return now(slot.id);
        }));

    registry.tools.push_back(
        make(env,
             {"move_pad", "Move or copy a pad",
              "A pad's sound and settings onto another pad, as a drag between pads does: moved (swapped with "
              "what's "
              "there), or with copy, copied over it. One undo step.",
              object({{"kit", id(IdKind::Module, "The kit")},
                      {"from", pad("The pad")},
                      {"to", pad("Where it goes")},
                      {"copy", boolean("Copy it, leaving the first where it is (default: move)")}},
                     {"kit", "from", "to"}),
              kitResult, changes},
             [&env, now](const Json& args, CallContext&)
             {
                 const auto [slot, owner] = kitArg(env, args["kit"]);
                 const int from = padArg(args["from"]), to = padArg(args["to"]);
                 if (!env.session.movePad(slot.id, from, to, args.value("copy", false)))
                     throw Problem{from == to ? std::string("They're the same pad.")
                                              : "The pad " + model::kit::noteName(from) + " is empty."};
                 return now(slot.id);
             }));

    registry.tools.push_back(
        make(env,
             {"clear_pad", "Clear a pad", "A pad's modules and settings go: it's empty (undoable).",
              object({{"kit", id(IdKind::Module, "The kit")}, {"pad", pad("The pad")}}, {"kit", "pad"}),
              kitResult, removes},
             [&env, now](const Json& args, CallContext&)
             {
                 const auto [slot, owner] = kitArg(env, args["kit"]);
                 const int note = padArg(args["pad"]);
                 if (!env.session.clearPad(slot.id, note))
                     throw Problem{"The pad " + model::kit::noteName(note) + " is already empty."};
                 return now(slot.id);
             }));
}
} // namespace fd::mcp
