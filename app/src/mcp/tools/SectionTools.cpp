// The song's sections (M3 step C3, ADR 0049, docs/design/arranger.md §13): marking them, and moving,
// copying, inserting and deleting them with their music. Each tool is one undo step.

#include "mcp/tools/Tools.h"
#include "model/Sections.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
model::SectionInfo sectionArg(ToolEnv& env, const Json& value)
{
    const auto id = parseId(value, IdKind::Section);
    if (!id)
        throw Problem{"Give a section by its id (\"s:12\"): list_sections lists them."};
    const auto* s = env.session.snapshot()->section(*id);
    if (s == nullptr)
        throw Problem{"There's no section " + formatId(IdKind::Section, *id) + "."};
    return *s;
}

std::vector<std::uint64_t> sectionsArg(ToolEnv& env, const Json& value)
{
    std::vector<std::uint64_t> out;
    for (const auto& v : value.is_array() ? value : Json::array({value}))
        out.push_back(sectionArg(env, v).id);
    return out;
}

/// Where a run lands: right before or right after another section, one of the two given.
double targetOf(ToolEnv& env, const Json& args)
{
    if (args.contains("before") == args.contains("after"))
        throw Problem{"Say where: \"before\" one section, or \"after\" one."};
    const auto target = sectionArg(env, args.contains("before") ? args["before"] : args["after"]);
    return args.contains("before") ? target.start : target.end;
}

int colourArg(const Json& value)
{
    if (!value.is_number_integer() || value.get<int>() < 0 || value.get<int>() >= model::kPaletteSize)
        throw Problem{"Give the colour as a palette index, 0 to 15."};
    return value.get<int>();
}

Json sectionsJson(ToolEnv& env)
{
    env.session.flushSnapshot();
    const auto snap = env.session.snapshot();
    const auto t = snap->tempo;
    Json list = Json::array();
    for (const auto& s : snap->sections)
        list.push_back({{"section", formatId(IdKind::Section, s.id)},
                        {"name", s.name},
                        {"colour", s.colour},
                        {"start", positionJson(t.beatsForSeconds(s.start), t)},
                        {"end", positionJson(t.beatsForSeconds(s.end), t)},
                        {"seconds", {{"start", s.start}, {"end", s.end}}}});
    return list;
}

Json listResult()
{
    return result(
        {{"sections", arrayOf(result({{"section", string("Its id")}, {"name", string("")}}), "By start", 0)}},
        {"sections"});
}

Json where()
{
    return id(IdKind::Section, "Another section");
}
} // namespace

void addSectionTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations changes;
    ToolAnnotations destructive;
    destructive.destructive = true;

    registry.tools.push_back(make(env,
                                  {"list_sections", "List the song's sections",
                                   "The song's sections, by start (the song row, ADR 0049): each one's id, "
                                   "name, colour (a palette index), "
                                   "start and end.",
                                   object({}), listResult(), reads, false},
                                  [&env](const Json&, CallContext&)
                                  { return ToolResult::ok({{"sections", sectionsJson(env)}}); }));

    registry.tools.push_back(make(env,
                                  {"add_section", "Mark a section",
                                   "Marks a section of the song from `start` to `end`, named `name` (else "
                                   "the next letter: A, B, C…) and "
                                   "coloured `colour` (else the next colour). The music stays where it is. "
                                   "Refuses one that would overlap "
                                   "another. One undo step.",
                                   object({{"start", position("Where it starts.")},
                                           {"end", position("Where it ends.")},
                                           {"name", string("Its name: Intro, Verse, Chorus…")},
                                           {"colour", integer("Its colour: a palette index", 0, 15)}},
                                          {"start", "end"}),
                                   result({{"section", string("Its id")}}, {"section"}), changes},
                                  [&env](const Json& args, CallContext&)
                                  {
                                      const double start = seconds(env, positionArg(env, args["start"])),
                                                   end = seconds(env, positionArg(env, args["end"]));
                                      const auto id = env.session.addSection(
                                          start, end, args.value("name", std::string()),
                                          args.contains("colour") ? std::optional(colourArg(args["colour"]))
                                                                  : std::nullopt);
                                      if (id == 0)
                                          throw Problem{"That would overlap another section (or it's empty): "
                                                        "list_sections shows where they are."};
                                      return ToolResult::ok({{"section", formatId(IdKind::Section, id)},
                                                             {"sections", sectionsJson(env)}});
                                  }));

    registry.tools.push_back(make(
        env,
        {"set_section", "Rename, recolour or re-mark a section",
         "Sets a section's name, colour (a palette index) and/or its marks (`start`, `end`). Moving its "
         "marks "
         "doesn't move any music: move_section does that. One undo step.",
         object({{"section", id(IdKind::Section, "The section")},
                 {"name", string("Its new name")},
                 {"colour", integer("Its colour: a palette index", 0, 15)},
                 {"start", position("Where it now starts.")},
                 {"end", position("Where it now ends.")}},
                {"section"}),
         listResult(), changes},
        [&env](const Json& args, CallContext&)
        {
            const auto s = sectionArg(env, args["section"]);
            if (!args.contains("name") && !args.contains("colour") && !args.contains("start") &&
                !args.contains("end"))
                throw Problem{"Give a name, a colour, a start or an end to set."};
            if (args.contains("name"))
            {
                const auto name = args["name"].get<std::string>();
                if (juce::String(name).trim().isEmpty())
                    throw Problem{"A section's name can't be empty."};
                env.session.renameSection(s.id, name);
            }
            if (args.contains("colour"))
                env.session.setSectionColour(s.id, colourArg(args["colour"]));
            if (args.contains("start") || args.contains("end"))
            {
                const double start =
                    args.contains("start") ? seconds(env, positionArg(env, args["start"])) : s.start;
                const double end = args.contains("end") ? seconds(env, positionArg(env, args["end"])) : s.end;
                if (!env.session.setSectionMarks(s.id, start, end))
                    throw Problem{"Those marks would overlap another section, or leave it empty."};
            }
            return ToolResult::ok({{"sections", sectionsJson(env)}});
        }));

    registry.tools.push_back(make(
        env,
        {"move_section", "Move sections with their music",
         "Moves one section, or a run of them, with everything in its time (clips, takes, automation) to "
         "right "
         "before or after another section. The rest of the song makes room; the song keeps its length. Clips "
         "across its edges are split there. One undo step.",
         object({{"sections", ids(IdKind::Section, "The section, or a run of them")},
                 {"before", where()},
                 {"after", where()}},
                {"sections"}),
         listResult(), destructive},
        [&env](const Json& args, CallContext&)
        {
            const auto ids = sectionsArg(env, args["sections"]);
            if (!env.session.moveSections(ids, targetOf(env, args)))
                throw Problem{"It can't land inside itself, or where it already is."};
            return ToolResult::ok({{"sections", sectionsJson(env)}});
        }));

    registry.tools.push_back(make(
        env,
        {"copy_section", "Copy sections with their music",
         "Copies one section, or a run of them, with its music to right before or after another section; the "
         "music after that moves later to make room. Copies keep the colour, named with a number (\"Chorus "
         "2\"). "
         "One undo step.",
         object({{"sections", ids(IdKind::Section, "The section, or a run of them")},
                 {"before", where()},
                 {"after", where()}},
                {"sections"}),
         result({{"copies", arrayOf(string("A copy's id"), "The copies", 0)}}, {"copies"}), destructive},
        [&env](const Json& args, CallContext&)
        {
            const auto ids = sectionsArg(env, args["sections"]);
            const auto copies = env.session.copySections(ids, targetOf(env, args));
            if (copies.empty())
                throw Problem{"It can't be copied inside itself."};
            Json list = Json::array();
            for (const auto id : copies)
                list.push_back(formatId(IdKind::Section, id));
            return ToolResult::ok({{"copies", std::move(list)}, {"sections", sectionsJson(env)}});
        }));

    registry.tools.push_back(make(env,
                                  {"insert_section", "Insert an empty section",
                                   "Puts an empty section `length` long right before or after another "
                                   "section; the music after it moves "
                                   "later. One undo step.",
                                   object({{"length", length("How long it is.")},
                                           {"before", where()},
                                           {"after", where()},
                                           {"name", string("Its name")}},
                                          {"length"}),
                                   result({{"section", string("Its id")}}, {"section"}), destructive},
                                  [&env](const Json& args, CallContext&)
                                  {
                                      const auto id = env.session.insertSection(
                                          targetOf(env, args), seconds(env, lengthArg(env, args["length"])),
                                          args.value("name", std::string()));
                                      if (id == 0)
                                          throw Problem{"An empty section can't go there."};
                                      return ToolResult::ok({{"section", formatId(IdKind::Section, id)},
                                                             {"sections", sectionsJson(env)}});
                                  }));

    registry.tools.push_back(make(
        env,
        {"delete_section", "Delete sections",
         "Deletes sections with their music, the gap closing (the song gets shorter); with `keep_music`, "
         "only "
         "their marks go and the music stays. One undo step.",
         object({{"sections", ids(IdKind::Section, "The sections")},
                 {"keep_music", boolean("Remove only the marks, leaving the music")}},
                {"sections"}),
         listResult(), destructive},
        [&env](const Json& args, CallContext&)
        {
            env.session.deleteSections(sectionsArg(env, args["sections"]), args.value("keep_music", false));
            return ToolResult::ok({{"sections", sectionsJson(env)}});
        }));
}
} // namespace fd::mcp
