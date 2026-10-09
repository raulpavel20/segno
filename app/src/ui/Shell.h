#pragma once

#include "ui/ActionRegistry.h"
#include "ui/ArrangementView.h"
#include "ui/MixerView.h"
#include "ui/AssistantsPopover.h"
#include "ui/CommandPalette.h"
#include "ui/Panels.h"

#include <map>

namespace fd::app
{
/// The window content (docs/design/daw-shell.md, C1): transport bar, browser, arrangement,
/// inspector and detail panel; adaptive layout; every action in the registry; per-frame updates.
class Shell final : public juce::Component
{
public:
    static constexpr int compactWidth = 1440;

    /// `assistants`: the app's MCP server (the Assistants popover); null hides it.
    explicit Shell(engine::Session& session, mcp::McpServer* assistants = nullptr);
    ~Shell() override;

    [[nodiscard]] const ActionRegistry& actions() const noexcept { return actions_; }
    [[nodiscard]] CommandPalette& palette() noexcept { return palette_; }
    /// Shows a warning in the window (engine warnings; never a modal dialog).
    void showWarning(const juce::String& message);

    /// Runs `then` once unsaved changes are saved or discarded; nothing if cancelled. The one
    /// modal dialog the UI principles allow besides confirming destruction.
    void confirmDiscard(std::function<void()> then);
    /// "Segno - name" with a mark while there are unsaved changes.
    std::function<void(const juce::String&)> onTitleChanged;

    /// Ends every eased layout change at once (tests and snapshots, which get no frames).
    void finishAnimations();
    [[nodiscard]] DetailPanel& detail() noexcept { return detail_; }
    [[nodiscard]] ArrangementView& arrangement() noexcept { return arrangement_; }

    /// Frame statistics overlay (paint time of the arrangement, frame rate): --stats.
    void setStatsVisible(bool visible);
    /// Scripted benchmark (--benchmark): fills the project with 32 tracks of clips, zooms in and
    /// out for `seconds`, then calls `done` with a one-line report.
    void runBenchmark(double seconds, std::function<void(juce::String)> done);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    struct Panel
    {
        bool open = true;
        float shown = 1.0f; // eased towards open
        float size = 0.0f;  // px when fully open
    };

    void registerActions();
    void openProject();
    void saveProjectAs(std::function<void()> then = {});
    [[nodiscard]] std::string viewState() const;
    void applyViewState(const std::string& json);
    void updateTitle();
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String title_;
    void onFrame(double timestamp);
    void selectionChanged(std::optional<std::uint64_t> track);
    void showAudioSettings();
    void toggle(Panel& p);
    [[nodiscard]] std::optional<model::TrackInfo> selectedTrackInfo() const;

    engine::Session& session_;
    mcp::McpServer* assistants_ = nullptr;
    std::unique_ptr<AssistantsPopover> assistantsPopover_;
    ActionRegistry actions_;
    std::unique_ptr<TransportBar> transport_;
    BrowserPanel browser_{actions_};
    InspectorPanel inspector_{session_};
    DetailPanel detail_{session_, actions_};
    enum class Area { Arrangement, Detail, Clip };
    Area lastArea_ = Area::Arrangement; // where Delete, Duplicate and Select all apply
    [[nodiscard]] bool notesActive() const; // the piano roll was used last and shows a clip
    ArrangementView arrangement_;
    MixerView mixer_{session_};
    bool mixerShown_ = false; // the centre shows the mixer view instead of the arrangement
    Splitter browserSplit_, inspectorSplit_;
    Splitter detailSplit_{true}; // the clip view's top edge
    float deviceViewHeight_ = 0.0f, clipViewHeight_ = 320.0f; // the detail panel's height per mode
    std::optional<std::uint64_t> shownMidiClip_; // the selection's MIDI clip (the mode follows it)
    CommandPalette palette_{actions_};

    Panel browserPanel_{.open = true, .shown = 1.0f, .size = 220.0f};
    Panel inspectorPanel_{.open = true, .shown = 1.0f, .size = 260.0f};
    Panel detailPanel_{.open = true, .shown = 1.0f, .size = 0.0f};
    bool focusMode_ = false;
    std::optional<bool> compact_;

    struct Stats
    {
        std::vector<double> paintMs, frameMs;
        void clear()
        {
            paintMs.clear();
            frameMs.clear();
        }
    };
    [[nodiscard]] juce::String statsReport() const;

    bool statsVisible_ = false;
    Stats stats_;
    struct Benchmark
    {
        double until = 0.0;
        int frame = 0;
        std::function<void(juce::String)> done;
    };
    std::optional<Benchmark> benchmark_;
    juce::TemporaryFile benchmarkFile_{".wav"};

    // Computer keyboard as MIDI input (C7): A W S E D F T G Y H U J K O L P ; play an octave
    // and a third from C; Z / X shift octaves, C / V velocity.
    bool computerKeyboard_ = false;
    int keyboardOctave_ = 0;
    float keyboardVelocity_ = 0.8f;
    std::map<int, int> heldKeys_; // key code -> note
    bool handleComputerKeyboard(const juce::KeyPress& key);

    juce::String warning_;
    double warningUntil_ = 0.0;
    double lastFrame_ = 0.0;
    juce::VBlankAttachment vblank_{this, [this](double t) { onFrame(t); }};
};
} // namespace fd::app
