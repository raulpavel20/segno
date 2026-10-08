#pragma once

#include "ui/ActionRegistry.h"
#include "ui/ArrangementView.h"
#include "ui/Panels.h"

namespace fd::app
{
/// The window content (docs/design/daw-shell.md, C1): transport bar, browser, arrangement,
/// inspector and detail panel; adaptive layout; every action in the registry; per-frame updates.
class Shell final : public juce::Component
{
public:
    static constexpr int compactWidth = 1440;

    explicit Shell(engine::Session& session);
    ~Shell() override;

    [[nodiscard]] const ActionRegistry& actions() const noexcept { return actions_; }
    /// Shows a warning in the window (engine warnings; never a modal dialog).
    void showWarning(const juce::String& message);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Panel
    {
        bool open = true;
        float shown = 1.0f; // eased towards open
        float size = 0.0f;  // px when fully open
    };

    void registerActions();
    void onFrame(double timestamp);
    void selectionChanged(std::optional<std::uint64_t> track);
    void showAudioSettings();
    void toggle(Panel& p);
    [[nodiscard]] std::optional<model::TrackInfo> selectedTrackInfo() const;

    engine::Session& session_;
    ActionRegistry actions_;
    std::unique_ptr<TransportBar> transport_;
    BrowserPanel browser_{actions_};
    InspectorPanel inspector_;
    DetailPanel detail_;
    ArrangementView arrangement_;
    Splitter browserSplit_, inspectorSplit_;

    Panel browserPanel_{.open = true, .shown = 1.0f, .size = 220.0f};
    Panel inspectorPanel_{.open = true, .shown = 1.0f, .size = 260.0f};
    Panel detailPanel_{.open = true, .shown = 1.0f, .size = 0.0f};
    bool focusMode_ = false;
    std::optional<bool> compact_;

    juce::String warning_;
    double warningUntil_ = 0.0;
    double lastFrame_ = 0.0;
    juce::VBlankAttachment vblank_{this, [this](double t) { onFrame(t); }};
};
} // namespace fd::app
