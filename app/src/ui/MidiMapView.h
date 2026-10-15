#pragma once

#include "engine/Session.h"
#include "model/MidiMap.h"

#include <fd_ui/fd_ui.h>
#include <functional>
#include <optional>
#include <vector>

namespace fd::app
{
/// Marks a control as a MIDI learn target (docs/design/midi-learn.md §7), for map mode, Learn MIDI
/// and its tooltip: the mixer's volume, pan and sends, mute, solo and arm, the transport's buttons.
/// A module's controls are found through their chain instead (DeviceChain::targetOf).
void setMidiTarget(juce::Component& component, const model::MidiTarget& target);
[[nodiscard]] std::optional<model::MidiTarget> midiTargetMark(const juce::Component& component);

/// Finds a control's target: its mark, or its module's chain.
using MidiTargetResolver = std::function<std::optional<model::MidiTarget>(const juce::Component&)>;

/// MIDI map mode and learning, drawn over the window (midi-learn.md §3, §4). In map mode every
/// control that can be mapped is outlined in `midiMap`, a mapped one is tinted with its source in
/// `Micro` centred over it, and the chosen one's outline is thicker; a click on a control chooses
/// it (the session learns it). Outside map mode only the control waiting to learn shows, its
/// outline pulsing. Clicks anywhere else go through: the views still scroll and select.
class MidiMapOverlay final : public juce::Component
{
public:
    MidiMapOverlay(engine::Session& session, juce::Component& root, MidiTargetResolver resolve);
    ~MidiMapOverlay() override;

    void setMapMode(bool on);
    [[nodiscard]] bool mapMode() const noexcept { return mapMode_; }
    /// Finds the controls again and repaints what changed (each frame).
    void update();

    struct Shown
    {
        juce::Component* control = nullptr;
        juce::Rectangle<int> bounds; // in the overlay
        model::MidiTarget target;
    };
    [[nodiscard]] const std::vector<Shown>& shown() const noexcept { return shown_; }
    /// A mapped control's label and where it's drawn, centred on it (empty: none, or it doesn't
    /// fit even short).
    struct Label
    {
        juce::String text;
        juce::Rectangle<int> area;
    };
    [[nodiscard]] Label labelOf(const Shown& shown) const;

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] const Shown* at(juce::Point<int> p) const;
    /// A press anywhere while a control waits outside map mode (the overlay listens to every
    /// press then): elsewhere, it gives up (ML1).
    void pressed(const juce::MouseEvent& e);

    engine::Session& session_;
    juce::Component& root_;
    MidiTargetResolver resolve_;
    bool mapMode_ = false;
    std::vector<Shown> shown_;
    std::optional<model::MidiTarget> waiting_;
    bool listening_ = false; // to every press, while a control waits outside map mode
};

/// "MIDI map mode · click a control, then move one on your device", with Done (§3): a strip above
/// the centre while the mode is on.
class MidiMapBanner final : public juce::Component
{
public:
    MidiMapBanner();
    static constexpr const char* text =
        "MIDI map mode \xc2\xb7 click a control, then move one on your device";
    [[nodiscard]] static int height();
    /// Where its text is drawn (tests).
    [[nodiscard]] juce::Rectangle<int> textArea() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    ui::Toggle done{"Done"};
};

/// The window's tooltips (ui-principles: every control says what it does), a mapped control's
/// adding its mapping ("Mapped to CC 21 (Launchkey)"), the one waiting to learn saying what to do.
class Tooltips final : public juce::TooltipWindow
{
public:
    Tooltips(juce::Component* parent, engine::Session& session, MidiTargetResolver resolve);
    juce::String getTipFor(juce::Component& component) override;

private:
    engine::Session& session_;
    MidiTargetResolver resolve_;
};
} // namespace fd::app
