#pragma once

#include <fd/device/AssetHandoff.h>
#include <fd/device/Device.h>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

/// DAW adapter (docs/architecture.md): one tracktion_engine plugin type that wraps any
/// fd::Device, so every device runs natively in the DAW, in the docked device chain.
///
/// Parameters become te::AutomatableParameters (automation, modifiers and macros for free) in
/// normalised units, the control's travel, as plugin hosts automate them: a straight automation
/// segment is a straight move of the knob (ADR 0028). They are converted to plain values for the
/// device's ParamStore before every block. All tracktion API use stays in app/src/engine
/// (ADR 0004).
class DevicePlugin final : public te::Plugin
{
public:
    /// Plugin type name for a device: "fd.<device-id>".
    static juce::String typeNameFor(std::string_view deviceId);
    /// The plugin state's property holding a parameter's value: its id, unless that's a property
    /// tracktion keeps on every plugin (the Filter's `type`, which names the plugin), then
    /// "fdParam.<id>". Never the same as a property tracktion reads, so a value can't overwrite one.
    [[nodiscard]] static juce::Identifier propertyFor(std::string_view paramId);
    /// The parameter a property holds: propertyFor the other way.
    [[nodiscard]] static juce::String paramIdOf(const juce::Identifier& property);

    DevicePlugin(te::PluginCreationInfo info, const DeviceFactory& factory);
    ~DevicePlugin() override;

    juce::String getName() const override { return name_; }
    juce::String getPluginType() override { return type_; }
    juce::String getSelectableDescription() override { return name_; }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& context) override;

    BusLayout getBusses() const override;
    /// A device with a sidechain bus lists its channels after the main ones, which is how
    /// tracktion routes a sidechain source to it (ADR 0025).
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override;
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool isSynth() override { return isInstrument(); }
    bool takesMidiInput() override { return device_->info().buses.acceptsMidi; }
    bool producesAudioWhenNoAudioInput() override { return isInstrument(); }
    /// A MIDI effect (ADR 0074): it changes the notes and leaves the track's audio alone.
    bool isNoteEffect() override { return isMidiEffect(); }
    /// The track a MIDI effect listens to (midi-effects.md §5.3): its notes reach the module apart
    /// from the track's own, from before that track's instrument (the note-sources patch).
    te::EditItemID getNoteSourceID() const override;
    void setSourceNotes(const te::MidiMessageArray& notes, te::TimeRange block) override;
    double getLatencySeconds() override;
    /// The rate it runs at; 0 before the engine has prepared it.
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    double getTailLength() const override { return device_->tailSeconds(); }

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;
    void flushPluginStateToValueTree() override;
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id) override;

    /// UI edit of non-parameter state (ADR 0017): recorded in the plugin's state with the Edit's
    /// undo manager; the device follows the state (including undo and redo).
    void applyExtraState(const nlohmann::json& patch);

    [[nodiscard]] Device& device() noexcept { return *device_; }
    /// Sets a parameter's stored value (normalised) at once, in the current undo step, and the
    /// parameter follows: a whole state set together (presets, ADR 0044). setParameter writes
    /// the stored value later, after the step has closed.
    void setStoredValue(size_t index, float normalised);
    /// After undo or redo: each parameter takes its stored value again. tracktion restores the
    /// stored values but leaves the parameters where they were (it doesn't follow property
    /// changes, which can come from automation); our modules follow their projects.
    void followStoredValues();
    /// A parameter's stored value (normalised): what the project keeps, under any automation.
    [[nodiscard]] float storedValue(size_t index) const { return values_[index]->get(); }
    [[nodiscard]] te::AutomatableParameter& parameter(size_t index) const noexcept { return *params_[index]; }
    /// The song's key as it hands it to its module (TransportInfo::key, ADR 0057).
    [[nodiscard]] KeyInfo songKey() const noexcept;

    [[nodiscard]] bool isMidiEffect() const noexcept
    {
        return device_->info().kind == DeviceKind::MidiEffect;
    }
    /// The song as a MIDI effect reads it (midi-effects.md §5): handed over whole whenever it
    /// changes (message thread); the module takes it at its next block.
    /// Where a MIDI effect keeps the track it listens to (midi-effects.md §5.3).
    static inline const juce::Identifier listenId{"fdListen"};
    /// The song view last handed over (message thread): what its display names along the top.
    [[nodiscard]] std::shared_ptr<const SongView> songView() const noexcept { return sentSong_; }
    void setSongView(std::shared_ptr<const SongView> view)
    {
        if (view == sentSong_)
            return; // it has this one
        sentSong_ = view;
        songView_.publish(std::move(view));
    }
    /// Listens to `track`'s notes, or to none (an invalid id); kept in the plugin's state, one undo
    /// step with `um`. Playback is rebuilt so the notes reach it.
    void setListenTo(te::EditItemID track, juce::UndoManager* um);

private:
    [[nodiscard]] bool isInstrument() const noexcept { return device_->info().kind == DeviceKind::Instrument; }
    void convertMidi(const te::PluginRenderContext& fc) noexcept;
    /// A MIDI effect's block: its notes replace the ones it was given (ADR 0074).
    void processNotes(const te::PluginRenderContext& fc) noexcept;
    void applyExtraFromState(const juce::String& json);

    std::unique_ptr<Device> device_;
    juce::String name_, type_;
    std::vector<te::AutomatableParameter::Ptr> params_;
    std::vector<std::unique_ptr<juce::CachedValue<float>>> values_; // normalised
    std::vector<float> pulled_; // the normalised values last pulled into the ParamStore (audio thread)
    std::vector<float*> channelPtrs_, sidechainPtrs_;
    EventList events_, outEvents_;
    /// The listened track's notes for the block, with their time in the edit (seconds), from
    /// setSourceNotes; handed to the module as each piece of the block is processed.
    struct HeardNote
    {
        double time = 0.0;
        Event event;
    };
    std::vector<HeardNote> heard_; // its capacity, reserved in initialise
    std::size_t heardCount_ = 0;
    EventList listen_;
    AssetHandoff<SongView> songView_;
    std::shared_ptr<const SongView> sentSong_; // the last handed over (message thread)
    AssetHandoff<SongView>::Ref song_;
    std::atomic<bool> listening_{false}; // whether it listens to a track: the audio thread's copy
    bool wasPlaying_ = false;            // audio thread: whether the song played in the last block
    te::MPESourceID midiSourceID_ = te::createUniqueMPESourceID();
    juce::AudioBuffer<float> instrumentOut_; // instruments render here, then add (ADR 0019)
    te::tempo::Sequence::Position tempoPos_{te::createPosition(edit.tempoSequence)};
    /// The song's key for the audio thread (docs/design/key-and-scale.md §15): packed
    /// (SongKey.h), kept up to date from the project's pitch sequence on the message thread.
    struct KeyWatch;
    std::atomic<std::uint32_t> songKey_{0};
    std::unique_ptr<KeyWatch> keyWatch_;
    double sampleRate_ = 48000.0;
    juce::String appliedExtra_; // the extra-state JSON the device currently reflects

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DevicePlugin)
};

/// Registers every device in fd::allDevices() as a built-in plugin type.
void registerDevices(te::Engine& engine);
/// Registers one device as a built-in plugin type (tests: a device of their own).
void registerDevice(te::Engine& engine, const DeviceFactory& factory);

/// Whether any of the track's modulators move `param` (modulators.md).
[[nodiscard]] bool movedByModulators(const te::AutomatableParameter& param);

/// UI binding for a native device: edits go through AutomatableParameter (automation
/// recording and undo work).
class DevicePluginBinding : public ui::ParamBinding
{
public:
    explicit DevicePluginBinding(DevicePlugin& plugin) : plugin_(plugin) {}

    /// Its value: what a macro or automation sets it to; under modulators, its own (their ring
    /// and dot show what they do, modulators.md §7.3).
    float getPlain(size_t i) const override
    {
        auto& p = plugin_.parameter(i);
        return spec(i).fromNormalized(movedByModulators(p) ? p.getCurrentBaseValue() : p.getCurrentValue());
    }
    void setPlain(size_t i, float plain) override
    {
        plugin_.parameter(i).setParameter(spec(i).toNormalized(plain), juce::sendNotification);
    }
    void beginGesture(size_t i) override { plugin_.parameter(i).parameterChangeGestureBegin(); }
    void endGesture(size_t i) override { plugin_.parameter(i).parameterChangeGestureEnd(); }
    void applyExtraState(const nlohmann::json& patch) override { plugin_.applyExtraState(patch); }

    /// A lane plays (On), is off (Off), or the parameter has no automation (ADR 0028).
    [[nodiscard]] ui::AutomationState automationState(size_t i) const override
    {
        auto& curve = plugin_.parameter(i).getCurve();
        if (curve.getNumPoints() == 0)
            return ui::AutomationState::None;
        return curve.bypass.get() ? ui::AutomationState::Off : ui::AutomationState::On;
    }
    [[nodiscard]] bool canAutomate(size_t i) const override
    {
        const auto& s = spec(i);
        return s.hasFlag(ParamFlags::Automatable) && !s.hasFlag(ParamFlags::ChangesLatency) && !s.hasFlag(ParamFlags::Hidden);
    }

protected:
    [[nodiscard]] const ParamSpec& spec(size_t i) const { return plugin_.device().info().params[i]; }

    DevicePlugin& plugin_;
};
} // namespace fd::engine
