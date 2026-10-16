// The audio editor's warping (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7.4–§7.7): the
// marker strip with the clip's transients and warp markers, their gestures and commands, the
// range's time handles. Everything is worked out on the warp as the file plays (a reversed clip's
// seen from its end, as it's drawn) and kept as recorded.

#include "model/AudioAnalysis.h"
#include "model/Editing.h"
#include "ui/AudioClipView.h"

#include <algorithm>
#include <cmath>

namespace fd::app
{
namespace
{
/// How far from a tick or a handle the mouse still grabs it.
float grab()
{
    return ui::Theme::current().input.edgeGrab * 0.5f;
}

juce::String stretchText(double stretch)
{
    return juce::String::fromUTF8("\xC3\x97 ") + juce::String(stretch, 2);
}
} // namespace

// ---- what's shown --------------------------------------------------------------------------

std::vector<double> AudioClipView::transients() const
{
    if (!clip_)
        return {};
    // Quantise's sensitivity while it's set: the ticks follow it as it's dragged (§7.4).
    if (sensitivityPreview_ && clip_->analysis != nullptr)
        return model::clipTransients(*clip_->analysis, *sensitivityPreview_, clip_->transientsAdded,
                                     clip_->transientsRemoved);
    return model::transientsOf(*clip_);
}

const model::AudioWarp& AudioClipView::shownWarp() const
{
    return warpPreview_ ? *warpPreview_ : clip_->warp;
}

model::AudioWarp AudioClipView::playedWarp() const
{
    return clip_->sound.reversed ? model::reversedWarp(shownWarp(), clip_->fileLength) : shownWarp();
}

void AudioClipView::keepWarp(const model::AudioWarp& played, const std::string& undoName)
{
    const auto recorded = clip_->sound.reversed ? model::reversedWarp(played, clip_->fileLength) : played;
    if (!session_.setWarpMarkers(clip_->id, recorded.markers, undoName) && onMessage)
        onMessage("Those markers would stretch a piece past four times its speed, or out of order.");
}

double AudioClipView::viewTimeOfBeat(double beat) const
{
    return origin() + beat / clip_->beatsPerSecond;
}

double AudioClipView::beatOfViewTime(double time) const
{
    return (time - origin()) * clip_->beatsPerSecond;
}

juce::Rectangle<int> AudioClipView::markerStripArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds()
        .withTrimmedLeft(s.keyboardWidth)
        .withTrimmedTop(s.loopStripHeight + s.rulerHeight)
        .removeFromTop(s.markerStripHeight);
}

std::optional<float> AudioClipView::markerX(size_t index) const
{
    if (!clip_ || !shownWarp().on)
        return std::nullopt;
    const auto played = playedWarp();
    if (index >= played.markers.size())
        return std::nullopt;
    return static_cast<float>(waveformArea().getX() +
                              timeline_.xForTime(viewTimeOfBeat(played.markers[index].beat)));
}

std::vector<float> AudioClipView::transientXs() const
{
    std::vector<float> out;
    if (!clip_)
        return out;
    auto shown = *clip_;
    shown.warp = shownWarp();
    const auto map = model::contentMap(shown);
    const double o = origin();
    const auto x0 = static_cast<float>(waveformArea().getX());
    for (double t : transients())
        out.push_back(x0 + static_cast<float>(timeline_.xForTime(o + map.contentAt(t))));
    return out;
}

std::optional<float> AudioClipView::transientX(size_t index) const
{
    const auto xs = transientXs();
    return index < xs.size() ? std::optional(xs[index]) : std::nullopt;
}

std::optional<size_t> AudioClipView::markerAt(juce::Point<float> p) const
{
    if (!markerStripArea().toFloat().contains(p))
        return std::nullopt;
    // Its handle, and at least what grabs an edge.
    std::optional<size_t> best;
    float bestDistance =
        std::max(static_cast<float>(ui::Theme::current().shell.warpMarkerWidth) * 0.5f, grab());
    for (size_t i = 0; i < playedWarp().markers.size(); ++i)
        if (const auto x = markerX(i); x && std::abs(*x - p.x) <= bestDistance)
        {
            best = i;
            bestDistance = std::abs(*x - p.x);
        }
    return best;
}

std::optional<size_t> AudioClipView::transientAt(juce::Point<float> p) const
{
    if (!markerStripArea().toFloat().contains(p))
        return std::nullopt;
    std::optional<size_t> best;
    float bestDistance = grab();
    const auto xs = transientXs();
    for (size_t i = 0; i < xs.size(); ++i)
        if (std::abs(xs[i] - p.x) <= bestDistance)
        {
            best = i;
            bestDistance = std::abs(xs[i] - p.x);
        }
    return best;
}

std::pair<juce::Rectangle<int>, juce::Rectangle<int>> AudioClipView::timeHandles() const
{
    if (!range_ || !clip_)
        return {};
    const auto ruler = rulerArea();
    return time::timeHandles(ruler,
                             ruler.getX() + static_cast<int>(std::round(timeline_.xForTime(range_->first))),
                             ruler.getX() + static_cast<int>(std::round(timeline_.xForTime(range_->second))));
}

// ---- painting ------------------------------------------------------------------------------

juce::String AudioClipView::markerTag() const
{
    if (!dragMarker_ || !warpPreview_)
        return {};
    const auto played = playedWarp();
    const auto pieces = model::warpPieces(played, clip_->fileLength);
    // The piece that ends at the marker: its stretch and the tempo it plays its file at.
    const auto& m = played.markers.at(std::min(*dragMarker_, played.markers.size() - 1));
    for (const auto& p : pieces)
        if (std::abs(p.file1 - m.file) < 1e-9)
            return stretchText(model::pieceStretch(played, p)) + juce::String::fromUTF8(" \xC2\xB7 ") +
                   juce::String(p.tempo(), 1) + " BPM";
    return {};
}

void AudioClipView::paintMarkers(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    const auto strip = markerStripArea();
    g.setColour(t.roles.shade);
    g.fillRect(strip);
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(strip.getUnion(waveformArea()));
    // Transients: a short tick at the strip's foot; the one under the mouse lighter, with a dashed
    // line down through the waveform where a marker would go (§7.4).
    const auto xs = transientXs();
    const auto tickTop = static_cast<float>(strip.getBottom() - s.transientTick);
    for (size_t i = 0; i < xs.size(); ++i)
        if (const float x = std::round(xs[i]);
            x >= static_cast<float>(strip.getX()) && x < static_cast<float>(strip.getRight()))
        {
            const bool hovered = hoveredTransient_ == i;
            g.setColour(hovered ? t.textSecondary : t.textTertiary);
            g.fillRect(juce::Rectangle<float>(x, tickTop, 1.0f, static_cast<float>(s.transientTick)));
            if (hovered)
            {
                const float dash[] = {static_cast<float>(s.dashLength), static_cast<float>(s.dashLength)};
                const auto area = waveformArea().toFloat();
                g.drawDashedLine({x + 0.5f, area.getY(), x + 0.5f, area.getBottom()}, dash, 2);
            }
        }
    if (!shownWarp().on)
        return;
    // Warp markers (§7.5): the strongest thing on the waveform. A pin pointing down at its moment,
    // a line through the rows; selected, in `active`. The file's start is pinned too.
    const auto played = playedWarp();
    const auto pin = [&](float x, juce::Colour colour)
    {
        const float half = static_cast<float>(s.warpMarkerWidth) * 0.5f;
        const float top = static_cast<float>(strip.getY() + s.warpMarkerInset),
                    tip = static_cast<float>(strip.getBottom() - s.warpMarkerInset);
        const float shoulder = tip - half;
        juce::Path p;
        p.startNewSubPath(x - half, top);
        p.lineTo(x + half, top);
        p.lineTo(x + half, shoulder);
        p.lineTo(x, tip);
        p.lineTo(x - half, shoulder);
        p.closeSubPath();
        g.setColour(t.roles.warpMarkerLine);
        g.fillRect(
            juce::Rectangle<float>(x, tip, 1.0f, static_cast<float>(waveformArea().getBottom()) - tip));
        g.setColour(colour);
        g.fillPath(p);
    };
    const auto xOf = [&](double beat)
    {
        return std::round(
            static_cast<float>(waveformArea().getX() + timeline_.xForTime(viewTimeOfBeat(beat))));
    };
    pin(xOf(0.0), t.textPrimary);
    for (size_t i = 0; i < played.markers.size(); ++i)
        pin(xOf(played.markers[i].beat), markers_.contains(i) ? t.active : t.textPrimary);
    if (gesture_ == Gesture::MarkerBox && moved_)
    {
        const float x0 = static_cast<float>(waveformArea().getX() + timeline_.xForTime(markerBox_.first));
        const float x1 = static_cast<float>(waveformArea().getX() + timeline_.xForTime(markerBox_.second));
        time::paintBoxSelection(g, juce::Rectangle<float>(std::min(x0, x1), static_cast<float>(strip.getY()),
                                                          std::abs(x1 - x0),
                                                          static_cast<float>(strip.getHeight())));
    }
    if ((gesture_ == Gesture::Marker || gesture_ == Gesture::Transient) && moved_ && dragMarker_)
        if (const auto x = markerX(*dragMarker_))
            ui::paintValueTag(g, {*x, static_cast<float>(strip.getBottom())}, getLocalBounds(), markerTag());
}

void AudioClipView::paintTimeHandles(juce::Graphics& g) const
{
    const auto handles = timeHandles();
    const auto& [left, right] = handles;
    if (left.isEmpty())
        return;
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(rulerArea());
    time::paintTimeHandles(g, handles);
    if (gesture_ == Gesture::TimeHandle && moved_ && range_)
    {
        const double was = rangeFrom_.second - rangeFrom_.first;
        ui::paintValueTag(g, (handleAtEnd_ ? right : left).getCentre().toFloat(), getLocalBounds(),
                          stretchText(was > 0.0 ? (range_->second - range_->first) / was : 1.0));
    }
}

// ---- commands ------------------------------------------------------------------------------

void AudioClipView::selectMarkers(std::set<size_t> markers)
{
    markers_ = std::move(markers);
    repaint(markerStripArea());
}

std::string AudioClipView::markersRefusal() const
{
    if (!clip_)
        return "Show an audio clip in the clip view.";
    if (!clip_->warp.on)
        return "Turn Warp on first: markers pin the audio to beats while it follows the tempo.";
    return {};
}

bool AudioClipView::markersAtTransients()
{
    if (!clip_)
        return false;
    if (clip_->analysis == nullptr)
    {
        if (onMessage)
            onMessage("The clip's transients are still being found.");
        return false;
    }
    auto played = clip_->sound.reversed ? model::reversedWarp(clip_->warp, clip_->fileLength) : clip_->warp;
    played.on = true;
    // In the file as it plays.
    auto found = transients();
    if (clip_->sound.reversed)
    {
        for (auto& t : found)
            t = clip_->fileLength - t;
        std::reverse(found.begin(), found.end());
    }
    double from = 0.0, to = clip_->fileLength;
    if (range_)
    {
        from = model::fileAtBeat(played, beatOfViewTime(range_->first));
        to = model::fileAtBeat(played, beatOfViewTime(range_->second));
    }
    keepWarp(model::withMarkersAtTransients(played, found, from, to), "Markers at transients");
    return true;
}

bool AudioClipView::warpFromHere()
{
    if (const auto why = markersRefusal(); !why.empty() || markers_.empty())
    {
        if (onMessage)
            onMessage(why.empty() ? "Select the marker to warp from first."
                                  : juce::String::fromUTF8(why.c_str()));
        return false;
    }
    keepWarp(model::warpedFrom(playedWarp(), *markers_.begin()), "Warp from here");
    markers_.clear();
    return true;
}

bool AudioClipView::setDownbeat()
{
    if (!clip_)
        return false;
    if (clip_->loop.on())
    {
        if (onMessage)
            onMessage("A looped clip starts where its loop does: drag the loop's start there instead.");
        return false;
    }
    // The moment: the first selected marker's, else the range's start. It becomes the clip's first
    // beat: the clip starts with it where it starts now, its end where it is.
    double at = 0.0;
    if (!markers_.empty() && *markers_.begin() < playedWarp().markers.size())
        at = viewTimeOfBeat(playedWarp().markers[*markers_.begin()].beat);
    else if (range_)
        at = range_->first;
    else
    {
        if (onMessage)
            onMessage("Select a marker, or a range from the moment that's the first beat.");
        return false;
    }
    const double content = at - origin();
    if (content <= clip_->offset + 1e-6 || at >= clip_->end())
    {
        if (onMessage)
            onMessage("That moment is the clip's start already, or past its end.");
        return false;
    }
    const double cut = content - clip_->offset;
    const auto snapshot = session_.snapshot();
    const auto found = model::findClip(*snapshot, clip_->id);
    if (!found)
        return false;
    session_.applyClipEdits({{.clip = clip_->id,
                              .track = snapshot->tracks[static_cast<size_t>(found->second)].id,
                              .start = clip_->start,
                              .length = clip_->length - cut,
                              .offset = content}},
                            "Set the downbeat");
    return true;
}

bool AudioClipView::removeMarkers()
{
    if (const auto why = markersRefusal(); !why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(why.c_str()));
        return false;
    }
    const auto played = playedWarp();
    std::vector<size_t> gone(markers_.begin(), markers_.end());
    if (gone.empty())
        for (size_t i = 0; i < played.markers.size(); ++i)
        {
            const double at = viewTimeOfBeat(played.markers[i].beat);
            if (!range_ || (at >= range_->first && at <= range_->second))
                gone.push_back(i);
        }
    if (gone.empty())
        return false;
    keepWarp(model::withoutMarkers(played, gone), "Remove markers");
    markers_.clear();
    return true;
}

std::optional<model::AudioWarp> AudioClipView::quantisedWarp(const model::WarpQuantise& settings) const
{
    if (!clip_ || clip_->analysis == nullptr)
        return std::nullopt;
    return model::quantisedClip(*clip_, transients(), settings, range_, origin());
}

bool AudioClipView::quantise(const model::WarpQuantise& settings)
{
    if (!clip_)
        return false;
    if (const auto why = session_.quantiseAudio(clip_->id, {.grid = settings, .range = range_}); !why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(why.c_str()));
        return false;
    }
    return true;
}

void AudioClipView::applyTempo(const model::TempoChoice& choice)
{
    if (clip_)
        session_.applyTempo(clip_->id, choice.bpm, choice.downbeat, choice.songTempo);
}

std::optional<float> AudioClipView::downbeatX() const
{
    if (!clip_ || !tempoPreview_)
        return std::nullopt;
    auto shown = *clip_;
    shown.warp = shownWarp();
    return static_cast<float>(
        waveformArea().getX() +
        timeline_.xForTime(origin() + model::contentMap(shown).contentAt(tempoPreview_->downbeat)));
}

void AudioClipView::paintTempo(juce::Graphics& g) const
{
    if (!clip_ || !tempoPreview_ || !(tempoPreview_->bpm > 0.0))
        return;
    const auto& t = ui::Theme::current();
    const auto area = waveformArea();
    auto shown = *clip_;
    shown.warp = shownWarp();
    const auto map = model::contentMap(shown);
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(area);
        // Its beats in the file from the downbeat, as the file plays here: each bar's first stronger.
        const double beat = 60.0 / tempoPreview_->bpm;
        const double d = tempoPreview_->downbeat;
        const auto first = static_cast<long>(std::floor(-d / beat));
        const auto last = static_cast<long>(std::ceil((clip_->fileLength - d) / beat));
        for (long k = first; k <= last; ++k)
        {
            const double file = d + static_cast<double>(k) * beat;
            if (file < 0.0 || file > clip_->fileLength)
                continue;
            const float x = std::round(
                static_cast<float>(area.getX() + timeline_.xForTime(origin() + map.contentAt(file))));
            // Brighter than the grid: these are the file's beats, not the song's.
            g.setColour(k == 0 ? t.active : (k % 4 == 0 ? t.textSecondary : t.roles.warpMarkerLine));
            g.fillRect(juce::Rectangle<float>(x, static_cast<float>(area.getY()), 1.0f,
                                              static_cast<float>(area.getHeight())));
        }
    }
    // Where it lands in the song, at the top of the rows beside it.
    if (const auto x = downbeatX())
    {
        const double at = origin() + map.contentAt(tempoPreview_->downbeat);
        const auto bb = tempo_.barsBeatsForSeconds(std::max(0.0, at));
        ui::paintValueTag(g, {*x, static_cast<float>(area.getY() + t.shell.rowHeight + 2 * t.shell.itemGap)},
                          area, "Downbeat " + juce::String(bb.bar) + "." + juce::String(bb.beat));
    }
}

void AudioClipView::paintSlices(juce::Graphics& g) const
{
    if (!clip_ || slicePreview_.empty())
        return;
    const auto& t = ui::Theme::current();
    const auto area = waveformArea();
    std::optional<float> first;
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(area);
        g.setColour(t.active);
        // A looped clip's laps show in its loop: their cuts fall on the same moments.
        for (const double at : slicePreview_)
        {
            const float x = std::round(
                static_cast<float>(area.getX() + timeline_.xForTime(model::clipViewTime(*clip_, at))));
            first = first ? std::min(*first, x) : x;
            g.fillRect(juce::Rectangle<float>(x, static_cast<float>(area.getY()), 1.0f,
                                              static_cast<float>(area.getHeight())));
        }
    }
    // How many clips it makes, at the first cut.
    const auto count = static_cast<int>(slicePreview_.size()) + 1;
    ui::paintValueTag(g, {*first, static_cast<float>(area.getY() + t.shell.rowHeight)}, area,
                      juce::String(count) + " slices");
}

void AudioClipView::nudgeMarkers(int steps)
{
    if (!clip_ || markers_.empty() || !clip_->warp.on)
        return;
    const double beats = timeline_.gridStepBeats(tempo_) * steps;
    keepWarp(model::withMarkersMoved(playedWarp(), {markers_.begin(), markers_.end()}, beats),
             "Move markers");
}

double AudioClipView::fileAtViewTime(double time) const
{
    auto shown = *clip_;
    shown.warp = shownWarp();
    return model::contentMap(shown).fileAt(time - origin());
}

double AudioClipView::transientPlace() const
{
    return range_ ? range_->first : playhead_;
}

std::vector<double> AudioClipView::transientsInRange() const
{
    std::vector<double> out;
    if (!clip_ || !range_)
        return out;
    const double a = fileAtViewTime(range_->first), b = fileAtViewTime(range_->second);
    for (double t : transients())
        if (t >= std::min(a, b) && t <= std::max(a, b))
            out.push_back(t);
    return out;
}

std::string AudioClipView::addTransientRefusal() const
{
    if (!clip_)
        return "Show an audio clip in the clip view.";
    const double file = fileAtViewTime(transientPlace());
    if (file < 0.0 || file > clip_->fileLength)
        return "Put the playhead where it goes in the clip, or select a range from there.";
    const auto found = transients();
    if (std::any_of(found.begin(), found.end(), [file](double t) { return std::abs(t - file) < 0.001; }))
        return "There's a transient there already.";
    return {};
}

std::string AudioClipView::removeTransientsRefusal() const
{
    if (!clip_)
        return "Show an audio clip in the clip view.";
    if (!range_)
        return "Select a range over the transients to remove.";
    if (transientsInRange().empty())
        return "There are no transients in the range.";
    return {};
}

bool AudioClipView::addTransient()
{
    if (const auto why = addTransientRefusal(); !why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(why.c_str()));
        return false;
    }
    return session_.editTransients(clip_->id, {fileAtViewTime(transientPlace())}, {});
}

bool AudioClipView::removeTransients()
{
    if (const auto why = removeTransientsRefusal(); !why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(why.c_str()));
        return false;
    }
    return session_.editTransients(clip_->id, {}, transientsInRange());
}

juce::PopupMenu AudioClipView::markerMenu(std::optional<size_t> marker, std::optional<double> at)
{
    juce::PopupMenu m;
    const bool warped = clip_->warp.on;
    if (marker && !markers_.contains(*marker))
        selectMarkers({*marker});
    m.addItem("Warp from here", warped && !markers_.empty(), false, [this] { warpFromHere(); });
    m.addItem("Set the downbeat here", !markers_.empty() || range_.has_value(), false,
              [this] { setDownbeat(); });
    m.addItem(range_ ? "Markers at transients in the range" : "Markers at transients",
              clip_->analysis != nullptr, false, [this] { markersAtTransients(); });
    m.addItem(!markers_.empty() ? "Remove markers"
                                : (range_ ? "Remove markers in the range" : "Remove all markers"),
              warped && !clip_->warp.markers.empty(), false, [this] { removeMarkers(); });
    if (at)
    {
        // Transients by hand (§7.4): where clicked here; the Audio menu's at the playhead or range.
        m.addSeparator();
        const auto near = transientAt({static_cast<float>(waveformArea().getX() + timeline_.xForTime(*at)),
                                       static_cast<float>(markerStripArea().getCentreY())});
        const double file = fileAtViewTime(*at);
        const auto id = clip_->id;
        m.addItem("Add transient here", !near && file >= 0.0 && file <= clip_->fileLength, false,
                  [this, id, file] { session_.editTransients(id, {file}, {}); });
        const auto found = transients();
        m.addItem("Remove transient", near.has_value(), false,
                  [this, id, t = near ? found[*near] : 0.0] { session_.editTransients(id, {}, {t}); });
    }
    return m;
}

// ---- gestures ------------------------------------------------------------------------------

bool AudioClipView::warpPress(const juce::MouseEvent& e)
{
    const auto p = e.position;
    const bool inStrip = markerStripArea().contains(e.getPosition());
    const double time = timeline_.timeForX(p.x - static_cast<float>(waveformArea().getX()));
    if (inStrip && e.mods.isPopupMenu())
    {
        markerMenu(clip_->warp.on ? markerAt(p) : std::nullopt, time)
            .showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1)));
        return true;
    }
    if (e.mods.isPopupMenu())
        return false;
    // The range's time handles stretch its audio (§7.6).
    if (const auto [left, right] = timeHandles(); !left.isEmpty())
        for (const auto& [handle, end] : {std::pair{left, false}, std::pair{right, true}})
            if (handle.expanded(static_cast<int>(grab())).contains(e.getPosition()))
            {
                gesture_ = Gesture::TimeHandle;
                handleAtEnd_ = end;
                rangeFrom_ = *range_;
                dragFrom_ = playedWarp();
                dragFrom_.on = true;
                return true;
            }
    const bool onWave = waveformArea().contains(e.getPosition());
    if (!inStrip && !(onWave && e.getNumberOfClicks() >= 2))
        return false;
    const auto played = playedWarp();
    const auto marker = clip_->warp.on ? markerAt(p) : std::nullopt;
    if (e.getNumberOfClicks() >= 2)
    {
        // Double-click: a marker goes; elsewhere one comes, on a transient within reach.
        if (marker)
        {
            markers_.clear();
            keepWarp(model::withoutMarkers(played, {*marker}), "Remove marker");
            return true;
        }
        // The moment of the file there, as recorded: a transient's own, or what's under the click.
        auto shown = *clip_;
        shown.warp = shownWarp();
        double file = model::contentMap(shown).fileAt(time - origin());
        if (const auto tick = transientAt({p.x, static_cast<float>(markerStripArea().getCentreY())}))
            file = transients()[*tick];
        if (clip_->sound.reversed)
            file = clip_->fileLength - file; // as the file plays
        auto warp = played;
        warp.on = true;
        const auto [added, index] = model::withMarkerAt(warp, file);
        if (index)
        {
            keepWarp(added, "Add marker");
            markers_ = {*index};
        }
        return true;
    }
    dragFrom_ = played;
    dragFrom_.on = true;
    if (marker)
    {
        // A click selects (Shift or Ctrl adds); a drag moves it, and the others selected with it.
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            if (!markers_.insert(*marker).second && !e.mods.isShiftDown())
                markers_.erase(*marker);
        }
        else if (!markers_.contains(*marker))
            markers_ = {*marker};
        gesture_ = Gesture::Marker;
        dragMarker_ = *marker;
        repaint(markerStripArea());
        return true;
    }
    if (const auto tick = transientAt(p))
    {
        gesture_ = Gesture::Transient;
        hoveredTransient_ = tick;
        dragMarker_.reset();
        return true;
    }
    gesture_ = Gesture::MarkerBox;
    markerBox_ = {time, time};
    if (!e.mods.isShiftDown())
        markers_.clear();
    repaint(markerStripArea());
    return true;
}

bool AudioClipView::warpDrag(const juce::MouseEvent& e)
{
    const bool past = e.getDistanceFromDragStart() >= ui::Theme::current().input.dragThreshold;
    const float x = e.position.x - static_cast<float>(waveformArea().getX());
    const auto recorded = [this](const model::AudioWarp& played)
    { return clip_->sound.reversed ? model::reversedWarp(played, clip_->fileLength) : played; };
    switch (gesture_)
    {
    case Gesture::Transient:
    {
        if (!moved_ && !past)
            return true;
        if (!moved_)
        {
            // It becomes a marker where it is, then moves.
            auto found = transients();
            if (!hoveredTransient_ || *hoveredTransient_ >= found.size())
                return true;
            double file = found[*hoveredTransient_];
            if (clip_->sound.reversed)
                file = clip_->fileLength - file;
            const auto [added, index] = model::withMarkerAt(dragFrom_, file);
            if (!index)
                return true;
            dragFrom_ = added;
            dragMarker_ = index;
            markers_ = {*index};
        }
        [[fallthrough]];
    }
    case Gesture::Marker:
    {
        if (!moved_ && !past)
            return true;
        moved_ = true;
        if (!dragMarker_ || *dragMarker_ >= dragFrom_.markers.size())
            return true;
        const auto& m = dragFrom_.markers[*dragMarker_];
        const double pressed = timeline_.timeForX(dragStart_.x - static_cast<float>(waveformArea().getX()));
        if (e.mods.isShiftDown())
        {
            // Slid: the marker stays, and pins the moment of the audio that's under it now.
            const double beats = (timeline_.timeForX(x) - pressed) * clip_->beatsPerSecond;
            warpPreview_ = recorded(
                model::withMarkerSlid(dragFrom_, *dragMarker_, model::fileAtBeat(dragFrom_, m.beat - beats)));
        }
        else
        {
            const double to = beatOfViewTime(
                snapped(timeline_.timeForX(x) + (viewTimeOfBeat(m.beat) - pressed), e.mods.isAltDown()));
            warpPreview_ = recorded(
                markers_.size() > 1 && markers_.contains(*dragMarker_)
                    ? model::withMarkersMoved(dragFrom_, {markers_.begin(), markers_.end()}, to - m.beat)
                    : model::withMarkerMoved(dragFrom_, *dragMarker_, to));
        }
        repaint();
        return true;
    }
    case Gesture::MarkerBox:
        moved_ = moved_ || past;
        markerBox_.second = timeline_.timeForX(x);
        repaint(markerStripArea());
        return true;
    case Gesture::Downbeat:
    {
        moved_ = moved_ || past;
        if (!moved_ || !tempoPreview_)
            return true;
        auto shown = *clip_;
        shown.warp = shownWarp();
        const double file = std::clamp(model::contentMap(shown).fileAt(timeline_.timeForX(x) - origin()), 0.0,
                                       clip_->fileLength);
        if (onToolValue)
            onToolValue("downbeat", file); // the pane shows it and previews it again
        else
            tempoPreview_->downbeat = file;
        repaint();
        return true;
    }
    case Gesture::TimeHandle:
    {
        if (!moved_ && !past)
            return true;
        moved_ = true;
        const double t = snapped(timeline_.timeForX(x), e.mods.isAltDown());
        auto edges = rangeFrom_;
        (handleAtEnd_ ? edges.second : edges.first) = t;
        if (edges.second - edges.first < 1e-3)
            return true;
        const auto stretched = model::withRangeStretched(
            dragFrom_, beatOfViewTime(rangeFrom_.first), beatOfViewTime(rangeFrom_.second),
            beatOfViewTime(edges.first), beatOfViewTime(edges.second));
        warpPreview_ = recorded(stretched);
        // The range follows its audio as far as the limits let it.
        const auto ends =
            model::fileAtBeat(dragFrom_, beatOfViewTime(handleAtEnd_ ? rangeFrom_.second : rangeFrom_.first));
        const double moved = viewTimeOfBeat(model::beatAtFile(stretched, ends));
        range_ = handleAtEnd_ ? std::pair{rangeFrom_.first, moved} : std::pair{moved, rangeFrom_.second};
        repaint();
        return true;
    }
    case Gesture::None:
    case Gesture::Ruler:
    case Gesture::Brace:
    case Gesture::Range:
    case Gesture::RangeGain:
    case Gesture::Line:
    case Gesture::Level:
        return false;
    }
    return false;
}

bool AudioClipView::warpRelease(const juce::MouseEvent& e)
{
    switch (gesture_)
    {
    case Gesture::Marker:
    case Gesture::Transient:
        if (moved_ && warpPreview_)
        {
            const auto played =
                clip_->sound.reversed ? model::reversedWarp(*warpPreview_, clip_->fileLength) : *warpPreview_;
            keepWarp(played, e.mods.isShiftDown()
                                 ? "Slide audio"
                                 : (gesture_ == Gesture::Transient ? "Warp" : "Move marker"));
        }
        warpPreview_.reset();
        dragMarker_.reset();
        hoveredTransient_.reset(); // the tick is a marker now, or let go
        return true;
    case Gesture::MarkerBox:
    {
        if (moved_)
        {
            const double a = std::min(markerBox_.first, markerBox_.second),
                         b = std::max(markerBox_.first, markerBox_.second);
            const auto played = playedWarp();
            for (size_t i = 0; i < played.markers.size(); ++i)
                if (const double at = viewTimeOfBeat(played.markers[i].beat); at >= a && at <= b)
                    markers_.insert(i);
        }
        repaint(markerStripArea());
        return true;
    }
    case Gesture::TimeHandle:
        if (moved_ && warpPreview_)
        {
            const auto played =
                clip_->sound.reversed ? model::reversedWarp(*warpPreview_, clip_->fileLength) : *warpPreview_;
            keepWarp(played, "Stretch");
        }
        warpPreview_.reset();
        if (onRangeChanged)
            onRangeChanged();
        return true;
    case Gesture::Downbeat:
        return true;
    case Gesture::None:
    case Gesture::Ruler:
    case Gesture::Brace:
    case Gesture::Range:
    case Gesture::RangeGain:
    case Gesture::Line:
    case Gesture::Level:
        return false;
    }
    return false;
}
} // namespace fd::app
