#include "ui/ToolPopover.h"

#include "model/MusicalTime.h"

#include <cmath>

namespace fd::app
{
namespace
{
juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

/// A number typed: as it's shown ("40 ms", "−8 per note", "60 %"), or a note's name for a pitch.
std::optional<double> numberFromText(const model::ToolSetting& s, const juce::String& text)
{
    auto t = text.trim().replace(juce::String::fromUTF8("\xE2\x88\x92"), "-");
    if (s.unit == "pitch")
    {
        if (t.containsOnly("0123456789") && t.isNotEmpty())
            return t.getDoubleValue();
        // "C3", "F#4", "B♭2": the letter and its accidental, then the octave (C3 = 60).
        int split = t.length();
        while (split > 0 && (juce::CharacterFunctions::isDigit(t[split - 1]) || t[split - 1] == '-'))
            --split;
        if (split == 0 || split == t.length())
            return std::nullopt;
        const auto pc = music::parsePitchClass(t.substring(0, split).toStdString());
        if (!pc)
            return std::nullopt;
        return static_cast<double>(*pc + (t.substring(split).getIntValue() + 2) * 12);
    }
    t = t.upToFirstOccurrenceOf(" ", false, false).trimCharactersAtEnd("%");
    if (t.isEmpty() || !t.containsOnly("0123456789.-+"))
        return std::nullopt;
    return t.getDoubleValue();
}
} // namespace

ToolPopover::ToolPopover(PianoRoll& roll, engine::Session& session)
    : roll_(roll), session_(session), outside_(*this, [this] { close(); })
{
    apply_.setClickingTogglesState(false);
    apply_.setTooltip("Keep it (Return); Escape puts the notes back");
    apply_.onClick = [this] { apply(); };
    addAndMakeVisible(apply_);
    setVisible(false);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

ToolPopover::~ToolPopover()
{
    outside_.detach();
}

void ToolPopover::open(juce::Component& parent, juce::Rectangle<int> notes, const model::ToolSpec& tool)
{
    if (isVisible())
        close(); // another tool: its notes back first
    parent_ = &parent;
    notes_ = notes;
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    tool_ = &tool;
    values_ = roll_.toolValues(tool);
    build();
    place();
    setVisible(true);
    toFront(true);
    grabKeyboardFocus(); // Return applies, Escape closes
    outside_.attach();
    preview();
}

void ToolPopover::build()
{
    for (auto& row : rows_)
        removeChildComponent(row.control.get());
    rows_.clear();
    const auto& theme = ui::Theme::current();
    for (const auto& s : tool_->settings)
    {
        Row row{.setting = &s};
        const auto id = std::string(s.id);
        const double value = values_.at(id);
        const auto index = static_cast<int>(std::lround(value));
        if (s.isChoice() && !s.menu)
        {
            juce::StringArray names;
            for (auto c : s.choices)
                names.add(utf8(std::string(c)));
            auto segments = std::make_unique<ui::Segments>(names);
            segments->setSelected(index);
            segments->onChange = [this, id](int chosen) { setValue(id, chosen); };
            row.control = std::move(segments);
        }
        else if (s.isChoice())
        {
            auto field = std::make_unique<ui::ChoiceField>();
            field->setFontSize(theme.textXs);
            field->setValue(utf8(model::settingText(s, value, nullptr)));
            auto* raw = field.get();
            field->onOpen = [this, raw, &s, id]
            {
                juce::PopupMenu menu;
                const auto now = static_cast<int>(std::lround(values_.at(id)));
                for (std::size_t c = 0; c < s.choices.size(); ++c)
                    menu.addItem(juce::PopupMenu::Item(utf8(std::string(s.choices[c])))
                                     .setTicked(static_cast<int>(c) == now)
                                     .setAction(
                                         [this, raw, &s, id, c]
                                         {
                                             raw->setValue(utf8(std::string(s.choices[c])));
                                             setValue(id, static_cast<double>(c));
                                         }));
                raw->setOpen(true);
                const juce::Component::SafePointer<ui::ChoiceField> safe(raw);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(raw),
                                   [safe](int)
                                   {
                                       if (safe != nullptr)
                                           safe->setOpen(false);
                                   });
            };
            row.control = std::move(field);
        }
        else
        {
            auto field = std::make_unique<ui::NumberField>();
            field->setFontSize(theme.textXs);
            field->setRange(s.min, s.max, s.step);
            field->setDragSensitivity(std::max(s.step, (s.max - s.min) / 200.0));
            const auto* key = roll_.key() ? &*roll_.key() : nullptr;
            field->textFromValue = [&s, key](double v) { return utf8(model::settingText(s, v, key)); };
            field->valueFromText = [&s](const juce::String& text) { return numberFromText(s, text); };
            field->setValue(value, juce::dontSendNotification);
            field->onValueChange = [this, id](double v) { setValue(id, v); };
            row.control = std::move(field);
        }
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(row.control.get()))
            tip->setTooltip(utf8(std::string(s.label)));
        addChildComponent(*row.control);
        rows_.push_back(std::move(row));
    }
    for (auto& row : rows_)
    {
        row.shown = model::settingShown(*tool_, *row.setting, values_);
        row.control->setVisible(row.shown);
    }
}

void ToolPopover::setValue(std::string_view setting, double value)
{
    if (tool_ == nullptr)
        return;
    values_[std::string(setting)] = value;
    values_ = model::toolValues(*tool_, values_);
    // A choice others show for (the Velocity tool's shape): the rows that show change.
    bool changed = false;
    for (auto& row : rows_)
        if (const bool shown = model::settingShown(*tool_, *row.setting, values_); shown != row.shown)
        {
            row.shown = shown;
            row.control->setVisible(shown);
            changed = true;
        }
    if (changed)
        place();
    preview();
}

juce::Component* ToolPopover::control(std::string_view setting) const
{
    for (const auto& row : rows_)
        if (row.setting->id == setting && row.shown)
            return row.control.get();
    return nullptr;
}

model::ToolRun ToolPopover::run() const
{
    return model::runNoteTool(*tool_, roll_.notes(), roll_.selection(), values_, roll_.toolContext());
}

void ToolPopover::preview()
{
    if (tool_ == nullptr)
        return;
    const auto r = run();
    roll_.previewEdit(r.why.empty() ? std::optional(r.edit) : std::nullopt);
}

void ToolPopover::apply()
{
    if (!isVisible() || tool_ == nullptr)
        return;
    const auto r = run();
    roll_.setToolValues(*tool_, values_);
    if (r.why.empty())
    {
        const engine::Session::EditGroup group(session_, std::string(tool_->name)); // the undo step's name
        roll_.applyEdit(r.edit);
    }
    else
        roll_.previewEdit(std::nullopt);
    outside_.detach();
    setVisible(false);
}

void ToolPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    roll_.previewEdit(std::nullopt);
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

bool ToolPopover::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        apply();
        return true;
    }
    return false;
}

juce::String ToolPopover::title() const
{
    if (tool_ == nullptr)
        return {};
    const auto dot = juce::String::fromUTF8(" · ");
    juce::String scope;
    if (tool_->kind == model::ToolKind::Transform)
    {
        const auto n =
            static_cast<int>(roll_.hasSelection() ? roll_.selection().size() : roll_.notes().size());
        scope = juce::String(n) + (n == 1 ? " note" : " notes");
    }
    else
    {
        const auto span = roll_.toolContext().span;
        scope = juce::String(model::lengthText(span.second - span.first, roll_.tempo()));
    }
    return utf8(std::string(tool_->name)) + dot + scope;
}

// ---- layout --------------------------------------------------------------------------------------

juce::Rectangle<int> ToolPopover::inner() const
{
    return getLocalBounds().reduced(juce::roundToInt(ui::Theme::current().shell.shadowRadius));
}

int ToolPopover::controlWidth(const Row& row) const
{
    const auto& s = *row.setting;
    if (auto* segments = dynamic_cast<ui::Segments*>(row.control.get()))
        return segments->preferredWidth();
    if (auto* field = dynamic_cast<ui::ChoiceField*>(row.control.get()))
    {
        int widest = 0;
        for (auto c : s.choices)
            widest = std::max(widest, field->widthFor(utf8(std::string(c))));
        return widest;
    }
    auto* field = dynamic_cast<ui::NumberField*>(row.control.get());
    int widest = 0;
    for (double v : {s.min, s.max, s.initial})
        widest = std::max(widest, field->widthFor(field->textFromValue(v)));
    return widest;
}

int ToolPopover::preferredWidth() const
{
    const auto& s = ui::Theme::current().shell;
    int content = 0;
    for (const auto& row : rows_)
        content = std::max(content, s.propertyLabelWidth + controlWidth(row));
    const auto titleWidth = static_cast<int>(
        std::ceil(juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), title().toUpperCase())));
    content = std::max(content, titleWidth + s.groupGap + ui::Toggle::widthFor(apply_.getButtonText()));
    return content + 2 * s.padding;
}

int ToolPopover::preferredHeight() const
{
    const auto& s = ui::Theme::current().shell;
    int rows = 0;
    for (const auto& row : rows_)
        rows += row.shown ? 1 : 0;
    return 2 * s.padding + s.rowHeight + rows * (s.itemGap + s.rowHeight);
}

void ToolPopover::place()
{
    if (parent_ == nullptr)
        return;
    const auto& s = ui::Theme::current().shell;
    const int margin = juce::roundToInt(s.shadowRadius);
    const int width = preferredWidth(), height = preferredHeight();
    const auto area = parent_->getLocalBounds().reduced(s.itemGap);
    // At the top right of the notes, inside the panel.
    const int x = std::clamp(notes_.getRight() - s.itemGap - width, area.getX(),
                             std::max(area.getX(), area.getRight() - width));
    const int y =
        std::clamp(notes_.getY() + s.itemGap, area.getY(), std::max(area.getY(), area.getBottom() - height));
    setBounds(juce::Rectangle<int>(x, y, width, height).expanded(margin));
    resized();
}

void ToolPopover::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = inner().reduced(s.padding);
    auto titleRow = area.removeFromTop(s.rowHeight);
    apply_.setBounds(titleRow.removeFromRight(ui::Toggle::widthFor(apply_.getButtonText())));
    for (auto& row : rows_)
    {
        if (!row.shown)
            continue;
        area.removeFromTop(s.itemGap);
        auto line = area.removeFromTop(s.rowHeight).withTrimmedLeft(s.propertyLabelWidth);
        // A field's text lines up with the labels' column, as the inspector's do.
        if (dynamic_cast<ui::NumberField*>(row.control.get()) != nullptr)
            line = line.withLeft(line.getX() - ui::NumberField::textInset);
        row.control->setBounds(line.withWidth(controlWidth(row)));
    }
}

void ToolPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    ui::paintPopover(g, inner());
    auto area = inner().reduced(s.padding);
    ui::paintCapsLabel(g, title(), area.removeFromTop(s.rowHeight).toFloat());
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textSecondary);
    for (const auto& row : rows_)
        if (row.shown)
            g.drawText(utf8(std::string(row.setting->label)),
                       row.control->getBounds().withX(area.getX()).withWidth(s.propertyLabelWidth),
                       juce::Justification::centredLeft, false);
}
} // namespace fd::app
