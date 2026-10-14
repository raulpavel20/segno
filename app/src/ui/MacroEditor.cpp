#include "ui/MacroEditor.h"

#include "model/Racks.h"

namespace fd::app
{
namespace
{
constexpr int kMinimumRows = model::RackInfo::macroCount; // the macros' list

juce::String macroName(const model::RackInfo& rack, int macro)
{
    const auto& name = rack.macros[static_cast<size_t>(macro)].name;
    return name.empty() ? "Macro " + juce::String(macro + 1) : juce::String::fromUTF8(name.c_str());
}

/// The module a mapping's parameter is on, by its name in the rack.
juce::String moduleName(const model::RackInfo& rack, std::uint64_t module)
{
    for (const auto& chain : rack.chains)
        for (const auto& d : chain.devices)
            if (d.id == module)
                return juce::String::fromUTF8(d.name.c_str());
    return {};
}

/// A mapping's colour: its row's swatch and its line in CURVES. The palette's hues five apart, so
/// rows next to each other are told apart.
juce::Colour mappingColour(int index)
{
    return ui::Theme::current().clipColour(index * 5);
}

juce::String percentText(float v)
{
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

/// Curve: -100 ... +100 %, up "late" and down "early", for the bar.
juce::String curveText(float bar)
{
    const int v = juce::roundToInt((bar * 2.0f - 1.0f) * 100.0f);
    return v == 0 ? juce::String("0 %") : (v > 0 ? "+" : "") + juce::String(v) + " %";
}
} // namespace

/// A mapping's module and parameter, after its colour's swatch.
class MacroEditor::MappingName final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MappingName(juce::String module, juce::String param, juce::Colour colour)
        : module_(std::move(module)), param_(std::move(param)), colour_(colour)
    {
        setTooltip(module_ + juce::String::fromUTF8(" \xc2\xb7 ") + param_);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        auto r = getLocalBounds();
        const int swatch = t.shell.lightSize;
        g.setColour(colour_);
        g.fillEllipse(r.removeFromLeft(swatch).toFloat().withSizeKeepingCentre(static_cast<float>(swatch),
                                                                               static_cast<float>(swatch)));
        r.removeFromLeft(t.shell.itemGap * 2);
        // The parameter first: it's what moves; the module after it, fainter.
        g.setFont(ui::font(ui::TextStyle::Name));
        g.setColour(t.textPrimary);
        const int paramWidth = std::min(
            r.getWidth(),
            static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), param_))));
        g.drawText(param_, r.removeFromLeft(paramWidth), juce::Justification::centredLeft, true);
        r.removeFromLeft(t.shell.itemGap * 2);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.setColour(t.textTertiary);
        g.drawText(module_, r, juce::Justification::centredLeft, true);
    }

private:
    juce::String module_, param_;
    juce::Colour colour_;
};

/// Every mapping of the chosen macro across its travel (racks.md §6): the parameter's place
/// (normalised, up) against the macro's (along), a line each in its row's colour, through the
/// curve the engine plays; the macro's position as a line, a dot where it crosses each.
class MacroEditor::Curves final : public juce::Component
{
public:
    void show(const model::RackMacro& macro)
    {
        macro_ = macro;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(bounds, t.radiusControl);
        const auto area = bounds.reduced(static_cast<float>(t.deviceGrid.displayInset));
        g.setColour(t.roles.gridBeat);
        for (float f : {0.25f, 0.5f, 0.75f})
        {
            g.drawHorizontalLine(juce::roundToInt(area.getY() + area.getHeight() * f), area.getX(),
                                 area.getRight());
            g.drawVerticalLine(juce::roundToInt(area.getX() + area.getWidth() * f), area.getY(),
                               area.getBottom());
        }
        if (macro_.mappings.empty())
            return;
        const auto at = [&](float x, float y)
        {
            return juce::Point<float>(area.getX() + x * area.getWidth(),
                                      area.getBottom() - y * area.getHeight());
        };
        for (size_t i = 0; i < macro_.mappings.size(); ++i)
        {
            const auto& m = macro_.mappings[i];
            juce::Path p;
            constexpr int steps = 64;
            for (int s = 0; s <= steps; ++s)
            {
                const float x = static_cast<float>(s) / steps;
                const auto pt = at(x, model::macroMapped(x, m.from, m.to, m.curve));
                s == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
            }
            g.setColour(mappingColour(static_cast<int>(i)));
            g.strokePath(p,
                         juce::PathStrokeType(1.5f, juce::PathStrokeType::curved)); // as the displays' curves
        }
        // The macro where it is now, and where that puts each parameter.
        const float x = macro_.value;
        g.setColour(t.value);
        g.drawVerticalLine(juce::roundToInt(at(x, 0.0f).x), area.getY(), area.getBottom());
        for (size_t i = 0; i < macro_.mappings.size(); ++i)
        {
            const auto& m = macro_.mappings[i];
            const auto pt = at(x, model::macroMapped(x, m.from, m.to, m.curve));
            g.setColour(mappingColour(static_cast<int>(i)));
            const auto dot = static_cast<float>(t.shell.lightSize);
            g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre(pt));
        }
    }

private:
    model::RackMacro macro_;
};

MacroEditor::MacroEditor(ValueText valueText)
    : valueText_(std::move(valueText)), curves_(std::make_unique<Curves>())
{
    const auto& grid = ui::Theme::current().deviceGrid;
    setMinimumRows(kMinimumRows);
    // Left to right as an edit goes: which macro, what it moves, how.
    macrosPane_ = addPane("Macros", 3);
    mappingsPane_ = addPane("Mappings", 0, 10);
    curvesPane_ = addPane("Curves", 4);

    add_.setTooltip("Map the macro to another parameter in the rack (or drag its name onto a knob)");
    add_.setWantsKeyboardFocus(false);
    add_.onClick = [this]
    {
        if (onAddMapping)
            onAddMapping(macro_);
    };
    placeInTitle(mappingsPane_, add_, grid.titleHeight, true);

    place(curvesPane_, *curves_, 0, 0, 0, -1);
    value_.setStyle(ui::Knob::Style::Bar);
    value_.textFromValue = percentText;
    wireGesture(value_);
    value_.onValueChange = [this](float v)
    {
        if (onMacroValue)
            onMacroValue(macro_, v);
    };
    place(curvesPane_, value_, 0, -1, 0, 1, Kind::Line);
}

MacroEditor::~MacroEditor() = default;

void MacroEditor::wireGesture(ui::Knob& bar)
{
    bar.onGestureStart = [this]
    {
        dragging_ = true;
        if (onGesture)
            onGesture(true);
    };
    bar.onGestureEnd = [this]
    {
        if (onGesture)
            onGesture(false);
        // Its own row is built again only now: the bar being dragged stays while it is.
        dragging_ = false;
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<MacroEditor>(this)]
            {
                if (safe != nullptr)
                    safe->rebuild();
            });
    };
}

void MacroEditor::setRack(const juce::String& name, const model::RackInfo& rack)
{
    name_ = name;
    rack_ = rack;
    if (dragging_) // a bar being dragged: only the curves follow
        curves_->show(rack_.macros[static_cast<size_t>(macro_)]);
    else
        rebuild();
    if (onTitleChanged)
        onTitleChanged();
}

void MacroEditor::selectMacro(int macro)
{
    macro_ = std::clamp(macro, 0, model::RackInfo::macroCount - 1);
    rebuild();
}

void MacroEditor::resized()
{
    const int before = rows();
    ModuleEditor::resized();
    if (rows() != before)
        rebuild();
}

void MacroEditor::rebuild()
{
    const auto& t = ui::Theme::current();
    // The macros, to choose one; one that maps nothing yet recedes, as in the rack's panel.
    clearPane(macrosPane_);
    juce::StringArray names;
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
        names.add(macroName(rack_, i));
    macros_ = std::make_unique<ui::Selector>(names, 1);
    macros_->setSelected(macro_);
    macros_->onSelect = [this](int i) { selectMacro(i); };
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
    {
        const auto count = rack_.macros[static_cast<size_t>(i)].mappings.size();
        auto* b = macros_->buttons()[i];
        b->setTooltip(names[i] + ": " +
                      (count == 0 ? juce::String("maps nothing yet")
                                  : "maps " + juce::String(static_cast<int>(count)) +
                                        (count == 1 ? " parameter" : " parameters")));
        b->setAlpha(count == 0 ? t.roles.fadedAlpha : 1.0f);
    }
    addAndMakeVisible(*macros_);
    place(macrosPane_, *macros_, 0, 0, 0, model::RackInfo::macroCount, Kind::Line);

    // Its mappings, a row each.
    const auto& macro = rack_.macros[static_cast<size_t>(macro_)];
    clearPane(mappingsPane_);
    rows_.clear();
    const int count = static_cast<int>(macro.mappings.size());
    const int showing = std::max(1, rows());
    setPaneTitle(mappingsPane_, "Mappings" + juce::String::fromUTF8(" \xc2\xb7 ") + names[macro_] +
                                    (count > showing - 1 ? " (" + juce::String(showing - 1) + " of " +
                                                               juce::String(count) + ")"
                                                         : juce::String()));
    const auto make = [this](std::unique_ptr<juce::Component> c) -> juce::Component&
    {
        addAndMakeVisible(*c);
        return *rows_.emplace_back(std::move(c));
    };
    if (count == 0)
    {
        auto note = std::make_unique<ui::NoteText>();
        note->setText(
            names[macro_] +
            " maps nothing yet. Drag its name onto a knob in the rack, or + to choose a parameter.");
        place(mappingsPane_, make(std::move(note)), 0, 0, 0, 2);
    }
    // A table: the headings over the columns, a row a mapping under them.
    if (count > 0)
    {
        label(mappingsPane_, "Parameter", 0, 0, 3);
        label(mappingsPane_, "From", 3, 0, 2);
        label(mappingsPane_, "To", 5, 0, 2);
        label(mappingsPane_, "Curve", 7, 0, 2);
    }
    for (int i = 0; i < std::min(count, showing - 1); ++i)
    {
        const int row = i + 1;
        const auto& m = macro.mappings[static_cast<size_t>(i)];
        place(mappingsPane_,
              make(std::make_unique<MappingName>(moduleName(rack_, m.module),
                                                 juce::String::fromUTF8(m.paramName.c_str()),
                                                 mappingColour(i))),
              0, row, 3, 1, Kind::Line);
        const auto bar = [&](juce::String tooltip, float value, float reset,
                             std::function<juce::String(float)> text, int column,
                             std::function<void(float)> set)
        {
            auto knob = std::make_unique<ui::Knob>();
            knob->setStyle(ui::Knob::Style::Bar);
            knob->setTooltip(tooltip);
            knob->setDefaultValue(reset);
            knob->setArcOrigin(reset); // Curve fills from its middle, as it bends both ways
            knob->textFromValue = std::move(text);
            knob->setValue(value, juce::dontSendNotification);
            knob->onValueChange = std::move(set);
            wireGesture(*knob);
            place(mappingsPane_, make(std::move(knob)), column, row, 2, 1, Kind::Line);
        };
        const auto change = [this, i](float from, float to, float curve)
        {
            if (onMappingChange)
                onMappingChange(macro_, i, from, to, curve);
        };
        // A double-click: From to the parameter's least, To to its most, Curve straight.
        bar(
            "From: where the parameter is with the macro at 0 %", m.from, 0.0f, [this, m](float v)
            { return valueText_(m, v); }, 3, [change, m](float v) { change(v, m.to, m.curve); });
        bar(
            "To: where it is with the macro at 100 % (below From, it turns the other way)", m.to, 1.0f,
            [this, m](float v) { return valueText_(m, v); }, 5,
            [change, m](float v) { change(m.from, v, m.curve); });
        bar("Curve: up, it comes in late; down, early (double-click: straight)", 0.5f + 0.5f * m.curve, 0.5f,
            curveText, 7, [change, m](float v) { change(m.from, m.to, v * 2.0f - 1.0f); });
        auto remove = std::make_unique<ui::IconButton>(ui::Icon::Close);
        remove->setTooltip("Unmap: the parameter is set by hand again");
        remove->setWantsKeyboardFocus(false);
        remove->onClick = [this, i]
        {
            if (onRemoveMapping)
                onRemoveMapping(macro_, i);
        };
        place(mappingsPane_, make(std::move(remove)), 9, row, 1, 1, Kind::Line);
    }

    curves_->show(macro);
    value_.setName(names[macro_]);
    value_.setTooltip(names[macro_] + ": where it is now (its knob in the rack's panel)");
    value_.setValue(macro.value, juce::dontSendNotification);
    shownRows_ = rows();
    resized();
}
} // namespace fd::app
