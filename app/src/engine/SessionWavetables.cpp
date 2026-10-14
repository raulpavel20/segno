// The Wavetable's tables in the browser (docs/design/table-editor.md §7): heard on a held note,
// loaded onto an oscillator.

#include "TableFiles.h"
#include "WavetableSynth.h"
#include "engine/PresetPreview.h"
#include "engine/SessionImpl.h"

#include <fd/device/Registry.h>
#include <fd/device/State.h>

namespace fd::engine
{
namespace
{
using W = devices::WavetableSynth;

/// The table in `file`, as the oscillator's state patch; nothing (and why) if it isn't one.
std::optional<nlohmann::json> tablePatch(int oscillator, const juce::File& file, std::string* error)
{
    const auto read = devices::wavetable::importTable(file.getFullPathName().toStdString(), 0, error);
    if (!read)
        return std::nullopt;
    const auto table =
        dsp::Wavetable::fromCycles(read->samples.data(), read->frameLength, read->frames, true);
    return W::userTablePatch(
        oscillator, read->name.empty() ? file.getFileNameWithoutExtension().toStdString() : read->name,
        table);
}
} // namespace

bool Session::previewWavetable(const juce::File& file)
{
    stopPreview();
    const auto* factory = findDevice("wavetable");
    const auto patch = tablePatch(0, file, nullptr);
    if (factory == nullptr || !patch)
        return false;
    // A Wavetable of its own at its defaults, the table on its first oscillator: a held C3.
    auto state = captureState(*factory->create());
    state.extra.merge_patch(*patch);
    const double rate = sampleRate() > 0.0 ? sampleRate() : 48000.0;
    auto sound = renderPresetPhrase(factory->info(), state, {}, rate);
    if (sound.getNumSamples() == 0)
        return false;
    impl_->preview.play(std::move(sound), rate);
    return true;
}

bool Session::loadWavetable(std::uint64_t deviceId, int oscillator, const juce::File& file,
                            std::string* error)
{
    auto handle = openDevice(deviceId);
    if (handle == nullptr || handle->deviceId() != "wavetable")
    {
        if (error != nullptr)
            *error = "It isn't a Wavetable.";
        return false;
    }
    const auto patch = tablePatch(std::clamp(oscillator, 0, W::numOscs - 1), file, error);
    if (!patch)
        return false;
    const EditGroup group(*this, "Load " + file.getFileNameWithoutExtension().toStdString());
    handle->context().params.applyExtraState(*patch);
    return true;
}
} // namespace fd::engine
