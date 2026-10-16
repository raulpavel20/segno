#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

/// A Drum Kit pad's inlet (docs/design/drum-kit.md §15, ADR 0064): the first plugin of each pad's
/// chain in the kit's rack, never shown as a module.
///   - It passes its pad's note, rewritten to the note the pad plays (Plays), with its polyphonic
///     pressure; and what every pad gets: pitch bend, channel pressure, controllers. Other notes
///     go no further.
///   - A note of another pad in its choke group becomes All sound off (CC 120), at that note's
///     time, so the pad's instrument stops. Every inlet hears every note, so a choke needs no
///     messages between pads.
///   - It passes no audio: a pad's sound starts from silence (the track's audio goes round the
///     pads, once).
/// The pad's note, Plays and choke group are kept on its state (undone as any edit); which notes
/// choke it follows from the other pads' (updateChokes).
class PadInlet final : public te::Plugin
{
public:
    static const char* xmlTypeName;
    static const juce::Identifier noteId, playsId, chokeId;
    /// Registers the plugin type with the engine (with the devices, DevicePlugin.h).
    static void registerType(te::Engine& e);

    explicit PadInlet(te::PluginCreationInfo info);
    ~PadInlet() override;

    juce::String getName() const override { return "Pad"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return "Pad"; }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer(const te::PluginRenderContext& context) override;

    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    bool takesMidiInput() override { return true; }
    bool takesAudioInput() override { return true; }
    bool producesAudioWhenNoAudioInput() override { return false; }
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool canBeAddedToClip() override { return false; }

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id) override;

    /// The note it answers to (0 … 127), the note it plays into its instrument, and its choke
    /// group (0: none, 1 … 16).
    [[nodiscard]] int note() const { return note_.get(); }
    [[nodiscard]] int plays() const { return plays_.get(); }
    [[nodiscard]] int choke() const { return choke_.get(); }
    void setNote(int note, juce::UndoManager* um);
    void setPlays(int note, juce::UndoManager* um);
    void setChoke(int group, juce::UndoManager* um);

    /// Each inlet in `rack`: the notes that choke it, from the other pads' notes and groups. The
    /// kit calls it after any change to its pads; an inlet calls it when its own note or group
    /// changes (undo included) and when it's prepared.
    static void updateChokes(te::RackType& rack);
    /// Whether a note-on of `note` chokes this pad (the audio thread's view).
    [[nodiscard]] bool chokedBy(int note) const noexcept;

    /// The most messages it filters in a block without allocating: the rest pass as they are.
    static constexpr int capacity = 1024;

private:
    void publish() noexcept;
    void setChokedBy(const std::array<std::uint64_t, 2>& notes) noexcept;

    juce::CachedValue<int> note_, plays_, choke_;
    // What the audio thread reads.
    std::atomic<int> liveNote_{-1}, livePlays_{60};
    std::array<std::atomic<std::uint64_t>, 2> chokedBy_{};
    te::MidiMessageArray kept_; // a block's messages that go on, before they're copied back

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadInlet)
};
} // namespace fd::engine
