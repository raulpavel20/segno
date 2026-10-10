#include "ui/Panels.h"

#include "model/Takes.h"
#include "model/TopBarLayout.h"

#include <fd/device/Registry.h>

#include <algorithm>
#include <map>

namespace fd::app
{
namespace
{
/// An inspector name field: typed, Return or leaving it commits, Escape restores.
void styleNameField(juce::TextEditor& field)
{
    const auto& t = ui::Theme::current();
    field.setFont(ui::font(ui::TextStyle::Body));
    field.setIndents(t.shell.fieldTextInset, 0); // its text where a combo box's starts
    field.setJustification(juce::Justification::centredLeft);
    field.setColour(juce::TextEditor::backgroundColourId, t.surface2);
    field.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    field.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
}

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
    add(record_, "transport.record");
    record_.setRecordAccent(true);
    record_.onPopupMenu = [this] { showRecordMenu(); };
    add(capture_, "transport.capture");
    add(loop_, "transport.loop");
    add(follow_, "view.follow");
    add(metronome_, "transport.metronome");
    add(countIn_, "transport.countIn");
    countIn_.setTooltip(actions_.tooltip("transport.countIn") + "; right-click for its length");
    countIn_.onPopupMenu = [this] { countInMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&countIn_)); };
    add(automationArm_, "automation.arm");
    automationArm_.setRecordAccent(true); // it records (automation.md §8)
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
    numeratorField_.setJustification(juce::Justification::centredRight); // against the slash
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
    {
        if (button->getToggleState() != actions_.isToggled(id))
            button->setToggleState(actions_.isToggled(id), juce::dontSendNotification);
        if (button->isEnabled() != actions_.isEnabled(id))
            button->setEnabled(actions_.isEnabled(id)); // e.g. the inspector in the mixer view
    }
    if (captureTooltip)
        if (const auto tip = captureTooltip(); tip != capture_.getTooltip())
            capture_.setTooltip(tip);
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
        repaint(wellArea_);
        repaint(cpuArea_);
    }
}

juce::PopupMenu TransportBar::countInMenu()
{
    using C = model::CountIn;
    juce::PopupMenu countIn;
    const auto now = session_.countIn();
    for (const auto& [value, name] : {std::pair{C::Off, "Off"}, std::pair{C::OneBeat, "1 beat"}, std::pair{C::TwoBeats, "2 beats"},
                                      std::pair{C::OneBar, "1 bar"}, std::pair{C::TwoBars, "2 bars"}})
        countIn.addItem(name, true, now == value, [this, v = value] { session_.setCountIn(v); });
    return countIn;
}

void TransportBar::showRecordMenu()
{
    juce::PopupMenu countIn = countInMenu(), midi;
    const bool overdub = session_.midiOverdub();
    midi.addItem("Each lap: a new take", true, !overdub, [this] { session_.setMidiOverdub(false); });
    midi.addItem("Each lap: add to the clip (overdub)", true, overdub, [this] { session_.setMidiOverdub(true); });
    juce::PopupMenu menu;
    menu.addSubMenu("Count-in", countIn);
    menu.addSubMenu("MIDI loop recording", midi);
    menu.addSeparator();
    juce::PopupMenu::Item settings(juce::String::fromUTF8("Recording settings\xE2\x80\xA6"));
    settings.setAction([this] { actions_.perform("app.audioSettings"); });
    menu.addItem(std::move(settings));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&record_));
}

void TransportBar::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (markOnly_)
        ui::paintMark(g, brandArea_.toFloat(), t.active);
    else if (!brandArea_.isEmpty())
        ui::paintBrand(g, brandArea_);

    // BPM label after the tempo field, the slash between the signature fields.
    g.setColour(t.textTertiary);
    g.setFont(ui::font(ui::TextStyle::ValueSmall));
    g.drawText("BPM", bpmArea_, juce::Justification::centredLeft, false);
    g.setFont(ui::font(ui::TextStyle::ValueMedium));
    g.drawText("/", slashArea_, juce::Justification::centred, false);

    // MIDI activity light beside the computer keyboard button.
    const auto lightSize = static_cast<float>(t.shell.lightSize);
    const auto light = midiLightArea().toFloat().withSizeKeepingCentre(lightSize, lightSize);
    g.setColour(midiLit_ ? t.accentMidi : t.surface3);
    g.fillEllipse(light);
    // Assistant activity beside the assistants button: lit after a request.
    if (assistants_.isVisible())
    {
        g.setColour(assistantLit_ ? t.active : t.surface3);
        g.fillEllipse(assistantLight_.toFloat().withSizeKeepingCentre(lightSize, lightSize));
    }

    // The position, in an inset well at the centre: bars.beats.16ths, then the time.
    g.setColour(t.surface0);
    g.fillRoundedRectangle(wellArea_.toFloat(), t.radiusControl);
    // The bars sit right, against the time (centred when the time is dropped, in a narrow window);
    // both keep the field inset from the well's edges.
    const int inset = t.shell.fieldTextInset;
    const auto barsText = barsArea_.reduced(inset, 0);
    const auto barsJustification = timeArea_.isEmpty() ? juce::Justification::centred : juce::Justification::centredRight;
    if (const auto from = session_.recordingFrom(); from && session_.isPlaying() && position_ < *from - 1e-3)
    {
        const auto beats = static_cast<int>(std::ceil(tempo_.beatsForSeconds(*from) - tempo_.beatsForSeconds(position_) - 1e-6));
        g.setColour(t.record);
        g.setFont(ui::font(ui::TextStyle::ValueLarge));
        g.drawText(juce::String::fromUTF8("\xE2\x88\x92") + juce::String(beats), barsText, barsJustification, false);
    }
    else
    {
        const auto bb = tempo_.barsBeatsForSeconds(std::max(0.0, position_));
        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::ValueLarge));
        g.drawText(juce::String(bb.bar) + ". " + juce::String(bb.beat) + ". " + juce::String(bb.sixteenth), barsText,
                   barsJustification, false);
    }
    if (!timeArea_.isEmpty())
    {
        g.setColour(t.textSecondary);
        g.setFont(ui::font(ui::TextStyle::ValueMedium));
        g.drawText(formatTime(position_), timeArea_.withTrimmedRight(inset), juce::Justification::centredLeft, false);
    }

    if (!cpuArea_.isEmpty())
    {
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Value));
        g.drawText("CPU " + juce::String(std::max(0, cpu_)) + " %", cpuArea_, juce::Justification::centredLeft, false);
    }
}

void TransportBar::resized()
{
    // Three sections over the whole width (daw-shell.md, "Top bar"): the brand and the song on the
    // left, the transport centred on the window, status, views and app on the right. One rhythm:
    // items 4 px apart, groups 12; model::layoutTopBar places them and drops what doesn't fit.
    const auto& t = ui::Theme::current();
    const auto inner = getLocalBounds().reduced(t.shell.barPadding, t.shell.barInset);
    const int h = inner.getHeight();
    auto textWidth = [](ui::TextStyle style, const juce::String& text) {
        return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(style), text)));
    };
    const auto digit = juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::ValueMedium), "0");
    const int inset = t.shell.fieldTextInset; // the position's text from its well's edges
    const int mark = ui::brandMarkWidth(h);
    const int light = 2 * t.shell.itemGap; // a light's slot (the light is shell.lightSize)

    using S = model::BarSection;
    struct Entry
    {
        model::BarItem item;
        std::function<void(juce::Rectangle<int>)> place; // empty rectangle: dropped
    };
    auto button = [](juce::Component& c) {
        return [&c](juce::Rectangle<int> r) {
            c.setVisible(!r.isEmpty());
            c.setBounds(r);
        };
    };
    auto area = [](juce::Rectangle<int>& a) { return [&a](juce::Rectangle<int> r) { a = r; }; };
    juce::Rectangle<int> markArea, wordArea;
    const bool assistants = actions_.find("app.assistants") != nullptr;
    std::vector<Entry> entries{
        // Left: the brand, then the song.
        {{S::Left, static_cast<double>(mark)}, area(markArea)},
        {{S::Left, static_cast<double>(ui::brandWidth(h) - mark - t.shell.itemGap), false, 2}, area(wordArea)},
        {{S::Left, static_cast<double>(tempoField_.widthFor("999.00")), true}, button(tempoField_)},
        {{S::Left, static_cast<double>(textWidth(ui::TextStyle::ValueSmall, "BPM"))}, area(bpmArea_)},
        {{S::Left, static_cast<double>(numeratorField_.widthFor("32")), true}, button(numeratorField_)},
        {{S::Left, std::ceil(digit)}, area(slashArea_)},
        {{S::Left, static_cast<double>(denominatorField_.widthFor("16"))}, button(denominatorField_)},
        {{S::Left, static_cast<double>(h), true}, button(metronome_)},
        {{S::Left, static_cast<double>(h)}, button(countIn_)},
        // Centre: the transport, the position, the loop.
        {{S::Centre, static_cast<double>(h)}, button(play_)},
        {{S::Centre, static_cast<double>(h)}, button(stop_)},
        {{S::Centre, static_cast<double>(h)}, button(record_)},
        {{S::Centre, static_cast<double>(h)}, button(capture_)},
        {{S::Centre, static_cast<double>(h)}, button(automationArm_)},
        {{S::Centre, static_cast<double>(textWidth(ui::TextStyle::ValueLarge, "000. 00. 00") + 2 * inset), true}, area(barsArea_)},
        {{S::Centre, static_cast<double>(textWidth(ui::TextStyle::ValueMedium, "00:00.000") + inset), false, 4}, area(timeArea_)},
        {{S::Centre, static_cast<double>(h), true}, button(loop_)},
        {{S::Centre, static_cast<double>(h)}, button(follow_)},
        // Right: status, views, app.
        {{S::Right, static_cast<double>(textWidth(ui::TextStyle::Value, "CPU 100 %")), false, 3}, area(cpuArea_)},
        {{S::Right, static_cast<double>(h), true}, button(keyboard_)},
        {{S::Right, static_cast<double>(light)}, area(midiLight_)},
        {{S::Right, static_cast<double>(ui::Toggle::widthFor(arrangeView_.getButtonText())), true}, button(arrangeView_)},
        {{S::Right, static_cast<double>(ui::Toggle::widthFor(mixView_.getButtonText()))}, button(mixView_)},
        {{S::Right, static_cast<double>(h), true, 1}, button(browser_)},
        {{S::Right, static_cast<double>(h), false, 1}, button(detail_)},
        {{S::Right, static_cast<double>(h), false, 1}, button(inspector_)},
    };
    if (assistants)
    {
        entries.push_back({{S::Right, static_cast<double>(h), true}, button(assistants_)});
        entries.push_back({{S::Right, static_cast<double>(light)}, area(assistantLight_)});
    }
    entries.push_back({{S::Right, static_cast<double>(h), !assistants}, button(settings_)});

    std::vector<model::BarItem> items;
    for (const auto& e : entries)
        items.push_back(e.item);
    const auto slots = model::layoutTopBar(getWidth(), items,
                                           {.padding = static_cast<double>(t.shell.barPadding),
                                            .itemGap = static_cast<double>(t.shell.itemGap),
                                            .groupGap = static_cast<double>(t.shell.groupGap),
                                            .sectionGap = 2.0 * t.shell.groupGap});
    for (size_t i = 0; i < entries.size(); ++i)
        entries[i].place(slots[i].shown ? juce::Rectangle<int>(juce::roundToInt(slots[i].x), inner.getY(),
                                                               juce::roundToInt(slots[i].width), h)
                                        : juce::Rectangle<int>());
    markOnly_ = wordArea.isEmpty();
    brandArea_ = markOnly_ ? markArea : markArea.getUnion(wordArea);
    wellArea_ = timeArea_.isEmpty() ? barsArea_ : barsArea_.getUnion(timeArea_);
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

InspectorPanel::InspectorPanel(engine::Session& session) : session_(session)
{
    header_.setTitle("Inspector");
    addAndMakeVisible(header_);
    addAndMakeVisible(mix_);
    addAndMakeVisible(sends_);

    pointValue_.setRange(0.0, 1.0, 0.0);
    pointValue_.setFontSize(ui::Theme::current().textXs); // a property row's number (the Value style)
    pointValue_.setDragSensitivity(0.005);
    pointValue_.setTooltip("The point's value: drag or type");
    pointValue_.textFromValue = [this](double v) {
        return lane_ ? juce::String::fromUTF8(session_.laneText(lane_->id, static_cast<float>(v)).c_str()) : juce::String();
    };
    pointValue_.valueFromText = [this](const juce::String& text) -> std::optional<double> {
        if (!lane_)
            return std::nullopt;
        if (const auto v = session_.laneFromText(lane_->id, text.toStdString()))
            return *v;
        return std::nullopt;
    };
    pointValue_.onGestureStart = [this] { pointGesture_.emplace(session_, "Point value"); };
    pointValue_.onGestureEnd = [this] { pointGesture_.reset(); };
    pointValue_.onValueChange = [this](double v) {
        if (!lane_ || !pointIndex_ || *pointIndex_ >= lane_->points.size())
            return;
        auto points = lane_->points;
        points[*pointIndex_].value = model::snapToSteps(static_cast<float>(v), lane_->rules().steps);
        session_.setLanePoints(lane_->id, points, std::nullopt, "Point value");
    };
    addChildComponent(pointValue_);

    styleNameField(laneName_);
    laneName_.setTooltip("The lane's name: type to rename it (empty: named after what it drives)");
    auto rename = [this] {
        if (lane_ && laneName_.getText().toStdString() != lane_->name)
            session_.renameAutomationLane(lane_->id, laneName_.getText().trim().toStdString());
    };
    laneName_.onReturnKey = [rename] {
        rename();
        juce::Component::unfocusAllComponents();
    };
    laneName_.onFocusLost = rename;
    laneName_.onEscapeKey = [this] {
        laneName_.setText(lane_ ? juce::String::fromUTF8(lane_->name.c_str()) : juce::String(), false);
        unfocusAllComponents();
    };
    addChildComponent(laneName_);
    addChildComponent(assignments_);
    addChildComponent(input_);

    styleNameField(takeName_);
    takeName_.setTooltip("The take's name: type to rename it");
    auto renameTake = [this] {
        const auto name = takeName_.getText().trim().toStdString();
        if (take_ && channel_ && !name.empty() && name != take_->name)
            session_.renameTake(channel_->id, take_->id, name);
    };
    takeName_.onReturnKey = [renameTake] {
        renameTake();
        juce::Component::unfocusAllComponents();
    };
    takeName_.onFocusLost = renameTake;
    takeName_.onEscapeKey = [this] {
        takeName_.setText(take_ ? juce::String::fromUTF8(take_->name.c_str()) : juce::String(), false);
        unfocusAllComponents();
    };
    addChildComponent(takeName_);
    useTake_.setTooltip("The main lane plays this take across its time range (or click it on its take lane)");
    useTake_.onClick = [this] {
        if (take_ && channel_)
            session_.useTake(channel_->id, take_->id);
    };
    addChildComponent(useTake_);
    // Fade lengths (fades.md §3): in ms, dragged or typed ("12", "12 ms", "0.5 s").
    for (auto* field : {&fadeIn_, &fadeOut_})
    {
        const bool in = field == &fadeIn_;
        field->setFontSize(ui::Theme::current().textXs); // a property row's number (the Value style)
        field->setDragSensitivity(1.0);
        field->setTooltip(in ? "The fade-in's length: drag or type" : "The fade-out's length: drag or type");
        field->textFromValue = [](double ms) { return juce::String(juce::roundToInt(ms)) + " ms"; };
        field->valueFromText = [](const juce::String& text) -> std::optional<double> {
            const auto t = text.trim().toLowerCase();
            if (t.isEmpty() || !(t[0] == '.' || juce::CharacterFunctions::isDigit(t[0])))
                return std::nullopt;
            const double v = t.getDoubleValue();
            return t.endsWith("ms") || !t.endsWith("s") ? v : v * 1000.0;
        };
        field->onGestureStart = [this] { fadeGesture_.emplace(session_, "Fades"); };
        field->onGestureEnd = [this] { fadeGesture_.reset(); };
        field->onValueChange = [this, in](double ms) {
            if (!clip_)
                return;
            const auto fade = model::Fade{ms / 1000.0, in ? clip_->fadeIn.bend : clip_->fadeOut.bend};
            session_.setFades({clip_->id}, in ? std::optional(fade) : std::nullopt, in ? std::nullopt : std::optional(fade));
        };
        addChildComponent(*field);
    }
    clipMute_.setTooltip("Mute the clip: it keeps its place and plays nothing (0)");
    clipMute_.onClick = [this] {
        if (clip_)
            session_.setClipsMuted({clip_->id}, !clip_->muted);
    };
    addChildComponent(clipMute_);
}

void InspectorPanel::setContext(const model::PanelContext& context, const model::TempoMap& tempo,
                                const std::vector<model::TrackInfo>& returns)
{
    sections_ = context.inspector;
    channel_ = context.channel;
    clip_ = context.clip;
    clipCount_ = context.clipCount;
    lane_ = context.lane;
    point_ = context.point;
    pointCount_ = context.pointCount;
    pointIndex_ = point_ && !context.key.selection.points.empty() ? std::optional(*context.key.selection.points.begin())
                                                                  : std::nullopt;
    pointValue_.setVisible(point_.has_value());
    if (point_ && !pointGesture_)
        pointValue_.setValue(point_->value, juce::dontSendNotification);
    tempo_ = tempo;
    auto has = [this](model::InspectorSection s) { return std::find(sections_.begin(), sections_.end(), s) != sections_.end(); };
    laneName_.setVisible(has(model::InspectorSection::Lane));
    if (lane_ && !laneName_.hasKeyboardFocus(true))
    {
        laneName_.setText(juce::String::fromUTF8(lane_->name.c_str()), false);
        laneName_.setTextToShowWhenEmpty(juce::String::fromUTF8(lane_->displayName().c_str()), ui::Theme::current().textTertiary);
    }
    take_ = context.take;
    takeName_.setVisible(has(model::InspectorSection::Take));
    useTake_.setVisible(has(model::InspectorSection::Take));
    clipMute_.setVisible(has(model::InspectorSection::Clip) && clip_.has_value());
    clipMute_.setToggleState(clip_ && clip_->muted, juce::dontSendNotification);
    const bool audio = has(model::InspectorSection::Clip) && clip_ && clip_->kind == model::ClipInfo::Kind::Audio;
    for (auto* field : {&fadeIn_, &fadeOut_})
    {
        field->setVisible(audio);
        if (audio)
            field->setRange(0.0, clip_->length * 1000.0, 1.0);
    }
    if (audio && !fadeGesture_)
    {
        fadeIn_.setValue(clip_->fadeIn.length * 1000.0, juce::dontSendNotification);
        fadeOut_.setValue(clip_->fadeOut.length * 1000.0, juce::dontSendNotification);
    }
    if (take_ && !takeName_.hasKeyboardFocus(true))
        takeName_.setText(juce::String::fromUTF8(take_->name.c_str()), false);
    assignments_.setLane(has(model::InspectorSection::Assignments) ? lane_ : std::nullopt);
    assignments_.setVisible(has(model::InspectorSection::Assignments));
    input_.setTrack(has(model::InspectorSection::Input) ? channel_ : std::nullopt);
    input_.setVisible(has(model::InspectorSection::Input));
    mix_.setChannel(has(model::InspectorSection::Mix) ? channel_ : std::nullopt);
    sends_.setChannel(has(model::InspectorSection::Sends) ? channel_ : std::nullopt, returns);
    resized();
    repaint();
}

void InspectorPanel::focusLaneName()
{
    if (laneName_.isVisible() && laneName_.isShowing())
    {
        laneName_.grabKeyboardFocus();
        laneName_.selectAll();
    }
}

void InspectorPanel::focusTakeName()
{
    if (takeName_.isVisible() && takeName_.isShowing())
    {
        takeName_.grabKeyboardFocus();
        takeName_.selectAll();
    }
}

void InspectorPanel::clear()
{
    setContext({}, {}, {});
}

std::vector<ui::Property> InspectorPanel::properties(model::InspectorSection section) const
{
    using S = model::InspectorSection;
    auto position = [&](double seconds) {
        const auto bb = tempo_.barsBeatsForSeconds(seconds);
        return juce::String(bb.bar) + "." + juce::String(bb.beat) + "." + juce::String(bb.sixteenth);
    };
    const auto name = channel_ ? juce::String::fromUTF8(channel_->name.c_str()) : juce::String();
    const auto modules = channel_ ? juce::String(static_cast<int>(channel_->devices.size())) : juce::String();
    switch (section)
    {
    case S::Point:
    {
        if (!point_ || !lane_)
            return {};
        return {{"Time", position(point_->time), true},
                {"Value", {}, true}, // the field
                {"Bend", lane_->rules().stepped() ? juce::String("Step")
                                                  : juce::String(juce::roundToInt(model::bendOf(*point_) * 100.0f)) + " %",
                 true}};
    }
    case S::Points: return {{"Selected", juce::String(pointCount_), true}};
    case S::Lane:
    {
        if (!lane_)
            return {};
        const auto now = session_.laneValue(lane_->id);
        const auto plays = lane_->points.empty() || lane_->assignments.empty() ? juce::String("Nothing to play")
                           : lane_->allOn()                                     ? juce::String("On")
                           : std::none_of(lane_->assignments.begin(), lane_->assignments.end(),
                                          [](const auto& a) { return a.on; })
                               ? juce::String("Off: the values set by hand")
                               : juce::String("Some off");
        std::vector<ui::Property> rows{{"Name", {}}, // the field
                                       {"Points", juce::String(static_cast<int>(lane_->points.size())), true}};
        if (now) // nothing to show for a lane without points or parameters
            rows.push_back({"Value", juce::String::fromUTF8(session_.laneText(lane_->id, *now).c_str()), true});
        rows.push_back({"Plays", plays});
        return rows;
    }
    case S::Take:
    {
        if (!take_ || !channel_ || take_->clips.empty())
            return {};
        // How much of it the main lane plays.
        double length = 0.0, used = 0.0;
        for (const auto& c : take_->clips)
        {
            length += c.length;
            for (const auto& [from, to] : model::inUse(c, channel_->clips))
                used += to - from;
        }
        const auto plays = used <= 1e-6 ? juce::String("No") : used >= length - 1e-3 ? juce::String("All of it")
                                                                                    : juce::String("Partly");
        return {{"Name", {}}, // the field
                {"Start", position(take_->clips.front().start), true},
                {"Length", juce::String(take_->clips.back().end() - take_->clips.front().start, 3) + " s", true},
                {"In use", plays},
                {{}, {}}};         // "Use this take"
    }
    case S::Clip:
    {
        if (!clip_)
            return {};
        std::vector<ui::Property> rows{{"Name", juce::String::fromUTF8(clip_->name.c_str())},
                                       {"Start", position(clip_->start), true},
                                       {"Length", juce::String(clip_->length, 3) + " s", true},
                                       {"Offset", juce::String(clip_->offset, 3) + " s", true},
                                       {"Mute", {}}}; // the toggle
        if (clip_->kind == model::ClipInfo::Kind::Audio) // its fades (fades.md §3): the fields
        {
            rows.push_back({"Fade in", {}});
            rows.push_back({"Fade out", {}});
        }
        return rows;
    }
    case S::Clips: return {{"Selected", juce::String(clipCount_), true}};
    case S::Track:
        if (!channel_)
            return {};
        return {{"Name", name},
                {"Instrument", channel_->instrument.empty() ? juce::String("None")
                                                            : juce::String::fromUTF8(channel_->instrument.c_str())},
                {"Clips", juce::String(static_cast<int>(channel_->clips.size())), true}};
    case S::Return: return {{"Name", name}, {"Modules", modules, true}};
    case S::Master: return {{"Modules", modules, true}};
    case S::Mix:
    case S::Sends:
    case S::Input:
    case S::Assignments: break; // components
    }
    return {};
}

void InspectorPanel::resized()
{
    using S = model::InspectorSection;
    const auto& t = ui::Theme::current();
    header_.setBounds(getLocalBounds().removeFromTop(t.shell.panelHeaderHeight));
    auto r = getLocalBounds().withTrimmedTop(header_.getBottom() + t.shell.itemGap).reduced(t.shell.padding, 0);

    static const std::map<S, juce::String> titles{{S::Point, "Point"}, {S::Points, "Points"}, {S::Lane, "Lane"},
                                                  {S::Assignments, "Assignments"}, {S::Input, "Input"}, {S::Take, "Take"},
                                                  {S::Clip, "Clip"},   {S::Clips, "Clips"},   {S::Track, "Track"},
                                                  {S::Return, "Return"}, {S::Master, "Master"}, {S::Mix, "Mix"},
                                                  {S::Sends, "Sends"}};
    std::vector<ui::SectionStack::Section> sections;
    for (const auto s : sections_)
    {
        const int height = s == S::Mix           ? mix_.preferredHeight()
                           : s == S::Sends       ? sends_.preferredHeight()
                           : s == S::Assignments ? assignments_.preferredHeight()
                           : s == S::Input       ? input_.preferredHeight()
                                                 : static_cast<int>(properties(s).size()) * t.shell.rowHeight;
        sections.push_back({titles.at(s), height});
    }
    stack_.layout(r, sections);
    mix_.setBounds({});
    sends_.setBounds({});
    assignments_.setBounds({});
    input_.setBounds({});
    for (size_t i = 0; i < sections_.size(); ++i)
        if (sections_[i] == S::Mix)
            mix_.setBounds(stack_.content(i));
        else if (sections_[i] == S::Sends)
            sends_.setBounds(stack_.content(i));
        else if (sections_[i] == S::Assignments)
            assignments_.setBounds(stack_.content(i));
        else if (sections_[i] == S::Input)
            input_.setBounds(stack_.content(i));
        else if (sections_[i] == S::Lane) // the Name row
            laneName_.setBounds(stack_.content(i)
                                    .withTrimmedLeft(t.shell.propertyLabelWidth)
                                    .withHeight(t.shell.rowHeight)
                                    .reduced(0, t.shell.propertyFieldInset));
        else if (sections_[i] == S::Take) // the Name row and the last
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth);
            auto row = [&](int n) { return content.withY(content.getY() + n * t.shell.rowHeight).withHeight(t.shell.rowHeight); };
            takeName_.setBounds(row(0).reduced(0, t.shell.propertyFieldInset));
            useTake_.setBounds(row(4).reduced(0, t.shell.propertyFieldInset));
        }
        else if (sections_[i] == S::Clip) // the Mute row
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth);
            clipMute_.setBounds(content.withY(content.getY() + 4 * t.shell.rowHeight)
                                    .withHeight(t.shell.rowHeight)
                                    .withWidth(ui::Toggle::widthFor(clipMute_.getButtonText()))
                                    .reduced(0, t.shell.propertyFieldInset));
            fadeIn_.setBounds(content.withY(content.getY() + 5 * t.shell.rowHeight).withHeight(t.shell.rowHeight));
            fadeOut_.setBounds(content.withY(content.getY() + 6 * t.shell.rowHeight).withHeight(t.shell.rowHeight));
        }
        else if (sections_[i] == S::Point) // the Value row
            pointValue_.setBounds(stack_.content(i)
                                      .withTrimmedLeft(t.shell.propertyLabelWidth)
                                      .withY(stack_.content(i).getY() + t.shell.rowHeight)
                                      .withHeight(t.shell.rowHeight));
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (sections_.empty())
    {
        auto r = getLocalBounds().withTrimmedTop(header_.getBottom()).reduced(t.shell.padding, 0);
        ui::paintEmptyState(g, r.removeFromTop(t.shell.listRowHeight * 2), "Nothing selected");
        return;
    }
    stack_.paint(g);
    for (size_t i = 0; i < sections_.size(); ++i)
        if (const auto rows = properties(sections_[i]); !rows.empty())
            ui::paintProperties(g, stack_.content(i), rows);
}

// ---- DetailPanel ---------------------------------------------------------------------------

DetailPanel::DetailPanel(engine::Session& session, const ActionRegistry& actions) : chain_(session, actions), roll_(session)
{
    header_.setTabs({"Modules", "Clip"}, 0);
    header_.setCaption("Tab switches");
    header_.onTabSelected = [this](int tab) {
        const auto m = tab == 0 ? Mode::Devices : Mode::Clip;
        onModeChosen ? onModeChosen(m) : setMode(m);
    };
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
    if (m == Mode::Clip && !clipViewAvailable_)
        m = Mode::Devices;
    if (mode_ == m)
        return;
    mode_ = m;
    updateVisibility();
    updateHeader();
    repaint();
}

void DetailPanel::setClipViewAvailable(bool available)
{
    if (available == clipViewAvailable_)
        return;
    clipViewAvailable_ = available;
    if (available)
    {
        header_.setTabs({"Modules", "Clip"}, mode_ == Mode::Devices ? 0 : 1);
        header_.setCaption("Tab switches");
    }
    else
    {
        setMode(Mode::Devices);
        header_.setTitle("Modules");
        header_.setCaption({});
    }
    updateHeader();
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
