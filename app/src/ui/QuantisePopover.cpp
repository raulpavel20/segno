#include "ui/QuantisePopover.h"

namespace fd::app
{
namespace
{
juce::String percent(double v)
{
    return juce::String(juce::roundToInt(v * 100.0)) + " %";
}

std::optional<double> fromPercent(const juce::String& text)
{
    const auto t = text.trim().trimCharactersAtEnd("%").trim();
    if (t.isEmpty() || !t.containsOnly("0123456789.-"))
        return std::nullopt;
    return t.getDoubleValue() / 100.0;
}
} // namespace

QuantisePopover::QuantisePopover(PianoRoll& roll) : roll_(roll), outside_(*this, [this] { close(); })
{
    for (auto* field : {&strength_, &swing_})
    {
        field->setRange(0.0, 1.0, 0.01);
        field->textFromValue = percent;
        field->valueFromText = fromPercent;
        addAndMakeVisible(*field);
    }
    strength_.setTooltip("How far each note moves to its grid line: all the way at 100 %");
    swing_.setTooltip("Every other grid line later, by up to a third of a step: a triplet feel at 100 %");
    strength_.onValueChange = [this](double v)
    {
        settings_.strength = v;
        preview();
    };
    swing_.onValueChange = [this](double v)
    {
        settings_.swing = v;
        preview();
    };
    moves_.onChange = [this](int index)
    {
        settings_.starts = index != 1;
        settings_.ends = index != 0;
        preview();
    };
    addAndMakeVisible(moves_);
    apply_.setClickingTogglesState(false);
    apply_.setTooltip("Keep it (Return); Escape puts the notes back");
    apply_.onClick = [this] { apply(); };
    addAndMakeVisible(apply_);
    setVisible(false);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

QuantisePopover::~QuantisePopover()
{
    outside_.detach();
}

void QuantisePopover::setSettings(const model::QuantiseSettings& settings)
{
    settings_ = settings;
    strength_.setValue(settings.strength, juce::dontSendNotification);
    swing_.setValue(settings.swing, juce::dontSendNotification);
    moves_.setSelected(settings.starts && settings.ends ? 2 : settings.ends ? 1 : 0);
    if (isVisible())
        preview();
}

void QuantisePopover::open(juce::Component& parent, juce::Rectangle<int> anchor)
{
    parent_ = &parent;
    anchor_ = anchor;
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    setSettings(roll_.options().quantise);
    place();
    setVisible(true);
    toFront(true);
    grabKeyboardFocus();
    outside_.attach();
    preview();
}

void QuantisePopover::preview()
{
    roll_.previewEdit(
        model::quantiseNotes(roll_.notes(), roll_.selection(), roll_.quantiseGrid(), settings_));
}

void QuantisePopover::apply()
{
    if (!isVisible())
        return;
    roll_.setQuantiseSettings(settings_);
    roll_.applyEdit(model::quantiseNotes(roll_.notes(), roll_.selection(), roll_.quantiseGrid(), settings_));
    outside_.detach();
    setVisible(false);
}

void QuantisePopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    roll_.previewEdit(std::nullopt);
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

bool QuantisePopover::keyPressed(const juce::KeyPress& key, juce::Component*)
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

juce::Rectangle<int> QuantisePopover::inner() const
{
    return getLocalBounds().reduced(juce::roundToInt(ui::Theme::current().shell.shadowRadius));
}

int QuantisePopover::preferredWidth() const
{
    const auto& s = ui::Theme::current().shell;
    const int field = strength_.widthFor("100 %");
    int content = s.propertyLabelWidth + std::max(field, moves_.preferredWidth());
    content = std::max(
        content,
        static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), "QUANTISE"))) +
            s.groupGap + ui::Toggle::widthFor("Apply"));
    return content + 2 * s.padding;
}

int QuantisePopover::preferredHeight() const
{
    const auto& s = ui::Theme::current().shell;
    return 2 * s.padding + s.rowHeight + s.itemGap + 3 * s.rowHeight + 2 * s.itemGap;
}

void QuantisePopover::place()
{
    if (parent_ == nullptr)
        return;
    const auto& s = ui::Theme::current().shell;
    const int margin = juce::roundToInt(s.shadowRadius);
    const int width = preferredWidth();
    const auto area = parent_->getLocalBounds().reduced(s.itemGap);
    const int x =
        std::clamp(anchor_.getRight() - width, area.getX(), std::max(area.getX(), area.getRight() - width));
    setBounds(
        juce::Rectangle<int>(x, anchor_.getBottom() + s.itemGap, width, preferredHeight()).expanded(margin));
}

void QuantisePopover::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = inner().reduced(s.padding);
    auto title = area.removeFromTop(s.rowHeight);
    apply_.setBounds(title.removeFromRight(ui::Toggle::widthFor(apply_.getButtonText())));
    area.removeFromTop(s.itemGap);
    const int field = strength_.widthFor("100 %");
    strength_.setBounds(
        area.removeFromTop(s.rowHeight).withTrimmedLeft(s.propertyLabelWidth).withWidth(field));
    area.removeFromTop(s.itemGap);
    swing_.setBounds(area.removeFromTop(s.rowHeight).withTrimmedLeft(s.propertyLabelWidth).withWidth(field));
    area.removeFromTop(s.itemGap);
    moves_.setBounds(area.removeFromTop(s.rowHeight)
                         .withTrimmedLeft(s.propertyLabelWidth)
                         .withWidth(moves_.preferredWidth()));
}

void QuantisePopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    ui::paintPopover(g, inner());
    auto area = inner().reduced(s.padding);
    ui::paintCapsLabel(g, "Quantise", area.removeFromTop(s.rowHeight).toFloat());
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textSecondary);
    for (const auto& [label, c] : {std::pair<const char*, juce::Component*>{"Strength", &strength_},
                                   {"Swing", &swing_},
                                   {"Moves", &moves_}})
        g.drawText(label, c->getBounds().withX(area.getX()).withWidth(s.propertyLabelWidth),
                   juce::Justification::centredLeft, false);
}
} // namespace fd::app
