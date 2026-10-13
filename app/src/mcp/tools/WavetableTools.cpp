#include "FactoryTables.h"
#include "TableFiles.h"
#include "WavetableSynth.h"
#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;
using W = devices::WavetableSynth;

// The Wavetable Synth's tables (devices/wavetable/SPEC.md, "Tables and their files";
// docs/design/mcp.md, "Modules").
void addWavetableTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    ToolAnnotations changes;

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
}
} // namespace fd::mcp
