#include "engine/SongKey.h"

#include <fd/music/Scales.h>

namespace fd::engine
{
namespace
{
const juce::Identifier scaleId{"segnoScale"};

/// The pitch sequence's first setting, by start.
juce::ValueTree firstSetting(const juce::ValueTree& sequence)
{
    juce::ValueTree first;
    double firstBeat = 0.0;
    for (const auto& child : sequence)
        if (child.hasType(te::IDs::PITCH))
        {
            const double beat = child.getProperty(te::IDs::startBeat, 0.0);
            if (!first.isValid() || beat < firstBeat)
            {
                first = child;
                firstBeat = beat;
            }
        }
    return first;
}

/// tracktion's nearest scale, for its own features.
te::Scale::ScaleType nearestScale(const music::Key& key)
{
    const std::string& id = key.scale;
    if (id == "major")
        return te::Scale::major;
    if (id == "minor")
        return te::Scale::minor;
    if (id == "dorian")
        return te::Scale::dorian;
    if (id == "phrygian")
        return te::Scale::phrygian;
    if (id == "lydian")
        return te::Scale::lydian;
    if (id == "mixolydian")
        return te::Scale::mixolydian;
    if (id == "locrian")
        return te::Scale::locrian;
    if (id == "melodic-minor")
        return te::Scale::melodicMinor;
    if (id == "harmonic-minor")
        return te::Scale::harmonicMinor;
    const auto* scale = music::scaleOf(key);
    const bool minor = scale != nullptr && (scale->intervals & (1u << 3)) != 0 && (scale->intervals & (1u << 4)) == 0;
    return minor ? te::Scale::minor : te::Scale::major;
}
} // namespace

juce::ValueTree songKeyState(const te::Edit& edit) { return edit.state.getChildWithName(te::IDs::PITCHSEQUENCE); }

std::optional<music::Key> songKeyOf(const te::Edit& edit)
{
    const auto setting = firstSetting(songKeyState(edit));
    if (!setting.isValid() || !setting.hasProperty(scaleId))
        return std::nullopt;
    const int pitch = setting.getProperty(te::IDs::pitch, 60);
    music::Key key{((pitch % 12) + 12) % 12, setting.getProperty(scaleId).toString().toStdString()};
    // A scale from a newer build (docs/ids.md): no key here, and the project keeps what it holds.
    if (music::scaleOf(key) == nullptr)
        return std::nullopt;
    return key;
}

void writeSongKey(te::Edit& edit, const std::optional<music::Key>& key)
{
    auto* setting = edit.pitchSequence.getPitch(0);
    if (setting == nullptr)
        setting = edit.pitchSequence.insertPitch(te::BeatPosition(), 60).get();
    if (setting == nullptr)
        return;
    auto* um = &edit.getUndoManager();
    if (!key)
    {
        setting->state.removeProperty(scaleId, um);
        return;
    }
    // The root in the octave above middle C, as tracktion's own default (60) is.
    setting->setPitch(60 + ((key->root % 12) + 12) % 12);
    setting->accidentalsSharp = !music::usesFlats(*key);
    setting->setScaleID(nearestScale(*key));
    setting->state.setProperty(scaleId, juce::String(key->scale), um);
}

KeyInfo keyInfoOf(const std::optional<music::Key>& key) noexcept
{
    if (!key || music::scaleOf(*key) == nullptr)
        return {};
    return {((key->root % 12) + 12) % 12, music::notes(*key)};
}
} // namespace fd::engine
