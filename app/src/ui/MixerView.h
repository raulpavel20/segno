#pragma once

#include "model/ChannelStripLayout.h"
#include "ui/ChannelControls.h"

#include <map>

namespace fd::app
{
/// One channel as a strip (docs/design/mixer.md §6): its name, its modules, its sends, pan, mute
/// and solo, the peak readout, the fader with a dB scale and the meter, and the volume typed.
/// Placed by model::layoutChannelStrip, so strips line up.
class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip(engine::Session& session, const model::TrackInfo& channel);

    [[nodiscard]] std::uint64_t channel() const noexcept { return info_.id; }
    /// The channel as the snapshot has it, the project's return tracks, how strips look, and
    /// whether it is the selected one.
    void update(const model::TrackInfo& channel, const std::vector<model::TrackInfo>& returns,
                const model::ChannelStripOptions& options, bool selected);

    std::function<void()> onSelect;
    std::function<void(std::uint64_t module)> onSelectModule;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    struct Send
    {
        std::unique_ptr<SendBar> level;
        std::unique_ptr<ChannelToggle> pre;
    };
    [[nodiscard]] juce::Rectangle<int> at(const model::HeaderRect& r) const;
    void tick(); // the peak readout follows the meter

    engine::Session& session_;
    model::TrackInfo info_;
    model::ChannelStripOptions options_;
    bool selected_ = false;
    model::ChannelStripSlots slots_;
    VolumeBar fader_;
    PanBar pan_;
    ChannelToggle mute_, solo_;
    ChannelMeter meter_;
    VolumeField value_;
    std::vector<Send> sends_;
    std::vector<std::uint64_t> sendReturns_;
    float shownPeak_ = -1.0f;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};

/// The mixer view (C3c, ADR 0026): every channel as a strip, side by side, in place of the
/// arrangement. Tracks, then the return tracks after a divider, scroll horizontally; the master
/// is pinned at the right. The header's toggles show or hide module names and sends and make
/// strips narrow; they are view state (saved with the project, never undo steps).
class MixerView final : public juce::Component
{
public:
    explicit MixerView(engine::Session& session);

    void setSnapshot(std::shared_ptr<const model::Snapshot> snapshot);
    void setSelection(std::optional<std::uint64_t> channel);
    /// Drops every strip (before the project is replaced: they hold the engine's meters).
    void clear();

    /// A strip was clicked: select that channel (and, for a module name, that module).
    std::function<void(std::uint64_t channel)> onSelect;
    std::function<void(std::uint64_t channel, std::uint64_t module)> onSelectModule;
    /// The view's options changed (the shell saves them with its view state).
    std::function<void()> onViewChanged;

    [[nodiscard]] const model::ChannelStripOptions& options() const noexcept { return options_; }
    void setOptions(bool modules, bool sends, bool narrow);
    [[nodiscard]] int scrollX() const;
    void setScrollX(int x);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Content : juce::Component
    {
        int dividerX = -1; // before the first return
        void paint(juce::Graphics& g) override;
    };
    void rebuild();
    void layoutStrips();
    [[nodiscard]] std::vector<std::uint64_t> order() const;

    engine::Session& session_;
    std::shared_ptr<const model::Snapshot> snapshot_;
    std::optional<std::uint64_t> selected_;
    model::ChannelStripOptions options_;
    ui::Toggle modules_{"Modules"}, sends_{"Sends"}, narrow_{"Narrow"};
    juce::Viewport viewport_;
    Content content_;
    std::map<std::uint64_t, std::unique_ptr<ChannelStrip>> strips_;
    std::unique_ptr<ChannelStrip> master_;
};
} // namespace fd::app
