namespace fd::ui
{
/// A sub-label placed like a control.
class PanelSection::CapsLabel final : public juce::Component
{
public:
    explicit CapsLabel(juce::String text) : text_(std::move(text)) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override { paintCapsLabel(g, text_, getLocalBounds().toFloat()); }

private:
    juce::String text_;
};

PanelSection::PanelSection(DevicePanel& panel, std::string id, juce::String title, int columns,
                           std::optional<size_t> powerParam)
    : panel_(panel), id_(std::move(id)), title_(std::move(title)), columns_(std::max(1, columns)), powerParam_(powerParam)
{
    setWantsKeyboardFocus(true);
    setSectionTitle(title_);
    addAndMakeVisible(content_);
    setPowerParam(powerParam);
}

void PanelSection::setPowerParam(std::optional<size_t> param)
{
    if (power_ != nullptr)
    {
        removeChildComponent(power_);
        panel_.destroy(power_);
        power_ = nullptr;
    }
    powerParam_ = param;
    if (powerParam_)
    {
        auto& power = panel_.adopt(std::make_unique<ParamToggle>(panel_.context(), *powerParam_), *this);
        power.setButtonText({}); // a square toggle without text draws as a power dot
        power_ = &power;
    }
    resized();
    refresh();
    repaint();
}

PanelSection::~PanelSection() = default;

void PanelSection::setSectionTitle(juce::String title)
{
    title_ = std::move(title);
    juce::Component::setTitle(title_.isEmpty() ? juce::String(id_) : title_); // accessibility
    setDescription("Section " + title_ + " (Return collapses or expands)");
    repaint();
}

int PanelSection::preferredWidth() const noexcept
{
    const auto& grid = Theme::current().deviceGrid;
    return collapsed_ ? grid.collapsedWidth : grid.sectionWidth(columns_);
}

void PanelSection::setCollapsed(bool collapsed, bool byUser)
{
    if (collapsed == collapsed_)
        return;
    collapsed_ = collapsed;
    panel_.viewChanged(byUser);
}

bool PanelSection::isPoweredOn() const
{
    return !powerParam_ || panel_.context().params.getPlain(*powerParam_) >= 0.5f;
}

void PanelSection::refresh()
{
    const float alpha = isPoweredOn() ? 1.0f : 0.35f;
    if (content_.getAlpha() != alpha)
        content_.setAlpha(alpha);
}

// ---- placement ----------------------------------------------------------------------------

juce::Rectangle<int> PanelSection::cellBounds(int column, int row, int columns, int rows)
{
    const auto& grid = Theme::current().deviceGrid;
    return {column * (grid.column + grid.columnGap), row * (grid.row + grid.rowGap), grid.width(columns),
            grid.height(rows)};
}

void PanelSection::add(juce::Component& component, Kind kind, int column, int row, int columns, int rows)
{
    const auto& grid = Theme::current().deviceGrid;
    // The grid rules (docs/design/device-panels.md); the layout lint checks them too.
    jassert(column >= 0 && columns >= 1 && column + columns <= columns_);
    jassert(row >= 0 && rows >= 1 && row + rows <= grid.rows);
    jassert(kind != Kind::Knob || (columns == 1 && rows == grid.knobRows && (row == 0 || row == grid.knobRows)));
    juce::ignoreUnused(grid);
    if (component.getParentComponent() != &content_)
        content_.addAndMakeVisible(component);
    placements_.push_back({&component, kind, column, row, columns, rows});
    component.setBounds(cellBounds(column, row, columns, rows));
}

ParamKnob& PanelSection::knob(size_t param, int column, int row)
{
    auto& k = panel_.adopt(std::make_unique<ParamKnob>(panel_.context(), param), content_);
    created_.push_back(&k);
    add(k, Kind::Knob, column, row, 1, Theme::current().deviceGrid.knobRows);
    return k;
}

ParamKnob& PanelSection::bar(size_t param, int column, int row, int columns)
{
    jassert(columns >= 2);
    auto& k = panel_.adopt(std::make_unique<ParamKnob>(panel_.context(), param), content_);
    k.setStyle(Knob::Style::Bar);
    created_.push_back(&k);
    add(k, Kind::Line, column, row, columns, 1);
    return k;
}

ParamToggle& PanelSection::toggle(size_t param, int column, int row, int columns)
{
    auto& b = panel_.adopt(std::make_unique<ParamToggle>(panel_.context(), param), content_);
    created_.push_back(&b);
    add(b, Kind::Line, column, row, columns, 1);
    return b;
}

ParamComboBox& PanelSection::combo(size_t param, int column, int row, int columns)
{
    jassert(columns >= 2);
    auto& c = panel_.adopt(std::make_unique<ParamComboBox>(panel_.context(), param), content_);
    created_.push_back(&c);
    add(c, Kind::Line, column, row, columns, 1);
    return c;
}

ParamSegmented& PanelSection::segmented(size_t param, int column, int row, int columns, int rows)
{
    const auto choices = static_cast<int>(panel_.context().info.params[param].choices.size());
    const int perRow = (choices + rows - 1) / std::max(1, rows);
    auto& s = panel_.adopt(std::make_unique<ParamSegmented>(panel_.context(), param, perRow), content_);
    created_.push_back(&s);
    add(s, Kind::Line, column, row, columns, rows);
    return s;
}

void PanelSection::place(juce::Component& component, int column, int row, int columns, int rows, Kind kind)
{
    add(component, kind, column, row, columns, rows);
}

void PanelSection::label(juce::String text, int column, int row, int columns)
{
    auto& l = *labels_.emplace_back(std::make_unique<CapsLabel>(std::move(text)));
    add(l, Kind::Label, column, row, columns, 1);
}

void PanelSection::clear()
{
    for (const auto& p : placements_)
        content_.removeChildComponent(p.component);
    placements_.clear();
    for (auto* c : created_)
        panel_.destroy(c);
    created_.clear();
    labels_.clear();
}

// ---- geometry and painting ----------------------------------------------------------------

juce::Rectangle<int> PanelSection::contentBounds() const
{
    if (collapsed_)
        return {};
    const auto& grid = Theme::current().deviceGrid;
    return {grid.sectionPadding, grid.contentTop(), grid.width(columns_), grid.height(grid.rows)};
}

juce::Rectangle<int> PanelSection::titleBounds() const
{
    const auto& grid = Theme::current().deviceGrid;
    if (collapsed_) // the whole strip
        return getLocalBounds().withTrimmedTop(grid.bodyTop).withTrimmedBottom(grid.bodyBottom);
    return {grid.sectionPadding, grid.bodyTop, grid.width(columns_), grid.titleHeight};
}

void PanelSection::resized()
{
    const auto& grid = Theme::current().deviceGrid;
    content_.setVisible(!collapsed_);
    content_.setBounds(contentBounds());
    if (power_ != nullptr)
    {
        const int size = 14;
        const auto title = titleBounds();
        power_->setBounds(collapsed_ ? juce::Rectangle<int>(0, grid.bodyTop, getWidth(), grid.titleHeight).withSizeKeepingCentre(size, size)
                                     : title.withWidth(size).withSizeKeepingCentre(size, size));
    }
}

void PanelSection::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto& grid = t.deviceGrid;
    const bool hot = isMouseOver(false) || hasKeyboardFocus(false);
    const auto colour = hot ? t.textSecondary : t.textTertiary;

    if (collapsed_)
    {
        // A strip: the title rotated, reading upwards, below the power toggle (if any).
        const auto strip = titleBounds().toFloat();
        if (hot)
        {
            g.setColour(t.roles.hoverWash);
            g.fillRoundedRectangle(strip.reduced(2.0f, 0.0f), t.radiusControl);
        }
        auto textArea = strip.withTrimmedTop(power_ != nullptr ? static_cast<float>(grid.titleHeight) + 4.0f : 4.0f);
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, textArea.getCentreX(),
                                                       textArea.getCentreY()));
        const auto rotated = juce::Rectangle<float>(textArea.getHeight(), textArea.getWidth()).withCentre(textArea.getCentre());
        paintCapsLabel(g, title_, rotated, juce::Justification::centredRight, colour);
        return;
    }

    auto title = titleBounds().toFloat();
    if (power_ != nullptr)
        title.removeFromLeft(18.0f);
    // The collapse affordance, on hover, where it doesn't cover the title.
    if (hot && juce::GlyphArrangement::getStringWidth(capsLabelFont(), title_.toUpperCase()) + 14.0f <= title.getWidth())
        drawIcon(g, Icon::ChevronLeft, title.withLeft(title.getRight() - 12.0f).withSizeKeepingCentre(10.0f, 10.0f), colour);
    paintCapsLabel(g, title_, title, juce::Justification::centredLeft, colour);
    if (hasKeyboardFocus(false))
    {
        g.setColour(t.roles.focusRing);
        g.drawRoundedRectangle(titleBounds().toFloat().expanded(2.0f, 0.0f), t.radiusControl, 1.0f);
    }
}

void PanelSection::mouseUp(const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() || !titleBounds().contains(e.getPosition()))
        return;
    if (e.mods.isAltDown())
        panel_.setAllSectionsCollapsed(!collapsed_, true);
    else
        setCollapsed(!collapsed_, true);
}

bool PanelSection::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() != juce::KeyPress::returnKey)
        return false;
    setCollapsed(!collapsed_, true);
    return true;
}
} // namespace fd::ui
