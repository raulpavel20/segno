// Projects and undo (mcp.md §5). Replacing the project or overwriting another file can't be
// undone, so those ask the user first (ToolResult::confirm).

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
std::string projectName(ToolEnv& env)
{
    const auto file = env.session.file();
    return file ? "\"" + file->getFileNameWithoutExtension().toStdString() + "\"" : std::string("the untitled project");
}

juce::File projectFile(const Json& value)
{
    auto path = juce::String(value.get<std::string>()).trim();
    if (path == "~" || path.startsWith("~/"))
        path = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName() + path.substring(1);
    if (!juce::File::isAbsolutePath(path))
        throw Problem{"\"" + path.toStdString() + "\" isn't an absolute path."};
    juce::File file(path);
    if (!file.hasFileExtension(engine::Session::fileExtension))
        file = file.withFileExtension(engine::Session::fileExtension);
    return file;
}

Json undoSteps(ToolEnv& env, const Json& args, bool redo)
{
    const int steps = args.value("steps", 1);
    Json done = Json::array();
    for (int i = 0; i < steps && (redo ? env.session.canRedo() : env.session.canUndo()); ++i)
    {
        const auto name = redo ? env.session.redoDescription() : env.session.undoDescription();
        redo ? env.session.redo() : env.session.undo();
        done.push_back(name.empty() ? std::string("(an unnamed edit)") : name);
    }
    return done;
}
} // namespace

void addProjectTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations history;
    const auto stepsResult = [](const char* key) {
        return result({{key, arrayOf(string("A step, by name"), "", std::nullopt)}}, {key});
    };
    registry.tools.push_back(make(env,
                                  {"undo", "Undo",
                                   "Takes back the last edits (yours or the user's), one step each; says what was "
                                   "undone. An assistant call is one step, named \"Assistant: <tool>\".",
                                   object({{"steps", integer("How many steps (default 1)", 1, 50)}}), stepsResult("undone"),
                                   history, false},
                                  [&env](const Json& args, CallContext&) {
                                      auto done = undoSteps(env, args, false);
                                      const bool none = done.empty();
                                      return ToolResult::ok({{"undone", std::move(done)}},
                                                            none ? "There was nothing to undo." : "");
                                  }));
    registry.tools.push_back(make(env,
                                  {"redo", "Redo", "Redoes steps taken back with undo.",
                                   object({{"steps", integer("How many steps (default 1)", 1, 50)}}), stepsResult("redone"),
                                   history, false},
                                  [&env](const Json& args, CallContext&) {
                                      auto done = undoSteps(env, args, true);
                                      const bool none = done.empty();
                                      return ToolResult::ok({{"redone", std::move(done)}},
                                                            none ? "There was nothing to redo." : "");
                                  }));

    ToolAnnotations saving;
    saving.destructive = true; // can overwrite a file (it asks first)
    saving.idempotent = true;
    const auto fileResult = result({{"file", string("The project's file")}}, {"file"});
    registry.tools.push_back(make(
        env,
        {"save_project", "Save the project",
         "Saves the project to its file, or to a new path (\".fdproj\" is added). Saving over another existing file "
         "asks the user first.",
         object({{"path", string("Where to save (default: its own file)")},
                 {"confirm", boolean("The user agreed to overwrite (only for clients that can't ask them)")}}),
         fileResult, saving, false},
        [&env](const Json& args, CallContext& context) {
            const auto current = env.session.file();
            juce::File file;
            if (args.contains("path"))
                file = projectFile(args["path"]);
            else if (current)
                file = *current;
            else
                throw Problem{"The project has no file yet: give a path to save it to."};
            if (file.existsAsFile() && (!current || file != *current) && !context.confirmed)
                return ToolResult::confirm("Overwrite " + file.getFullPathName().toStdString() + " with this project?");
            if (!env.session.saveAs(file))
                throw Problem{"The project couldn't be saved to " + file.getFullPathName().toStdString() + "."};
            return ToolResult::ok({{"file", file.getFullPathName().toStdString()}});
        }));

    ToolAnnotations replaces;
    replaces.destructive = true;
    registry.tools.push_back(make(
        env,
        {"new_project", "New project",
         "Starts a new project (four empty tracks). Discarding unsaved changes asks the user first.",
         object({{"confirm", boolean("The user agreed to discard unsaved changes (only for clients that can't ask them)")}}),
         projectResultSchema(), replaces, false},
        [&env](const Json&, CallContext& context) {
            if (env.session.hasUnsavedChanges() && !context.confirmed)
                return ToolResult::confirm("Discard the unsaved changes in " + projectName(env) + " and start a new project?");
            env.session.newProject();
            env.session.flushSnapshot();
            return ToolResult::ok(projectJson(env));
        }));
    registry.tools.push_back(make(
        env,
        {"open_project", "Open a project",
         "Opens a project file. Discarding unsaved changes in the open project asks the user first.",
         object({{"path", string("The project file's absolute path")},
                 {"confirm", boolean("The user agreed to discard unsaved changes (only for clients that can't ask them)")}},
                {"path"}),
         projectResultSchema(), replaces, false},
        [&env](const Json& args, CallContext& context) {
            const auto file = readableFile(args["path"]);
            if (env.session.hasUnsavedChanges() && !context.confirmed)
                return ToolResult::confirm("Discard the unsaved changes in " + projectName(env) + " and open " +
                                           file.getFileName().toStdString() + "?");
            std::string error;
            if (!env.session.open(file, &error))
                throw Problem{error};
            env.session.flushSnapshot();
            return ToolResult::ok(projectJson(env));
        }));
}
} // namespace fd::mcp
