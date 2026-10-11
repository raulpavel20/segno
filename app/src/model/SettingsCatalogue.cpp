#include "model/SettingsCatalogue.h"

#include <algorithm>
#include <sstream>

namespace fd::model
{
namespace
{
std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                   { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return out;
}

std::vector<std::string> words(std::string_view query)
{
    std::vector<std::string> out;
    std::istringstream in{std::string(query)};
    for (std::string w; in >> w;)
        out.push_back(lower(w));
    return out;
}

using S = SettingsSection;
} // namespace

std::string sectionName(SettingsSection section)
{
    switch (section)
    {
    case S::Audio:
        return "Audio";
    case S::Midi:
        return "MIDI";
    case S::Recording:
        return "Recording";
    case S::Library:
        return "Library";
    case S::Plugins:
        return "Plugins";
    case S::Export:
        return "Export";
    case S::Keys:
        return "Keys";
    case S::Display:
        return "Display";
    case S::Assistants:
        return "Assistants";
    }
    return {};
}

const std::vector<SettingInfo>& settingsCatalogue()
{
    static const std::vector<SettingInfo> all{
        {"audio.driver", S::Audio, "Driver",
         "How Segno talks to your audio interface: the system's audio, or a server such as JACK.",
         "type alsa jack pipewire asio wasapi core audio"},
        {"audio.output", S::Audio, "Output device",
         "Where Segno plays: your interface or the computer's speakers.", "interface speakers headphones"},
        {"audio.input", S::Audio, "Input device", "What Segno records from.", "interface microphone"},
        {"audio.outputChannels", S::Audio, "Output channels",
         "Which of the output device's channels Segno plays on.", "outputs pairs"},
        {"audio.inputChannels", S::Audio, "Input channels",
         "Which of the input device's channels Segno can record.", "inputs pairs"},
        {"audio.sampleRate", S::Audio, "Sample rate", "How many samples a second the interface runs at.",
         "rate khz"},
        {"audio.buffer", S::Audio, "Buffer size",
         "Smaller is quicker to respond when you play; larger leaves more room before the audio drops out.",
         "latency block dropouts"},
        {"audio.test", S::Audio, "Test tone", "Plays a short tone, to check the output works.",
         "check sound"},
        {"audio.load", S::Audio, "Load",
         "How busy the audio is, and how often it has dropped out since you opened Settings.",
         "cpu dropouts xruns"},
        {"midi.inputs", S::Midi, "MIDI inputs",
         "Which keyboards and controllers Segno listens to; one turned off is ignored everywhere.",
         "keyboard controller devices"},
        {"midi.computerKeyboard", S::Midi, "Computer keyboard",
         "Plays notes from the computer's keyboard from the moment Segno opens.", "typing qwerty"},
        {"recording.format", S::Recording, "File format", "The kind of file new recordings are written as.",
         "wav aiff flac"},
        {"recording.bitDepth", S::Recording, "Bit depth",
         "How finely new recordings are written; 24-bit is plenty for most.", "16 24 32 float resolution"},
        {"recording.countIn", S::Recording, "Count-in",
         "How long the metronome counts before recording starts.", "metronome bars beats"},
        {"recording.audioOffset", S::Recording, "Audio offset",
         "Moves new audio recordings earlier or later, for an interface that reports its latency wrongly.",
         "latency compensation timing"},
        {"recording.midiOffset", S::Recording, "MIDI offset",
         "Moves new MIDI recordings earlier or later, for a keyboard that reaches Segno late.",
         "latency timing"},
        {"library.folder", S::Library, "Segno folder",
         "Where your presets and unsaved projects are kept; moving it moves them along.",
         "documents presets unsaved location"},
        {"library.newProjects", S::Library, "New projects",
         "Where saving a project for the first time starts.", "save location documents"},
        {"library.places", S::Library, "Browser places",
         "Folders the browser lists under Places and searches, besides the project's and the Segno folder.",
         "browser samples sounds folders locations"},
        {"plugins.folders", S::Plugins, "Plugin folders",
         "Folders scanned for plugins besides the system's own.", "vst3 lv2 locations paths"},
        {"plugins.scan", S::Plugins, "Scan for plugins",
         "Looks for plugins in every folder, the system's and yours.", "rescan refresh"},
        {"plugins.skipped", S::Plugins, "Skipped plugins",
         "Plugins that crashed a scan and are left out until you try them again.",
         "blacklist crashed failed"},
        {"export.format", S::Export, "Export format",
         "What a new project exports as, until you change it in its export panel.", "wav flac aiff file"},
        {"export.bitDepth", S::Export, "Export bit depth", "A new project's export bit depth.", "16 24 32"},
        {"export.sampleRate", S::Export, "Export sample rate",
         "A new project's export sample rate; the project's own unless set.", "rate khz"},
        {"export.dither", S::Export, "Export dither",
         "Adds a little noise when an export is written below 32-bit float, so quiet endings fade smoothly.",
         "noise"},
        {"export.normalise", S::Export, "Export normalising",
         "Whether a new project's exports are brought to a level or a loudness.", "loudness lufs peak"},
        {"keys", S::Keys, "Shortcuts", "Every action's keys: click one to change it.",
         "keyboard keymap bindings"},
        {"display.interfaceSize", S::Display, "Interface size",
         "Makes everything in Segno larger or smaller.", "scale zoom ui dpi"},
        {"display.systemTitleBar", S::Display, "System title bar",
         "Uses the system's title bar instead of Segno's.", "window decorations"},
        {"assistants.enabled", S::Assistants, "Assistants",
         "Lets assistants you connect drive Segno, on this computer only.", "mcp ai server"},
        {"assistants.readOnly", S::Assistants, "Read only",
         "Assistants can look at the project but not change it.", "mcp permissions"},
    };
    return all;
}

const SettingInfo* findSetting(std::string_view id)
{
    for (const auto& s : settingsCatalogue())
        if (s.id == id)
            return &s;
    return nullptr;
}

bool textMatches(std::string_view text, std::string_view query)
{
    const auto haystack = lower(text);
    const auto ws = words(query);
    return std::all_of(ws.begin(), ws.end(),
                       [&](const std::string& w) { return haystack.find(w) != std::string::npos; });
}

bool settingMatches(const SettingInfo& s, std::string_view query)
{
    return textMatches(s.name + " " + s.description + " " + sectionName(s.section) + " " + s.words, query);
}

std::vector<std::pair<std::size_t, std::size_t>> matchRanges(std::string_view text, std::string_view query)
{
    const auto haystack = lower(text);
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& w : words(query))
        for (auto at = haystack.find(w); at != std::string::npos; at = haystack.find(w, at + w.size()))
            out.emplace_back(at, w.size());
    std::sort(out.begin(), out.end());
    // Overlaps merge into one.
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    for (const auto& [start, length] : out)
        if (!merged.empty() && start <= merged.back().first + merged.back().second)
            merged.back().second = std::max(merged.back().second, start + length - merged.back().first);
        else
            merged.emplace_back(start, length);
    return merged;
}
} // namespace fd::model
