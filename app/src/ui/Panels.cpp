#include "ui/Panels.h"

#include "model/ClipLoops.h"
#include "model/FadeEditing.h"
#include "model/MusicalTime.h"
#include "model/Takes.h"
#include "model/TopBarLayout.h"
#include "ui/GridMenu.h"

#include <fd/device/Registry.h>

#include <algorithm>
#include <initializer_list>
#include <limits>
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
    // Its octave and velocity, by mouse too (menu-bar.md §8).
    keyboard_.onPopupMenu = [this] {
        juce::PopupMenu menu;
        actions_.addMenuItem(menu, "midi.computerKeyboard");
        menu.addSeparator();
        for (const auto* id : {"midi.octaveDown", "midi.octaveUp", "midi.velocityDown", "midi.velocityUp"})
            actions_.addMenuItem(menu, id);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&keyboard_));
    };
    add(undo_, "edit.undo");
    add(redo_, "edit.redo");
    add(search_, "app.commandPalette");
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
    // The song's key (key-and-scale.md §6): chosen in the Key popover, never dragged.
    keyField_.setFontSize(ui::Theme::current().textMd);
    keyField_.onOpen = [this] {
        if (onKeyOpen)
            onKeyOpen();
    };
    keyField_.onPopupMenu = [this] {
        if (onKeyMenu)
            onKeyMenu();
    };
    addAndMakeVisible(keyField_);
    frame(0.0, session_.snapshot()->tempo); // show the project's tempo before the first frame
    add(browser_, "view.browser");
    add(detail_, "view.detail");
    add(inspector_, "view.inspector");
    add(settings_, "app.settings");
    if (actions_.find("app.assistants") != nullptr)
        add(assistants_, "app.assistants");
}

void TransportBar::setBrandShown(bool shown)
{
    if (brandShown_ == shown)
        return;
    brandShown_ = shown;
    resized();
    repaint();
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

void TransportBar::showKey()
{
    const auto snapshot = session_.snapshot();
    const auto& key = snapshot->keys.first();
    if (keyName_.isNotEmpty() && key == keyShown_)
        return; // once per change, not every frame
    keyShown_ = key;
    keyName_ = key ? juce::String::fromUTF8(music::keyName(*key).c_str()) : juce::String("No key");
    keyShort_ = key ? juce::String::fromUTF8(music::keyShortName(*key).c_str()) : keyName_;
    juce::String notes;
    if (key)
        for (const auto& n : music::spelledNotes(*key))
            notes << " " << juce::String::fromUTF8(n.c_str());
    keyField_.setTooltip(key ? "Key: " + keyName_ + ":" + notes : juce::String("The song's key: click to choose one"));
    keyField_.setValue(keyName_, !key.has_value());
    resized(); // its width follows its name, and it may take its short name
}

void TransportBar::frame(double positionSeconds, const model::TempoMap& tempo)
{
    showKey();
    for (auto& [button, id] : bound_)
    {
        if (button->getToggleState() != actions_.isToggled(id))
            button->setToggleState(actions_.isToggled(id), juce::dontSendNotification);
        if (button->isEnabled() != actions_.isEnabled(id))
            button->setEnabled(actions_.isEnabled(id)); // e.g. the inspector in the mixer view
    }
    // Undo and Redo name their step ("Undo Add track (Ctrl+Z)").
    for (const auto& [button, id] : {std::pair{&undo_, "edit.undo"}, std::pair{&redo_, "edit.redo"}})
        if (const auto tip = actions_.tooltip(id); tip != button->getTooltip())
            button->setTooltip(tip);
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

juce::PopupMenu TransportBar::midiLoopMenu()
{
    juce::PopupMenu midi;
    const bool overdub = session_.midiOverdub();
    midi.addItem("Each lap: a new take", true, !overdub, [this] { session_.setMidiOverdub(false); });
    midi.addItem("Each lap: add to the clip (overdub)", true, overdub, [this] { session_.setMidiOverdub(true); });
    return midi;
}

void TransportBar::showRecordMenu()
{
    juce::PopupMenu menu;
    menu.addSubMenu("Count-in", countInMenu());
    menu.addSubMenu("MIDI loop recording", midiLoopMenu());
    menu.addSeparator();
    juce::PopupMenu::Item settings(juce::String::fromUTF8("Recording settings\xE2\x80\xA6"));
    settings.setAction(
        [this]
        {
            if (onRecordingSettings)
                onRecordingSettings();
        });
    menu.addItem(std::move(settings));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&record_));
}

void TransportBar::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (!brandArea_.isEmpty() && markOnly_)
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
    // Items drop when it's narrow, highest first (daw-shell.md, "Top bar"): the time, the CPU load,
    // the wordmark, the panel toggles, then Undo and Redo; the menu bar has them all.
    enum Drop
    {
        kUndo = 1,
        kPanels = 2,
        kWordmark = 3,
        kCpu = 4,
        kTime = 5,
    };
    std::vector<Entry> entries;
    if (brandShown_)
    {
        entries.push_back({{S::Left, static_cast<double>(mark)}, area(markArea)});
        entries.push_back({{S::Left, static_cast<double>(ui::brandWidth(h) - mark - t.shell.itemGap), false, kWordmark}, area(wordArea)});
    }
    entries.insert(entries.end(), {
        // Left: the song, then Undo and Redo.
        {{S::Left, static_cast<double>(tempoField_.widthFor("999.00")), true}, button(tempoField_)},
        {{S::Left, static_cast<double>(textWidth(ui::TextStyle::ValueSmall, "BPM"))}, area(bpmArea_)},
        {{S::Left, static_cast<double>(numeratorField_.widthFor("32")), true}, button(numeratorField_)},
        {{S::Left, std::ceil(digit)}, area(slashArea_)},
        {{S::Left, static_cast<double>(denominatorField_.widthFor("16"))}, button(denominatorField_)},
        // The key, a group of its own: its short name when it's tight, before anything drops.
        {{S::Left, static_cast<double>(keyField_.widthFor(keyName_.isEmpty() ? juce::String("No key") : keyName_)), true, 0,
          static_cast<double>(keyField_.widthFor(keyShort_.isEmpty() ? juce::String("No key") : keyShort_))},
         button(keyField_)},
        {{S::Left, static_cast<double>(h), true}, button(metronome_)},
        {{S::Left, static_cast<double>(h)}, button(countIn_)},
        {{S::Left, static_cast<double>(h), true, kUndo}, button(undo_)},
        {{S::Left, static_cast<double>(h), false, kUndo}, button(redo_)},
        // Centre: the transport, the position, the loop.
        {{S::Centre, static_cast<double>(h)}, button(play_)},
        {{S::Centre, static_cast<double>(h)}, button(stop_)},
        {{S::Centre, static_cast<double>(h)}, button(record_)},
        {{S::Centre, static_cast<double>(h)}, button(capture_)},
        {{S::Centre, static_cast<double>(h)}, button(automationArm_)},
        {{S::Centre, static_cast<double>(textWidth(ui::TextStyle::ValueLarge, "000. 00. 00") + 2 * inset), true}, area(barsArea_)},
        {{S::Centre, static_cast<double>(textWidth(ui::TextStyle::ValueMedium, "00:00.000") + inset), false, kTime}, area(timeArea_)},
        {{S::Centre, static_cast<double>(h), true}, button(loop_)},
        {{S::Centre, static_cast<double>(h)}, button(follow_)},
        // Right: status, views, app.
        {{S::Right, static_cast<double>(textWidth(ui::TextStyle::Value, "CPU 100 %")), false, kCpu}, area(cpuArea_)},
        {{S::Right, static_cast<double>(h), true}, button(keyboard_)},
        {{S::Right, static_cast<double>(light)}, area(midiLight_)},
        {{S::Right, static_cast<double>(ui::Toggle::widthFor(arrangeView_.getButtonText())), true}, button(arrangeView_)},
        {{S::Right, static_cast<double>(ui::Toggle::widthFor(mixView_.getButtonText()))}, button(mixView_)},
        {{S::Right, static_cast<double>(h), true, kPanels}, button(browser_)},
        {{S::Right, static_cast<double>(h), false, kPanels}, button(detail_)},
        {{S::Right, static_cast<double>(h), false, kPanels}, button(inspector_)},
        // App: the command palette, assistants, settings.
        {{S::Right, static_cast<double>(h), true}, button(search_)},
    });
    if (assistants)
    {
        entries.push_back({{S::Right, static_cast<double>(h)}, button(assistants_)});
        entries.push_back({{S::Right, static_cast<double>(light)}, area(assistantLight_)});
    }
    entries.push_back({{S::Right, static_cast<double>(h)}, button(settings_)});

    std::vector<model::BarItem> items;
    for (const auto& e : entries)
        items.push_back(e.item);
    const auto slots = model::layoutTopBar(getWidth(), items,
                                           {.padding = static_cast<double>(t.shell.barPadding),
                                            .itemGap = static_cast<double>(t.shell.itemGap),
                                            .groupGap = static_cast<double>(t.shell.groupGap),
                                            .sectionGap = 2.0 * t.shell.groupGap});
    for (size_t i = 0; i < entries.size(); ++i)
    {
        entries[i].place(slots[i].shown ? juce::Rectangle<int>(juce::roundToInt(slots[i].x), inner.getY(),
                                                               juce::roundToInt(slots[i].width), h)
                                        : juce::Rectangle<int>());
        if (items[i].narrowWidth > 0.0) // the key: its name or its short name, as it fits
        {
            keyNarrow_ = slots[i].narrow;
            keyField_.setValue(keyNarrow_ ? keyShort_ : keyName_, keyField_.isPlaceholder());
        }
    }
    markOnly_ = wordArea.isEmpty();
    brandArea_ = !brandShown_ ? juce::Rectangle<int>() : markOnly_ ? markArea : markArea.getUnion(wordArea);
    wellArea_ = timeArea_.isEmpty() ? barsArea_ : barsArea_.getUnion(timeArea_);
}

// ---- InspectorPanel ------------------------------------------------------------------------

InspectorPanel::InspectorPanel(engine::Session& session) : session_(session)
{
    header_.setTitle("Inspector");
    addAndMakeVisible(header_);
    viewport_.setViewedComponent(&body_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    wireSection();
    wireNotes();
    body_.addAndMakeVisible(mix_);
    body_.addAndMakeVisible(sends_);

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
    body_.addChildComponent(pointValue_);

    styleNameField(laneName_);
    laneName_.setTooltip("The lane's name: type to rename it");
    auto rename = [this] {
        if (!lane_)
            return;
        const auto name = laneName_.getText().trim();
        if (name.isEmpty()) // never empty: its name back
            laneName_.setText(juce::String::fromUTF8(lane_->name.c_str()), false);
        else if (name.toStdString() != lane_->name)
            session_.renameAutomationLane(lane_->id, name.toStdString());
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
    body_.addChildComponent(laneName_);
    body_.addChildComponent(assignments_);
    body_.addChildComponent(input_);

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
    body_.addChildComponent(takeName_);
    useTake_.setTooltip("The main lane plays this take across its time range (or click it on its take lane)");
    useTake_.onClick = [this] {
        if (take_ && channel_)
            session_.useTake(channel_->id, take_->id);
    };
    body_.addChildComponent(useTake_);
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
        body_.addChildComponent(*field);
    }
    // Each fade's curve beside its length (menu-bar.md §15): −100 … 100 %, 0 straight.
    for (auto* field : {&fadeInCurve_, &fadeOutCurve_})
    {
        const bool in = field == &fadeInCurve_;
        field->setFontSize(ui::Theme::current().textXs);
        field->setRange(-100.0, 100.0, 1.0);
        field->setDragSensitivity(1.0);
        field->setTooltip(in ? "The fade-in's curve: drag or type; 0 is straight"
                             : "The fade-out's curve: drag or type; 0 is straight");
        field->textFromValue = [](double percent) {
            const int p = juce::roundToInt(percent);
            if (p == 0)
                return juce::String("Straight"); // says what the field is, where a bare "0 %" wouldn't
            return (p > 0 ? juce::String("+") : juce::String::fromUTF8("\xe2\x88\x92")) + juce::String(std::abs(p)) + " %";
        };
        field->valueFromText = [](const juce::String& text) -> std::optional<double> {
            if (text.trim().equalsIgnoreCase("straight"))
                return 0.0;
            const auto t = text.trim().replace(juce::String::fromUTF8("\xe2\x88\x92"), "-").retainCharacters("+-0123456789.");
            if (t.isEmpty())
                return std::nullopt;
            return t.getDoubleValue();
        };
        field->onGestureStart = [this] { fadeGesture_.emplace(session_, "Fades"); };
        field->onGestureEnd = [this] { fadeGesture_.reset(); };
        field->onValueChange = [this, in](double percent) {
            if (!clip_)
                return;
            const auto fade = model::Fade{(in ? clip_->fadeIn : clip_->fadeOut).length, static_cast<float>(percent / 100.0)};
            session_.setFades({clip_->id}, in ? std::optional(fade) : std::nullopt, in ? std::nullopt : std::optional(fade));
        };
        body_.addChildComponent(*field);
    }
    // The crossfades at its ends: their shape and where they sit, as a crossfade's menu offers.
    for (auto* shape : {&crossInShape_, &crossOutShape_})
    {
        shape->addItem("None", 1);
        shape->addItem("Equal power", 2);
        shape->addItem("Equal gain", 3);
        shape->addItem("S-curve", 4);
        shape->setTooltip("Equal power for different material, equal gain for the same; None removes the crossfade");
        shape->onChange = [this, shape] { setCrossfadeShape(shape == &crossInShape_ ? crossIn_ : crossOut_, shape->getSelectedId()); };
        body_.addChildComponent(*shape);
    }
    for (auto* place : {&crossInPlace_, &crossOutPlace_})
    {
        place->addItem("Before the join", 1);
        place->addItem("Centred on the join", 2);
        place->addItem("After the join", 3);
        place->setTextWhenNothingSelected("As dragged");
        place->setTooltip("Where it sits against the join (only where the spare audio holds it whole)");
        place->onChange = [this, place] { setCrossfadePlace(place == &crossInPlace_ ? crossIn_ : crossOut_, place->getSelectedId()); };
        body_.addChildComponent(*place);
    }
    // The take's commands (the shell's actions, on the selected take).
    for (const auto& [button, id] : {std::pair{&copyTake_, "take.copyToTrack"}, std::pair{&moveTake_, "take.moveToTrack"},
                                     std::pair{&keepTake_, "take.keepOnly"}})
    {
        button->setClickingTogglesState(false);
        button->onClick = [this, action = std::string(id)] {
            if (actions_ != nullptr)
                actions_->perform(action);
        };
        body_.addChildComponent(*button);
    }
    // Names typed in place of the painted ones; colours from the palette (tracks-and-clips.md §4, §8).
    for (auto* field : {&channelName_, &clipName_})
    {
        styleNameField(*field);
        const bool clip = field == &clipName_;
        field->setTooltip(clip ? "The clip's name: type to rename it" : "Its name: type to rename it");
        auto commit = [this, field, clip] {
            const auto name = field->getText().trim();
            if (name.isEmpty()) // never empty: its name back
            {
                if (const auto* now = clip ? (clip_ ? &clip_->name : nullptr) : (channel_ ? &channel_->name : nullptr))
                    field->setText(juce::String::fromUTF8(now->c_str()), false);
                return;
            }
            if (clip && clip_ && name.toStdString() != clip_->name)
                session_.renameClips(clipParts(), name.toStdString());
            else if (!clip && channel_ && name.toStdString() != channel_->name)
                session_.renameTrack(channel_->id, name.toStdString());
        };
        field->onReturnKey = [commit] {
            commit();
            juce::Component::unfocusAllComponents();
        };
        field->onFocusLost = commit;
        field->onEscapeKey = [this, field, clip] {
            field->setText(clip ? (clip_ ? juce::String::fromUTF8(clip_->name.c_str()) : juce::String())
                                : (channel_ ? juce::String::fromUTF8(channel_->name.c_str()) : juce::String()),
                           false);
            unfocusAllComponents();
        };
        body_.addChildComponent(*field);
    }
    channelColour_.onChosen = [this](int c)
    {
        if (channel_)
            session_.setTrackColour(channel_->id, c);
    };
    clipColour_.onChosen = [this](int c)
    {
        if (clip_)
            session_.setClipColour(clipParts(), c);
    };
    laneColour_.onChosen = [this](int c)
    {
        if (lane_)
            session_.setLaneColour(lane_->id, c);
    };
    for (auto* swatch : {&channelColour_, &clipColour_, &laneColour_})
        body_.addChildComponent(*swatch);
    clipMute_.setTooltip("Mute the clip: it keeps its place and plays nothing (0)");
    clipMute_.onClick = [this] {
        if (clip_)
            session_.setClipsMuted({clip_->id}, !clip_->muted);
    };
    body_.addChildComponent(clipMute_);
    // Its loop (clip-loops.md §7): on loops what it shows; the fields set the loop, in the content.
    clipLoop_.onClick = [this] {
        if (clip_)
            session_.setClipsLooping({clip_->id}, !clip_->loop.on());
    };
    body_.addChildComponent(clipLoop_);
    for (auto* field : {&loopStart_, &loopLength_})
    {
        const bool start = field == &loopStart_;
        field->setFontSize(ui::Theme::current().textXs);
        field->setDragSensitivity(0.25); // a sixteenth a pixel
        field->setTooltip(start ? "Where the loop starts in the clip's content: drag, or type \"2.1.1\""
                                : "The loop's length: drag, or type \"1 bar\", \"6 beats\" (audio also \"1.5 s\")");
        field->textFromValue = [this, start](double beats) {
            return juce::String(start ? model::positionText(beats, tempo_) : model::lengthText(beats, tempo_));
        };
        field->valueFromText = [this, start](const juce::String& text) -> std::optional<double> {
            const auto t = text.trim().toLowerCase();
            // Seconds, for audio ("1.5 s", "750 ms").
            if (clip_ && clip_->kind == model::ClipInfo::Kind::Audio && t.endsWith("s") && !t.endsWith("beats") && !t.endsWith("bars"))
            {
                const auto n = t.trimCharactersAtEnd("ms ").trim();
                if (n.isNotEmpty() && n.containsOnly("0123456789."))
                    return tempo_.beatsForSeconds(n.getDoubleValue() / (t.endsWith("ms") ? 1000.0 : 1.0));
            }
            return start ? model::positionFromText(t.toStdString(), tempo_) : model::lengthFromText(t.toStdString(), tempo_);
        };
        field->onGestureStart = [this] { loopGesture_.emplace(session_, "Loop"); };
        field->onGestureEnd = [this] { loopGesture_.reset(); };
        field->onValueChange = [this, start](double beats) {
            if (!clip_ || !clip_->loop.on())
                return;
            auto loop = clip_->loop;
            (start ? loop.start : loop.length) = tempo_.secondsForBeats(beats);
            session_.setClipLoop(clip_->id, loop);
        };
        body_.addChildComponent(*field);
    }
}

void InspectorPanel::wireSection()
{
    // The selected section (arranger.md §10): name, colour, and its marks; the music stays.
    styleNameField(sectionName_);
    sectionName_.setTooltip("The section's name: type to rename it");
    auto commit = [this]
    {
        const auto name = sectionName_.getText().trim();
        if (!section_)
            return;
        if (name.isEmpty())
            sectionName_.setText(juce::String::fromUTF8(section_->name.c_str()), false);
        else if (name.toStdString() != section_->name)
            session_.renameSection(section_->id, name.toStdString());
    };
    sectionName_.onReturnKey = [commit]
    {
        commit();
        juce::Component::unfocusAllComponents();
    };
    sectionName_.onFocusLost = commit;
    sectionName_.onEscapeKey = [this]
    {
        sectionName_.setText(section_ ? juce::String::fromUTF8(section_->name.c_str()) : juce::String(),
                             false);
        unfocusAllComponents();
    };
    body_.addChildComponent(sectionName_);
    sectionColour_.onChosen = [this](int c)
    {
        if (section_)
            session_.setSectionColour(section_->id, c);
    };
    body_.addChildComponent(sectionColour_);
    for (auto* field : {&sectionStart_, &sectionLength_})
    {
        const bool start = field == &sectionStart_;
        field->setFontSize(ui::Theme::current().textXs);
        field->setDragSensitivity(0.25); // a sixteenth a pixel
        field->setTooltip(
            start ? "Where the section starts: drag, or type \"9.1.1\" (moves its mark; the music stays)"
                  : "How long the section is: drag, or type \"8 bars\" (moves its end; the music stays)");
        field->textFromValue = [this, start](double beats)
        {
            return juce::String(start ? model::positionText(beats, tempo_)
                                      : model::lengthText(beats, tempo_));
        };
        field->valueFromText = [this, start](const juce::String& text) -> std::optional<double>
        {
            const auto t = text.trim().toLowerCase().toStdString();
            return start ? model::positionFromText(t, tempo_) : model::lengthFromText(t, tempo_);
        };
        field->onGestureStart = [this] { sectionGesture_.emplace(session_, "Move section mark"); };
        field->onGestureEnd = [this] { sectionGesture_.reset(); };
        field->onValueChange = [this, start](double beats)
        {
            if (!section_)
                return;
            const double seconds = tempo_.secondsForBeats(beats);
            if (start)
                session_.setSectionMarks(section_->id, seconds, seconds + section_->length());
            else
                session_.setSectionMarks(section_->id, section_->start, section_->start + seconds);
        };
        body_.addChildComponent(*field);
    }
}

namespace
{
/// A note typed as a name with its octave ("C3", "F#4", "Bb2"; C3 = 60) or a number.
std::optional<double> pitchFromText(const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty())
        return std::nullopt;
    if (t.containsOnly("0123456789"))
        return t.getDoubleValue();
    int split = t.length();
    while (split > 0 && juce::CharacterFunctions::isDigit(t[split - 1]))
        --split;
    if (split > 0 && t[split - 1] == '-')
        --split;
    if (split == 0 || split == t.length())
        return std::nullopt;
    const auto pc = music::parsePitchClass(t.substring(0, split).toStdString());
    if (!pc)
        return std::nullopt;
    return static_cast<double>(*pc + (t.substring(split).getIntValue() + 2) * 12);
}
} // namespace

void InspectorPanel::wireNotes()
{
    using F = model::NoteField;
    for (auto [field, kind] : {std::pair{&notePitch_, F::Pitch}, std::pair{&noteStart_, F::Start},
                               std::pair{&noteLength_, F::Length}, std::pair{&noteVelocity_, F::Velocity},
                               std::pair{&noteSpread_, F::Spread}, std::pair{&noteRelease_, F::Release},
                               std::pair{&noteChance_, F::Chance}})
    {
        // MIDI values in whole steps; chance in whole percent; times free.
        const bool midi = kind == F::Pitch || kind == F::Velocity || kind == F::Spread || kind == F::Release;
        field->setFontSize(ui::Theme::current().textXs);
        field->setRange(kind == F::Velocity ? 1.0 : 0.0,
                        midi                ? 127.0
                        : kind == F::Chance ? 100.0
                                            : 4096.0 * 4.0,
                        midi || kind == F::Chance ? 1.0 : 0.0);
        field->setDragSensitivity(kind == F::Pitch ? 0.2 : midi || kind == F::Chance ? 0.5 : 1.0 / 16.0);
        field->textFromValue = [this, kind](double v)
        {
            const auto one = [this, kind](double value) -> juce::String
            {
                switch (kind)
                {
                case F::Pitch:
                {
                    const auto& key = roll_ != nullptr ? roll_->key() : std::nullopt;
                    return juce::String::fromUTF8(
                        music::noteName(key ? &*key : nullptr, static_cast<int>(std::lround(value))).c_str());
                }
                case F::Start:
                    return juce::String(
                        model::positionText(roll_ != nullptr ? roll_->songBeat(value) : value, tempo_));
                case F::Length:
                    return juce::String(model::lengthText(value, tempo_));
                case F::Chance:
                    return juce::String(juce::roundToInt(value)) + " %";
                case F::Spread:
                    return juce::String::fromUTF8("± ") + juce::String(juce::roundToInt(value));
                case F::Velocity:
                case F::Release:
                    break;
                }
                return juce::String(juce::roundToInt(value));
            };
            // Several notes: their range, as far apart as they are.
            const auto spread = notesSpread_.contains(kind) ? notesSpread_.at(kind) : 0.0;
            return spread > 1e-9 ? one(v) + juce::String::fromUTF8(" – ") + one(v + spread) : one(v);
        };
        field->valueFromText = [this, kind](const juce::String& text) -> std::optional<double>
        {
            switch (kind)
            {
            case F::Pitch:
                return pitchFromText(text);
            case F::Start:
            {
                const auto beat = model::positionFromText(text.trim().toLowerCase().toStdString(), tempo_);
                return beat && roll_ != nullptr ? std::optional(*beat - roll_->songBeat(0.0)) : beat;
            }
            case F::Length:
                return model::lengthFromText(text.trim().toLowerCase().toStdString(), tempo_);
            case F::Velocity:
            case F::Spread:
            case F::Release:
            case F::Chance:
                break;
            }
            // A number, as it's shown ("50 %", "± 10") or bare.
            const auto number =
                text.trim().trimCharactersAtEnd("% ").trimCharactersAtStart(juce::String::fromUTF8("± +"));
            if (number.containsOnly("0123456789") && number.isNotEmpty())
                return number.getDoubleValue();
            return std::nullopt;
        };
        field->onGestureStart = [this, field]
        {
            if (roll_ == nullptr)
                return;
            notesGesture_.emplace(session_, "Edit notes");
            notesBase_ = model::NoteEdit{roll_->notes(), roll_->selection()};
            notesFrom_ = field->getValue();
        };
        field->onGestureEnd = [this]
        {
            notesGesture_.reset();
            notesBase_.reset();
            updateNotes();
        };
        field->onValueChange = [this, kind](double v)
        {
            if (roll_ == nullptr || !roll_->hasSelection())
                return;
            // A drag moves every note by the same amount; a value typed sets them all.
            if (notesBase_)
                roll_->applyEdit(
                    model::offsetNotesField(notesBase_->notes, notesBase_->selection, kind, v - notesFrom_));
            else
                roll_->applyEdit(model::setNotesField(roll_->notes(), roll_->selection(), kind, v));
        };
        body_.addChildComponent(*field);
    }
    notePitch_.setTooltip("The notes' pitch: drag, or type a note (\"C3\", \"F#4\") or a number");
    noteStart_.setTooltip("Where the notes start: drag, or type \"2.1.1\"; several move together");
    noteLength_.setTooltip("How long the notes last: drag, or type \"1/8\" or \"1 beat\"");
    noteVelocity_.setTooltip("How hard the notes are played, 1 to 127: drag, or type a number");
    noteSpread_.setTooltip("How far the notes' velocity may move up or down each time they play, 0 to 127");
    noteRelease_.setTooltip("How fast the notes are let go (their note-off velocity), 0 to 127: some "
                            "instruments use it");
    noteChance_.setTooltip("How likely the notes are to play each time they come round, in percent; "
                           "Notes > Re-roll chance decides again");
    noteMute_.setClickingTogglesState(false);
    noteMute_.setTooltip("Mute the notes: they stay, drawn hollow, and don't play (0)");
    noteMute_.onClick = [this]
    {
        if (roll_ != nullptr)
            roll_->toggleMute();
    };
    body_.addChildComponent(noteMute_);
}

void InspectorPanel::setPianoRoll(PianoRoll& roll)
{
    roll_ = &roll;
}

ui::NumberField& InspectorPanel::noteField(model::NoteField field) noexcept
{
    switch (field)
    {
    case model::NoteField::Pitch:
        return notePitch_;
    case model::NoteField::Start:
        return noteStart_;
    case model::NoteField::Length:
        return noteLength_;
    case model::NoteField::Spread:
        return noteSpread_;
    case model::NoteField::Release:
        return noteRelease_;
    case model::NoteField::Chance:
        return noteChance_;
    case model::NoteField::Velocity:
        break;
    }
    return noteVelocity_;
}

bool InspectorPanel::showsNotes() const
{
    return roll_ != nullptr && roll_->clip() && clip_ && clip_->id == roll_->clip()->id &&
           roll_->hasSelection();
}

juce::String InspectorPanel::notesTitle() const
{
    if (roll_ == nullptr)
        return "Notes";
    const auto dot = juce::String::fromUTF8(" · ");
    juce::String title =
        "Notes" + dot + juce::String(static_cast<int>(roll_->selection().size())) + " selected";
    std::vector<int> pitches;
    for (auto i : roll_->selection())
        if (i < roll_->notes().size())
            pitches.push_back(roll_->notes()[i].pitch);
    if (const auto chord = music::nameChord(pitches))
    {
        const auto& key = roll_->key();
        title << dot << juce::String::fromUTF8(music::chordName(*chord, key ? &*key : nullptr).c_str());
    }
    return title;
}

void InspectorPanel::updateNotes()
{
    using S = model::InspectorSection;
    std::erase(sections_, S::Notes);
    const bool shown = showsNotes();
    if (shown)
    {
        const auto clip = std::find(sections_.begin(), sections_.end(), S::Clip);
        sections_.insert(clip == sections_.end() ? sections_.begin() : clip + 1, S::Notes);
    }
    for (auto* c :
         std::initializer_list<juce::Component*>{&notePitch_, &noteStart_, &noteLength_, &noteVelocity_,
                                                 &noteSpread_, &noteRelease_, &noteChance_, &noteMute_})
        c->setVisible(shown);
    if (!shown || notesGesture_)
        return;
    for (auto field : {model::NoteField::Pitch, model::NoteField::Start, model::NoteField::Length,
                       model::NoteField::Velocity, model::NoteField::Spread, model::NoteField::Release,
                       model::NoteField::Chance})
        if (const auto range = model::fieldRange(roll_->notes(), roll_->selection(), field))
        {
            notesSpread_[field] = range->second - range->first;
            noteField(field).setValue(range->first, juce::dontSendNotification);
            noteField(field).repaint();
        }
    noteMute_.setToggleState(roll_->allMuted(), juce::dontSendNotification);
}

void InspectorPanel::notesChanged()
{
    updateNotes();
    resized();
    repaint();
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
    section_ = context.section;
    sectionCount_ = context.sectionCount;
    const bool sectionShown = has(model::InspectorSection::Section) && section_;
    for (auto* c : std::initializer_list<juce::Component*>{&sectionName_, &sectionColour_, &sectionStart_,
                                                           &sectionLength_})
        c->setVisible(sectionShown);
    if (sectionShown)
    {
        if (!sectionName_.hasKeyboardFocus(true))
            sectionName_.setText(juce::String::fromUTF8(section_->name.c_str()), false);
        sectionColour_.setColour(section_->colour);
        if (!sectionGesture_)
        {
            constexpr double kLongest = 4096.0 * 4.0; // beats: far past any song
            sectionStart_.setRange(0.0, kLongest, 0.25);
            sectionLength_.setRange(0.25, kLongest, 0.25);
            sectionStart_.setValue(tempo.beatsForSeconds(section_->start), juce::dontSendNotification);
            sectionLength_.setValue(tempo.beatsForSeconds(section_->length()), juce::dontSendNotification);
        }
    }
    laneName_.setVisible(has(model::InspectorSection::Lane));
    if (lane_ && !laneName_.hasKeyboardFocus(true))
        laneName_.setText(juce::String::fromUTF8(lane_->name.c_str()), false);
    take_ = context.take;
    takeCount_ = context.takeCount;
    takeName_.setVisible(has(model::InspectorSection::Take));
    useTake_.setVisible(has(model::InspectorSection::Take));
    for (auto* button : {&copyTake_, &moveTake_, &keepTake_})
        button->setVisible(has(model::InspectorSection::Take));
    clipMute_.setVisible(has(model::InspectorSection::Clip) && clip_.has_value());
    // Names and colours.
    const bool channelShown = (has(model::InspectorSection::Track) || has(model::InspectorSection::Return)) && channel_;
    channelName_.setVisible(channelShown);
    channelColour_.setVisible(channelShown);
    if (channelShown)
    {
        if (!channelName_.hasKeyboardFocus(true))
            channelName_.setText(juce::String::fromUTF8(channel_->name.c_str()), false);
        channelColour_.setColour(channel_->colour);
    }
    const bool clipShown = has(model::InspectorSection::Clip) && clip_;
    clipName_.setVisible(clipShown);
    clipColour_.setVisible(clipShown);
    if (clipShown)
    {
        if (!clipName_.hasKeyboardFocus(true))
            clipName_.setText(juce::String::fromUTF8(clip_->name.c_str()), false);
        clipColour_.setColour(clip_->colour);
    }
    laneColour_.setVisible(has(model::InspectorSection::Lane) && lane_);
    if (lane_)
        laneColour_.setColour(lane_->colour);
    clipMute_.setToggleState(clip_ && clip_->muted, juce::dontSendNotification);
    // Its loop: the toggle (dimmed, saying why, where it can't loop), the fields while it loops.
    const bool clipSection = has(model::InspectorSection::Clip) && clip_.has_value();
    clipLoop_.setVisible(clipSection);
    clipLoop_.setToggleState(clip_ && clip_->loop.on(), juce::dontSendNotification);
    const auto why = clipSection && channel_ && !clip_->loop.on() ? model::whyCantLoop(*clip_, *channel_) : std::string();
    clipLoop_.setEnabled(why.empty());
    clipLoop_.setTooltip(why.empty() ? juce::String("Loop the clip: it plays this stretch of its content again and again, and its "
                                                    "edges go as far as you drag them (Clip > Loop clips)")
                                     : juce::String::fromUTF8(why.c_str()));
    const bool looping = clipSection && clip_->loop.on();
    loopStart_.setVisible(looping);
    loopLength_.setVisible(looping);
    if (looping && !loopGesture_)
    {
        // In beats of the content; an audio loop stays inside its file.
        const bool audioLoop = clip_->kind == model::ClipInfo::Kind::Audio && clip_->sourceLength > 0.0;
        constexpr double kLongest = 4096.0; // beats: far longer than any MIDI loop
        const double fileBeats = audioLoop ? tempo_.beatsForSeconds(clip_->sourceLength) : kLongest;
        const double shortest = tempo_.beatsForSeconds(model::minClipLength);
        loopStart_.setRange(0.0, std::max(0.0, fileBeats - shortest), 0.25);
        loopLength_.setRange(0.25, std::max(0.25, fileBeats - tempo_.beatsForSeconds(clip_->loop.start)), 0.25);
        loopStart_.setValue(tempo_.beatsForSeconds(clip_->loop.start), juce::dontSendNotification);
        loopLength_.setValue(tempo_.beatsForSeconds(clip_->loop.length), juce::dontSendNotification);
    }
    const bool audio = has(model::InspectorSection::Clip) && clip_ && clip_->kind == model::ClipInfo::Kind::Audio;
    for (auto* field : {&fadeIn_, &fadeOut_})
    {
        field->setVisible(audio);
        if (audio)
            field->setRange(0.0, clip_->length * 1000.0, 1.0);
    }
    for (auto* field : {&fadeInCurve_, &fadeOutCurve_})
        field->setVisible(audio);
    if (audio && !fadeGesture_)
    {
        fadeIn_.setValue(clip_->fadeIn.length * 1000.0, juce::dontSendNotification);
        fadeOut_.setValue(clip_->fadeOut.length * 1000.0, juce::dontSendNotification);
        fadeInCurve_.setValue(clip_->fadeIn.bend * 100.0, juce::dontSendNotification);
        fadeOutCurve_.setValue(clip_->fadeOut.bend * 100.0, juce::dontSendNotification);
    }
    // A curve only on a fade that's there.
    fadeInCurve_.setEnabled(audio && clip_->fadeIn.length > 0.0);
    fadeOutCurve_.setEnabled(audio && clip_->fadeOut.length > 0.0);
    // The crossfades meeting its start and its end (the joins at its edges, not a comp's own).
    crossIn_.reset();
    crossOut_.reset();
    if (audio && channel_)
    {
        const auto clips = model::fadeClips(*channel_);
        for (size_t i = 0; i + 1 < clips.size(); ++i)
            if (clips[i].crossOut)
            {
                if (std::abs(clips[i + 1].start - clip_->start) < 1e-6)
                    crossIn_ = Join{clips[i], clips[i + 1]};
                if (std::abs(clips[i].end() - clip_->end()) < 1e-6)
                    crossOut_ = Join{clips[i], clips[i + 1]};
            }
    }
    showCrossfade(crossInShape_, crossInPlace_, crossIn_);
    showCrossfade(crossOutShape_, crossOutPlace_, crossOut_);
    if (take_ && !takeName_.hasKeyboardFocus(true))
        takeName_.setText(juce::String::fromUTF8(take_->name.c_str()), false);
    assignments_.setLane(has(model::InspectorSection::Assignments) ? lane_ : std::nullopt);
    assignments_.setVisible(has(model::InspectorSection::Assignments));
    input_.setTrack(has(model::InspectorSection::Input) ? channel_ : std::nullopt);
    input_.setVisible(has(model::InspectorSection::Input));
    mix_.setChannel(has(model::InspectorSection::Mix) ? channel_ : std::nullopt);
    sends_.setChannel(has(model::InspectorSection::Sends) ? channel_ : std::nullopt, returns);
    updateNotes();
    resized();
    repaint();
}

std::vector<std::uint64_t> InspectorPanel::clipParts() const
{
    std::vector<std::uint64_t> out;
    if (!clip_)
        return out;
    out.push_back(clip_->id);
    if (channel_ && clip_->stack != 0)
        for (const auto& c : channel_->clips)
            if (c.stack == clip_->stack && c.id != clip_->id)
                out.push_back(c.id);
    return out;
}

void InspectorPanel::setActions(const ActionRegistry& actions)
{
    actions_ = &actions;
    copyTake_.setTooltip(actions.tooltip("take.copyToTrack"));
    moveTake_.setTooltip(actions.tooltip("take.moveToTrack"));
    keepTake_.setTooltip("Keep only this take: the clip's other takes are deleted");
}

void InspectorPanel::showCrossfade(juce::ComboBox& shape, juce::ComboBox& place, const std::optional<Join>& join)
{
    shape.setVisible(join.has_value());
    place.setVisible(join.has_value());
    if (!join)
        return;
    const auto& x = *join->out.crossOut;
    shape.setSelectedId(static_cast<int>(x.shape) + 2, juce::dontSendNotification);
    for (const auto p : {model::CrossfadePlacement::Before, model::CrossfadePlacement::Centred, model::CrossfadePlacement::After})
        place.setItemEnabled(static_cast<int>(p) + 1, model::placeCrossfadeAt(join->out, join->in, x.length, p, x.shape).has_value());
    const auto now = model::placementOf(x);
    place.setSelectedId(now ? static_cast<int>(*now) + 1 : 0, juce::dontSendNotification);
}

void InspectorPanel::setCrossfadeShape(const std::optional<Join>& join, int id)
{
    if (!join || id < 1)
        return;
    if (id == 1)
    {
        session_.setCrossfade(join->out.id, std::nullopt, "Remove crossfade");
        return;
    }
    const auto& x = *join->out.crossOut;
    session_.setCrossfade(join->out.id, model::Crossfade{x.length, x.from, static_cast<model::CrossfadeShape>(id - 2)},
                          "Crossfade shape");
}

void InspectorPanel::setCrossfadePlace(const std::optional<Join>& join, int id)
{
    if (!join || id < 1)
        return;
    const auto& x = *join->out.crossOut;
    if (const auto placed = model::placeCrossfadeAt(join->out, join->in, x.length, static_cast<model::CrossfadePlacement>(id - 1), x.shape))
        session_.setCrossfade(join->out.id, placed, "Crossfade placement");
}

void InspectorPanel::reveal(const juce::Component& c)
{
    const auto view = viewport_.getViewArea();
    if (c.getY() < view.getY())
        viewport_.setViewPosition(0, c.getY());
    else if (c.getBottom() > view.getBottom())
        viewport_.setViewPosition(0, c.getBottom() - view.getHeight());
}

void InspectorPanel::focusLaneName()
{
    if (laneName_.isVisible() && laneName_.isShowing())
    {
        reveal(laneName_);
        laneName_.selectAll();
        ui::takeFocus(laneName_); // asked from a menu too (kept from a late hand-back)
    }
}

void InspectorPanel::focusTakeName()
{
    if (takeName_.isVisible() && takeName_.isShowing())
    {
        reveal(takeName_);
        takeName_.selectAll();
        ui::takeFocus(takeName_); // asked from a menu too (kept from a late hand-back)
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
        std::vector<ui::Property> rows{{"Name", {}},   // the field
                                       {"Colour", {}}, // the swatch
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
                {{}, {}},          // "Use this take"
                {"New track", {}}, // Copy, Move
                {{}, {}}};         // "Keep only this take"
    }
    case S::Clip:
    {
        if (!clip_)
            return {};
        // Offset: where in its content it starts (inside the loop while it loops).
        std::vector<ui::Property> rows{{"Name", {}, false, "name"},
                                       {"Colour", {}, false, "colour"},
                                       {"Start", position(clip_->start), true},
                                       {"Length", juce::String(clip_->length, 3) + " s", true},
                                       {"Offset", juce::String(model::contentAt(*clip_, clip_->start), 3) + " s", true},
                                       {"Loop", {}, false, "loop"}};
        if (clip_->loop.on()) // its loop (clip-loops.md §7): the fields
        {
            rows.push_back({"Loop start", {}, false, "loopStart"});
            rows.push_back({"Loop length", {}, false, "loopLength"});
        }
        rows.push_back({"Mute", {}, false, "mute"});
        if (clip_->kind == model::ClipInfo::Kind::Audio) // its fades (fades.md §3): the fields
        {
            rows.push_back({"Fade in", {}, false, "fadeIn"});
            rows.push_back({"Fade out", {}, false, "fadeOut"});
        }
        if (crossIn_) // the crossfades at its ends: their choices (menu-bar.md §15)
        {
            rows.push_back({"Xfade in", {}, false, "crossInShape"});
            rows.push_back({"Position", {}, false, "crossInPlace"});
        }
        if (crossOut_)
        {
            rows.push_back({"Xfade out", {}, false, "crossOutShape"});
            rows.push_back({"Position", {}, false, "crossOutPlace"});
        }
        return rows;
    }
    case S::Section:
        if (!section_)
            return {};
        return {{"Name", {}, false, "name"},
                {"Colour", {}, false, "colour"},
                {"Start", {}, false, "start"},
                {"Length", {}, false, "length"}};
    case S::Sections:
        return {{"Selected", juce::String(sectionCount_), true}};
    case S::Clips: return {{"Selected", juce::String(clipCount_), true}};
    case S::Notes: // the fields, the mute (piano-roll.md §9.6)
        return {
            {"Pitch", {}, false, "pitch"},
            {"Start", {}, false, "start"},
            {"Length", {}, false, "length"},
            {"Velocity", {}, false, "velocity"},
            {"Spread", {}, false, "spread"},
            {"Release", {}, false, "release"},
            {"Chance", {}, false, "chance", roll_ != nullptr && roll_->clip() && roll_->clip()->chanceKept},
            {"Mute", {}, false, "mute"}};
    case S::Takes: return {{"Selected", juce::String(takeCount_), true}};
    case S::Track:
        if (!channel_)
            return {};
        return {{"Name", {}},   // the field
                {"Colour", {}}, // the swatch
                {"Instrument", channel_->instrument.empty() ? juce::String("None")
                                                            : juce::String::fromUTF8(channel_->instrument.c_str())},
                {"Clips", juce::String(static_cast<int>(channel_->clips.size())), true}};
    case S::Return: return {{"Name", {}}, {"Colour", {}}, {"Modules", modules, true}}; // the field, the swatch
    case S::Master: return {{"Modules", modules, true}};
    case S::Mix:
    case S::Sends:
    case S::Input:
    case S::Assignments: break; // components
    }
    return {};
}

int InspectorPanel::rowOf(model::InspectorSection section, std::string_view key) const
{
    const auto rows = properties(section);
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].key != nullptr && key == rows[i].key)
            return static_cast<int>(i);
    return -1;
}

void InspectorPanel::resized()
{
    const auto& t = ui::Theme::current();
    header_.setBounds(getLocalBounds().removeFromTop(t.shell.panelHeaderHeight));
    viewport_.setBounds(getLocalBounds().withTrimmedTop(header_.getBottom()));
    // As wide as the viewport shows; again if that brought its scroll bar in or out.
    for (int pass = 0; pass < 2; ++pass)
    {
        const int width = std::max(1, viewport_.getMaximumVisibleWidth());
        body_.setSize(width, std::max(1, layoutSections(width)));
        if (viewport_.getMaximumVisibleWidth() == width)
            break;
    }
}

int InspectorPanel::layoutSections(int width)
{
    using S = model::InspectorSection;
    const auto& t = ui::Theme::current();
    auto r = juce::Rectangle<int>(0, t.shell.itemGap, width, std::numeric_limits<int>::max() / 2).reduced(t.shell.padding, 0);

    static const std::map<S, juce::String> titles{{S::Point, "Point"},       {S::Points, "Points"},
                                                  {S::Lane, "Lane"},         {S::Assignments, "Assignments"},
                                                  {S::Input, "Input"},       {S::Take, "Take"},
                                                  {S::Takes, "Takes"},       {S::Clip, "Clip"},
                                                  {S::Clips, "Clips"},       {S::Section, "Section"},
                                                  {S::Sections, "Sections"}, {S::Track, "Track"},
                                                  {S::Return, "Return"},     {S::Master, "Master"},
                                                  {S::Mix, "Mix"},           {S::Sends, "Sends"}};
    std::vector<ui::SectionStack::Section> sections;
    for (const auto s : sections_)
    {
        const int height = s == S::Mix           ? mix_.preferredHeight()
                           : s == S::Sends       ? sends_.preferredHeight()
                           : s == S::Assignments ? assignments_.preferredHeight()
                           : s == S::Input       ? input_.preferredHeight()
                                                 : static_cast<int>(properties(s).size()) * t.shell.rowHeight;
        sections.push_back({s == S::Notes ? notesTitle() : titles.at(s), height});
    }
    const int height = stack_.layout(r, sections).getY() + t.shell.padding;
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
        else if (sections_[i] == S::Lane) // the Name and Colour rows
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth).withHeight(t.shell.rowHeight);
            laneName_.setBounds(content.reduced(0, t.shell.propertyFieldInset));
            laneColour_.setBounds(content.translated(0, t.shell.rowHeight));
        }
        else if (sections_[i] == S::Track || sections_[i] == S::Return) // the Name and Colour rows
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth).withHeight(t.shell.rowHeight);
            channelName_.setBounds(content.reduced(0, t.shell.propertyFieldInset));
            channelColour_.setBounds(content.translated(0, t.shell.rowHeight));
        }
        else if (sections_[i] == S::Take) // the Name row and the last
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth);
            auto row = [&](int n) { return content.withY(content.getY() + n * t.shell.rowHeight).withHeight(t.shell.rowHeight); };
            takeName_.setBounds(row(0).reduced(0, t.shell.propertyFieldInset));
            useTake_.setBounds(row(4).reduced(0, t.shell.propertyFieldInset));
            // Copy and Move side by side, then Keep only: each as wide as its label.
            auto commands = row(5).reduced(0, t.shell.propertyFieldInset);
            copyTake_.setBounds(commands.removeFromLeft(ui::Toggle::widthFor(copyTake_.getButtonText())));
            commands.removeFromLeft(t.shell.itemGap);
            moveTake_.setBounds(commands.removeFromLeft(ui::Toggle::widthFor(moveTake_.getButtonText())));
            keepTake_.setBounds(row(6).reduced(0, t.shell.propertyFieldInset).withWidth(ui::Toggle::widthFor(keepTake_.getButtonText())));
        }
        else if (sections_[i] == S::Clip) // Name, Colour, Loop, Mute, the fades and crossfades: by row
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth);
            const auto row = [&](const char* key) {
                const int n = rowOf(S::Clip, key);
                return n < 0 ? juce::Rectangle<int>() : content.withY(content.getY() + n * t.shell.rowHeight).withHeight(t.shell.rowHeight);
            };
            const auto toggle = [&](ui::Toggle& b, const char* key) {
                b.setBounds(row(key).withWidth(ui::Toggle::widthFor(b.getButtonText())).reduced(0, t.shell.propertyFieldInset));
            };
            clipName_.setBounds(row("name").reduced(0, t.shell.propertyFieldInset));
            clipColour_.setBounds(row("colour"));
            toggle(clipLoop_, "loop");
            toggle(clipMute_, "mute");
            // A field's text lines up with the values painted in the rows above it.
            loopStart_.setBounds(row("loopStart").withTrimmedLeft(-ui::NumberField::textInset));
            loopLength_.setBounds(row("loopLength").withTrimmedLeft(-ui::NumberField::textInset));
            // A fade's length, then its curve, halves of the value column.
            const int half = (content.getWidth() - t.shell.itemGap) / 2;
            for (const auto& [length, curve, key] : {std::tuple{&fadeIn_, &fadeInCurve_, "fadeIn"}, std::tuple{&fadeOut_, &fadeOutCurve_, "fadeOut"}})
            {
                length->setBounds(row(key).withWidth(half));
                curve->setBounds(row(key).withTrimmedLeft(half + t.shell.itemGap));
            }
            // Each crossfade's shape, then where it sits.
            crossInShape_.setBounds(row("crossInShape").reduced(0, t.shell.propertyFieldInset));
            crossInPlace_.setBounds(row("crossInPlace").reduced(0, t.shell.propertyFieldInset));
            crossOutShape_.setBounds(row("crossOutShape").reduced(0, t.shell.propertyFieldInset));
            crossOutPlace_.setBounds(row("crossOutPlace").reduced(0, t.shell.propertyFieldInset));
        }
        else if (sections_[i] == S::Notes) // its fields, then Mute
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth);
            const auto row = [&](int n)
            { return content.withY(content.getY() + n * t.shell.rowHeight).withHeight(t.shell.rowHeight); };
            // A field's text lines up with the values painted in the rows above it.
            const auto field = [&](int n)
            { return row(n).withLeft(row(n).getX() - ui::NumberField::textInset); };
            notePitch_.setBounds(field(0));
            noteStart_.setBounds(field(1));
            noteLength_.setBounds(field(2));
            noteVelocity_.setBounds(field(3));
            noteSpread_.setBounds(field(4));
            noteRelease_.setBounds(field(5));
            noteChance_.setBounds(field(6));
            noteMute_.setBounds(row(7)
                                    .withWidth(ui::Toggle::widthFor(noteMute_.getButtonText()))
                                    .reduced(0, t.shell.propertyFieldInset));
        }
        else if (sections_[i] == S::Section) // Name, Colour, Start, Length
        {
            const auto content = stack_.content(i).withTrimmedLeft(t.shell.propertyLabelWidth);
            const auto row = [&](const char* key)
            {
                const int n = rowOf(S::Section, key);
                return n < 0 ? juce::Rectangle<int>()
                             : content.withY(content.getY() + n * t.shell.rowHeight)
                                   .withHeight(t.shell.rowHeight);
            };
            sectionName_.setBounds(row("name").reduced(0, t.shell.propertyFieldInset));
            sectionColour_.setBounds(row("colour"));
            sectionStart_.setBounds(row("start").withTrimmedLeft(-ui::NumberField::textInset));
            sectionLength_.setBounds(row("length").withTrimmedLeft(-ui::NumberField::textInset));
        }
        else if (sections_[i] == S::Point) // the Value row
            pointValue_.setBounds(
                stack_.content(i)
                    .withTrimmedLeft(t.shell.propertyLabelWidth - ui::NumberField::textInset)
                    .withY(stack_.content(i).getY() + t.shell.rowHeight)
                    .withHeight(t.shell.rowHeight));
    return height;
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (sections_.empty())
    {
        auto r = getLocalBounds().withTrimmedTop(header_.getBottom()).reduced(t.shell.padding, 0);
        ui::paintEmptyState(g, r.removeFromTop(t.shell.listRowHeight * 2), "Nothing selected");
    }
}

void InspectorPanel::paintSections(juce::Graphics& g)
{
    stack_.paint(g);
    for (size_t i = 0; i < sections_.size(); ++i)
        if (const auto rows = properties(sections_[i]); !rows.empty())
            ui::paintProperties(g, stack_.content(i), rows);
}

// ---- DetailPanel ---------------------------------------------------------------------------

DetailPanel::DetailPanel(engine::Session& session, const ActionRegistry& actions)
    : actions_(actions), chain_(session, actions), roll_(session), tools_(roll_, session), audio_(session)
{
    header_.onTabSelected = [this](int tab)
    {
        chooseMode(tab >= 0 && tab < static_cast<int>(tabModes_.size()) ? tabModes_[static_cast<size_t>(tab)]
                                                                        : Mode::Devices);
    };
    header_.onTabClosed = [this](int) { closeEditor(); }; // the editor's tab, the only one that closes
    updateTabs();
    addAndMakeVisible(header_);
    addAndMakeVisible(chain_);
    addChildComponent(roll_);
    addChildComponent(audio_);
    chain_.onShowEditor = [this](std::uint64_t module, std::unique_ptr<ui::ModuleEditor> editor)
    { showEditor(module, std::move(editor)); };
    chain_.onModuleGone = [this](std::uint64_t module)
    {
        if (editor_ != nullptr && module == editorModule_)
            closeEditor();
    };
    // The Notes menu, then Quantise (piano-roll.md §4).
    notes_.setTooltip("Every tool for notes: the Notes menu");
    notes_.onClick = [this]
    {
        if (notesMenu)
            notes_.showMenu(notesMenu());
    };
    notes_.setSize(notes_.preferredWidth(), ui::Theme::current().shell.rowHeight);
    header_.addCommand(notes_);
    notes_.setVisible(false);
    quantise_.setClickingTogglesState(false);
    quantise_.setWantsKeyboardFocus(false);
    quantise_.onClick = [this] { actions_.perform("notes.quantise"); };
    quantise_.setSize(ui::Toggle::widthFor(quantise_.getButtonText()), ui::Theme::current().shell.rowHeight);
    header_.addCommand(quantise_);
    quantise_.setVisible(false);
    // Transform and Generate (§25): the Tools pane on their tab; clicked again, it closes.
    for (auto [button, icon, tab, tip] :
         {std::tuple{&transform_, ui::Icon::Transform, model::ToolKind::Transform,
                     "The tools that change notes: Quantise, Strum, Arpeggiate, Make chords and more"},
          std::tuple{&generate_, ui::Icon::Generate, model::ToolKind::Generate,
                     "The tools that write notes: a rhythm, a melody, chords, a bassline"}})
    {
        button->setIcon(icon);
        button->setClickingTogglesState(false);
        button->setWantsKeyboardFocus(false);
        button->setTooltip(tip);
        button->onClick = [this, kind = tab]
        { showTools(toolsShown(kind) ? std::nullopt : std::optional(kind)); };
        button->setSize(ui::Toggle::widthFor(button->getButtonText(), true),
                        ui::Theme::current().shell.rowHeight);
        header_.addCommand(*button);
        button->setVisible(false);
    }
    addChildComponent(tools_);
    tools_.onEscape = [this] { showTools(std::nullopt); };
    tools_.onTabChanged = [this](model::ToolKind tab) { showTools(tab); };
    // The piano roll's options; at compact widths Others, then Fold, the voicing, Draw and last the
    // kinds give way (§4, §20.2).
    draw_.setTooltip("What a double-click draws: a note, the key's chord on it, or the key's chord under it");
    draw_.onClick = [this] { draw_.showMenu(roll_.drawMenu()); };
    // While drawing chords: the kind, one click each, and the voicing (§20.2).
    {
        juce::StringArray kinds;
        for (auto shape : {music::ChordShape::Triad, music::ChordShape::Seventh, music::ChordShape::Ninth,
                           music::ChordShape::Sus2, music::ChordShape::Sus4, music::ChordShape::Add9})
            kinds.add(model::chordShapeName(shape));
        drawKind_.setItems(kinds);
    }
    drawKind_.setTitle("The chords drawn");
    drawKind_.onChange = [this](int index)
    {
        auto style = roll_.options().chord;
        style.shape = static_cast<music::ChordShape>(index);
        roll_.setChordStyle(style);
    };
    drawVoicing_.setTooltip(
        "How the chords drawn are voiced: Smooth moves least from the chord before; and their inversion");
    drawVoicing_.onClick = [this] { drawVoicing_.showMenu(roll_.voicingMenu()); };
    inKey_.setClickingTogglesState(false);
    inKey_.onClick = [this] { roll_.setInKey(!roll_.inKey()); };
    others_.setClickingTogglesState(false);
    others_.onClick = [this] { roll_.setOthers(!roll_.options().others); };
    // Right-click: which tracks, one by one (§8); also View > Other tracks' notes.
    others_.onPopupMenu = [this]
    { roll_.othersMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&others_)); };
    for (auto* option : {&draw_, &drawVoicing_})
        option->setVisible(false);
    drawKind_.setVisible(false);
    header_.addOption(draw_);
    header_.addOption(drawKind_);
    header_.addOption(drawVoicing_);
    header_.addOption(inKey_);
    header_.addOption(others_);
    inKey_.setVisible(false);
    others_.setVisible(false);
    header_.setDropOrder(others_, 1);
    header_.setDropOrder(drawVoicing_, 2);
    header_.setDropOrder(draw_, 3);
    header_.setDropOrder(drawKind_, 4);
    header_.setDropOrder(generate_, 5); // the commands that open the Tools pane go last (§25)
    header_.setDropOrder(transform_, 6);
    roll_.onOptionsChanged = [this] { updateOptions(); };
    snap_.setClickingTogglesState(false); // the piano roll's, which the Session keeps
    snap_.onClick = [this] { roll_.setSnapEnabled(!roll_.isSnapEnabled()); };
    grid_.setTooltip(
        "The clip view's grid: the step it snaps to, and a drawn note's length. Click to set it: "
        "adaptive or fixed, triplets, snap");
    grid_.onClick = [this] { grid_.showMenu(roll_.gridMenu()); };
    header_.addOption(snap_);
    header_.addOption(grid_);
    snap_.setVisible(false);
    grid_.setVisible(false);
    updateOptions();
    header_.resized();
}

void DetailPanel::updateOptions()
{
    const auto& o = roll_.options();
    draw_.setValue(model::drawModeName(o.draw));
    drawKind_.setSelected(static_cast<int>(o.chord.shape));
    drawVoicing_.setValue(o.chord.inversion == music::Inversion::Auto
                              ? juce::String(model::voicingName(o.chord.voicing))
                              : juce::String(model::voicingName(o.chord.voicing)) +
                                    juce::String::fromUTF8(" · ") + model::inversionName(o.chord.inversion));
    const bool chords = showsPianoRoll() && o.draw != model::DrawMode::Note;
    drawKind_.setVisible(chords);
    drawVoicing_.setVisible(chords);
    inKey_.setToggleState(roll_.inKey(), juce::dontSendNotification);
    others_.setToggleState(o.others, juce::dontSendNotification);
    inKey_.setTooltip(actions_.tooltip("view.clipInKey"));
    others_.setTooltip(actions_.tooltip("view.clipOthers"));
    // The Tools pane as the view state has it (a project opened with it open, on a tab).
    if (o.toolsTab != tools_.tab())
        tools_.setTab(o.toolsTab);
    if (tools_.isVisible() != (showsPianoRoll() && o.toolsOpen) ||
        transform_.getToggleState() != (showsPianoRoll() && toolsShown(model::ToolKind::Transform)) ||
        generate_.getToggleState() != (showsPianoRoll() && toolsShown(model::ToolKind::Generate)))
        updateVisibility();
    header_.resized(); // the choices are as wide as their values
    header_.repaint();
}

void DetailPanel::chooseMode(Mode mode)
{
    onModeChosen ? onModeChosen(mode) : setMode(mode);
}

void DetailPanel::updateTabs()
{
    tabModes_ = {Mode::Devices};
    juce::StringArray names{"Modules"};
    if (clipViewAvailable_)
    {
        tabModes_.push_back(Mode::Clip);
        names.add("Clip");
    }
    if (editor_ != nullptr)
    {
        tabModes_.push_back(Mode::Editor);
        names.add(editor_->tabName());
    }
    if (names.size() == 1)
    {
        header_.setTitle("Modules");
        header_.setCaption({});
    }
    else
    {
        const auto at = std::find(tabModes_.begin(), tabModes_.end(), mode_);
        header_.setTabs(names, at != tabModes_.end() ? static_cast<int>(at - tabModes_.begin()) : 0);
        header_.setClosableTab(editor_ != nullptr ? names.size() - 1 : -1,
                               editor_ != nullptr ? editor_->closeTooltip() : juce::String());
        header_.setCaption("Tab switches");
    }
    updateHeader();
}

void DetailPanel::updateHeader()
{
    if (const auto at = std::find(tabModes_.begin(), tabModes_.end(), mode_); at != tabModes_.end())
        header_.setSelectedTab(static_cast<int>(at - tabModes_.begin()));
    if (mode_ == Mode::Devices)
        header_.setContext(track_ ? juce::String::fromUTF8(track_->name.c_str()) : juce::String());
    else if (mode_ == Mode::Editor)
        header_.setContext(editor_ != nullptr ? editor_->title() : juce::String());
    else
    {
        const auto& clip = roll_.clip() ? roll_.clip() : audio_.clip();
        header_.setContext(clip ? juce::String::fromUTF8(clip->name.c_str()) : juce::String());
    }
}

void DetailPanel::setMode(Mode m)
{
    if ((m == Mode::Clip && !clipViewAvailable_) || (m == Mode::Editor && editor_ == nullptr))
        m = Mode::Devices;
    if (mode_ == m)
        return;
    mode_ = m;
    updateVisibility();
    updateHeader();
    resized();
    repaint();
}

void DetailPanel::setClipViewAvailable(bool available)
{
    if (available == clipViewAvailable_)
        return;
    clipViewAvailable_ = available;
    if (!available && mode_ == Mode::Clip)
        setMode(Mode::Devices);
    updateTabs();
}

void DetailPanel::showEditor(std::uint64_t module, std::unique_ptr<ui::ModuleEditor> editor)
{
    if (editor_ != nullptr)
        closeEditor();
    editor_ = std::move(editor);
    editorModule_ = module;
    addChildComponent(*editor_);
    for (auto* c : editor_->commands())
    {
        header_.addCommand(*c);
        c->setVisible(false);
    }
    editor_->onTitleChanged = [this] { updateHeader(); };
    editor_->onShowRequested = [this] { chooseMode(Mode::Editor); };
    updateTabs();
    if (onEditorChanged)
        onEditorChanged();
    chooseMode(Mode::Editor);
}

void DetailPanel::closeEditor()
{
    if (editor_ == nullptr)
        return;
    editor_->closing();
    for (auto* c : editor_->commands())
        header_.removeCommand(*c);
    // Out of the panel before it goes: what it shows asks nothing of it any more.
    const auto closing = std::move(editor_);
    editorModule_ = 0;
    removeChildComponent(closing.get());
    if (mode_ == Mode::Editor)
        setMode(Mode::Devices);
    updateTabs();
    updateVisibility();
    if (onEditorChanged)
        onEditorChanged();
}

void DetailPanel::updateVisibility()
{
    const bool editing = mode_ == Mode::Editor && editor_ != nullptr;
    chain_.setVisible(mode_ == Mode::Devices || editing);
    roll_.setVisible(showsPianoRoll());
    audio_.setVisible(showsAudioClip());
    if (editor_ != nullptr)
    {
        editor_->setVisible(editing);
        for (auto* c : editor_->commands())
            c->setVisible(editing);
    }
    // Snap and the step for either clip view; Notes, Quantise and the piano roll's options for notes.
    const bool grid = showsPianoRoll() || showsAudioClip();
    const bool notes = showsPianoRoll();
    notes_.setVisible(notes);
    quantise_.setVisible(notes);
    draw_.setVisible(notes);
    drawKind_.setVisible(notes && roll_.options().draw != model::DrawMode::Note);
    drawVoicing_.setVisible(notes && roll_.options().draw != model::DrawMode::Note);
    inKey_.setVisible(notes && roll_.key().has_value());
    inKey_.setToggleState(roll_.inKey(), juce::dontSendNotification); // each clip its own
    others_.setVisible(notes);
    transform_.setVisible(notes);
    generate_.setVisible(notes);
    transform_.setToggleState(notes && toolsShown(model::ToolKind::Transform), juce::dontSendNotification);
    generate_.setToggleState(notes && toolsShown(model::ToolKind::Generate), juce::dontSendNotification);
    const bool tools = notes && roll_.options().toolsOpen;
    if (!tools && tools_.isVisible())
        tools_.disarm(); // the notes back as it goes
    tools_.setVisible(tools);
    snap_.setVisible(grid);
    grid_.setVisible(grid);
    header_.resized();
    header_.repaint();
    resized();
}

void DetailPanel::showTools(std::optional<model::ToolKind> tab)
{
    auto o = roll_.options();
    o.toolsOpen = tab.has_value();
    if (tab)
        o.toolsTab = *tab;
    if (tab && *tab != tools_.tab())
        tools_.setTab(*tab);
    roll_.setOptions(o); // view state; updateOptions lays the pane out
    updateVisibility();
}

bool DetailPanel::toolsShown(model::ToolKind tab) const
{
    return roll_.options().toolsOpen && roll_.options().toolsTab == tab;
}

void DetailPanel::showTool(std::string_view id)
{
    const auto* tool = model::findNoteTool(id);
    showTools(tool != nullptr ? tool->kind : model::ToolKind::Transform);
    tools_.choose(id);
}

void DetailPanel::setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo)
{
    const bool audio = clip && clip->kind == model::ClipInfo::Kind::Audio;
    roll_.setClip(audio ? std::nullopt : clip, tempo);
    audio_.setClip(audio ? std::move(clip) : std::nullopt, tempo);
    updateVisibility();
    updateHeader();
}

void DetailPanel::frame(double playheadSeconds)
{
    if (tools_.isVisible())
        tools_.refresh(); // its tiles as the notes allow; its tool shown again on changed notes
    if (roll_.isVisible() || audio_.isVisible())
    {
        if (roll_.isVisible())
            roll_.frame(playheadSeconds);
        else
            audio_.frame(playheadSeconds);
        snap_.setToggleState(roll_.isVisible() ? roll_.isSnapEnabled() : audio_.timeline().grid().snap, juce::dontSendNotification);
        if (const auto step = roll_.isVisible() ? roll_.gridStepName() : audio_.gridStepName(); step != grid_.value())
        {
            grid_.setValue(step);
            header_.resized(); // as wide as its step
        }
        if (const auto tip = actions_.tooltip("view.snap"); tip != snap_.getTooltip())
            snap_.setTooltip(tip);
    }
    if (const bool can = actions_.isEnabled("notes.quantise"); quantise_.isEnabled() != can)
        quantise_.setEnabled(can);
    // Its tooltip names its key: the actions are registered after the panel is made.
    if (const auto tip = actions_.tooltip("notes.quantise") + ": the selected notes, or all of them, to the grid";
        tip != quantise_.getTooltip())
        quantise_.setTooltip(tip);
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
    audio_.setBounds(r);
    // The Tools pane at the clip view's right edge (§25): beside the notes, or floating over their
    // right side when the clip view is narrow.
    if (tools_.isVisible())
    {
        tools_.setFloating(r.getWidth() < sh.toolsDockFrom);
        const int width = tools_.preferredWidth();
        if (tools_.isFloating())
        {
            roll_.setBounds(r);
            tools_.setBounds(r.withLeft(std::max(r.getX(), r.getRight() - width)));
            tools_.toFront(false);
        }
        else
        {
            tools_.setBounds(r.withLeft(r.getRight() - width));
            roll_.setBounds(r.withTrimmedRight(width));
        }
    }
    else
        roll_.setBounds(r);
    if (mode_ == Mode::Editor && editor_ != nullptr)
    {
        // The module where it was, its editor below, lined up with the module panels.
        chain_.setBounds(r.removeFromTop(DeviceChain::preferredHeight())
                             .reduced(sh.itemGap, 0)
                             .withTrimmedBottom(sh.itemGap));
        editor_->setBounds(r.reduced(sh.itemGap + DeviceChain::gap, 0).withTrimmedBottom(DeviceChain::gap));
        return;
    }
    chain_.setBounds(r.reduced(sh.itemGap, 0).withTrimmedBottom(sh.itemGap));
}

void DetailPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface1);
    if (mode_ == Mode::Clip && !roll_.clip() && !audio_.clip())
        ui::paintEmptyState(g, getLocalBounds().withTrimmedTop(header_.getBottom()),
                            "Select a clip: a MIDI clip's notes or an audio clip's waveform show here. Double-click an empty "
                            "spot on a track to make a MIDI clip.");
}
void DetailPanel::selectModule(std::uint64_t id)
{
    setMode(Mode::Devices);
    chain_.selectDevice(id);
}
} // namespace fd::app
