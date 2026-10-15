#include "ui/MidiMappingList.h"

namespace fd::app
{
namespace
{
const juce::String kMin = "Min", kMax = "Max", kMode = "Mode";

juce::String text(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

/// The module a target's parameter is on ("m<id>/<param>"), else 0.
std::uint64_t moduleOf(const model::MidiTarget& target)
{
    if (target.key.size() < 2 || target.key[0] != 'm')
        return 0;
    const auto slash = target.key.find('/');
    if (slash == std::string::npos)
        return 0;
    return std::strtoull(target.key.substr(1, slash - 1).c_str(), nullptr, 10);
}
} // namespace

MidiMappingList::MidiMappingList(engine::Session& session) : session_(session) {}

MidiMappingList::~MidiMappingList() = default;

int MidiMappingList::rowHeight()
{
    // The message and Show, the target, Min and Max, the mode: four lines a mapping.
    const auto& s = ui::Theme::current().shell;
    return 4 * s.rowHeight + 3 * s.itemGap;
}

int MidiMappingList::preferredHeight(int width) const
{
    const auto& s = ui::Theme::current().shell;
    const int count = static_cast<int>(rows_.size());
    const int content = count == 0 ? 3 * s.rowHeight : count * rowHeight() + (count - 1) * s.groupGap;
    ui::SectionStack measure;
    return measure.layout(area(width), {{"MIDI mappings", content}}).getY();
}

juce::Rectangle<int> MidiMappingList::area(int width)
{
    // As the inspector's sections sit: in from its sides, a gap under its header.
    const auto& s = ui::Theme::current().shell;
    return juce::Rectangle<int>(0, s.itemGap, width, 1 << 20).reduced(s.padding, 0);
}

void MidiMappingList::update()
{
    std::vector<model::MidiMapping> now;
    for (const auto& m : session_.midiMappings())
        if (!m.target.isTransport()) // the app's: Settings > Controllers
            now.push_back(m);
    if (now == shown_)
        return;
    // The same mappings: their values change in place (a field may be being dragged).
    const bool same =
        std::equal(now.begin(), now.end(), shown_.begin(), shown_.end(),
                   [](const auto& a, const auto& b) { return a.source == b.source && a.target == b.target; });
    shown_ = std::move(now);
    if (!same)
    {
        rebuild();
        return;
    }
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        auto& row = *rows_[i];
        row.mapping = shown_[i];
        if (gesture_ == nullptr)
        {
            row.min->setValue(row.mapping.min, juce::dontSendNotification);
            row.max->setValue(row.mapping.max, juce::dontSendNotification);
        }
        row.mode->setValue(text(model::modeName(row.mapping.mode)));
    }
    repaint();
}

void MidiMappingList::rebuild()
{
    rows_.clear();
    for (const auto& m : shown_)
    {
        auto row = std::make_unique<Row>();
        row->mapping = m;
        const auto target = m.target;
        row->show = std::make_unique<ui::IconButton>(ui::Icon::ChevronRight, "Show");
        row->show->setTooltip("Show " + text(session_.midiTargetName(target)));
        row->show->onClick = [this, target]
        {
            if (onShow)
                onShow(target.channel, moduleOf(target));
        };
        row->remove = std::make_unique<ui::IconButton>(ui::Icon::Close, "Remove");
        row->remove->setTooltip("Remove this mapping");
        row->remove->onClick = [this, target]
        {
            // Rows go once this click returns: after it.
            juce::MessageManager::callAsync(
                [this, safe = juce::Component::SafePointer(this), target]
                {
                    if (safe != nullptr)
                    {
                        session_.removeMidiMapping(target);
                        session_.flushSnapshot();
                        update();
                    }
                });
        };
        // Min and Max in the control's own units, dragged or typed.
        for (const bool isMax : {false, true})
        {
            auto field = std::make_unique<ui::NumberField>();
            field->setRange(0.0, 1.0, 0.0);
            field->setDragSensitivity(0.005);
            field->setFontSize(ui::Theme::current().textXs);
            field->textFromValue = [this, target](double v)
            { return text(session_.midiValueText(target, static_cast<float>(v))); };
            field->valueFromText = [this, target](const juce::String& t) -> std::optional<double>
            {
                if (const auto v = session_.midiValueFromText(target, t.toStdString()))
                    return *v;
                return std::nullopt;
            };
            field->setTooltip(isMax ? "Where the hardware's top takes it (below Min: the other way)"
                                    : "Where the hardware's bottom takes it");
            field->onGestureStart = [this]
            { gesture_ = std::make_unique<engine::Session::EditGroup>(session_, "MIDI mapping"); };
            field->onGestureEnd = [this] { gesture_.reset(); };
            field->onValueChange = [this, target, isMax](double v)
            {
                if (const auto now = session_.midiMappingOf(target))
                    session_.setMidiMapping(target, isMax ? now->min : static_cast<float>(v),
                                            isMax ? static_cast<float>(v) : now->max, now->mode);
            };
            field->setValue(isMax ? m.max : m.min, juce::dontSendNotification);
            (isMax ? row->max : row->min) = std::move(field);
        }
        row->mode = std::make_unique<ui::ChoiceField>();
        row->mode->setFontSize(ui::Theme::current().textXs); // as Min and Max beside it
        row->mode->setValue(text(model::modeName(m.mode)));
        row->mode->setTooltip("How the hardware moves it: sets it, flips it, holds it, or adds steps");
        row->mode->onOpen = [this, target, kind = m.source.kind, field = row->mode.get()]
        {
            juce::PopupMenu menu;
            const auto now = session_.midiMappingOf(target);
            for (const auto mode : model::modesFor(kind))
                menu.addItem(text(model::modeName(mode)), true, now && now->mode == mode,
                             [this, target, mode]
                             {
                                 if (const auto current = session_.midiMappingOf(target))
                                     session_.setMidiMapping(target, current->min, current->max, mode);
                             });
            field->setOpen(true);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(field),
                               [safe = juce::Component::SafePointer(field)](int)
                               {
                                   if (safe != nullptr)
                                       safe->setOpen(false);
                               });
        };
        for (juce::Component* c :
             {static_cast<juce::Component*>(row->show.get()),
              static_cast<juce::Component*>(row->remove.get()), static_cast<juce::Component*>(row->min.get()),
              static_cast<juce::Component*>(row->max.get()), static_cast<juce::Component*>(row->mode.get())})
            addAndMakeVisible(c);
        rows_.push_back(std::move(row));
    }
    resized();
    repaint();
}

void MidiMappingList::resized()
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    const int count = static_cast<int>(rows_.size());
    const int content = count == 0 ? 3 * s.rowHeight : count * rowHeight() + (count - 1) * s.groupGap;
    stack_.layout(area(getWidth()),
                  {{juce::String::fromUTF8("MIDI mappings \xc2\xb7 ") + juce::String(count), content}});
    auto area = stack_.content(0);
    const auto caption = [&](const juce::String& c)
    {
        return static_cast<int>(
            std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Caption), c)));
    };
    for (auto& row : rows_)
    {
        auto r = area.removeFromTop(rowHeight());
        area.removeFromTop(s.groupGap);
        // The message and input, Show and Remove at the right.
        auto line = r.removeFromTop(s.rowHeight);
        row->remove->setBounds(line.removeFromRight(s.rowHeight));
        line.removeFromRight(s.itemGap);
        row->show->setBounds(line.removeFromRight(s.rowHeight));
        line.removeFromRight(s.itemGap);
        row->source = line;
        r.removeFromTop(s.itemGap);
        row->target = r.removeFromTop(s.rowHeight);
        r.removeFromTop(s.itemGap);
        // Min and Max side by side, each its caption then its field.
        line = r.removeFromTop(s.rowHeight);
        const int half = (line.getWidth() - s.groupGap) / 2;
        auto left = line.removeFromLeft(half), right = line.withTrimmedLeft(s.groupGap);
        row->minCaption = left.removeFromLeft(caption(kMin));
        left.removeFromLeft(s.itemGap);
        row->min->setBounds(left);
        row->maxCaption = right.removeFromLeft(caption(kMax));
        right.removeFromLeft(s.itemGap);
        row->max->setBounds(right);
        r.removeFromTop(s.itemGap);
        line = r.removeFromTop(s.rowHeight);
        row->modeCaption = line.removeFromLeft(std::max(caption(kMin), caption(kMode)));
        line.removeFromLeft(s.itemGap);
        row->mode->setBounds(
            line.withWidth(std::min(line.getWidth(), row->mode->widthFor("Relative (offset)"))));
    }
}

void MidiMappingList::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    stack_.paint(g);
    if (rows_.empty())
    {
        ui::paintEmptyState(g, stack_.content(0), "Click a control, then move one on your device");
        return;
    }
    for (const auto& row : rows_)
    {
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.setColour(t.textSecondary);
        g.drawText(text(model::sourceText(row->mapping.source) + " \xc2\xb7 " + row->mapping.source.device),
                   row->source, juce::Justification::centredLeft, true);
        g.drawText(kMin, row->minCaption, juce::Justification::centredLeft, false);
        g.drawText(kMax, row->maxCaption, juce::Justification::centredLeft, false);
        g.drawText(kMode, row->modeCaption, juce::Justification::centredLeft, false);
        g.setFont(ui::font(ui::TextStyle::Body));
        g.setColour(t.textPrimary);
        g.drawText(text(session_.midiTargetName(row->mapping.target)), row->target,
                   juce::Justification::centredLeft, true);
    }
}
} // namespace fd::app
