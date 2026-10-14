#include "AudioToTable.h"
#include "FactoryTables.h"
#include "TableFiles.h"
#include "TableRecipe.h"
#include "WavetableSynth.h"
#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;
using W = devices::WavetableSynth;

namespace
{
namespace wt = devices::wavetable;

/// A Wavetable module's handle and the oscillator (0-2) the arguments name.
struct Oscillator
{
    std::unique_ptr<engine::DeviceHandle> handle;
    std::uint64_t module = 0;
    int n = 0;

    [[nodiscard]] const W& synth() const { return dynamic_cast<const W&>(*handle->context().device); }
    [[nodiscard]] ui::DeviceUiContext& context() const { return handle->context(); }
};

Oscillator oscillatorOf(ToolEnv& env, const Json& args)
{
    const auto [slot, owner] = module(env, args["module"]);
    Oscillator o;
    o.handle = env.session.openDevice(slot.id);
    if (o.handle == nullptr || o.handle->deviceId() != "wavetable" ||
        dynamic_cast<const W*>(o.handle->context().device) == nullptr)
        throw Problem{formatId(IdKind::Module, slot.id) + " isn't a Wavetable."};
    o.module = slot.id;
    o.n = args["oscillator"].get<int>() - 1;
    return o;
}

/// The oscillator's recipe: the editor's, or one of the table it plays, as the editor opens it.
wt::Recipe recipeOf(const Oscillator& o, bool* edited = nullptr)
{
    const auto& synth = o.synth();
    if (const auto& kept = synth.recipe(o.n); kept.is_object())
        if (auto r = wt::recipeFromJson(kept))
        {
            if (edited != nullptr)
                *edited = true;
            return *r;
        }
    if (edited != nullptr)
        *edited = false;
    const auto& id = synth.tableId(o.n);
    const bool factory = id != "user";
    auto r = wt::recipeFromTable(*synth.table(o.n), factory ? id : std::string());
    r.name = synth.tableName(o.n) + (factory ? " (edited)" : "");
    return r;
}

/// The table the oscillator plays now.
Json tableJson(const Oscillator& o)
{
    const auto& synth = o.synth();
    const auto table = synth.table(o.n);
    return {{"module", formatId(IdKind::Module, o.module)},
            {"oscillator", o.n + 1},
            {"table", synth.tableId(o.n)},
            {"name", synth.tableName(o.n)},
            {"frames", table != nullptr ? table->frames() : 0}};
}

/// Bakes a recipe and puts it on the oscillator, with its recipe: one undo step (the tool's).
void keep(const Oscillator& o, const wt::Recipe& recipe)
{
    o.context().params.applyExtraState(W::recipePatch(o.n, recipe, wt::bake(recipe)));
}

const char* const kRecipeFormat =
    "A recipe: {name, frames (1-256), layers: [up to 4 of {kind, on, level (0-1), fill (\"crossfade\" or "
    "\"spectral\": how frames between keyframes or source frames are filled), and by kind: draw: keyframes "
    "[{frame, points: [[x 0-1, y -1..1, bend -1..1], ...]}]; harmonics: keyframes [{frame, levels: [64 of "
    "0-1], "
    "phases: [64 of 0-1]}]; formula: formula (in x, the phase 0-1, and t, where in the table 0-1: "
    "\"sin(2*pi*x) + 0.5*t*sin(6*pi*x)\"); table: table (a factory id); audio, image, or a table's own "
    "frames: "
    "data (base64 16-bit frames of 2048 samples; left out, the layer keeps the frames of the layer at the "
    "same "
    "place now, if it's the same kind), note; image: asHarmonics}], changes: [{kind (normalise, remove-dc, "
    "phase-align, smooth, fold, filter, reverse, mirror, warp), on, filter (low, high, band), warp "
    "(formant-shift, harmonic-stretch, inharmonic-stretch, smear, spectral-low-pass, spectral-high-pass, "
    "phase-disperse, frame-skew, random-harmonics, shepard), amounts: [[frame, 0-1], ...] (one for the whole "
    "table; none is 1)}]}. Layers are summed, then the changes shape every frame in order.";
} // namespace

// The Wavetable Synth's tables (devices/wavetable/SPEC.md, "Tables and their files";
// docs/design/table-editor.md §8; docs/design/mcp.md, "Modules").
void addWavetableTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    ToolAnnotations changes;
    ToolAnnotations writesFile; // a file, not the project: it may overwrite one
    writesFile.destructive = true;
    writesFile.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"list_wavetables", "List wavetables",
         "The Wavetable's tables (the wavetable synth): the factory ones by category (an id set_wavetable takes), and the "
         "WAV and .wt "
         "files in the Segno folder's Wavetables (a path set_wavetable takes).",
         object({{"category",
                  string("Only this category (Basic, Analog, Digital, Harmonic, Formant, FM, Texture, "
                         "Spectral)")}}),
         result({{"factory", arrayOf(result({{"id", string("")}, {"name", string("")}}), "", std::nullopt)},
                 {"files", arrayOf(result({{"path", string("")}, {"name", string("")}}), "", std::nullopt)}},
                {"factory", "files"}),
         reading},
        [&env](const Json& args, CallContext&)
        {
            const auto category = args.value("category", std::string());
            Json factory = Json::array();
            for (const auto& t : devices::wavetable::factoryTables())
            {
                if (!category.empty() &&
                    !juce::String(std::string(t.category)).equalsIgnoreCase(juce::String(category)))
                    continue;
                const auto table = devices::wavetable::factoryTable(t.id);
                factory.push_back({{"id", std::string(t.id)},
                                   {"name", std::string(t.name)},
                                   {"category", std::string(t.category)},
                                   {"frames", table != nullptr ? table->frames() : 0}});
            }
            Json files = Json::array();
            const auto folder = env.session.segnoFolder().getChildFile("Wavetables");
            for (const auto& entry :
                 juce::RangedDirectoryIterator(folder, true, "*.wav;*.wt", juce::File::findFiles))
                files.push_back({{"path", entry.getFile().getFullPathName().toStdString()},
                                 {"name", entry.getFile().getFileNameWithoutExtension().toStdString()}});
            return ToolResult::ok({{"factory", std::move(factory)}, {"files", std::move(files)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_wavetable", "Set a wavetable",
         "Puts a table on one of a Wavetable's oscillators: a factory table by its id "
         "(list_wavetables), or a "
         "WAV or .wt file (its frames are carried in the project, so it always plays). One undo step.",
         object({{"module", id(IdKind::Module, "The Wavetable module")},
                 {"oscillator", integer("1, 2 or 3", 1, 3)},
                 {"table", string("A factory table's id")},
                 {"path", string("A WAV or .wt file's absolute path")}},
                {"module", "oscillator"}),
         result({{"table", string("The factory id, or \"user\" for a file")},
                 {"name", string("")},
                 {"frames", integer("", 1, 256)}},
                {"table", "name", "frames"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = module(env, args["module"]);
            auto handle = env.session.openDevice(slot.id);
            if (handle == nullptr || handle->deviceId() != "wavetable")
                throw Problem{formatId(IdKind::Module, slot.id) + " isn't a Wavetable."};
            const int n = args["oscillator"].get<int>() - 1;
            const bool byId = args.contains("table"), byFile = args.contains("path");
            if (byId == byFile)
                throw Problem{"Give either a factory table's id (table) or a file (path)."};
            auto& context = handle->context();
            if (byId)
            {
                const auto id = args["table"].get<std::string>();
                if (devices::wavetable::factoryTable(id) == nullptr)
                    throw Problem{"There's no factory table \"" + id + "\": list_wavetables lists them."};
                context.params.applyExtraState(W::factoryTablePatch(n, id));
            }
            else
            {
                const auto file = readableFile(args["path"]);
                std::string error;
                const auto read =
                    devices::wavetable::importTable(file.getFullPathName().toStdString(), 0, &error);
                if (!read)
                    throw Problem{file.getFullPathName().toStdString() +
                                  " isn't a table the synth can read (" + error + ")."};
                const auto table =
                    dsp::Wavetable::fromCycles(read->samples.data(), read->frameLength, read->frames, true);
                context.params.applyExtraState(W::userTablePatch(
                    n, read->name.empty() ? file.getFileNameWithoutExtension().toStdString() : read->name,
                    table));
            }
            const auto* synth = dynamic_cast<const W*>(context.device);
            const auto table = synth != nullptr ? synth->table(n) : nullptr;
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                   {"oscillator", n + 1},
                                   {"table", synth != nullptr ? synth->tableId(n) : std::string()},
                                   {"name", synth != nullptr ? synth->tableName(n) : std::string()},
                                   {"frames", table != nullptr ? table->frames() : 0}});
        }));

    registry.tools.push_back(make(
        env,
        {"make_wavetable", "Make a wavetable from audio",
         "Makes a table for one of a Wavetable's oscillators from audio, as Clip > Make wavetable does: an "
         "audio "
         "clip (from and to choose a stretch of it on the timeline) or a sound file, taken one of three "
         "ways. "
         "pitched: single cycles where the pitch is, phase-aligned (notes, voices, instruments); spectral: "
         "the "
         "harmonics' levels at the sound's pitch over time (pads, chorused sounds); slices: windows as they "
         "are "
         "(anything). The table keeps its recipe, so it opens in the table editor. One undo step.",
         object(
             {{"module", id(IdKind::Module, "The Wavetable module")},
              {"oscillator", integer("1, 2 or 3", 1, 3)},
              {"clip", id(IdKind::Clip, "An audio clip")},
              {"from", position("Where in the clip to start (default: its start).")},
              {"to", position("Where in the clip to end (default: its end).")},
              {"path", string("A sound file's absolute path, instead of a clip")},
              {"mode",
               {{"type", "string"},
                {"enum", Json::array({"pitched", "spectral", "slices"})},
                {"description", "How the audio becomes frames"}}},
              {"frames", integer("How many frames to take, spread through the audio (default 64)", 8, 256)}},
             {"module", "oscillator", "mode"}),
         result({{"table", string("\"user\": a table of yours, carried in the project")},
                 {"name", string("")},
                 {"frames", integer("", 1, 256)},
                 {"pitch", number("The pitch found (Hz), for pitched and spectral")}},
                {"table", "name", "frames"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto o = oscillatorOf(env, args);
            const bool byClip = args.contains("clip"), byFile = args.contains("path");
            if (byClip == byFile)
                throw Problem{"Give either an audio clip (clip) or a sound file (path)."};
            std::vector<float> samples;
            double rate = 0.0;
            std::string name;
            if (byClip)
            {
                const auto c = clip(env, args["clip"]);
                if (c.kind != model::ClipInfo::Kind::Audio)
                    throw Problem{formatId(IdKind::Clip, c.id) +
                                  " is a MIDI clip: a table is made from audio."};
                std::optional<double> from, to;
                if (args.contains("from"))
                    from = seconds(env, positionArg(env, args["from"]));
                if (args.contains("to"))
                    to = seconds(env, positionArg(env, args["to"]));
                auto sound = env.session.clipSound(c.id, from, to);
                if (!sound)
                    throw Problem{"The clip's audio can't be read, or from and to leave none of it."};
                samples = std::move(sound->samples);
                rate = sound->sampleRate;
                name = sound->name;
            }
            else
            {
                const auto file = readableFile(args["path"]);
                std::string error;
                auto mono = wt::readMono(file.getFullPathName().toStdString(), &error);
                if (!mono)
                    throw Problem{file.getFullPathName().toStdString() + " isn't audio (" + error + ")."};
                samples = std::move(mono->samples);
                rate = mono->sampleRate;
                name = file.getFileNameWithoutExtension().toStdString();
            }
            const auto way = wt::wayFromName(args["mode"].get<std::string>());
            if (!way)
                throw Problem{"mode is pitched, spectral or slices."};
            // A minute at most, as the editor takes.
            samples.resize(std::min(samples.size(), static_cast<size_t>(60.0 * rate)));
            std::string error;
            const auto frames = wt::framesFromAudio(samples.data(), static_cast<int>(samples.size()), rate,
                                                    *way, args.value("frames", 64), &error);
            if (!frames)
                throw Problem{error};
            keep(o, wt::recipeFromAudio(*frames, *way, name));
            auto out = tableJson(o);
            if (frames->pitch > 0.0)
                out["pitch"] = frames->pitch;
            return ToolResult::ok(std::move(out));
        }));

    registry.tools.push_back(make(
        env,
        {"get_wavetable", "Get a wavetable",
         std::string(
             "One of a Wavetable's oscillators' table: what it plays (a factory id, or \"user\"), its name "
             "and "
             "frames, and its recipe: the one the table editor made, or (edited false) one of the table it "
             "plays, to start from with edit_wavetable. ") +
             kRecipeFormat,
         object({{"module", id(IdKind::Module, "The Wavetable module")},
                 {"oscillator", integer("1, 2 or 3", 1, 3)},
                 {"data", boolean("Include the frames of audio, image and file layers (base64, large); "
                                  "default false: their count instead")}},
                {"module", "oscillator"}),
         result({{"table", string("")},
                 {"name", string("")},
                 {"frames", integer("", 1, 256)},
                 {"edited", boolean("Whether the table editor made it")},
                 {"recipe", {{"type", "object"}, {"description", "The recipe"}}}},
                {"table", "name", "frames", "edited", "recipe"}),
         reading, false},
        [&env](const Json& args, CallContext&)
        {
            const auto o = oscillatorOf(env, args);
            bool edited = false;
            const auto recipe = recipeOf(o, &edited);
            auto json = wt::toJson(recipe);
            if (!args.value("data", false))
                for (size_t i = 0; i < json["layers"].size(); ++i)
                    if (auto& layer = json["layers"][i]; layer.contains("data"))
                    {
                        layer.erase("data");
                        layer["dataFrames"] = recipe.layers[i].sourceFrames();
                    }
            auto out = tableJson(o);
            out["edited"] = edited;
            out["recipe"] = std::move(json);
            return ToolResult::ok(std::move(out));
        }));

    registry.tools.push_back(make(
        env,
        {"edit_wavetable", "Edit a wavetable",
         std::string(
             "Replaces one of a Wavetable's oscillators' recipe (get_wavetable gives the one now) and bakes "
             "its table, as the table editor does: checked whole first, one undo step. ") +
             kRecipeFormat,
         object({{"module", id(IdKind::Module, "The Wavetable module")},
                 {"oscillator", integer("1, 2 or 3", 1, 3)},
                 {"recipe", {{"type", "object"}, {"description", "The whole recipe"}}}},
                {"module", "oscillator", "recipe"}),
         result({{"table", string("")}, {"name", string("")}, {"frames", integer("", 1, 256)}},
                {"table", "name", "frames"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto o = oscillatorOf(env, args);
            auto json = args["recipe"];
            if (!json.is_object())
                throw Problem{"recipe is an object: get_wavetable shows one."};
            // Layers that leave their frames out keep the ones of the layer at their place now.
            const auto now = wt::toJson(recipeOf(o));
            if (json.contains("layers") && json["layers"].is_array())
                for (size_t i = 0; i < json["layers"].size(); ++i)
                {
                    auto& layer = json["layers"][i];
                    if (!layer.is_object() || layer.contains("data") || i >= now["layers"].size())
                        continue;
                    const auto& before = now["layers"][i];
                    if (before.contains("data") &&
                        layer.value("kind", std::string()) == before.value("kind", std::string()))
                        layer["data"] = before["data"];
                    layer.erase("dataFrames");
                }
            std::string error;
            const auto recipe = wt::recipeFromJson(json, &error);
            if (!recipe)
                throw Problem{"The recipe can't be used: " + error};
            keep(o, *recipe);
            return ToolResult::ok(tableJson(o));
        }));

    registry.tools.push_back(make(
        env,
        {"export_wavetable", "Export a wavetable",
         "Writes one of a Wavetable's oscillators' table to a file: WAV (32-bit float with a clm chunk "
         "giving the "
         "frame size, as other synths read) or .wt. By default into the Segno folder's Wavetables, named for "
         "the "
         "table. Changes nothing in the project.",
         object({{"module", id(IdKind::Module, "The Wavetable module")},
                 {"oscillator", integer("1, 2 or 3", 1, 3)},
                 {"path", string("Where to write it (absolute); its extension sets the format")},
                 {"format",
                  {{"type", "string"},
                   {"enum", Json::array({"wav", "wt"})},
                   {"description", "The format, where the path doesn't say (default wav)"}}}},
                {"module", "oscillator"}),
         result({{"path", string("The file written")}, {"frames", integer("", 1, 256)}}, {"path", "frames"}),
         writesFile, false},
        [&env](const Json& args, CallContext&)
        {
            const auto o = oscillatorOf(env, args);
            const auto table = o.synth().table(o.n);
            if (table == nullptr)
                throw Problem{"The oscillator has no table."};
            juce::File file;
            if (args.contains("path"))
            {
                const auto path = juce::String(args["path"].get<std::string>()).trim();
                if (!juce::File::isAbsolutePath(path))
                    throw Problem{"Give the file's absolute path."};
                file = juce::File(path);
            }
            else
                file = env.session.segnoFolder()
                           .getChildFile("Wavetables")
                           .getChildFile(
                               juce::File::createLegalFileName(juce::String(o.synth().tableName(o.n))));
            auto format = args.value("format", std::string(file.hasFileExtension("wt") ? "wt" : "wav"));
            if (!file.hasFileExtension("wav;wt"))
                file = file.withFileExtension(format);
            else
                format = file.hasFileExtension("wt") ? "wt" : "wav";
            file.getParentDirectory().createDirectory();
            std::string error;
            const auto path = file.getFullPathName().toStdString();
            if (!(format == "wt" ? wt::exportWt(*table, path, &error) : wt::exportWav(*table, path, &error)))
                throw Problem{"Couldn't write " + path + ": " + error};
            return ToolResult::ok({{"path", path}, {"frames", table->frames()}});
        }));
}
} // namespace fd::mcp
