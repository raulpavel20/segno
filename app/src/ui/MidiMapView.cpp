#include "ui/MidiMapView.h"

namespace fd::app
{
namespace
{
const juce::Identifier kChannelMark{"fdMidiChannel"}, kKeyMark{"fdMidiKey"};

/// A target found on a control or the one holding it (a knob's text box is part of the knob).
std::optional<model::MidiTarget> resolveUp(const MidiTargetResolver& resolve, juce::Component& c)
{
    int levels = 0;
    for (auto* at = &c; at != nullptr && levels < 3; at = at->getParentComponent(), ++levels)
        if (auto t = resolve(*at))
            return t;
    return std::nullopt;
}
} // namespace

void setMidiTarget(juce::Component& component, const model::MidiTarget& target)
{
    component.getProperties().set(kChannelMark, static_cast<juce::int64>(target.channel));
    component.getProperties().set(kKeyMark, juce::String(target.key));
}

std::optional<model::MidiTarget> midiTargetMark(const juce::Component& component)
{
    const auto& p = component.getProperties();
    if (!p.contains(kKeyMark))
        return std::nullopt;
    return model::MidiTarget{static_cast<std::uint64_t>(static_cast<juce::int64>(p[kChannelMark])),
                             p[kKeyMark].toString().toStdString()};
}

// ---- the overlay -----------------------------------------------------------------------------

MidiMapOverlay::MidiMapOverlay(engine::Session& session, juce::Component& root, MidiTargetResolver resolve)
    : session_(session), root_(root), resolve_(std::move(resolve))
{
    setInterceptsMouseClicks(true, false);
    setAlwaysOnTop(true);
}

MidiMapOverlay::~MidiMapOverlay()
{
    if (listening_)
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void MidiMapOverlay::setMapMode(bool on)
{
    if (mapMode_ == on)
        return;
    mapMode_ = on;
    if (!on && session_.midiLearnTarget())
        session_.cancelMidiLearn(); // leaving the mode gives up the control chosen
    update();
    repaint();
}

void MidiMapOverlay::update()
{
    waiting_ = session_.midiLearnTarget();
    // Outside map mode, a press elsewhere gives up the control waiting (ML1).
    const bool listen = waiting_.has_value() && !mapMode_;
    if (listen != listening_)
    {
        listening_ = listen;
        if (listen)
            juce::Desktop::getInstance().addGlobalMouseListener(this);
        else
            juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }
    std::vector<Shown> found;
    if (mapMode_ || waiting_)
    {
        // Every control showing that can be mapped (not looking inside one that can).
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (child == this || !child->isVisible())
                    continue;
                if (auto t = resolve_(*child); t && session_.canMapMidi(*t))
                {
                    if (mapMode_ || *t == *waiting_)
                        found.push_back({child, getLocalArea(child, child->getLocalBounds()), *t});
                    continue;
                }
                walk(*child);
            }
        };
        walk(root_);
    }
    const auto same = [](const Shown& a, const Shown& b)
    { return a.control == b.control && a.bounds == b.bounds && a.target == b.target; };
    if (!std::equal(found.begin(), found.end(), shown_.begin(), shown_.end(), same))
    {
        shown_ = std::move(found);
        repaint();
    }
    else if (waiting_) // its outline pulses
        for (const auto& s : shown_)
            if (s.target == *waiting_)
                repaint(s.bounds.expanded(2));
}

MidiMapOverlay::Label MidiMapOverlay::labelOf(const Shown& s) const
{
    const auto mapping = session_.midiMappingOf(s.target);
    if (!mapping)
        return {};
    const auto& t = ui::Theme::current();
    const auto font = ui::font(ui::TextStyle::Micro);
    const int inset = 1, pad = t.shell.itemGap / 2;
    const int height = static_cast<int>(std::ceil(font.getHeight()));
    // Centred over the control (its tint behind it): the whole label where it fits, else only
    // its number or note.
    const auto shortText = mapping->source.kind == model::MidiSource::Kind::Cc ||
                                   mapping->source.kind == model::MidiSource::Kind::Cc14
                               ? juce::String(mapping->source.number)
                               : juce::String(model::sourceLabel(mapping->source));
    for (const auto& text : {juce::String(model::sourceLabel(mapping->source)), shortText})
    {
        const int width =
            static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text))) + 2 * pad;
        if (width + 2 * inset <= s.bounds.getWidth() && height + 2 * inset <= s.bounds.getHeight())
            return {text, s.bounds.withSizeKeepingCentre(width, height)};
    }
    return {};
}

void MidiMapOverlay::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (const auto& s : shown_)
    {
        const auto r = s.bounds.toFloat();
        const bool chosen = waiting_ && s.target == *waiting_;
        const auto label = mapMode_ ? labelOf(s) : Label{};
        if (mapMode_ && label.text.isNotEmpty())
        {
            // Mapped: tinted, its message over it.
            g.setColour(t.roles.midiMapFill);
            g.fillRoundedRectangle(r, t.radiusControl);
        }
        if (chosen)
        {
            // Waiting: solid and thicker; outside map mode it pulses, as the hardware's to move.
            const float pulse =
                mapMode_ ? 1.0f : 0.55f + 0.45f * static_cast<float>(std::sin(seconds * 6.0) * 0.5 + 0.5);
            g.setColour(t.roles.midiMap);
            g.setOpacity(pulse);
            g.drawRoundedRectangle(r.reduced(1.0f), t.radiusControl, 2.0f);
            g.setOpacity(1.0f);
        }
        else if (mapMode_)
        {
            g.setColour(t.roles.midiMap);
            g.drawRoundedRectangle(r.reduced(0.5f), t.radiusControl, 1.0f);
        }
        if (!label.area.isEmpty())
        {
            g.setColour(t.surface0);
            g.fillRoundedRectangle(label.area.toFloat(), t.radiusControl * 0.5f);
            g.setColour(t.roles.midiMap);
            g.setFont(ui::font(ui::TextStyle::Micro));
            g.drawText(label.text, label.area, juce::Justification::centred, false);
        }
    }
}

const MidiMapOverlay::Shown* MidiMapOverlay::at(juce::Point<int> p) const
{
    const Shown* best = nullptr;
    for (const auto& s : shown_)
        if (s.bounds.contains(p) &&
            (best == nullptr ||
             s.bounds.getWidth() * s.bounds.getHeight() < best->bounds.getWidth() * best->bounds.getHeight()))
            best = &s;
    return best;
}

bool MidiMapOverlay::hitTest(int x, int y)
{
    // In map mode, the controls' clicks are the overlay's (they choose); elsewhere, the views'.
    return mapMode_ && at({x, y}) != nullptr;
}

void MidiMapOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent != this)
    {
        pressed(e);
        return;
    }
    if (const auto* s = at(e.getPosition()))
    {
        session_.learnMidi(s->target);
        update();
        repaint();
    }
}

void MidiMapOverlay::pressed(const juce::MouseEvent& e)
{
    if (!waiting_ || mapMode_)
        return;
    for (const auto& s : shown_)
        if (s.target == *waiting_ && s.control != nullptr &&
            (e.eventComponent == s.control || s.control->isParentOf(e.eventComponent)))
            return; // on the control waiting: it keeps waiting
    session_.cancelMidiLearn();
    update();
    repaint();
}

// ---- the banner ------------------------------------------------------------------------------

MidiMapBanner::MidiMapBanner()
{
    done.setClickingTogglesState(false);
    done.setWantsKeyboardFocus(false);
    done.setTooltip("Leave MIDI map mode (Escape)");
    addAndMakeVisible(done);
}

int MidiMapBanner::height()
{
    return ui::Theme::current().shell.listRowHeight;
}

juce::Rectangle<int> MidiMapBanner::textArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().reduced(s.padding, 0).withRight(done.getX() - s.groupGap);
}

void MidiMapBanner::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface2);
    g.setColour(t.roles.midiMap);
    g.fillRect(getLocalBounds().removeFromBottom(1));
    g.setColour(t.textPrimary);
    g.setFont(ui::font(ui::TextStyle::Body));
    g.drawText(juce::String::fromUTF8(text), textArea(), juce::Justification::centredLeft, true);
}

void MidiMapBanner::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto r = getLocalBounds().reduced(s.padding, 0);
    const int w = ui::Toggle::widthFor(done.getButtonText());
    done.setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, s.rowHeight));
}

// ---- tooltips --------------------------------------------------------------------------------

Tooltips::Tooltips(juce::Component* parent, engine::Session& session, MidiTargetResolver resolve)
    : juce::TooltipWindow(parent), session_(session), resolve_(std::move(resolve))
{
}

juce::String Tooltips::getTipFor(juce::Component& component)
{
    auto tip = juce::TooltipWindow::getTipFor(component);
    const auto target = resolveUp(resolve_, component);
    if (!target)
        return tip;
    if (session_.midiLearnTarget() == target)
        return "Move a control on your MIDI device";
    if (const auto m = session_.midiMappingOf(*target))
    {
        const auto line = "Mapped to " + juce::String::fromUTF8(model::sourceText(m->source).c_str()) + " (" +
                          juce::String::fromUTF8(m->source.device.c_str()) + ")";
        return tip.isEmpty() ? line : tip + "\n" + line;
    }
    return tip;
}
} // namespace fd::app
