// Settings in the engine (M3 C1, ADR 0045, docs/design/settings.md): the recording format, MIDI
// inputs, plugin folders and skipped plugins, export defaults, and applying a whole settings file.

#include "engine/SessionImpl.h"

namespace fd::engine
{
namespace
{
juce::AudioFormat* formatFor(te::Engine& engine, model::RecordingFormat format)
{
    auto& afm = engine.getAudioFileFormatManager();
    switch (format)
    {
    case model::RecordingFormat::Wav:
        return afm.getWavFormat();
    case model::RecordingFormat::Flac:
        return afm.getFlacFormat();
    case model::RecordingFormat::Aiff:
        return afm.getAiffFormat();
    case model::RecordingFormat::Mp3: // never a recording's: WAV
    case model::RecordingFormat::Ogg:
    case model::RecordingFormat::Opus:
        break;
    }
    return afm.getWavFormat();
}

/// The computer keyboard and other inputs Segno makes itself aren't connected devices.
bool isConnected(te::MidiInputDevice& d)
{
    return dynamic_cast<te::VirtualMidiInputDevice*>(&d) == nullptr;
}
} // namespace

bool Session::preferAudioServerOnFirstLaunch()
{
#if JUCE_LINUX
    if (impl_->engine.getPropertyStorage().getXmlProperty(te::SettingID::audio_device_setup) != nullptr)
        return false; // chosen before
    auto& dm = deviceManager();
    for (auto* type : dm.getAvailableDeviceTypes())
        if (type->getTypeName() == "JACK")
        {
            type->scanForDevices();
            if (type->getDeviceNames(false).isEmpty())
                return false; // no server runs
            const auto before = dm.getCurrentAudioDeviceType();
            dm.setCurrentAudioDeviceType("JACK", true);
            if (dm.getCurrentAudioDevice() != nullptr)
                return true;
            dm.setCurrentAudioDeviceType(before, true);
            return false;
        }
#endif
    return false;
}

void Session::setRecordingFormat(model::RecordingFormat format, int bitDepth)
{
    const auto depths = model::bitDepthsFor(format);
    if (std::find(depths.begin(), depths.end(), bitDepth) == depths.end())
        bitDepth = 24;
    const auto name = formatFor(impl_->engine, format)->getFormatName();
    for (auto* d : impl_->engine.getDeviceManager().getWaveInputDevices())
    {
        d->setOutputFormat(name);
        d->setBitDepth(bitDepth);
    }
}

std::pair<model::RecordingFormat, int> Session::recordingFormat() const
{
    const auto devices = impl_->engine.getDeviceManager().getWaveInputDevices();
    if (devices.empty())
        return {model::RecordingFormat::Wav, 24};
    auto* d = devices.front();
    for (const auto f :
         {model::RecordingFormat::Wav, model::RecordingFormat::Flac, model::RecordingFormat::Aiff})
        if (formatFor(impl_->engine, f)->getFormatName() == d->getOutputFormat())
            return {f, d->getBitDepth()};
    return {model::RecordingFormat::Wav, d->getBitDepth()};
}

std::vector<Session::MidiDevice> Session::midiDevices() const
{
    std::vector<MidiDevice> out;
    for (auto& d : impl_->engine.getDeviceManager().getMidiInDevices())
        if (isConnected(*d))
            out.push_back({d->getName().toStdString(), d->isEnabled()});
    return out;
}

void Session::setMidiDeviceEnabled(const std::string& name, bool enabled)
{
    for (auto& d : impl_->engine.getDeviceManager().getMidiInDevices())
        if (isConnected(*d) && d->getName().toStdString() == name && d->isEnabled() != enabled)
            d->setEnabled(enabled);
}

void Session::setPluginFolders(std::vector<std::string> folders)
{
    impl_->pluginFolders = std::move(folders);
}

const std::vector<std::string>& Session::pluginFolders() const
{
    return impl_->pluginFolders;
}

std::vector<std::string> Session::skippedPlugins() const
{
    std::vector<std::string> out;
    for (const auto& f : impl_->engine.getPluginManager().knownPluginList.getBlacklistedFiles())
        out.push_back(f.toStdString());
    return out;
}

void Session::retrySkippedPlugin(const std::string& file)
{
    impl_->engine.getPluginManager().knownPluginList.removeFromBlacklist(
        juce::String::fromUTF8(file.c_str()));
}

void Session::setExportDefaults(const model::ExportSettings& defaults)
{
    impl_->exportDefaults = defaults;
    impl_->exportDefaults.folder.clear(); // the project's own
    impl_->exportDefaults.name.clear();
}

void applySettings(Session& session, const model::AppSettings& settings)
{
    session.setRecordingFormat(settings.recording.format, settings.recording.bitDepth);
    session.setWarpDefaults(settings.audioClips.loopsFollowTempo, settings.audioClips.recordingsFollowTempo,
                            settings.audioClips.loopsFollowKey);
    // The Segno folder: its Unsaved and Presets (by default Documents/Segno; the presets' default
    // also honours SEGNO_PRESETS_FOLDER, the tests' own).
    const bool moved = !settings.library.folder.empty();
    const auto segno =
        moved ? juce::File(juce::String::fromUTF8(settings.library.folder.c_str())) : defaultSegnoFolder();
    session.setUnsavedRoot(segno.getChildFile("Unsaved"));
    const auto presets = moved ? segno.getChildFile("Presets") : ui::defaultPresetsFolder();
    if (presets.getFullPathName().toStdString() != session.presetLibrary().root().string())
        session.setPresetsFolder(presets);
    session.setPluginFolders(settings.plugins.folders);
    session.setExportDefaults(settings.exportDefaults);
    // The browser (ADR 0046): its places and the previews' volume; the Library first (ADR 0055).
    session.setLibraryFolder(settings.library.location.empty()
                                 ? juce::File()
                                 : juce::File(juce::String::fromUTF8(settings.library.location.c_str())));
    session.setSegnoFolder(segno);
    session.setBrowserPlaces(settings.browser.places);
    session.setPreviewVolume(static_cast<float>(settings.browser.previewVolume));
    // Controllers (midi-learn.md §6): takeover, values sent back, the transport's mappings.
    session.setMidiTakeover(settings.controllers.takeover);
    session.setMidiSendBack(settings.controllers.sendBack);
    if (session.appMidiMappings() != settings.controllers.transport)
        session.setAppMidiMappings(settings.controllers.transport);
}

juce::File defaultSegnoFolder()
{
    if (const auto custom = juce::SystemStats::getEnvironmentVariable("SEGNO_PRESETS_FOLDER", {});
        custom.isNotEmpty())
        return juce::File(custom).getParentDirectory();
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Segno");
}
} // namespace fd::engine
