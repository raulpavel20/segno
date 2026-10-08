#include "ui/Shell.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace fd::app
{
namespace
{
constexpr float kRailWidth = 44.0f;
constexpr double kPanelTimeConstant = 0.04; // s; ~150 ms to settle (ease-out)

juce::KeyPress key(int code, int mods = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(mods), 0);
}

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;
} // namespace

Shell::Shell(engine::Session& session) : session_(session), arrangement_(session)
{
    registerActions();
    transport_ = std::make_unique<TransportBar>(session_, actions_);

    detailPanel_.size = static_cast<float>(ui::Theme::current().devicePanelHeight + 24 + DetailPanel::headerHeight);
    for (juce::Component* c : {static_cast<juce::Component*>(transport_.get()), static_cast<juce::Component*>(&browser_),
                               static_cast<juce::Component*>(&inspector_), static_cast<juce::Component*>(&detail_),
                               static_cast<juce::Component*>(&arrangement_), static_cast<juce::Component*>(&browserSplit_),
                               static_cast<juce::Component*>(&inspectorSplit_)})
        addAndMakeVisible(c);

    browserSplit_.onDrag = [this](float dx) {
        browserPanel_.size = juce::jlimit(160.0f, 480.0f, browserPanel_.size + dx);
        resized();
    };
    inspectorSplit_.onDrag = [this](float dx) {
        inspectorPanel_.size = juce::jlimit(200.0f, 520.0f, inspectorPanel_.size - dx);
        resized();
    };

    arrangement_.onSelectionChanged = [this](auto track) { selectionChanged(track); };
    session_.onSnapshotChanged = [this] {
        arrangement_.setSnapshot(session_.snapshot());
        selectionChanged(arrangement_.selectedTrack());
    };

    setWantsKeyboardFocus(true);
    setSize(1600, 960);
}

Shell::~Shell()
{
    session_.onSnapshotChanged = nullptr;
}

// ---- actions -------------------------------------------------------------------------------

void Shell::registerActions()
{
    auto& s = session_;
    actions_.add({.id = "transport.playStop", .name = "Play / stop", .category = "Transport",
                  .keys = {key(juce::KeyPress::spaceKey)},
                  .perform = [&s] { s.isPlaying() ? s.stop() : s.play(); },
                  .isToggled = [&s] { return s.isPlaying(); }});
    actions_.add({.id = "transport.toStart", .name = "Stop and return to start", .category = "Transport",
                  .keys = {key(juce::KeyPress::homeKey)},
                  .perform = [&s] {
                      s.stop();
                      s.setPosition(0.0);
                  }});
    actions_.add({.id = "edit.undo", .name = "Undo", .category = "Edit", .keys = {key('z', kCmd)},
                  .perform = [&s] { s.undo(); }, .isEnabled = [&s] { return s.canUndo(); }});
    actions_.add({.id = "edit.redo", .name = "Redo", .category = "Edit", .keys = {key('z', kCmd | kShift), key('y', kCmd)},
                  .perform = [&s] { s.redo(); }, .isEnabled = [&s] { return s.canRedo(); }});
    actions_.add({.id = "track.addAudio", .name = "Add audio track", .category = "Track", .keys = {key('t', kCmd)},
                  .perform = [&s] { s.addAudioTrack(); }});

    actions_.add({.id = "view.browser", .name = "Show browser", .category = "View", .keys = {key('b', kCmd | kAlt)},
                  .perform = [this] { toggle(browserPanel_); }, .isToggled = [this] { return browserPanel_.open; }});
    actions_.add({.id = "view.inspector", .name = "Show inspector", .category = "View", .keys = {key('i', kCmd | kAlt)},
                  .perform = [this] { toggle(inspectorPanel_); }, .isToggled = [this] { return inspectorPanel_.open; }});
    actions_.add({.id = "view.detail", .name = "Show detail panel", .category = "View", .keys = {key('d', kCmd | kAlt)},
                  .perform = [this] { toggle(detailPanel_); }, .isToggled = [this] { return detailPanel_.open; }});
    actions_.add({.id = "view.detailMode", .name = "Switch devices / clip view", .category = "View",
                  .keys = {key(juce::KeyPress::tabKey)},
                  .perform = [this] {
                      detail_.setMode(detail_.mode() == DetailPanel::Mode::Devices ? DetailPanel::Mode::Clip
                                                                                   : DetailPanel::Mode::Devices);
                      if (!detailPanel_.open)
                          toggle(detailPanel_);
                  }});
    actions_.add({.id = "view.focus", .name = "Focus mode", .category = "View", .keys = {key('f', kCmd | kAlt)},
                  .perform = [this] {
                      focusMode_ = !focusMode_;
                      resized();
                  },
                  .isToggled = [this] { return focusMode_; }});
    actions_.add({.id = "view.zoomIn", .name = "Zoom in", .category = "View", .keys = {key('='), key('+')},
                  .perform = [this] { arrangement_.zoomIn(); }});
    actions_.add({.id = "view.zoomOut", .name = "Zoom out", .category = "View", .keys = {key('-')},
                  .perform = [this] { arrangement_.zoomOut(); }});
    actions_.add({.id = "view.zoomFit", .name = "Zoom to fit", .category = "View", .keys = {key('0', kCmd)},
                  .perform = [this] { arrangement_.zoomToFit(); }});
    actions_.add({.id = "view.snap", .name = "Snap to grid", .category = "View", .keys = {key('4', kCmd)},
                  .perform = [this] { arrangement_.setSnapEnabled(!arrangement_.isSnapEnabled()); },
                  .isToggled = [this] { return arrangement_.isSnapEnabled(); }});
    actions_.add({.id = "app.audioSettings", .name = "Audio settings", .category = "App", .keys = {key(',', kCmd)},
                  .perform = [this] { showAudioSettings(); }});
}

bool Shell::keyPressed(const juce::KeyPress& k)
{
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false; // typing goes to the text field
    return actions_.handleKey(k);
}

void Shell::toggle(Panel& p)
{
    p.open = !p.open;
    if (focusMode_)
        focusMode_ = false;
    resized();
}

void Shell::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(session_.deviceManager(), 0, 2, 2, 2, false,
                                                                         false, true, false);
    selector->setSize(500, 380);
    // A popover next to its button, closed by clicking away (ui-principles §2: no modal dialog).
    auto& anchor = transport_->settingsButton();
    juce::CallOutBox::launchAsynchronously(std::move(selector), getLocalArea(&anchor, anchor.getLocalBounds()), this);
}

// ---- selection -----------------------------------------------------------------------------

std::optional<model::TrackInfo> Shell::selectedTrackInfo() const
{
    const auto id = arrangement_.selectedTrack();
    if (!id)
        return std::nullopt;
    for (const auto& t : session_.snapshot()->tracks)
        if (t.id == *id)
            return t;
    return std::nullopt;
}

void Shell::selectionChanged(std::optional<std::uint64_t>)
{
    const auto info = selectedTrackInfo();
    inspector_.setSelection(info);
    detail_.setSelection(info);
}

void Shell::showWarning(const juce::String& message)
{
    warning_ = message;
    warningUntil_ = juce::Time::getMillisecondCounterHiRes() * 0.001 + 5.0;
    repaint();
}

// ---- frames and layout ---------------------------------------------------------------------

void Shell::onFrame(double timestamp)
{
    const double dt = lastFrame_ > 0.0 ? std::clamp(timestamp - lastFrame_, 0.0, 0.1) : 1.0 / 60.0;
    lastFrame_ = timestamp;

    bool relayout = false;
    for (auto* p : {&browserPanel_, &inspectorPanel_, &detailPanel_})
    {
        const float target = p->open ? 1.0f : 0.0f;
        if (p->shown != target)
        {
            const auto k = static_cast<float>(1.0 - std::exp(-dt / kPanelTimeConstant));
            p->shown += (target - p->shown) * k;
            if (std::abs(p->shown - target) < 0.01f)
                p->shown = target;
            relayout = true;
        }
    }
    if (relayout)
        resized();

    const double position = session_.position();
    arrangement_.frame(dt, position);
    transport_->frame(position, session_.snapshot()->tempo);

    if (warning_.isNotEmpty() && timestamp > 0.0 && juce::Time::getMillisecondCounterHiRes() * 0.001 > warningUntil_)
    {
        warning_.clear();
        repaint();
    }
}

void Shell::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface0); // shows through the 1 px gaps between panels
}

void Shell::paintOverChildren(juce::Graphics& g)
{
    if (warning_.isEmpty())
        return;
    const auto& t = ui::Theme::current();
    auto box = arrangement_.getBounds().reduced(16).removeFromBottom(36).removeFromRight(420);
    g.setColour(t.surface3);
    g.fillRoundedRectangle(box.toFloat(), t.radiusPanel);
    g.setColour(t.textPrimary);
    g.setFont(ui::fonts::font(t.textSm));
    g.drawFittedText(warning_, box.reduced(12, 4), juce::Justification::centredLeft, 2);
}

void Shell::resized()
{
    // Adaptive: crossing the compact threshold resets the side panels; toggles win otherwise.
    const bool compact = getWidth() < compactWidth;
    if (compact_ != compact)
    {
        compact_ = compact;
        browserPanel_.open = inspectorPanel_.open = !compact;
        browserPanel_.shown = inspectorPanel_.shown = compact ? 0.0f : 1.0f;
    }

    auto r = getLocalBounds();
    transport_->setBounds(r.removeFromTop(TransportBar::height));
    r.removeFromTop(1);

    const bool showSides = !focusMode_;
    const int detailH = showSides ? juce::roundToInt(detailPanel_.size * detailPanel_.shown) : 0;
    detail_.setVisible(detailH > 0);
    detail_.setBounds(r.removeFromBottom(detailH));
    if (detailH > 0)
        r.removeFromBottom(1);

    const float browserW = showSides ? kRailWidth + (browserPanel_.size - kRailWidth) * browserPanel_.shown : 0.0f;
    browser_.setVisible(browserW > 0.0f);
    browser_.setRail(browserPanel_.shown < 0.5f);
    browser_.setBounds(r.removeFromLeft(juce::roundToInt(browserW)));
    browserSplit_.setVisible(showSides && browserPanel_.open);
    browserSplit_.setBounds(r.removeFromLeft(browserW > 0.0f ? 1 : 0).withWidth(5).translated(-2, 0));

    const int inspectorW = showSides ? juce::roundToInt(inspectorPanel_.size * inspectorPanel_.shown) : 0;
    inspector_.setVisible(inspectorW > 0);
    inspector_.setBounds(r.removeFromRight(inspectorW));
    inspectorSplit_.setVisible(showSides && inspectorPanel_.open);
    inspectorSplit_.setBounds(r.removeFromRight(inspectorW > 0 ? 1 : 0).withWidth(5).translated(-2, 0));

    arrangement_.setBounds(r);
    browserSplit_.toFront(false);
    inspectorSplit_.toFront(false);
}
} // namespace fd::app
