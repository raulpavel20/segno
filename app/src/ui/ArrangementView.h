#pragma once

#include "engine/Session.h"
#include "model/Editing.h"
#include "model/Timeline.h"
#include "model/TrackLayout.h"
#include "ui/WaveformCache.h"
#include "ui/WaveformTiles.h"

#include <fd_ui/fd_ui.h>

#include <optional>
#include <set>

namespace fd::app
{
/// The arrangement (docs/design/daw-shell.md, C2): ruler, track headers and lanes with the grid,
/// clips and playhead. Draws only from the Session's snapshot and its own view models.
class ArrangementView final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    static constexpr int headerWidth = 180;
    static constexpr int rulerHeight = 36;
    static constexpr int loopStripHeight = 12; // top of the ruler: the loop brace

    explicit ArrangementView(engine::Session& session);

    void setSnapshot(std::shared_ptr<const model::Snapshot> snapshot);

    /// Once per display frame (message thread): animations and the playhead.
    void frame(double dt, double playheadSeconds);

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    /// Follow the playhead while playing (pages the view smoothly).
    void setFollowEnabled(bool on) noexcept { follow_ = on; }
    [[nodiscard]] bool isFollowEnabled() const noexcept { return follow_; }
    void setSnapEnabled(bool on) noexcept { snap_ = on; }
    [[nodiscard]] bool isSnapEnabled() const noexcept { return snap_; }

    [[nodiscard]] std::optional<std::uint64_t> selectedTrack() const noexcept { return selectedTrack_; }
    [[nodiscard]] const std::set<std::uint64_t>& selectedClips() const noexcept { return selectedClips_; }
    /// Track or clip selection changed.
    std::function<void(std::optional<std::uint64_t>)> onSelectionChanged;
    /// Called when the arrangement is clicked (the shell routes Delete to the area used last).
    std::function<void()> onActivated;

    // Edit commands on the selection (C4), each one undo step.
    void deleteSelection();
    void duplicateSelection();
    /// Splits the selected clips at the playhead (or, with none selected, the clips under the
    /// playhead on the selected track).
    void splitAtPlayhead();
    void selectAllClips();
    [[nodiscard]] bool hasClipSelection() const noexcept { return !selectedClips_.empty(); }

    [[nodiscard]] const model::Timeline& timeline() const noexcept { return timeline_; }
    [[nodiscard]] WaveformCache& waveforms() noexcept { return waveforms_; }
    /// Immediate zoom around a lane x (benchmarks; input uses the gesture handlers).
    void zoomAt(double laneX, double factor);
    /// Sets every track's height (clamped to the layout's limits).
    void setAllTrackHeights(double height);
    /// Wall time of the last paint, ms (frame statistics).
    [[nodiscard]] double lastPaintMs() const noexcept { return lastPaintMs_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    [[nodiscard]] juce::Rectangle<int> headerArea() const;
    [[nodiscard]] juce::Rectangle<int> laneArea() const;
    [[nodiscard]] float laneX(double seconds) const;
    [[nodiscard]] double snapped(double seconds, bool bypass) const;
    void select(std::optional<std::uint64_t> track);
    void setClipSelection(std::set<std::uint64_t> clips);
    [[nodiscard]] std::function<double(double)> snapFunction(bool bypass) const;
    void paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h, juce::Rectangle<int> area,
                   juce::Rectangle<int> visible, bool selected);
    void syncLayout();

    void paintRuler(juce::Graphics& g);
    void paintHeaders(juce::Graphics& g);
    void paintLanes(juce::Graphics& g);
    void paintWaveform(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                       juce::Rectangle<int> visible, juce::Colour colour);

    engine::Session& session_;
    std::shared_ptr<const model::Snapshot> snapshot_;
    WaveformCache waveforms_;
    WaveformTiles tiles_;
    double lastPixelsPerSecond_ = 0.0, lastZoomChange_ = 0.0;
    bool zoomSettled_ = false;
    model::Timeline timeline_;
    model::TrackLayout layout_;
    std::vector<model::Timeline::GridLine> grid_; // paint scratch
    double playhead_ = 0.0;
    double lastPaintMs_ = 0.0;
    float drawnPlayheadX_ = -1.0f;
    bool snap_ = true;
    std::optional<std::uint64_t> selectedTrack_;

    std::set<std::uint64_t> selectedClips_;

    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    enum class LoopZone { None, Body, Start, End };
    [[nodiscard]] LoopZone loopZoneAt(float x) const;

    bool follow_ = true;
    std::optional<double> scrollTarget_; // follow-playhead paging, eased
    std::pair<double, double> loop_{0.0, 0.0};
    bool looping_ = false;
    std::optional<std::pair<double, double>> loopPreview_; // while dragging the brace
    LoopZone loopDrag_ = LoopZone::None;
    std::pair<double, double> loopAtDragStart_{0.0, 0.0};

    enum class Drag { None, Ruler, Clip, Box, Loop };
    Drag drag_ = Drag::None;
    juce::Point<float> lastDrag_, dragStart_;
    float rulerAnchorX_ = 0.0f;
    model::Drag clipDrag_;
    int dragStartTrack_ = 0;
    bool dragMoved_ = false;
    std::vector<model::ClipEdit> preview_; // where dragged clips will land
    std::set<std::uint64_t> boxBase_;      // selection before a Shift box drag

    // File drag preview: target track index (or -1 = new track) and snapped time.
    std::optional<std::pair<int, double>> dropTarget_;
};
} // namespace fd::app
