namespace fd::ui
{
// ---- PanelHeader ------------------------------------------------------------------------------

PanelHeader::PanelHeader()
{
    setWantsKeyboardFocus(false);
}

void PanelHeader::setTitle(juce::String title)
{
    title_ = std::move(title);
    tabs_.clear();
    repaint();
}

void PanelHeader::setTabs(juce::StringArray tabs, int selected)
{
    tabs_ = std::move(tabs);
    selected_ = selected;
    title_.clear();
    repaint();
}

void PanelHeader::setSelectedTab(int index)
{
    if (index != selected_)
    {
        selected_ = index;
        repaint();
    }
}

void PanelHeader::setContext(juce::String text)
{
    if (text != context_)
    {
        context_ = std::move(text);
        repaint();
    }
}

void PanelHeader::setCaption(juce::String text)
{
    if (text != caption_)
    {
        caption_ = std::move(text);
        repaint();
    }
}

void PanelHeader::addOption(ViewOption& option)
{
    options_.push_back(&option);
    addAndMakeVisible(option);
    resized();
}

void PanelHeader::addCommand(juce::Component& command)
{
    commands_.push_back(&command);
    addAndMakeVisible(command);
    resized();
}

void PanelHeader::resized()
{
    // Right to left, `itemGap` apart. The last one's label ends at the padding, where a caption
    // would (the option's own padding is half the shell's).
    const auto& s = Theme::current().shell;
    int right = getWidth() - s.padding / 2;
    for (auto it = options_.rbegin(); it != options_.rend(); ++it)
    {
        const int w = (*it)->preferredWidth();
        (*it)->setBounds(right - w, 0, w, getHeight());
        right -= w + s.itemGap;
    }
    // Commands left of them, a group apart (from the padding when there are none), centred.
    const bool commands = std::any_of(commands_.begin(), commands_.end(), [](auto* c) { return c->isVisible(); });
    if (commands)
    {
        right = options_.empty() ? getWidth() - s.padding : right + s.itemGap - s.groupGap;
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            if ((*it)->isVisible())
            {
                const int w = (*it)->getWidth(), h = (*it)->getHeight();
                (*it)->setBounds(right - w, (getHeight() - h) / 2, w, h);
                right -= w + s.itemGap;
            }
    }
    optionsLeft_ = options_.empty() && !commands ? getWidth() : right;
}

std::vector<juce::Rectangle<int>> PanelHeader::tabAreas() const
{
    const auto& t = Theme::current();
    std::vector<juce::Rectangle<int>> out;
    int x = t.shell.padding;
    for (const auto& tab : tabs_)
    {
        const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), tab))) + 16;
        out.push_back({x - 8, 0, w, getHeight()});
        x += w;
    }
    return out;
}

void PanelHeader::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    auto r = getLocalBounds().reduced(t.shell.padding, 0);
    if (optionsLeft_ < getWidth()) // options or commands on the right
        r.setRight(optionsLeft_ - t.shell.groupGap);
    if (caption_.isNotEmpty())
    {
        g.setColour(t.textTertiary);
        g.setFont(font(TextStyle::Caption));
        g.drawText(caption_, r, juce::Justification::centredRight, false);
    }
    if (tabs_.isEmpty())
    {
        const int titleWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(capsLabelFont(), title_.toUpperCase())));
        paintCapsLabel(g, title_, r.removeFromLeft(titleWidth).toFloat());
    }
    else
    {
        const auto areas = tabAreas();
        g.setFont(font(TextStyle::Control));
        for (int i = 0; i < tabs_.size(); ++i)
        {
            g.setColour(i == selected_ ? t.textPrimary : (i == hovered_ ? t.textSecondary : t.textTertiary));
            g.drawText(tabs_[i], areas[static_cast<size_t>(i)], juce::Justification::centred, false);
        }
        r.setLeft(areas.back().getRight());
    }
    if (context_.isNotEmpty())
    {
        g.setColour(t.textSecondary);
        g.setFont(font(TextStyle::Name));
        g.drawText(context_, r.withTrimmedLeft(t.shell.padding).withTrimmedRight(120), juce::Justification::centredLeft, true);
    }
}

void PanelHeader::mouseMove(const juce::MouseEvent& e)
{
    int hovered = -1;
    const auto areas = tabAreas();
    for (size_t i = 0; i < areas.size(); ++i)
        if (areas[i].contains(e.getPosition()))
            hovered = static_cast<int>(i);
    if (hovered != hovered_)
    {
        hovered_ = hovered;
        repaint();
    }
}

void PanelHeader::mouseExit(const juce::MouseEvent&)
{
    hovered_ = -1;
    repaint();
}

void PanelHeader::mouseUp(const juce::MouseEvent& e)
{
    const auto areas = tabAreas();
    for (size_t i = 0; i < areas.size(); ++i)
        if (areas[i].contains(e.getPosition()))
        {
            setSelectedTab(static_cast<int>(i));
            if (onTabSelected)
                onTabSelected(static_cast<int>(i));
        }
}

// ---- rows, popovers, toasts, empty states -------------------------------------------------------

juce::Rectangle<int> paintProperties(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Property>& rows)
{
    const auto& t = Theme::current();
    for (const auto& p : rows)
    {
        auto line = area.removeFromTop(t.shell.rowHeight);
        g.setFont(font(TextStyle::Body));
        g.setColour(t.textSecondary);
        g.drawText(p.label, line.removeFromLeft(t.shell.propertyLabelWidth), juce::Justification::centredLeft, false);
        g.setFont(font(p.number ? TextStyle::Value : TextStyle::Body));
        g.setColour(t.textPrimary);
        g.drawText(p.value, line, juce::Justification::centredLeft, true);
    }
    return area;
}

juce::Rectangle<int> paintListRow(juce::Graphics& g, juce::Rectangle<int> row, const ListItem& item)
{
    const auto& t = Theme::current();
    if (item.selected)
    {
        g.setColour(t.surface3);
        g.fillRoundedRectangle(row.toFloat(), t.radiusControl);
    }
    row.reduce(t.shell.listTextInset, 0);
    const auto trailing = row.removeFromRight(item.trailing);
    if (item.shortcut.isNotEmpty())
    {
        g.setColour(t.textTertiary);
        g.setFont(font(TextStyle::ValueSmall));
        g.drawText(item.shortcut, row.removeFromRight(120), juce::Justification::centredRight, false);
    }
    if (item.toggled)
    {
        g.setColour(t.active);
        g.fillEllipse(row.removeFromLeft(14).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
    }
    g.setFont(font(TextStyle::Name));
    g.setColour(item.enabled ? t.textPrimary : t.textTertiary);
    const int nameWidth = static_cast<int>(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), item.name)) + 12;
    g.drawText(item.name, row.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);
    if (item.caption.isNotEmpty())
    {
        g.setColour(t.textTertiary);
        g.setFont(font(TextStyle::Caption));
        g.drawText(item.caption, row, juce::Justification::centredLeft, true);
    }
    return trailing;
}

void paintPopover(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto& t = Theme::current();
    // A floating popover may cast one subtle shadow (ui-principles §4).
    juce::DropShadow(t.roles.shadow, juce::roundToInt(t.shell.shadowRadius), {0, juce::roundToInt(t.shell.shadowOffset)})
        .drawForRectangle(g, bounds);
    const auto r = bounds.toFloat();
    g.setColour(t.surface1);
    g.fillRoundedRectangle(r, t.radiusPanel);
    g.setColour(t.line);
    g.drawRoundedRectangle(r.reduced(0.5f), t.radiusPanel, 1.0f);
}

juce::Rectangle<int> toastArea(juce::Rectangle<int> host)
{
    const auto& s = Theme::current().shell;
    return host.reduced(s.padding + 4).removeFromBottom(s.toastHeight).removeFromRight(s.toastWidth);
}

void paintToast(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, const juce::String& action)
{
    const auto& t = Theme::current();
    g.setColour(t.surface3);
    g.fillRoundedRectangle(area.toFloat(), t.radiusPanel);
    auto inside = area.reduced(t.shell.padding, 4);
    if (action.isNotEmpty())
    {
        g.setColour(t.active);
        g.setFont(font(TextStyle::Control));
        const int width = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), action)));
        g.drawText(action, inside.removeFromRight(width), juce::Justification::centredRight, false);
        inside.removeFromRight(t.shell.groupGap);
    }
    g.setColour(t.textPrimary);
    g.setFont(font(TextStyle::Body));
    g.drawFittedText(text, inside, juce::Justification::centredLeft, 2);
}

void paintEmptyState(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    const auto& t = Theme::current();
    g.setColour(t.textTertiary);
    g.setFont(font(TextStyle::Body));
    g.drawFittedText(text, area.reduced(t.shell.padding), juce::Justification::centred, 2);
}

void paintValueTag(juce::Graphics& g, juce::Point<float> anchor, juce::Rectangle<int> within, const juce::String& text)
{
    const auto& t = Theme::current();
    const auto f = font(TextStyle::Value);
    const float width = juce::GlyphArrangement::getStringWidth(f, text) + 2.0f * static_cast<float>(t.shell.fieldTextInset);
    const float height = static_cast<float>(t.shell.rowHeight), gap = static_cast<float>(t.shell.itemGap);
    auto tag = juce::Rectangle<float>(anchor.x + gap, anchor.y - gap - height, width, height);
    tag = tag.constrainedWithin(within.toFloat());
    g.setColour(t.surface3);
    g.fillRoundedRectangle(tag, t.radiusControl);
    g.setColour(t.textPrimary);
    g.setFont(f);
    g.drawText(text, tag, juce::Justification::centred, false);
}

void paintProgress(juce::Graphics& g, juce::Rectangle<float> area, float fraction)
{
    const auto& t = Theme::current();
    const auto line = area.removeFromBottom(static_cast<float>(t.shell.progressHeight));
    g.setColour(t.surface3); // the track, as under a knob's arc
    g.fillRect(line);
    g.setColour(t.value);
    g.fillRect(line.withWidth(line.getWidth() * std::clamp(fraction, 0.0f, 1.0f)));
}

// ---- BarLayout ----------------------------------------------------------------------------------

// ---- OutsideClick -----------------------------------------------------------------------------

OutsideClick::OutsideClick(juce::Component& popover, std::function<void()> close)
    : popover_(popover), close_(std::move(close))
{
}

OutsideClick::~OutsideClick()
{
    detach();
}

void OutsideClick::attach()
{
    if (!attached_)
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    attached_ = true;
}

void OutsideClick::detach()
{
    if (attached_)
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    attached_ = false;
}

void OutsideClick::clickedOn(juce::Component& component)
{
    if (!popover_.isVisible() || &component == &popover_ || popover_.isParentOf(&component))
        return;
    if (component.getTopLevelComponent() != popover_.getTopLevelComponent())
        return; // another window: a menu the popover opened, a plugin's editor
    if (ignore && ignore(component))
        return;
    if (close_)
        close_();
}

void OutsideClick::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent != nullptr)
        clickedOn(*e.eventComponent);
}

// ---- SectionStack -----------------------------------------------------------------------------

juce::Rectangle<int> SectionStack::layout(juce::Rectangle<int> area, const std::vector<Section>& sections)
{
    const auto& s = Theme::current().shell;
    placed_.clear();
    for (const auto& section : sections)
    {
        Placed p{.title = section.title};
        if (!placed_.empty())
        {
            // The line between neighbours, in the middle of the group gap.
            const auto gap = area.removeFromTop(s.groupGap);
            p.line = gap.withHeight(1).withY(gap.getCentreY());
        }
        p.titleArea = area.removeFromTop(s.rowHeight);
        area.removeFromTop(s.itemGap);
        p.contentArea = area.removeFromTop(section.contentHeight);
        placed_.push_back(std::move(p));
    }
    return area;
}

juce::Rectangle<int> SectionStack::content(size_t index) const
{
    return index < placed_.size() ? placed_[index].contentArea : juce::Rectangle<int>();
}

juce::Rectangle<int> SectionStack::titleArea(size_t index) const
{
    return index < placed_.size() ? placed_[index].titleArea : juce::Rectangle<int>();
}

void SectionStack::paint(juce::Graphics& g) const
{
    const auto& t = Theme::current();
    for (const auto& p : placed_)
    {
        if (!p.line.isEmpty())
        {
            g.setColour(t.line);
            g.fillRect(p.line);
        }
        paintCapsLabel(g, p.title, p.titleArea.toFloat());
    }
}

juce::Rectangle<int> BarLayout::left(int width)
{
    const auto& s = Theme::current().shell;
    if (!firstLeft_)
        area_.removeFromLeft(groupLeft_ ? s.groupGap : s.itemGap);
    firstLeft_ = groupLeft_ = false;
    return area_.removeFromLeft(width < 0 ? area_.getHeight() : width);
}

juce::Rectangle<int> BarLayout::right(int width)
{
    const auto& s = Theme::current().shell;
    if (!firstRight_)
        area_.removeFromRight(groupRight_ ? s.groupGap : s.itemGap);
    firstRight_ = groupRight_ = false;
    return area_.removeFromRight(width < 0 ? area_.getHeight() : width);
}
} // namespace fd::ui
