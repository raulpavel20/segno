#include "ui/Panels.h"

#include <fd/device/Registry.h>

namespace fd::app
{
namespace
{
juce::String formatTime(double seconds)
{
    const auto ms = static_cast<long>(std::floor(std::max(0.0, seconds) * 1000.0));
    return juce::String(ms / 60000) + ":" + juce::String((ms / 1000) % 60).paddedLeft('0', 2) + "." +
           juce::String(ms % 1000).paddedLeft('0', 3);
}
} // namespace

// ---- TransportBar --------------------------------------------------------------------------

TransportBar::TransportBar(engine::Session& session, const ActionRegistry& actions) : session_(session), actions_(actions)
{
    add(play_, "transport.playStop");
    add(stop_, "transport.toStart");
    add(loop_, "transport.loop");
    add(metronome_, "transport.metronome");
    add(keyboard_, "midi.computerKeyboard");
    for (auto* view : {&arrangeView_, &mixView_})
    {
        view->setClickingTogglesState(false);
        view->setWantsKeyboardFocus(false);
        view->onClick = [this, view] {
            if ((view == &mixView_) != actions_.isToggled("view.mixer"))
                actions_.perform("view.mixer");
        };
        addAndMakeVisible(*view);
    }
    arrangeView_.setTooltip("Arrangement (" + actions_.tooltip("view.mixer").fromFirstOccurrenceOf("(", false, false));
    mixView_.setTooltip(actions_.tooltip("view.mixer"));

    // Tempo and time signature: drag or type (each edit is one undo step).
    tempoField_.setRange(20.0, 999.0, 0.01);
    tempoField_.setDragSensitivity(0.1);
    tempoField_.setTooltip("Tempo: drag or type");
    tempoField_.textFromValue = [](double v) { return juce::String(v, 2); };
    tempoField_.onValueChange = [this](double v) { session_.setTempo(v); };
    numeratorField_.setRange(1.0, 32.0, 1.0);
    numeratorField_.setDragSensitivity(0.1);
    numeratorField_.setTooltip("Beats per bar");
    numeratorField_.textFromValue = [](double v) { return juce::String(juce::roundToInt(v)); };
    numeratorField_.onValueChange = [this](double v) { session_.setTimeSignature(juce::roundToInt(v), tempo_.denominator); };
    denominatorField_.setRange(0.0, 4.0, 1.0); // index: 1, 2, 4, 8, 16
    denominatorField_.setDragSensitivity(0.05);
    denominatorField_.setTooltip("Beat unit");
    denominatorField_.textFromValue = [](double v) { return juce::String(1 << juce::roundToInt(v)); };
    denominatorField_.valueFromText = [](const juce::String& s) -> std::optional<double> {
        const int d = s.getIntValue();
        for (int i = 0; i <= 4; ++i)
            if ((1 << i) == d)
                return i;
        return std::nullopt;
    };
    denominatorField_.onValueChange = [this](double v) {
        session_.setTimeSignature(tempo_.numerator, 1 << juce::roundToInt(v));
    };
    for (auto* f : {&tempoField_, &numeratorField_, &denominatorField_})
    {
        f->setFontSize(ui::Theme::current().textMd);
        addAndMakeVisible(f);
    }
    frame(0.0, session_.snapshot()->tempo); // show the project's tempo before the first frame
    add(browser_, "view.browser");
    add(detail_, "view.detail");
    add(inspector_, "view.inspector");
    add(settings_, "app.audioSettings");
    if (actions_.find("app.assistants") != nullptr)
        add(assistants_, "app.assistants");
}

ui::IconButton& TransportBar::add(ui::IconButton& b, std::string id)
{
    b.setWantsKeyboardFocus(false);
    b.setTooltip(actions_.tooltip(id));
    b.onClick = [this, id] { actions_.perform(id); };
    addAndMakeVisible(b);
    bound_.emplace_back(&b, std::move(id));
    return b;
}

void TransportBar::frame(double positionSeconds, const model::TempoMap& tempo)
{
    for (auto& [button, id] : bound_)
        if (button->getToggleState() != actions_.isToggled(id))
            button->setToggleState(actions_.isToggled(id), juce::dontSendNotification);
    const bool mix = actions_.isToggled("view.mixer");
    if (mixView_.getToggleState() != mix)
    {
        mixView_.setToggleState(mix, juce::dontSendNotification);
        arrangeView_.setToggleState(!mix, juce::dontSendNotification);
    }
    else if (!mix && !arrangeView_.getToggleState())
        arrangeView_.setToggleState(true, juce::dontSendNotification);
    if (!tempoShown_ || !(tempo == tempo_))
    {
        tempoShown_ = true;
        tempo_ = tempo;
        tempoField_.setValue(tempo.bpm, juce::dontSendNotification);
        numeratorField_.setValue(tempo.numerator, juce::dontSendNotification);
        denominatorField_.setValue(std::log2(std::max(1, tempo.denominator)), juce::dontSendNotification);
        position_ = -1.0; // redraw the position in the new tempo
    }
    // MIDI activity: lit while notes are held, and briefly after a tap.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (session_.midiNotesHeld())
        lastMidi_ = now;
    const bool lit = lastMidi_ >= 0.0 && now - lastMidi_ < 0.12;
    if (lit != midiLit_)
    {
        midiLit_ = lit;
        repaint(midiLightArea());
    }

    if (const bool assistant = assistantsActive && assistantsActive(); assistant != assistantLit_)
    {
        assistantLit_ = assistant;
        repaint(assistantLight_);
    }

    const int cpu = static_cast<int>(std::lround(session_.cpuUsage() * 100.0));
    if (positionSeconds != position_ || cpu != cpu_)
    {
        position_ = positionSeconds;
        cpu_ = cpu;
        repaint(readoutArea_);
    }
}

void TransportBar::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (!brandArea_.isEmpty())
        ui::paintBrand(g, brandArea_);

    // BPM label after the tempo field, the slash between the signature fields.
    g.setColour(t.textTertiary);
    g.setFont(ui::font(ui::TextStyle::ValueSmall));
    g.drawText("BPM", bpmArea_, juce::Justification::centredLeft, false);
    g.setFont(ui::font(ui::TextStyle::ValueMedium));
    g.drawText("/", slashArea_, juce::Justification::centred, false);

    // MIDI activity light beside the computer keyboard button.
    const auto light = midiLightArea().toFloat().withSizeKeepingCentre(6.0f, 6.0f);
    g.setColour(midiLit_ ? t.accentMidi : t.surface3);
    g.fillEllipse(light);
    // Assistant activity beside the assistants button: lit after a request.
    if (assistants_.isVisible())
    {
        g.setColour(assistantLit_ ? t.active : t.surface3);
        g.fillEllipse(assistantLight_.toFloat().withSizeKeepingCentre(6.0f, 6.0f));
    }

    auto r = readoutArea_;
    auto field = [&](const juce::String& value, const juce::String& unit, int width, ui::TextStyle style) {
        auto box = r.removeFromLeft(width);
        r.removeFromLeft(16);
        g.setColour(t.textPrimary);
        g.setFont(ui::font(style));
        const int valueWidth = static_cast<int>(juce::GlyphArrangement::getStringWidth(ui::font(style), value)) + 4;
        g.drawText(value, box.removeFromLeft(valueWidth), juce::Justification::centredLeft, false);
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(unit, box, juce::Justification::centredLeft, false);
    };
    const auto bb = tempo_.barsBeatsForSeconds(std::max(0.0, position_));
    field(juce::String(bb.bar) + ". " + juce::String(bb.beat) + ". " + juce::String(bb.sixteenth), "", 84,
          ui::TextStyle::ValueLarge);
    field(formatTime(position_), "", 84, ui::TextStyle::ValueMedium);

    g.setColour(t.textTertiary);
    g.setFont(ui::font(ui::TextStyle::Value));
    g.drawText("CPU " + juce::String(std::max(0, cpu_)) + " %", r, juce::Justification::centredRight, false);
}

void TransportBar::resized()
{
    // One rhythm for the whole bar (docs/design/ui-foundations.md): items 4 px apart, groups 12.
    const auto& t = ui::Theme::current();
    ui::BarLayout bar(getLocalBounds().reduced(t.shell.barPadding, t.shell.barInset));
    // The mark and name at the top left (ADR 0024), the height of the bar's buttons.
    brandArea_ = bar.left(ui::brandWidth(bar.remaining().getHeight()));
    bar.groupLeft();
    play_.setBounds(bar.left());
    stop_.setBounds(bar.left());
    bar.groupLeft();
    loop_.setBounds(bar.left());
    metronome_.setBounds(bar.left());
    bar.groupLeft();
    // Fields sized for their mono digits: "999.00", "32".
    const auto digit = juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::ValueMedium), "0");
    tempoField_.setBounds(bar.left(static_cast<int>(std::ceil(digit * 6.0f)) + 10));
    bpmArea_ = bar.left(static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::ValueSmall), "BPM"))));
    bar.groupLeft();
    const int small = static_cast<int>(std::ceil(digit * 2.0f)) + 10;
    numeratorField_.setBounds(bar.left(small));
    slashArea_ = bar.left(static_cast<int>(std::ceil(digit)));
    denominatorField_.setBounds(bar.left(small));

    settings_.setBounds(bar.right());
    if (assistants_.isVisible())
    {
        bar.groupRight();
        assistantLight_ = bar.right(t.shell.itemGap * 2);
        assistants_.setBounds(bar.right());
    }
    bar.groupRight();
    for (auto* view : {&mixView_, &arrangeView_}) // right to left: "Arrange | Mix"
        view->setBounds(bar.right(static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(
                                      ui::font(ui::TextStyle::Control), view->getButtonText()))) + 16));
    bar.groupRight();
    inspector_.setBounds(bar.right());
    detail_.setBounds(bar.right());
    browser_.setBounds(bar.right());
    // The computer keyboard toggle sits on the right, like the MIDI section of a control bar.
    bar.groupRight();
    midiLight_ = bar.right(t.shell.itemGap * 2);
    keyboard_.setBounds(bar.right());
    bar.groupRight();
    bar.groupLeft();
    readoutArea_ = bar.left(0).withRight(bar.right(0).getX()).withTop(0).withBottom(getHeight());
}

// ---- BrowserPanel --------------------------------------------------------------------------

BrowserPanel::BrowserPanel(const ActionRegistry& actions) : actions_(actions)
{
    expand_.setWantsKeyboardFocus(false);
    expand_.onClick = [this] { actions_.perform("view.browser"); };
    addChildComponent(expand_);

    search_.setTextToShowWhenEmpty("Search", ui::Theme::current().textTertiary);
    search_.setTooltip("Search arrives with the browser (M3)");
    search_.setEnabled(false);
    addAndMakeVisible(search_);

    std::vector<juce::String> instruments, effects;
    for (const auto* f : fd::allDevices())
    {
        const auto name = juce::String(std::string(f->info().name));
        (f->info().kind == DeviceKind::Instrument ? instruments : effects).push_back(name);
    }
    sections_ = {{"Instruments", instruments}, {"Audio effects", effects}, {"Samples", {}}, {"Plugins", {}}};
}

void BrowserPanel::setRail(bool rail)
{
    rail_ = rail;
    search_.setVisible(!rail);
    expand_.setVisible(rail);
    expand_.setTooltip(actions_.tooltip("view.browser")); // actions are registered after construction
    repaint();
}

void BrowserPanel::resized()
{
    const auto& sh = ui::Theme::current().shell;
    search_.setBounds(getLocalBounds().reduced(sh.barPadding).removeFromTop(sh.fieldHeight));
    expand_.setBounds(getLocalBounds().removeFromTop(sh.railWidth).withSizeKeepingCentre(sh.buttonSize, sh.buttonSize));
}

void BrowserPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (rail_)
        return;
    auto r = getLocalBounds().reduced(t.shell.padding, 0).withTrimmedTop(search_.getBottom() + t.shell.barPadding);
    for (const auto& section : sections_)
    {
        ui::paintCapsLabel(g, section.title, r.removeFromTop(t.shell.panelHeaderHeight).toFloat());
        if (section.items.empty())
            ui::paintListRow(g, r.removeFromTop(t.shell.rowHeight).withTrimmedLeft(-10),
                             {.name = "Coming with the browser", .enabled = false});
        for (const auto& item : section.items)
            ui::paintListRow(g, r.removeFromTop(t.shell.rowHeight).withTrimmedLeft(-10), {.name = item});
        r.removeFromTop(t.shell.groupGap);
    }
}

// ---- InspectorPanel ------------------------------------------------------------------------

void InspectorPanel::setSelection(std::optional<model::TrackInfo> track, std::optional<model::ClipInfo> clip,
                                  int clipCount, model::TempoMap tempo)
{
    track_ = std::move(track);
    clip_ = std::move(clip);
    clipCount_ = clipCount;
    tempo_ = tempo;
    channel_.setChannel(track_, session_.snapshot()->returns);
    resized();
    repaint();
}

InspectorPanel::InspectorPanel(engine::Session& session) : session_(session)
{
    header_.setTitle("Inspector");
    addAndMakeVisible(header_);
    addAndMakeVisible(channel_);
}

std::vector<ui::Property> InspectorPanel::clipProperties() const
{
    auto position = [&](double seconds) {
        const auto bb = tempo_.barsBeatsForSeconds(seconds);
        return juce::String(bb.bar) + "." + juce::String(bb.beat) + "." + juce::String(bb.sixteenth);
    };
    if (clip_)
        return {{"Clip", juce::String::fromUTF8(clip_->name.c_str())},
                {"Start", position(clip_->start), true},
                {"Length", juce::String(clip_->length, 3) + " s", true},
                {"Offset", juce::String(clip_->offset, 3) + " s", true}};
    if (clipCount_ > 1)
        return {{"Clips", juce::String(clipCount_) + " selected", true}};
    return {};
}

std::vector<ui::Property> InspectorPanel::channelProperties() const
{
    if (!track_)
        return {};
    const auto name = juce::String::fromUTF8(track_->name.c_str());
    const auto modules = juce::String(static_cast<int>(track_->devices.size()));
    switch (track_->role)
    {
    case model::TrackInfo::Role::Return: return {{"Return", name}, {"Modules", modules, true}};
    case model::TrackInfo::Role::Master: return {{"Master", "The mix"}, {"Modules", modules, true}};
    case model::TrackInfo::Role::Track: break;
    }
    return {{"Track", name},
            {"Instrument", track_->instrument.empty() ? juce::String("None")
                                                      : juce::String::fromUTF8(track_->instrument.c_str())},
            {"Clips", juce::String(static_cast<int>(track_->clips.size())), true}};
}

void InspectorPanel::resized()
{
    const auto& t = ui::Theme::current();
    header_.setBounds(getLocalBounds().removeFromTop(t.shell.panelHeaderHeight));
    // The channel's controls, under the property rows.
    auto r = getLocalBounds().withTrimmedTop(header_.getBottom()).reduced(t.shell.padding, 0);
    if (const auto clipRows = static_cast<int>(clipProperties().size()); clipRows > 0)
        r.removeFromTop(clipRows * t.shell.rowHeight + t.shell.groupGap);
    r.removeFromTop(static_cast<int>(channelProperties().size()) * t.shell.rowHeight + t.shell.groupGap);
    channel_.setBounds(r.withHeight(channel_.preferredHeight()));
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    auto r = getLocalBounds().withTrimmedTop(header_.getBottom()).reduced(t.shell.padding, 0);
    if (!track_ && clipCount_ == 0)
    {
        ui::paintEmptyState(g, r.removeFromTop(t.shell.listRowHeight * 2), "Nothing selected");
        return;
    }
    if (const auto rows = clipProperties(); !rows.empty())
    {
        r = ui::paintProperties(g, r, rows);
        r.removeFromTop(t.shell.groupGap);
    }
    ui::paintProperties(g, r, channelProperties());
}

// ---- DetailPanel ---------------------------------------------------------------------------

DetailPanel::DetailPanel(engine::Session& session, const ActionRegistry& actions) : chain_(session, actions), roll_(session)
{
    header_.setTabs({"Modules", "Clip"}, 0);
    header_.setCaption("Tab switches");
    header_.onTabSelected = [this](int tab) { setMode(tab == 0 ? Mode::Devices : Mode::Clip); };
    addAndMakeVisible(header_);
    addAndMakeVisible(chain_);
    addChildComponent(roll_);
}

void DetailPanel::updateHeader()
{
    header_.setSelectedTab(mode_ == Mode::Devices ? 0 : 1);
    if (mode_ == Mode::Devices)
        header_.setContext(track_ ? juce::String::fromUTF8(track_->name.c_str()) : juce::String());
    else
        header_.setContext(roll_.clip() ? juce::String::fromUTF8(roll_.clip()->name.c_str()) : juce::String());
}

void DetailPanel::setMode(Mode m)
{
    if (mode_ == m)
        return;
    mode_ = m;
    updateVisibility();
    updateHeader();
    repaint();
}

void DetailPanel::updateVisibility()
{
    chain_.setVisible(mode_ == Mode::Devices);
    roll_.setVisible(showsPianoRoll());
}

void DetailPanel::setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo)
{
    roll_.setClip(std::move(clip), tempo);
    updateVisibility();
    updateHeader();
}

void DetailPanel::frame(double playheadSeconds)
{
    if (roll_.isVisible())
        roll_.frame(playheadSeconds);
}

void DetailPanel::setSelection(std::optional<model::TrackInfo> track)
{
    track_ = std::move(track);
    chain_.setTrack(track_);
    updateHeader();
    repaint();
}

void DetailPanel::resized()
{
    const auto& sh = ui::Theme::current().shell;
    auto r = getLocalBounds();
    header_.setBounds(r.removeFromTop(sh.panelHeaderHeight));
    chain_.setBounds(r.reduced(sh.itemGap, 0).withTrimmedBottom(sh.itemGap));
    roll_.setBounds(r);
}

void DetailPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface1);
    if (mode_ == Mode::Clip && !roll_.clip())
        ui::paintEmptyState(g, getLocalBounds().withTrimmedTop(header_.getBottom()),
                            "Select a MIDI clip to edit its notes. Double-click an empty spot on a track to make one.");
}
void DetailPanel::selectModule(std::uint64_t id)
{
    setMode(Mode::Devices);
    chain_.selectDevice(id);
}
} // namespace fd::app
