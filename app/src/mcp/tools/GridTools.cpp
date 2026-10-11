// The editing grid of each editor (ADR 0047, docs/design/mcp.md §5): view state, never an undo step.

#include "mcp/tools/Tools.h"
#include "model/Timeline.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
using Editor = engine::Session::GridEditor;

Json enumOf(std::initializer_list<std::string_view> values, const std::string& description)
{
    Json names = Json::array();
    for (const auto v : values)
        names.push_back(std::string(v));
    return {{"type", "string"}, {"enum", std::move(names)}, {"description", description}};
}

/// `justSet`: the editor hasn't drawn the new grid yet, so an adaptive step isn't known.
Json gridJson(ToolEnv& env, Editor editor, bool justSet = false)
{
    const auto g = env.session.grid(editor);
    std::string step;
    if (g.mode == model::GridSettings::Mode::Fixed)
    {
        model::Timeline timeline; // a fixed step is the same at any zoom
        timeline.setGrid(g);
        step = timeline.gridStepName(tempo(env));
    }
    else if (!justSet)
        step = env.session.gridStep(editor);
    return {{"mode", std::string(model::modeName(g.mode))},
            {"width", std::string(model::widthName(g.width))},
            {"value", std::string(model::valueName(g.value))},
            {"triplets", g.triplets},
            {"snap", g.snap},
            {"step", step.empty() ? Json(nullptr) : Json(step)}};
}

Json gridResult()
{
    return result({{"mode", string("")},
                   {"width", string("")},
                   {"value", string("")},
                   {"triplets", boolean("")},
                   {"snap", boolean("")},
                   {"step", {{"type", {"string", "null"}}, {"description", "The step it snaps to now"}}}},
                  {"mode", "width", "value", "triplets", "snap", "step"});
}
} // namespace

void addGridTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    ToolAnnotations changes;
    changes.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"get_grid", "Read the editing grids",
         "Each editor's grid: the arrangement's and the piano roll's, which are separate. mode: adaptive "
         "(follows the "
         "zoom, its lines at least width apart) or fixed (value at every zoom); triplets; snap; and step, "
         "the grid "
         "the editor snaps to now (\"1/16\", \"1/8T\", \"1 bar\"; null while the piano roll shows no clip). "
         "In the "
         "piano roll the step is also the length of a note drawn by double-click.",
         object({}),
         result({{"arrangement", gridResult()}, {"piano_roll", gridResult()}}, {"arrangement", "piano_roll"}),
         reading, false},
        [&env](const Json&, CallContext&)
        {
            return ToolResult::ok({{"arrangement", gridJson(env, Editor::Arrangement)},
                                   {"piano_roll", gridJson(env, Editor::PianoRoll)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_grid", "Set an editor's grid",
         "Sets the arrangement's or the piano roll's grid, as its step's menu does; the other editor's stays "
         "as it "
         "is. Only what's given changes: mode (adaptive or fixed), width (adaptive: how far apart its lines "
         "must be), "
         "value (fixed: its step), triplets, snap. Returns the grid, with a fixed grid's step (an adaptive "
         "one's "
         "follows the editor's zoom: get_grid reports it once the editor shows it). View state only: never "
         "an undo "
         "step.",
         object(
             {{"editor", enumOf({"arrangement", "piano_roll"}, "Which editor")},
              {"mode", enumOf({"adaptive", "fixed"}, "Follow the zoom, or keep one value")},
              {"width",
               enumOf({"narrowest", "narrow", "medium", "wide", "widest"}, "The adaptive grid's width")},
              {"value", enumOf({"8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"},
                               "The fixed grid's step")},
              {"triplets", boolean("Steps shorter than a bar as triplets")},
              {"snap", boolean("Edits snap to the grid")}},
             {"editor"}),
         gridResult(), changes, false},
        [&env](const Json& args, CallContext&)
        {
            const auto editor =
                args["editor"].get<std::string>() == "piano_roll" ? Editor::PianoRoll : Editor::Arrangement;
            auto g = env.session.grid(editor);
            if (args.contains("mode"))
                g.mode = model::modeFromName(args["mode"].get<std::string>()).value_or(g.mode);
            if (args.contains("width"))
                g.width = model::widthFromName(args["width"].get<std::string>()).value_or(g.width);
            if (args.contains("value"))
                g.value = model::valueFromName(args["value"].get<std::string>()).value_or(g.value);
            if (args.contains("triplets"))
                g.triplets = args["triplets"].get<bool>();
            if (args.contains("snap"))
                g.snap = args["snap"].get<bool>();
            env.session.setGrid(editor, g);
            return ToolResult::ok(gridJson(env, editor, true));
        }));
}
} // namespace fd::mcp
