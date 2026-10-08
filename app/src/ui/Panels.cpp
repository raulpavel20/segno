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
    const int cpu = static_cast<int>(std::lround(session_.cpuUsage() * 100.0));
    if (positionSeconds != position_ || !(tempo == tempo_) || cpu != cpu_)
    {
        position_ = positionSeconds;
        tempo_ = tempo;
        cpu_ = cpu;
        repaint(getLocalBounds().withTrimmedLeft(play_.getRight()).withRight(browser_.getX()));
    }
}

void TransportBar::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);

    auto r = getLocalBounds().withTrimmedLeft(stop_.getRight() + 20).withRight(browser_.getX() - 12);
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
    field(juce::String(tempo_.bpm, 2), "BPM", 92, t.textMd);
    field(juce::String(tempo_.numerator) + " / " + juce::String(tempo_.denominator), "", 44, t.textMd);
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
    settings_.setBounds(square(r, false));
    r.removeFromRight(8);
    inspector_.setBounds(square(r, false));
    detail_.setBounds(square(r, false));
    browser_.setBounds(square(r, false));
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
    row("Type", track_->kind == model::TrackInfo::Kind::Instrument ? "Instrument" : "Audio");
    row("Clips", juce::String(static_cast<int>(track_->clips.size())));
}

// ---- DetailPanel ---------------------------------------------------------------------------

void DetailPanel::setMode(Mode m)
{
    mode_ = m;
    repaint();
}

void DetailPanel::setSelection(std::optional<model::TrackInfo> track)
{
    track_ = std::move(track);
    repaint();
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

    g.setFont(ui::fonts::font(t.textSm));
    const juce::String text = !track_ ? juce::String("Select a track to see its devices")
                              : mode_ == Mode::Devices
                                  ? juce::String::fromUTF8(track_->name.c_str()) + juce::String(": the device chain arrives with step C6")
                                  : juce::String("The clip view arrives with clip editing");
    g.drawText(text, r, juce::Justification::centred, false);
}
} // namespace fd::app
