#include "ui/Panels.h"

#include <fd/device/Registry.h>

namespace fd::app
{
namespace
{
void paintSectionLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.textTertiary);
    g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::SemiBold).withExtraKerningFactor(0.06f));
    g.drawText(text.toUpperCase(), area, juce::Justification::centredLeft, false);
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
    add(loop_, "transport.loop");
    add(metronome_, "transport.metronome");
    add(keyboard_, "midi.computerKeyboard");

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

    const int cpu = static_cast<int>(std::lround(session_.cpuUsage() * 100.0));
    if (positionSeconds != position_ || cpu != cpu_)
    {
        position_ = positionSeconds;
        cpu_ = cpu;
        repaint(getLocalBounds().withTrimmedLeft(denominatorField_.getRight()).withRight(browser_.getX()));
    }
}

void TransportBar::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);

    // BPM label after the tempo field, the slash between the signature fields.
    g.setColour(t.textTertiary);
    g.setFont(ui::fonts::font(t.textXs));
    g.drawText("BPM", tempoField_.getBounds().translated(tempoField_.getWidth() + 2, 0).withWidth(30),
               juce::Justification::centredLeft, false);
    g.setFont(ui::fonts::font(t.textMd, ui::fonts::Weight::Medium));
    g.drawText("/", numeratorField_.getBounds().withX(numeratorField_.getRight()).withWidth(10),
               juce::Justification::centred, false);

    // MIDI activity light beside the computer keyboard button.
    const auto light = midiLightArea().toFloat().withSizeKeepingCentre(6.0f, 6.0f);
    g.setColour(midiLit_ ? t.accentMidi : t.surface3);
    g.fillEllipse(light);

    auto r = getLocalBounds().withTrimmedLeft(denominatorField_.getRight() + 20).withRight(keyboard_.getX() - 12);
    auto field = [&](const juce::String& value, const juce::String& unit, int width, float size) {
        auto box = r.removeFromLeft(width);
        r.removeFromLeft(16);
        g.setColour(t.textPrimary);
        g.setFont(ui::fonts::font(size, ui::fonts::Weight::Medium, true));
        const int valueWidth = static_cast<int>(
            juce::GlyphArrangement::getStringWidth(ui::fonts::font(size, ui::fonts::Weight::Medium, true), value)) + 4;
        g.drawText(value, box.removeFromLeft(valueWidth), juce::Justification::centredLeft, false);
        g.setColour(t.textTertiary);
        g.setFont(ui::fonts::font(t.textXs));
        g.drawText(unit, box, juce::Justification::centredLeft, false);
    };
    const auto bb = tempo_.barsBeatsForSeconds(std::max(0.0, position_));
    field(juce::String(bb.bar) + ". " + juce::String(bb.beat) + ". " + juce::String(bb.sixteenth), "", 84, t.textLg);
    field(formatTime(position_), "", 84, t.textSm);

    g.setColour(t.textTertiary);
    g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::Regular, true));
    g.drawText("CPU " + juce::String(std::max(0, cpu_)) + " %", r, juce::Justification::centredRight, false);
}

void TransportBar::resized()
{
    const auto& t = ui::Theme::current();
    auto r = getLocalBounds().reduced(t.grid * 2, 6);
    auto square = [&](juce::Rectangle<int>& area, bool left) {
        auto b = left ? area.removeFromLeft(area.getHeight()) : area.removeFromRight(area.getHeight());
        left ? area.removeFromLeft(4) : area.removeFromRight(4);
        return b;
    };
    play_.setBounds(square(r, true));
    stop_.setBounds(square(r, true));
    r.removeFromLeft(8);
    loop_.setBounds(square(r, true));
    metronome_.setBounds(square(r, true));
    r.removeFromLeft(16);
    tempoField_.setBounds(r.removeFromLeft(54));
    r.removeFromLeft(34); // "BPM"
    numeratorField_.setBounds(r.removeFromLeft(26));
    r.removeFromLeft(10); // "/"
    denominatorField_.setBounds(r.removeFromLeft(26));
    settings_.setBounds(square(r, false));
    r.removeFromRight(8);
    inspector_.setBounds(square(r, false));
    detail_.setBounds(square(r, false));
    browser_.setBounds(square(r, false));
    // The computer keyboard toggle sits on the right, like the MIDI section of a control bar.
    r.removeFromRight(12);
    r.removeFromRight(10); // activity light
    keyboard_.setBounds(square(r, false));
}

juce::Rectangle<int> TransportBar::midiLightArea() const
{
    return keyboard_.getBounds().withX(keyboard_.getRight() + 2).withWidth(10);
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
    search_.setBounds(getLocalBounds().reduced(8).removeFromTop(24));
    expand_.setBounds(getLocalBounds().removeFromTop(44).withSizeKeepingCentre(28, 28));
}

void BrowserPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (rail_)
        return;
    auto r = getLocalBounds().reduced(12, 8).withTrimmedTop(32);
    for (const auto& [title, items] : sections_)
    {
        paintSectionLabel(g, title, r.removeFromTop(22));
        g.setFont(ui::fonts::font(t.textSm));
        if (items.empty())
        {
            g.setColour(t.textTertiary);
            g.drawText("Coming with the browser", r.removeFromTop(20), juce::Justification::centredLeft, false);
        }
        for (const auto& item : items)
        {
            g.setColour(t.textPrimary);
            g.drawText(item, r.removeFromTop(20), juce::Justification::centredLeft, true);
        }
        r.removeFromTop(10);
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
    repaint();
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    auto r = getLocalBounds().reduced(12, 8);
    paintSectionLabel(g, "Inspector", r.removeFromTop(22));
    g.setFont(ui::fonts::font(t.textSm));
    if (!track_ && clipCount_ == 0)
    {
        g.setColour(t.textTertiary);
        g.drawText("Nothing selected", r.removeFromTop(20), juce::Justification::centredLeft, false);
        return;
    }
    auto row = [&](const juce::String& label, const juce::String& value) {
        auto line = r.removeFromTop(20);
        g.setColour(t.textSecondary);
        g.drawText(label, line.removeFromLeft(80), juce::Justification::centredLeft, false);
        g.setColour(t.textPrimary);
        g.drawText(value, line, juce::Justification::centredLeft, true);
    };
    auto position = [&](double seconds) {
        const auto bb = tempo_.barsBeatsForSeconds(seconds);
        return juce::String(bb.bar) + "." + juce::String(bb.beat) + "." + juce::String(bb.sixteenth);
    };
    if (clip_)
    {
        row("Clip", juce::String::fromUTF8(clip_->name.c_str()));
        row("Start", position(clip_->start));
        row("Length", juce::String(clip_->length, 3) + " s");
        row("Offset", juce::String(clip_->offset, 3) + " s");
        r.removeFromTop(12);
    }
    else if (clipCount_ > 1)
    {
        row("Clips", juce::String(clipCount_) + " selected");
        r.removeFromTop(12);
    }
    if (!track_)
        return;
    row("Track", juce::String::fromUTF8(track_->name.c_str()));
    row("Instrument", track_->instrument.empty() ? juce::String("None") : juce::String::fromUTF8(track_->instrument.c_str()));
    row("Clips", juce::String(static_cast<int>(track_->clips.size())));
}

// ---- DetailPanel ---------------------------------------------------------------------------

DetailPanel::DetailPanel(engine::Session& session, const ActionRegistry& actions) : chain_(session, actions), roll_(session)
{
    addAndMakeVisible(chain_);
    addChildComponent(roll_);
}

void DetailPanel::setMode(Mode m)
{
    if (mode_ == m)
        return;
    mode_ = m;
    updateVisibility();
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
    repaint(getLocalBounds().removeFromTop(headerHeight));
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
    repaint();
}

void DetailPanel::resized()
{
    chain_.setBounds(getLocalBounds().withTrimmedTop(headerHeight).reduced(4, 0).withTrimmedBottom(4));
    roll_.setBounds(getLocalBounds().withTrimmedTop(headerHeight));
}

void DetailPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    auto r = getLocalBounds().reduced(12, 0);
    auto header = r.removeFromTop(headerHeight);
    g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::SemiBold));
    for (auto [m, label] : {std::pair{Mode::Devices, "Devices"}, std::pair{Mode::Clip, "Clip"}})
    {
        g.setColour(mode_ == m ? t.textPrimary : t.textTertiary);
        g.drawText(label, header.removeFromLeft(60), juce::Justification::centredLeft, false);
    }
    g.setColour(t.textTertiary);
    g.drawText("Tab switches", header, juce::Justification::centredRight, false);

    if (mode_ == Mode::Devices)
    {
        if (track_)
        {
            g.setColour(t.textSecondary);
            g.drawText(juce::String::fromUTF8(track_->name.c_str()), header.withTrimmedLeft(16).withTrimmedRight(100),
                       juce::Justification::centredLeft, true);
        }
        return; // the chain draws the rest
    }
    if (const auto& clip = roll_.clip())
    {
        g.setColour(t.textSecondary);
        g.drawText(juce::String::fromUTF8(clip->name.c_str()), header.withTrimmedLeft(16).withTrimmedRight(100),
                   juce::Justification::centredLeft, true);
        return; // the piano roll draws the rest
    }
    g.setFont(ui::fonts::font(t.textSm));
    g.drawText("Select a MIDI clip to edit its notes. Double-click an empty spot on a track to make one.", r,
               juce::Justification::centred, false);
}
} // namespace fd::app
