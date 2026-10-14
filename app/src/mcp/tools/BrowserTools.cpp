// The browser (M3 C2, ADR 0046, docs/design/browser.md §15): searching what the browser finds,
// looking in its places, previewing for the person, and keeping a project's files together.
// Assistants never delete files: there's no tool for Remove unused files.

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
using Kind = model::BrowserKind;

const char* kindName(Kind k)
{
    switch (k)
    {
    case Kind::Module:
        return "module";
    case Kind::Preset:
        return "preset";
    case Kind::Plugin:
        return "plugin";
    case Kind::Rack:
        return "rack";
    case Kind::Sample:
        return "sample";
    case Kind::Multisample:
        return "multisample";
    case Kind::Wavetable:
        return "wavetable";
    case Kind::Midi:
        return "midi";
    case Kind::Project:
        return "project";
    case Kind::Folder:
        return "folder";
    }
    return "sample";
}

std::string path(const juce::File& f)
{
    return f.getFullPathName().toStdString();
}

/// Every place, as the browser lists them: the project's folder, the Library (ADR 0055; none when
/// it isn't installed), the Segno folder as User library, yours.
std::vector<std::pair<std::string, juce::File>> places(engine::Session& s)
{
    std::vector<std::pair<std::string, juce::File>> out;
    if (const auto project = s.projectFolderOnDisk())
        out.emplace_back("Project", *project);
    out.emplace_back("Library", s.libraryFolder());
    out.emplace_back("User library", s.segnoFolder());
    for (const auto& p : s.browserPlaces())
        out.emplace_back(p.getFileName().toStdString(), p);
    return out;
}

const model::IndexedFile* indexed(engine::Session& s, const std::string& file)
{
    for (const auto& [place, files] : s.browserIndex().places)
        for (const auto& f : files)
            if (f.path == file)
                return &f;
    return nullptr;
}

Json fileJson(engine::Session& s, const juce::File& file, Kind kind)
{
    Json j{{"path", path(file)}, {"kind", kindName(kind)}};
    if (const auto* f = indexed(s, path(file)); f != nullptr && kind == Kind::Sample)
    {
        j["seconds"] = std::round(f->seconds * 1000.0) / 1000.0;
        if (f->tempo > 0.0)
            j["tempo"] = f->tempo;
    }
    if (const auto* f = indexed(s, path(file)); f != nullptr && kind == Kind::Wavetable && f->frames > 0)
        j["frames"] = f->frames;
    if (const auto* f = indexed(s, path(file)); f != nullptr && kind == Kind::Multisample && f->zones > 0)
        j["zones"] = f->zones;
    return j;
}
} // namespace

void addBrowserTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    ToolAnnotations playing;
    playing.readOnly = true; // the project stays as it is
    ToolAnnotations writing;

    registry.tools.push_back(make(
        env,
        {"search", "Search the browser",
         "What the person's browser finds for some words: our modules, their presets, plugins, and the "
         "samples, multisamples (SFZ files), wavetables, MIDI files, projects and folders in the browser's "
         "places (the project's "
         "folder, Segno's Library of sounds and factory tables, the person's Segno folder shown as User "
         "library, folders the person added). Every word must match a name, tag, module, folder or "
         "description. Each result has an id: add_module takes a module's or plugin's type and a preset's "
         "id (preset), insert_audio_file and insert_midi_file a file's path, load_sample a sample's or a "
         "multisample's, set_wavetable a wavetable's "
         "path (a Library table's sets its factory table), preview any sample, multisample, wavetable, MIDI "
         "file or "
         "instrument preset.",
         object(
             {{"query", string("Words to find")},
              {"kinds",
               arrayOf(
                   string("module, preset, plugin, sample, multisample, wavetable, midi, project or folder"),
                   "Only these kinds")},
              {"limit", integer("At most this many results (20)", 1, 200)}},
             {"query"}),
         result({{"results", arrayOf(result({{"id", string("")}, {"kind", string("")}, {"name", string("")}}),
                                     "Best first", std::nullopt)},
                 {"total", integer("How many matched")}},
                {"results", "total"}),
         reading, false},
        [&env](const Json& args, CallContext&)
        {
            auto& s = env.session;
            std::set<std::string> kinds;
            if (args.contains("kinds"))
                for (const auto& k : args["kinds"])
                    kinds.insert(k.get<std::string>());
            const int limit = args.value("limit", 20);
            model::BrowserSearch search;
            search.setItems(s.browserItems());
            Json results = Json::array();
            int total = 0;
            for (const auto& g : search.search(args["query"].get<std::string>()))
            {
                if (!kinds.empty() && !kinds.contains(kindName(g.kind)))
                    continue;
                total += static_cast<int>(g.items.size());
                for (const auto i : g.items)
                {
                    if (static_cast<int>(results.size()) >= limit)
                        break;
                    const auto& item = search.items()[i];
                    Json r{{"id", item.id}, {"kind", kindName(item.kind)}, {"name", item.name}};
                    if (!item.caption.empty())
                        r["from"] = item.caption;
                    if (!item.tags.empty())
                        r["tags"] = item.tags;
                    if (item.id.starts_with("module:") || item.id.starts_with("plugin:"))
                        r["type"] = item.id.substr(item.id.find(':') + 1);
                    if (item.id.starts_with("file:"))
                        r["path"] = item.id.substr(5);
                    results.push_back(std::move(r));
                }
            }
            return ToolResult::ok({{"results", std::move(results)}, {"total", total}});
        }));

    registry.tools.push_back(make(
        env,
        {"browse", "Look in the browser's places",
         "Without a folder: the browser's places (the project's folder, the Library, the Segno folder as "
         "User "
         "library, folders the person added; the Library is missing when it isn't installed). "
         "With one (a place or a folder in one): its folders and the files the browser lists (samples with "
         "their "
         "length and tempo when known, MIDI files, projects).",
         object({{"folder", string("A place or a folder in one, by its absolute path")}}),
         result({{"places", arrayOf(result({}), "The places", std::nullopt)},
                 {"folders", arrayOf(string(""), "Folders in it", std::nullopt)},
                 {"files", arrayOf(result({}), "Files in it", std::nullopt)}}),
         reading, false},
        [&env](const Json& args, CallContext&)
        {
            auto& s = env.session;
            const auto all = places(s);
            if (!args.contains("folder"))
            {
                Json list = Json::array();
                for (const auto& [name, folder] : all)
                    list.push_back(
                        {{"name", name}, {"path", path(folder)}, {"missing", !folder.isDirectory()}});
                return ToolResult::ok({{"places", std::move(list)}});
            }
            const juce::File folder(juce::String::fromUTF8(args["folder"].get<std::string>().c_str()));
            if (std::none_of(all.begin(), all.end(), [&](const auto& p)
                             { return folder == p.second || folder.isAChildOf(p.second); }))
                throw Problem{
                    "That folder isn't in one of the browser's places (browse without a folder lists them)."};
            if (!folder.isDirectory())
                throw Problem{"There's no folder " + path(folder) + "."};
            Json folders = Json::array(), files = Json::array();
            for (const auto& entry :
                 juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories))
            {
                const auto& f = entry.getFile();
                if (f.getFileName().startsWithChar('.') || entry.isHidden())
                    continue;
                if (entry.isDirectory())
                    folders.push_back(path(f));
                else if (const auto kind = model::fileKind(path(f)))
                    files.push_back(fileJson(s, f, *kind));
            }
            return ToolResult::ok({{"folders", std::move(folders)}, {"files", std::move(files)}});
        }));

    registry.tools.push_back(make(
        env,
        {"preview", "Preview for the person",
         "Plays something from the browser for the person, as selecting it would: a sample (in time with the "
         "song "
         "while it plays, when it's a loop that fits its tempo), a wavetable file on a held "
         "note, a multisample (SFZ) in a short phrase, a MIDI file on the track live MIDI plays, "
         "or a short "
         "phrase with an instrument preset. Previews are never recorded or exported. stop: true stops it.",
         object({{"item", string("A result's id (search), or a file's absolute path")},
                 {"stop", boolean("Stop the preview")}}),
         result({{"playing", boolean("")}}, {"playing"}), playing, false},
        [&env](const Json& args, CallContext&)
        {
            auto& s = env.session;
            if (args.value("stop", false) || !args.contains("item"))
            {
                s.stopPreview();
                return ToolResult::ok({{"playing", false}});
            }
            auto item = args["item"].get<std::string>();
            if (!item.starts_with("file:") && !item.starts_with("preset:"))
                item = "file:" + item;
            Json out{{"playing", false}};
            if (item.starts_with("preset:"))
            {
                const auto bar = item.find('|');
                if (bar == std::string::npos || !item.starts_with("preset:fd:"))
                    throw Problem{"That isn't a preset id (search gives them)."};
                if (!s.previewPreset(item.substr(10, bar - 10), item.substr(bar + 1)))
                    throw Problem{"Only our instruments' presets preview (a short phrase)."};
                out["playing"] = true;
                return ToolResult::ok(out);
            }
            const juce::File file(juce::String::fromUTF8(item.substr(5).c_str()));
            const auto kind = model::fileKind(path(file));
            if (!file.existsAsFile() || !kind)
                throw Problem{"There's no sample or MIDI file " + path(file) + "."};
            if (*kind == Kind::Wavetable)
            {
                if (!s.previewWavetable(file))
                    throw Problem{path(file) + " isn't a table Segno can read."};
                out["playing"] = true;
                return ToolResult::ok(out);
            }
            if (*kind == Kind::Multisample)
            {
                if (!s.previewMultisample(file))
                    throw Problem{path(file) + " has no zones the Sampler can play."};
                out["playing"] = true;
                return ToolResult::ok(out);
            }
            if (*kind == Kind::Midi)
            {
                if (!s.previewMidiFile(file))
                    throw Problem{
                        "A MIDI file previews on the track live MIDI plays, which needs an instrument."};
                out["playing"] = true;
                return ToolResult::ok(out);
            }
            const auto p = s.previewFile(file);
            if (!p)
                throw Problem{path(file) + " isn't audio Segno can play."};
            out = {{"playing", true},
                   {"seconds", std::round(p->seconds * 1000.0) / 1000.0},
                   {"inTime", p->inTime}};
            if (p->tempo > 0.0)
                out["tempo"] = p->tempo;
            return ToolResult::ok(out);
        }));

    registry.tools.push_back(make(
        env,
        {"collect_files", "Collect the project's files",
         "Copies every file the project uses from outside its folder into its Samples folder, points the "
         "clips at "
         "the copies, and saves, so the folder holds everything (File > Collect files and save). The project "
         "must "
         "have been saved once. Not an undo step; nothing outside the project is touched.",
         object({}),
         result({{"copied", integer("Files copied")},
                 {"failed", arrayOf(string(""), "Files that couldn't be copied", std::nullopt)}},
                {"copied"}),
         writing, false},
        [&env](const Json&, CallContext&)
        {
            auto& s = env.session;
            if (!s.file())
                throw Problem{"Save the project first (save_project): its folder is where the files go."};
            const auto collected = s.collectFiles();
            if (!s.save())
                throw Problem{"The files were copied, but the project couldn't be saved."};
            return ToolResult::ok({{"copied", collected.copied}, {"failed", collected.failed}},
                                  collected.copied == 0 ? "Every file is already in the project's folder."
                                                        : "Collected " + std::to_string(collected.copied) +
                                                              " files into Samples.");
        }));
}
} // namespace fd::mcp
