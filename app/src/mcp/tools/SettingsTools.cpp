// Settings (M3 C1, ADR 0045, docs/design/settings.md §11): reading every setting, and changing the
// ones about sound and files. Never the assistants' own settings, nor your shortcuts: an assistant
// can't widen its own reach or change your keys.

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
using model::ExportFormat;

const char* formatKey(ExportFormat f)
{
    return f == ExportFormat::Flac ? "flac" : f == ExportFormat::Aiff ? "aiff" : "wav";
}

ExportFormat formatArg(const Json& value)
{
    const auto v = value.get<std::string>();
    if (v == "wav")
        return ExportFormat::Wav;
    if (v == "flac")
        return ExportFormat::Flac;
    if (v == "aiff")
        return ExportFormat::Aiff;
    throw Problem{"format is \"wav\", \"flac\" or \"aiff\"."};
}

int bitDepthArg(ExportFormat format, const Json& value)
{
    const int depth = value.get<int>();
    const auto depths = model::bitDepthsFor(format);
    if (std::find(depths.begin(), depths.end(), depth) == depths.end())
        throw Problem{model::formatName(format) + (format == ExportFormat::Wav
                                                       ? " holds 16, 24 or 32 (float) bits."
                                                       : " holds 16 or 24 bits.")};
    return depth;
}

constexpr std::pair<model::CountIn, const char*> kCountIns[] = {{model::CountIn::Off, "off"},
                                                                {model::CountIn::OneBeat, "1 beat"},
                                                                {model::CountIn::TwoBeats, "2 beats"},
                                                                {model::CountIn::OneBar, "1 bar"},
                                                                {model::CountIn::TwoBars, "2 bars"}};

constexpr std::pair<model::ExportNormalise, const char*> kNormalise[] = {
    {model::ExportNormalise::Off, "off"},
    {model::ExportNormalise::Peak, "peak"},
    {model::ExportNormalise::Loudness, "loudness"}};

model::SettingsStore& store(ToolEnv& env)
{
    if (env.settings == nullptr)
        throw Problem{"This DAW keeps no settings."};
    return *env.settings;
}

Json settingsJson(ToolEnv& env)
{
    const auto& s = store(env).get();
    auto& session = env.session;
    auto& dm = session.deviceManager();

    Json audio{{"driver", dm.getCurrentAudioDeviceType().toStdString()}};
    if (auto* d = dm.getCurrentAudioDevice())
    {
        const auto setup = dm.getAudioDeviceSetup();
        Json rates = Json::array(), buffers = Json::array();
        for (const auto r : d->getAvailableSampleRates())
            rates.push_back(r);
        for (const auto b : d->getAvailableBufferSizes())
            buffers.push_back(b);
        audio["output"] = setup.outputDeviceName.toStdString();
        audio["input"] = setup.inputDeviceName.isEmpty() ? Json() : Json(setup.inputDeviceName.toStdString());
        audio["sampleRate"] = d->getCurrentSampleRate();
        audio["buffer"] = d->getCurrentBufferSizeSamples();
        audio["latencyMs"] =
            std::round(10000.0 * d->getCurrentBufferSizeSamples() / d->getCurrentSampleRate()) / 10.0;
        audio["sampleRates"] = std::move(rates);
        audio["buffers"] = std::move(buffers);
    }
    else
        audio["output"] = nullptr;

    Json midiInputs = Json::array();
    for (const auto& m : session.midiDevices())
        midiInputs.push_back({{"name", m.name}, {"enabled", m.enabled}});

    const auto [audioMs, midiMs] = session.recordingOffsets();
    std::string countIn = "1 bar";
    for (const auto& [value, name] : kCountIns)
        if (value == session.countIn())
            countIn = name;

    const auto& e = s.exportDefaults;
    std::string normalise = "off";
    for (const auto& [value, name] : kNormalise)
        if (value == e.normalise)
            normalise = name;

    Json keys = Json::object();
    for (const auto& [action, list] : s.keys)
        keys[action] = list;

    return {{"audio", std::move(audio)},
            {"midi", {{"inputs", std::move(midiInputs)}, {"computerKeyboard", s.midi.computerKeyboard}}},
            {"recording",
             {{"format", formatKey(s.recording.format)},
              {"bitDepth", s.recording.bitDepth},
              {"countIn", countIn},
              {"audioOffsetMs", audioMs},
              {"midiOffsetMs", midiMs}}},
            {"library",
             {{"folder", s.library.folder.empty() ? Json() : Json(s.library.folder)},
              {"newProjects", s.library.newProjects.empty() ? Json() : Json(s.library.newProjects)}}},
            {"plugins", {{"folders", s.plugins.folders}, {"skipped", session.skippedPlugins()}}},
            {"export",
             {{"format", formatKey(e.format)},
              {"bitDepth", e.bitDepth},
              {"sampleRate", e.sampleRate > 0.0 ? Json(e.sampleRate) : Json("project")},
              {"dither", e.dither},
              {"normalise", normalise}}},
            {"keys", std::move(keys)},
            {"display",
             {{"interfaceSize",
               s.display.interfaceSize > 0 ? Json(s.display.interfaceSize) : Json("automatic")}}},
            {"assistants", {{"enabled", s.assistants.enabled}, {"readOnly", s.assistants.readOnly}}}};
}

/// The sections get_settings returns, as a result schema.
Json settingsSchema()
{
    Json sections = Json::object();
    for (const auto* name :
         {"audio", "midi", "recording", "library", "plugins", "export", "keys", "display", "assistants"})
        sections[name] = result({});
    return result(sections, {"audio", "midi", "recording", "export", "display"});
}
} // namespace

void addSettingsTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    ToolAnnotations changes;
    changes.idempotent = true;

    registry.tools.push_back(make(env,
                                  {"get_settings", "Get the settings",
                                   "The settings Segno keeps for every project (Settings, Ctrl+,), by "
                                   "section: audio (the driver, devices, sample "
                                   "rate, buffer and the latency it makes, and the rates and buffers "
                                   "offered), MIDI inputs, recording (file format, "
                                   "bit depth, count-in, offsets), library folders, plugin folders and "
                                   "skipped plugins, a new project's export "
                                   "settings, the shortcuts changed from their defaults, the interface's "
                                   "size, and the assistants' switches.",
                                   object({}), settingsSchema(), reading, false},
                                  [&env](const Json&, CallContext&)
                                  { return ToolResult::ok(settingsJson(env)); }));

    registry.tools.push_back(make(
        env,
        {"set_settings", "Change settings",
         "Changes some settings, at once and for every project (not an undo step): recording {format, "
         "bitDepth, countIn, "
         "audioOffsetMs, midiOffsetMs}; export {format, bitDepth, sampleRate (44100, 48000, 88200, 96000, or "
         "\"project\"), "
         "dither, normalise}: what a new project's export starts from; display {interfaceSize: 75-200 in "
         "steps of 25, or "
         "\"automatic\"}; midi {inputs: [{name, enabled}], computerKeyboard}; audio {sampleRate, buffer}, "
         "from the ones "
         "get_settings offers. Never the assistants' own settings or the shortcuts: the person changes those "
         "in Settings.",
         object({{"recording",
                  object({{"format", string("\"wav\", \"flac\" or \"aiff\"")},
                          {"bitDepth", integer("16, 24, or 32 (float, WAV only)")},
                          {"countIn", string("\"off\", \"1 beat\", \"2 beats\", \"1 bar\" or \"2 bars\"")},
                          {"audioOffsetMs", number("-500 to 500: moves new audio recordings")},
                          {"midiOffsetMs", number("-500 to 500: moves new MIDI recordings")}})},
                 {"export",
                  object({{"format", string("\"wav\", \"flac\" or \"aiff\"")},
                          {"bitDepth", integer("16, 24, or 32 (float, WAV only)")},
                          {"sampleRate",
                           {{"type", {"number", "string"}}, {"description", "A rate, or \"project\""}}},
                          {"dither", boolean("Dither below 32-bit float")},
                          {"normalise", string("\"off\", \"peak\" or \"loudness\"")}})},
                 {"display",
                  object({{"interfaceSize",
                           {{"type", {"integer", "string"}}, {"description", "75-200, or \"automatic\""}}}})},
                 {"midi",
                  object({{"inputs",
                           arrayOf(object({{"name", string("A MIDI input's name")}, {"enabled", boolean("")}},
                                          {"name", "enabled"}),
                                   "MIDI inputs to turn on or off")},
                          {"computerKeyboard", boolean("The computer keyboard as MIDI from launch")}})},
                 {"audio", object({{"sampleRate", number("One of the rates offered")},
                                   {"buffer", integer("One of the buffer sizes offered")}})},
                 {"assistants", result({})},
                 {"keys", result({})}}),
         settingsSchema(), changes, false},
        [&env](const Json& args, CallContext&)
        {
            if (args.contains("assistants") || args.contains("keys"))
                throw Problem{
                    "Assistants can't change their own settings or the shortcuts: the person changes them in "
                    "Settings (Ctrl+,)."};
            auto& st = store(env);
            auto& session = env.session;
            // Everything is checked before anything changes.
            auto next = st.get();
            std::optional<model::CountIn> countIn;
            std::optional<double> audioMs, midiMs;
            if (args.contains("recording"))
            {
                const auto& r = args["recording"];
                if (r.contains("format"))
                    next.recording.format = formatArg(r["format"]);
                if (r.contains("bitDepth"))
                    next.recording.bitDepth = bitDepthArg(next.recording.format, r["bitDepth"]);
                else if (const auto depths = model::bitDepthsFor(next.recording.format);
                         std::find(depths.begin(), depths.end(), next.recording.bitDepth) == depths.end())
                    next.recording.bitDepth = 24;
                if (r.contains("countIn"))
                {
                    const auto v = r["countIn"].get<std::string>();
                    for (const auto& [value, name] : kCountIns)
                        if (v == name)
                            countIn = value;
                    if (!countIn)
                        throw Problem{
                            "countIn is \"off\", \"1 beat\", \"2 beats\", \"1 bar\" or \"2 bars\"."};
                }
                for (const auto& [key, out] :
                     {std::pair{"audioOffsetMs", &audioMs}, std::pair{"midiOffsetMs", &midiMs}})
                    if (r.contains(key))
                    {
                        *out = r[key].get<double>();
                        if (std::abs(**out) > 500.0)
                            throw Problem{std::string(key) + " is between -500 and 500."};
                    }
            }
            if (args.contains("export"))
            {
                const auto& e = args["export"];
                auto& d = next.exportDefaults;
                if (e.contains("format"))
                    d.format = formatArg(e["format"]);
                if (e.contains("bitDepth"))
                    d.bitDepth = bitDepthArg(d.format, e["bitDepth"]);
                if (e.contains("sampleRate"))
                {
                    if (e["sampleRate"].is_string() && e["sampleRate"] == "project")
                        d.sampleRate = 0.0;
                    else if (e["sampleRate"].is_number() &&
                             std::find(std::begin(model::kExportRates), std::end(model::kExportRates),
                                       e["sampleRate"].get<double>()) != std::end(model::kExportRates))
                        d.sampleRate = e["sampleRate"].get<double>();
                    else
                        throw Problem{"sampleRate is 44100, 48000, 88200, 96000 or \"project\"."};
                }
                if (e.contains("dither"))
                    d.dither = e["dither"].get<bool>();
                if (e.contains("normalise"))
                {
                    const auto v = e["normalise"].get<std::string>();
                    const auto it = std::find_if(std::begin(kNormalise), std::end(kNormalise),
                                                 [&](const auto& n) { return v == n.second; });
                    if (it == std::end(kNormalise))
                        throw Problem{"normalise is \"off\", \"peak\" or \"loudness\"."};
                    d.normalise = it->first;
                }
                d = model::validated(d);
            }
            if (args.contains("display") && args["display"].contains("interfaceSize"))
            {
                const auto& v = args["display"]["interfaceSize"];
                if (v.is_string() && v == "automatic")
                    next.display.interfaceSize = 0;
                else if (v.is_number_integer() &&
                         std::find(std::begin(model::kInterfaceSizes), std::end(model::kInterfaceSizes),
                                   v.get<int>()) != std::end(model::kInterfaceSizes))
                    next.display.interfaceSize = v.get<int>();
                else
                    throw Problem{"interfaceSize is 75, 100, 125, 150, 175, 200 or \"automatic\"."};
            }
            std::vector<std::pair<std::string, bool>> midiInputs;
            if (args.contains("midi"))
            {
                const auto& m = args["midi"];
                if (m.contains("computerKeyboard"))
                    next.midi.computerKeyboard = m["computerKeyboard"].get<bool>();
                if (m.contains("inputs"))
                {
                    const auto devices = session.midiDevices();
                    for (const auto& i : m["inputs"])
                    {
                        const auto name = i["name"].get<std::string>();
                        if (std::none_of(devices.begin(), devices.end(),
                                         [&](const auto& d) { return d.name == name; }))
                            throw Problem{"There's no MIDI input \"" + name +
                                          "\" (get_settings lists them)."};
                        midiInputs.emplace_back(name, i["enabled"].get<bool>());
                    }
                }
            }
            std::optional<juce::AudioDeviceManager::AudioDeviceSetup> audio;
            if (args.contains("audio"))
            {
                auto& dm = session.deviceManager();
                auto* device = dm.getCurrentAudioDevice();
                if (device == nullptr)
                    throw Problem{"No audio device is open: the person chooses one in Settings."};
                audio = dm.getAudioDeviceSetup();
                const auto& a = args["audio"];
                if (a.contains("sampleRate"))
                {
                    const double rate = a["sampleRate"].get<double>();
                    if (!device->getAvailableSampleRates().contains(rate))
                        throw Problem{"The device doesn't offer " + juce::String(rate).toStdString() +
                                      " Hz (get_settings lists its rates)."};
                    audio->sampleRate = rate;
                }
                if (a.contains("buffer"))
                {
                    const int buffer = a["buffer"].get<int>();
                    if (!device->getAvailableBufferSizes().contains(buffer))
                        throw Problem{"The device doesn't offer a buffer of " + std::to_string(buffer) +
                                      " (get_settings lists them)."};
                    audio->bufferSize = buffer;
                }
            }

            st.change([&next](model::AppSettings& s) { s = next; });
            engine::applySettings(session, st.get()); // without a window, nothing else would
            if (countIn)
                session.setCountIn(*countIn);
            if (audioMs || midiMs)
            {
                const auto [a, m] = session.recordingOffsets();
                session.setRecordingOffsets(audioMs.value_or(a), midiMs.value_or(m));
            }
            for (const auto& [name, enabled] : midiInputs)
                session.setMidiDeviceEnabled(name, enabled);
            if (audio)
                session.deviceManager().setAudioDeviceSetup(*audio, true);
            return ToolResult::ok(settingsJson(env), "Settings changed.");
        }));
}
} // namespace fd::mcp
