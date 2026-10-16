#pragma once

#include "model/TempoMap.h"
#include "model/Timeline.h"

#include <fd_ui/fd_ui.h>

#include <functional>
#include <optional>
#include <vector>

namespace fd::app::time
{
/// The shared parts of every view that shows time: the arrangement, the piano roll, automation
/// lanes (docs/design/ui-foundations.md, "Time views", ADR 0023). Each view keeps its own
/// model::Timeline; these draw it and map input to it the same way everywhere. Areas are in
/// the view's coordinates; a time's x is the area's left edge plus Timeline::xForTime.

/// Grid lines across `area` in the grid roles (subdivisions optional, e.g. for a sub-lane).
void paintGrid(juce::Graphics& g, const std::vector<model::Timeline::GridLine>& lines, const model::Timeline& timeline,
               juce::Rectangle<int> area, bool subdivisions = true);

struct RulerOptions
{
    std::optional<double> playhead;                 // a marker at the bottom
    std::optional<std::pair<double, double>> range; // a strip along the bottom (a clip's extent)
    juce::Colour rangeColour;
};

/// The ruler row: bar numbers and ticks (and the range strip and playhead marker). The loop
/// strip, when a view has one, sits above it (paintLoopStrip).
void paintRuler(juce::Graphics& g, const std::vector<model::Timeline::GridLine>& lines, const model::Timeline& timeline,
                juce::Rectangle<int> area, const RulerOptions& options);

/// The loop strip and its brace (the range being dragged, or the transport's).
void paintLoopStrip(juce::Graphics& g, const model::Timeline& timeline, juce::Rectangle<int> area,
                    std::pair<double, double> range, bool looping);

/// The playhead line down `area`.
void paintPlayhead(juce::Graphics& g, const model::Timeline& timeline, juce::Rectangle<int> area, double seconds);

/// A rubber-band selection.
void paintBoxSelection(juce::Graphics& g, juce::Rectangle<float> box);

/// A selection's time handles in a ruler (piano-roll.md §9.2, audio-editor.md §7.6): one inside each
/// end of the span from `x0` to `x1` (the view's x), in the ruler's lower half. Empty when there's no
/// room for both.
[[nodiscard]] std::pair<juce::Rectangle<int>, juce::Rectangle<int>> timeHandles(juce::Rectangle<int> ruler,
                                                                                int x0, int x1);
/// The handles, joined by a bar over the span, in `active` (the caller clips to its ruler).
void paintTimeHandles(juce::Graphics& g,
                      const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& handles);

/// Invalidates only the old and new playhead strips when the playhead moves (the GL thread
/// redraws just those).
class PlayheadTracker
{
public:
    void update(juce::Component& component, const model::Timeline& timeline, juce::Rectangle<int> lanes, double seconds);

private:
    float drawnX_ = -1.0f;
};

/// Dragging a ruler: vertical zooms around the press point, horizontal scrolls.
class RulerGesture
{
public:
    /// `laneX`: the mouse in lane coordinates (0 = the timeline's left edge).
    void begin(float laneX) noexcept { anchorX_ = laneX; }
    void drag(model::Timeline& timeline, juce::Point<float> delta, float laneX);

private:
    float anchorX_ = 0.0f;
};

/// The wheel in a time view: Ctrl zooms time at the mouse, Shift and horizontal wheels scroll
/// time, and the plain vertical wheel scrolls the view's other axis (`otherAxis`, in pixels:
/// tracks, pitches).
void applyWheel(model::Timeline& timeline, double laneX, const juce::ModifierKeys& mods,
                const juce::MouseWheelDetails& wheel, const std::function<void(double pixels)>& otherAxis);
/// A trackpad pinch: zooms time at the mouse.
void applyMagnify(model::Timeline& timeline, double laneX, float scaleFactor);

/// The time a click in the ruler means: under the mouse, on the grid unless `free` (Alt).
[[nodiscard]] double rulerTime(const model::Timeline& timeline, const model::TempoMap& tempo, double laneX, bool free);
} // namespace fd::app::time
