#pragma once

#include "model/Snapshot.h"

#include <array>
#include <bitset>
#include <fd_device_ui/fd_device_ui.h>

namespace fd::app
{
/// A Drum Kit's panel in the chain (docs/design/drum-kit.md §8, ADR 0064), on the module grid like
/// any module's:
///   - PADS: the 16 pads shown (ui::PadGrid) and, beside them, all 128 (ui::PadOverview);
///   - PAD: the selected pad's settings: on (the title's power), Vol, Pan, Plays, Choke and Solo;
///     dimmed while the pad is empty, which has none;
///   - MACROS, from the first mapping (§10): the kit's eight macros, as a rack's.
/// The macros are the kit's own parameters (engine::Session::drumKit()), set through the host's
/// binding as a module's are; the pads are the host's, edited through the callbacks below.
class DrumKitPanel final : public ui::DevicePanel
{
public:
    explicit DrumKitPanel(ui::DeviceUiContext& context);
    ~DrumKitPanel() override;

    /// What it shows: the kit's pads (its chains), the selected pad and the 16 shown, its macros.
    void setKit(const model::RackInfo& kit);
    /// The notes sounding now: their pads light.
    void setPlaying(const std::bitset<128>& notes);

    [[nodiscard]] ui::PadGrid& pads() noexcept { return pads_; }
    [[nodiscard]] ui::PadOverview& overview() noexcept { return overview_; }
    [[nodiscard]] ui::PanelSection& padSection() const noexcept { return *padSection_; }

    // The pads' edits, recorded by the host (each one undo step; showing and selecting are view
    // state, playing a pad is neither).
    std::function<void(int note)> onPlayPad, onStopPad;
    std::function<void(int note)> onSelectPad;
    std::function<void(int from)> onShowPads;
    std::function<void(int note)> onPadMenu; // its right-click
    std::function<void(int from, int to, bool copy)> onMovePad;
    std::function<void(int note, const juce::String& name)> onRenamePad;
    std::function<void(int note, bool on)> onPadOn;
    std::function<void(int note, bool solo)> onPadSolo;
    std::function<void(int note, float volumeDb)> onPadVolume;
    std::function<void(int note, float pan)> onPadPan;
    std::function<void(int note, int plays)> onPadPlays;
    std::function<void(int note, int group)> onPadChoke;
    /// A drag of Vol, Pan or Plays begins (true) and ends: one undo step between.
    std::function<void(bool started)> onPadGesture;
    /// Renames a pad in place, on the pad.
    void renamePad(int note);

private:
    void updatePads();
    void updatePad();
    void updateMacros();
    [[nodiscard]] const model::RackChain* selectedPad() const;

    ui::PadGrid pads_;
    ui::PadOverview overview_;
    ui::PanelSection* padSection_ = nullptr;
    ui::Knob volume_, pan_, plays_;
    ui::ChoiceBox choke_;
    ui::Toggle solo_{"Solo"};
    ui::PanelSection* macrosSection_ = nullptr;
    std::array<ui::ParamKnob*, model::RackInfo::macroCount> macros_{};
    ui::NameEditor nameEditor_{*this};
    model::RackInfo kit_;
    bool padGesture_ = false; // Vol, Pan or Plays being dragged: not set from the snapshot meanwhile
};
} // namespace fd::app
