#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace fd::app
{
/// The export panel (docs/design/export.md §6, ADR 0039): what to export, the range, the format,
/// where the files go, and Export. A popover at the top of the window (Ctrl+Shift+E): the export
/// runs in the background, so it can close at once; Escape or a click elsewhere closes it. Every
/// choice is the project's export settings (Session::exportSettings), so it opens as it was left.
class ExportPopover final : public juce::Component
{
public:
    explicit ExportPopover(engine::Session& session);
    ~ExportPopover() override;

    void open(juce::Component& parent);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    /// Exports with the panel's settings (Enter); false, saying why through onMessage, if it can't.
    bool exportNow();
    /// Cancels the export running, if any (Escape with no popover open); true if there was one.
    bool cancel();
    /// How far the export running is (0..1); none while none runs.
    [[nodiscard]] std::optional<float> progress() const;

    /// The tracks "Selected tracks" exports: the shell's selection.
    std::function<std::vector<std::uint64_t>()> selectedTracks;
    /// The stretch the Selection range exports: the arrangement's time selection, if any
    /// (time-selection.md §13).
    std::function<std::optional<model::TimeSpan>()> selectedTime;
    /// A refusal or failure to say (the shell's toast).
    std::function<void(const juce::String&)> onMessage;
    /// An export finished: its files and their reports (the shell's toast, with Show).
    std::function<void(const engine::Session::ExportResult&)> onExported;
    /// The report's Show in browser (export.md §17.3): the browser at the file.
    std::function<void(const juce::File&)> onShowInBrowser;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    /// The settings into the controls; the controls into the settings.
    void load();
    void store();
    /// The controls that depend on others (depths a format holds, the target with loudness).
    void updateEnabled();
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] juce::String report() const;
    void chooseFolder();

    engine::Session& session_;
    juce::ComboBox what_, range_, format_, depth_, quality_, rate_, dither_, normalise_;
    /// A second format from the same render (export.md §17.2), and its bit depth or bit rate.
    juce::ComboBox also_, alsoDetail_;
    ui::Toggle through_{"Through the returns and master"};
    ui::NumberField target_;
    juce::TextEditor name_;
    juce::TextButton choose_{"Choose..."}, defaultFolder_{"Exports folder"}, export_{"Export"}, show_{"Show"},
        inBrowser_{"Show in browser"};
    // What the song leaves to chance (ADR 0062): each export differs; Keep all as played beside it.
    juce::TextButton keepAll_{"Keep all as played"};
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<engine::Session::RenderJob> job_;
    std::optional<engine::Session::ExportResult> last_;
    bool loading_ = false;
    ui::OutsideClick outside_{*this, [this] { close(); }};

    // Laid out by resized(): the painted parts, and each row's label.
    juce::Rectangle<int> title_, folder_, extension_, report_, chance_, progressLine_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels_;
    std::vector<int> separators_;
    /// A format's bit depths or bit rates into `box`, the one in `settings` chosen.
    static void fillDetail(juce::ComboBox& box, model::ExportFormat format, int bitDepth, int kbps);
    /// Rows shown only where they apply: through the returns (stems), the loudness target, dither
    /// (a PCM file below 32-bit float), the second format's detail.
    [[nodiscard]] bool showsDither() const;
    [[nodiscard]] bool showsAlsoDetail() const;
    [[nodiscard]] bool showsThrough() const;
    [[nodiscard]] bool showsTarget() const;
    /// What plays by chance ("2 clips and 1 modulator play by chance"), empty when nothing does.
    [[nodiscard]] juce::String chanceText() const;

public:
    /// The chance line and its Keep all as played, shown (tests).
    [[nodiscard]] bool showsChance() const { return chanceText().isNotEmpty(); }
    [[nodiscard]] juce::TextButton& keepAllButton() noexcept { return keepAll_; }
    [[nodiscard]] juce::TextButton& inBrowserButton() noexcept { return inBrowser_; }
};
} // namespace fd::app
