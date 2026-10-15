#include "ui/ToolForm.h"

#include <cmath>
#include <map>

namespace fd::app
{
namespace
{
juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

juce::String utf8(std::string_view s)
{
    return utf8(std::string(s));
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

/// A number with ×2 and ÷2 beside it, an item gap apart (ToolSetting::halveDouble).
class ToolForm::HalveDouble final : public juce::Component
{
public:
    HalveDouble()
    {
        addAndMakeVisible(field);
        for (auto* b : {&twice, &half})
        {
            b->setClickingTogglesState(false);
            b->setWantsKeyboardFocus(false);
            addAndMakeVisible(*b);
        }
        twice.setTooltip("Twice the value: it was found at half speed");
        half.setTooltip("Half the value: it was found at double speed");
    }
    [[nodiscard]] int buttonsWidth() const
    {
        const int gap = ui::Theme::current().shell.itemGap;
        return gap + ui::Toggle::widthFor(twice.getButtonText()) + gap +
               ui::Toggle::widthFor(half.getButtonText());
    }
    void resized() override
    {
        const auto& s = ui::Theme::current().shell;
        auto r = getLocalBounds();
        half.setBounds(
            r.removeFromRight(ui::Toggle::widthFor(half.getButtonText())).reduced(0, s.propertyFieldInset));
        r.removeFromRight(s.itemGap);
        twice.setBounds(
            r.removeFromRight(ui::Toggle::widthFor(twice.getButtonText())).reduced(0, s.propertyFieldInset));
        r.removeFromRight(s.itemGap);
        field.setBounds(r);
    }
    ui::NumberField field;
    ui::Toggle twice{juce::String::fromUTF8("\xC3\x97"
                                            "2")},
        half{juce::String::fromUTF8("\xC3\xB7"
                                    "2")};
};

/// The rows: their labels painted beside their controls.
class ToolForm::Rows final : public juce::Component
{
public:
    explicit Rows(const ToolForm& form) : form_(form) {}
    void paint(juce::Graphics& g) override { form_.paintLabels(g); }

private:
    const ToolForm& form_;
};

ToolForm::ToolForm(ToolHost& host) : host_(&host), holder_(std::make_unique<Rows>(*this))
{
    viewport_.setViewedComponent(holder_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    apply_.setClickingTogglesState(false);
    apply_.setTooltip("Keep it (Return); Escape puts " + host_->what() + " back");
    apply_.onClick = [this]
    {
        if (onApply)
            onApply();
        else
            apply();
    };
    addAndMakeVisible(apply_);
    const auto size = ui::Theme::current().textXs;
    measureNumber_.setFontSize(size);
    measureChoice_.setFontSize(size);
}

ToolForm::~ToolForm() = default;

void ToolForm::setHost(ToolHost& host)
{
    if (&host == host_)
        return;
    if (tool_ != nullptr)
        setTool(nullptr);
    host_ = &host;
    apply_.setTooltip("Keep it (Return); Escape puts " + host_->what() + " back");
}

void ToolForm::setTool(const model::ToolSpec* tool)
{
    tool_ = tool;
    values_ = tool != nullptr ? host_->toolValues(*tool) : model::ToolValues{};
    build();
    resized();
    if (tool != nullptr)
        preview();
    else
        host_->cancel();
}

void ToolForm::build()
{
    for (auto& row : rows_)
        holder_->removeChildComponent(row.control.get());
    rows_.clear();
    viewport_.setViewPosition(0, 0);
    if (tool_ == nullptr)
        return;
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
                names.add(utf8(c));
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
                    menu.addItem(juce::PopupMenu::Item(utf8(s.choices[c]))
                                     .setTicked(static_cast<int>(c) == now)
                                     .setAction(
                                         [this, raw, &s, id, c]
                                         {
                                             raw->setValue(utf8(s.choices[c]));
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
            const auto setUp = [&](ui::NumberField& field)
            {
                field.setFontSize(theme.textXs);
                field.setRange(s.min, s.max, s.step);
                field.setDragSensitivity(std::max(s.step, (s.max - s.min) / 200.0));
                const auto* key = host_->key();
                field.textFromValue = [&s, key](double v) { return utf8(model::settingText(s, v, key)); };
                field.valueFromText = [&s](const juce::String& text) { return numberFromText(s, text); };
                field.setValue(value, juce::dontSendNotification);
                field.onValueChange = [this, id](double v) { setValue(id, v); };
            };
            if (s.halveDouble)
            {
                auto pair = std::make_unique<HalveDouble>();
                setUp(pair->field);
                auto* raw = pair.get();
                for (auto [button, factor] : {std::pair{&raw->twice, 2.0}, std::pair{&raw->half, 0.5}})
                    button->onClick = [this, &s, id, times = factor]
                    { show(id, std::clamp(values_.at(id) * times, s.min, s.max)); };
                row.control = std::move(pair);
            }
            else
            {
                auto field = std::make_unique<ui::NumberField>();
                setUp(*field);
                row.control = std::move(field);
            }
        }
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(row.control.get()))
            tip->setTooltip(utf8(s.label));
        holder_->addChildComponent(*row.control);
        rows_.push_back(std::move(row));
    }
    for (auto& row : rows_)
    {
        row.shown = model::settingShown(*tool_, *row.setting, values_);
        row.control->setVisible(row.shown);
    }
}

void ToolForm::setValue(std::string_view setting, double value)
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
    {
        resized();
        if (onLayoutChanged)
            onLayoutChanged();
    }
    preview();
}

void ToolForm::show(std::string_view setting, double value)
{
    for (auto& row : rows_)
        if (row.setting->id == setting)
        {
            auto* field = dynamic_cast<ui::NumberField*>(row.control.get());
            if (auto* pair = dynamic_cast<HalveDouble*>(row.control.get()))
                field = &pair->field;
            if (field != nullptr)
                field->setValue(value, juce::dontSendNotification);
        }
    setValue(setting, value);
}

juce::Component* ToolForm::control(std::string_view setting) const
{
    for (const auto& row : rows_)
        if (row.setting->id == setting && row.shown)
            return row.control.get();
    return nullptr;
}

void ToolForm::preview()
{
    if (tool_ == nullptr)
        return;
    host_->preview(*tool_, values_);
    repaint(); // its title counts what it acts on
}

void ToolForm::apply()
{
    if (tool_ == nullptr)
        return;
    host_->setToolValues(*tool_, values_);
    host_->apply(*tool_, values_);
}

void ToolForm::cancel()
{
    host_->cancel();
}

juce::String ToolForm::title() const
{
    if (tool_ == nullptr)
        return {};
    return utf8(tool_->name) + juce::String::fromUTF8(" · ") + host_->scope(*tool_);
}

// ---- layout --------------------------------------------------------------------------------------

int ToolForm::controlWidth(const model::ToolSetting& s) const
{
    if (s.isChoice() && !s.menu)
    {
        int width = 0;
        for (auto c : s.choices)
            width += ui::Toggle::widthFor(utf8(c));
        return width;
    }
    if (s.isChoice())
    {
        int widest = 0;
        for (auto c : s.choices)
            widest = std::max(widest, measureChoice_.widthFor(utf8(c)));
        return widest;
    }
    int widest = 0;
    const auto* key = host_->key();
    for (double v : {s.min, s.max, s.initial})
        widest = std::max(widest, measureNumber_.widthFor(utf8(model::settingText(s, v, key))));
    if (s.halveDouble)
        widest += HalveDouble().buttonsWidth();
    return widest;
}

int ToolForm::preferredWidth(const model::ToolSpec& tool) const
{
    const auto& s = ui::Theme::current().shell;
    int content = 0;
    for (const auto& setting : tool.settings)
        content = std::max(content, s.propertyLabelWidth + controlWidth(setting));
    // The title at its longest ("RANDOMISE PITCH · 9999 NOTES") and Apply.
    const auto title = (utf8(tool.name) + juce::String::fromUTF8(" · ") + host_->widestScope()).toUpperCase();
    const auto titleWidth =
        static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), title)));
    return std::max(content, titleWidth + s.groupGap + ui::Toggle::widthFor(apply_.getButtonText()));
}

int ToolForm::heightFor(int rows)
{
    const auto& s = ui::Theme::current().shell;
    return s.rowHeight + rows * (s.itemGap + s.rowHeight);
}

int ToolForm::preferredHeight() const
{
    int rows = 0;
    for (const auto& row : rows_)
        rows += row.shown ? 1 : 0;
    return heightFor(rows);
}

int ToolForm::preferredHeight(const model::ToolSpec& tool)
{
    // The most rows any of its choices shows (the Velocity tool's shapes), so it keeps its height.
    int rows = 0;
    std::map<std::pair<std::string_view, double>, int> shownFor; // the rows a choice shows
    for (const auto& s : tool.settings)
        if (s.when.empty())
            ++rows;
        else
            ++shownFor[{s.when, s.whenIs}];
    std::map<std::string_view, int> most;
    for (const auto& [choice, count] : shownFor)
        most[choice.first] = std::max(most[choice.first], count);
    for (const auto& [setting, count] : most)
        rows += count;
    return heightFor(rows);
}

bool ToolForm::scrolls() const
{
    return holder_->getHeight() > viewport_.getHeight();
}

void ToolForm::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = getLocalBounds();
    auto titleRow = area.removeFromTop(s.rowHeight);
    apply_.setBounds(titleRow.removeFromRight(ui::Toggle::widthFor(apply_.getButtonText())));
    apply_.setVisible(tool_ != nullptr);
    // The rows under the title, scrolling when they don't fit.
    viewport_.setBounds(area);
    const int rowsHeight = preferredHeight() - s.rowHeight;
    const int width =
        area.getWidth() - (rowsHeight > area.getHeight() ? viewport_.getScrollBarThickness() + s.itemGap : 0);
    holder_->setSize(width, rowsHeight);
    int y = 0;
    for (auto& row : rows_)
    {
        if (!row.shown)
            continue;
        y += s.itemGap;
        auto line = juce::Rectangle<int>(s.propertyLabelWidth, y, width - s.propertyLabelWidth, s.rowHeight);
        y += s.rowHeight;
        // A field's text lines up with the labels' column, as the inspector's do.
        // A number's or a menu's text lines up with the other rows' values (a number with ×2 and ÷2
        // too): each draws its text a field's inset in.
        if (dynamic_cast<ui::NumberField*>(row.control.get()) != nullptr ||
            dynamic_cast<ui::ChoiceField*>(row.control.get()) != nullptr ||
            dynamic_cast<HalveDouble*>(row.control.get()) != nullptr)
            line = line.withLeft(line.getX() - ui::NumberField::textInset);
        row.control->setBounds(line.withWidth(controlWidth(*row.setting)));
    }
}

void ToolForm::paint(juce::Graphics& g)
{
    const auto& s = ui::Theme::current().shell;
    if (tool_ != nullptr)
        ui::paintCapsLabel(g, title(), getLocalBounds().removeFromTop(s.rowHeight).toFloat());
}

void ToolForm::paintLabels(juce::Graphics& g) const
{
    const auto& t = ui::Theme::current();
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textSecondary);
    for (const auto& row : rows_)
        if (row.shown)
            g.drawText(utf8(row.setting->label),
                       row.control->getBounds().withX(0).withWidth(t.shell.propertyLabelWidth),
                       juce::Justification::centredLeft, false);
}
} // namespace fd::app
