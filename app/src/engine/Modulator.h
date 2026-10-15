#pragma once

#include "model/Modulators.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <fd/dsp/Modulators.h>
#include <memory>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::engine
{
namespace te = tracktion;

class SpecParameter;

/// A modulator (docs/design/modulators.md, ADR 0061): a tracktion modifier of Segno's own type
/// (FDMODULATOR, through the custom-modifiers patch) on a track or in a rack, moving parameters on
/// its track around where they're set. Its parameters are model::modulatorInfo's, kept in its
/// state as a module's are (by id, normalised).
///
/// Its values come from fd::dsp::mod, exact to the time a parameter asks for (getValueFor), each
/// route a copy of its own with Per Target (§8): the LFO, Random and Steps are functions of the
/// song's time and the roll; the Envelope's firings and the Follower's level are taken in the
/// graph where it sits in its chain (ProcessingPosition::beforePlugin). Real-time safe: what the
/// audio thread reads is in atomics; nothing allocates there.
class Modulator final : public te::Modifier, private juce::ValueTree::Listener
{
public:
    /// The state's type: one for every kind (its `fdType` property says which).
    static const juce::Identifier stateType;
    /// Makes tracktion know the type (ModifierList's hooks). Once, at start-up; again is harmless.
    static void registerType();
    /// A new modulator's state, its parameters at their defaults.
    [[nodiscard]] static juce::ValueTree createState(model::ModulatorKind kind);
    /// The kind a state is, if it's a modulator's.
    [[nodiscard]] static std::optional<model::ModulatorKind> kindOf(const juce::ValueTree& v);
    /// The most copies it keeps (Per Target), and the most steps.
    static constexpr int maxCopies = 64;

    Modulator(te::Edit& edit, const juce::ValueTree& state);
    ~Modulator() override;

    [[nodiscard]] model::ModulatorKind kind() const noexcept { return kind_; }
    [[nodiscard]] const DeviceInfo& info() const noexcept { return model::modulatorInfo(kind_); }
    [[nodiscard]] te::AutomatableParameter& parameter(size_t index);
    [[nodiscard]] size_t parameterCount() const noexcept { return params_.size(); }
    /// A parameter's plain value, as the panel shows it.
    [[nodiscard]] float plain(size_t index) const;
    /// After undo or redo: each parameter takes its stored value again (as DevicePlugin's).
    void followStoredValues();

    /// Where it's processed in its chain: just before `plugin`, or after the chain (invalid).
    [[nodiscard]] te::EditItemID place() const;
    void setPlace(te::EditItemID plugin, juce::UndoManager* um);

    /// How many routes it has: its copies with Per Target (the LFO spreads its phases over them).
    void setRouteCount(int routes) noexcept;
    [[nodiscard]] int routeCount() const noexcept { return routes_.load(std::memory_order_relaxed); }

    // ---- rolls (ADR 0062) ---------------------------------------------------------------------
    /// A new pass (playback starting, a render): fresh dice, unless it keeps a roll.
    void startPass(std::uint64_t roll) noexcept;
    /// The roll it plays now: the kept one, or this pass's.
    [[nodiscard]] std::uint64_t roll() const noexcept;
    [[nodiscard]] bool kept() const;
    /// Keep holds the roll playing now (and the lap the loop is on); off lets it roll again.
    void setKept(bool keep, juce::UndoManager* um);

    // ---- Steps' pattern -----------------------------------------------------------------------
    [[nodiscard]] std::vector<float> stepValues() const;
    [[nodiscard]] std::vector<float> stepChances() const;
    /// Step `step`'s value (-1 ... +1) and chance (0..1).
    void setStep(int step, float value, float chance, juce::UndoManager* um);

    // ---- what the panel shows (message thread) -------------------------------------------------
    /// Where it is now: cycles (LFO), steps (Random, Steps), ms since copy 0 fired (Envelope).
    [[nodiscard]] double position() const noexcept { return position_.load(std::memory_order_relaxed); }
    /// The Envelope: each copy's ms since it fired, -1 while it isn't playing.
    [[nodiscard]] std::vector<double> playing() const;
    /// The Follower: the levels it heard, 0..1, oldest first, the last `seconds`.
    [[nodiscard]] std::vector<float> heard(double seconds) const;

    // ---- te::Modifier -------------------------------------------------------------------------
    using te::Modifier::initialise;
    void initialise() override;
    juce::String getName() const override;
    juce::String getSelectableDescription() override { return getName(); }
    float getCurrentValue() override;
    float getValueFor(const te::AutomatableParameter::ModifierAssignment& assignment,
                      te::TimePosition time) override;
    te::AutomatableParameter::ModifierAssignment* createAssignment(const juce::ValueTree& v) override;
    ProcessingPosition getProcessingPosition() override { return ProcessingPosition::beforePlugin; }
    te::EditItemID getPluginToProcessBefore() override;
    juce::StringArray getAudioInputNames() override { return {"Left", "Right"}; }
    juce::StringArray getMidiInputNames() override { return {"MIDI"}; }
    void initialise(double sampleRate, int blockSize) override;
    void applyToBuffer(const te::PluginRenderContext& context) override;

    /// One of its routes: which copy it moves with Per Target (`fdCopy` on the route).
    struct Assignment final : te::AutomatableParameter::ModifierAssignment
    {
        Assignment(const juce::ValueTree& v, const Modulator& m);
        bool isForModifierSource(const ModifierSource& source) const override;
        const te::EditItemID modulator;
        std::atomic<int> copy{0};
    };
    /// A route's copy, as kept on it.
    static const juce::Identifier copyId;

private:
    struct Timer;
    friend struct Timer;

    /// Its output for `copy` at edit time `seconds`, in route units: both ways -0.5 ... +0.5 a
    /// unit of amount, one way 0 ... 1; Depth in it. [[audio thread]]
    [[nodiscard]] float output(int copy, double seconds) const noexcept;
    /// Its own signal for `copy`: -1 ... +1 (LFO, Random, Steps) or 0..1 (Envelope, Follower).
    [[nodiscard]] double signal(int copy, double seconds) const noexcept;
    [[nodiscard]] double beatsAt(double seconds) const noexcept;
    /// The LFO's cycles at `seconds`: since playback started (Free), since its last note
    /// (Retrigger; Envelope, once), locked to the song while synced; before Phase and Per Target.
    [[nodiscard]] double lfoCycles(double seconds) const noexcept;
    [[nodiscard]] double stepsAt(double seconds, size_t rateParam, size_t syncParam, double originSeconds,
                                 double originBeats) const noexcept;
    [[nodiscard]] std::uint64_t seed() const noexcept;
    [[nodiscard]] float value(size_t index) const noexcept; // plain, from the live parameter
    [[nodiscard]] dsp::mod::EnvelopeShape envelopeShape() const noexcept;
    void fire(double seconds) noexcept;
    void release(double seconds) noexcept;
    void tick(te::TimePosition editTime, int numSamples) noexcept;
    void refreshFromState();
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id) override;

    model::ModulatorKind kind_;
    std::vector<SpecParameter*> params_;
    std::vector<std::unique_ptr<juce::CachedValue<float>>> values_;
    std::unique_ptr<Timer> timer_;

    // The song's clock, published at each block's start (the timer) for the block's parameters.
    std::atomic<double> blockTime_{0.0}, blockBeats_{0.0}, beatsPerSecond_{2.0}, beatsPerBar_{4.0};
    std::atomic<double> sampleRate_{48000.0};
    // Playback: where this pass started, the free clock while stopped, the loop's laps.
    std::atomic<bool> playing_{false};
    std::atomic<double> passStart_{0.0}, stoppedClock_{0.0}, lastTime_{0.0};
    std::atomic<std::uint64_t> roll_{1}, keptRoll_{0};
    std::atomic<int> lap_{0}, keptLap_{0};
    std::atomic<bool> kept_{false};
    std::atomic<int> routes_{1};
    // Restarts (Retrigger, Steps' Restart), Steps' advances, Freeze.
    std::atomic<double> restartTime_{0.0}, restartBeats_{0.0};
    std::atomic<std::int64_t> advances_{0};
    std::atomic<double> frozenAt_{-1.0};
    bool fireWas_ = false;
    int notesHeld_ = 0;
    // The Envelope: each copy's firing (edit seconds; -1 none) and release (-1 while held).
    std::array<std::atomic<double>, maxCopies> fired_{}, released_{};
    std::atomic<std::int64_t> firings_{0};
    // Steps' pattern, readable on the audio thread.
    std::array<std::atomic<float>, model::modulator::Steps::maxSteps> steps_{}, chances_{};
    // The Follower: its level, filters and follower state (audio thread).
    std::atomic<float> level_{0.0f};
    double followed_ = 0.0;
    juce::IIRFilter lowCut_[2], highCut_[2];
    float lastLow_ = -1.0f, lastHigh_ = -1.0f;
    // What the panel reads.
    std::atomic<double> position_{0.0};
};
} // namespace fd::engine
