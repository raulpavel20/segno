namespace fd::ui
{
// ---- TabRow -----------------------------------------------------------------------------------

TabRow::TabRow()
{
    setWantsKeyboardFocus(false);
}

void TabRow::setTabs(juce::StringArray tabs, int selected)
{
    tabs_ = std::move(tabs);
    selected_ = selected;
    hovered_ = -1;
    if (closable_ >= tabs_.size())
        closable_ = -1;
    repaint();
}

void TabRow::setClosable(int index, juce::String tooltip)
{
    closable_ = index >= 0 && index < tabs_.size() ? index : -1;
    closeTooltip_ = std::move(tooltip);
    repaint();
}

juce::Rectangle<int> TabRow::closeArea() const
{
    if (closable_ < 0)
        return {};
    // At the end of its tab, before the tab's right padding.
    const auto& s = Theme::current().shell;
    const auto tab = tabAreas()[static_cast<size_t>(closable_)];
    return juce::Rectangle<int>(tab.getRight() - s.buttonTextPadding - s.listIconSize, 0, s.listIconSize,
                                getHeight())
        .withSizeKeepingCentre(s.listIconSize, s.listIconSize);
}

juce::String TabRow::getTooltip()
{
    return closeHovered_ ? closeTooltip_ : juce::String();
}

void TabRow::setSelected(int index)
{
    if (index != selected_)
    {
        selected_ = index;
        repaint();
    }
}

std::vector<juce::Rectangle<int>> TabRow::tabAreas() const
{
    const int padding = Theme::current().shell.buttonTextPadding;
    std::vector<juce::Rectangle<int>> out;
    int x = 0;
    const auto& s = Theme::current().shell;
    for (int i = 0; i < tabs_.size(); ++i)
    {
        // A closable tab: its name, then the ×, an item gap apart.
        const int w = static_cast<int>(std::ceil(
                          juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), tabs_[i]))) +
                      2 * padding + (i == closable_ ? s.itemGap + s.listIconSize : 0);
        out.push_back({x, 0, w, getHeight()});
        x += w;
    }
    return out;
}

int TabRow::preferredWidth() const
{
    const auto areas = tabAreas();
    return areas.empty() ? 0 : areas.back().getRight();
}

int TabRow::tabAt(juce::Point<int> position) const
{
    const auto areas = tabAreas();
    for (size_t i = 0; i < areas.size(); ++i)
        if (areas[i].contains(position))
            return static_cast<int>(i);
    return -1;
}

void TabRow::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto areas = tabAreas();
    g.setFont(font(TextStyle::Control));
    for (int i = 0; i < tabs_.size(); ++i)
    {
        const auto colour =
            i == selected_ ? t.textPrimary : (i == hovered_ ? t.textSecondary : t.textTertiary);
        auto area = areas[static_cast<size_t>(i)];
        if (i == closable_)
            area.removeFromRight(t.shell.itemGap + t.shell.listIconSize);
        g.setColour(colour);
        g.drawText(tabs_[i], area, juce::Justification::centred, false);
    }
    if (closable_ >= 0)
        drawIcon(g, Icon::Close, closeArea().toFloat(), closeHovered_ ? t.textPrimary : t.textTertiary);
}

void TabRow::mouseMove(const juce::MouseEvent& e)
{
    const bool overClose = closeArea().expanded(2).contains(e.getPosition());
    if (const int hovered = tabAt(e.getPosition()); hovered != hovered_ || overClose != closeHovered_)
    {
        hovered_ = hovered;
        closeHovered_ = overClose;
        repaint();
    }
}

void TabRow::mouseExit(const juce::MouseEvent&)
{
    hovered_ = -1;
    closeHovered_ = false;
    repaint();
}

void TabRow::mouseUp(const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    if (closeArea().expanded(2).contains(e.getPosition()))
    {
        if (onClose)
            onClose(closable_);
        return;
    }
    if (const int tab = tabAt(e.getPosition()); tab >= 0 && tab != selected_)
    {
        setSelected(tab);
        if (onSelect)
            onSelect(tab);
    }
}

// ---- PanelHeader ------------------------------------------------------------------------------

PanelHeader::PanelHeader()
{
    setWantsKeyboardFocus(false);
    addChildComponent(tabs_);
    tabs_.onSelect = [this](int index)
    {
        if (onTabSelected)
            onTabSelected(index);
    };
    tabs_.onClose = [this](int index)
    {
        if (onTabClosed)
            onTabClosed(index);
    };
}

void PanelHeader::setClosableTab(int index, juce::String tooltip)
{
    tabs_.setClosable(index, std::move(tooltip));
    resized();
    repaint();
}

void PanelHeader::setTitle(juce::String title)
{
    title_ = std::move(title);
    tabs_.setTabs({}, 0);
    tabs_.setVisible(false);
    resized();
    repaint();
}

void PanelHeader::setTabs(juce::StringArray tabs, int selected)
{
    tabs_.setTabs(std::move(tabs), selected);
    tabs_.setVisible(true);
    title_.clear();
    resized();
    repaint();
}

void PanelHeader::setSelectedTab(int index)
{
    tabs_.setSelected(index);
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
    options_.push_back({&option, [&option] { return option.preferredWidth(); }});
    addAndMakeVisible(option);
    resized();
}

void PanelHeader::addOption(ViewChoice& choice)
{
    options_.push_back({&choice, [&choice] { return choice.preferredWidth(); }});
    addAndMakeVisible(choice);
    resized();
}

void PanelHeader::addCommand(juce::Component& command)
{
    commands_.push_back(&command);
    addAndMakeVisible(command);
    resized();
}

void PanelHeader::removeCommand(juce::Component& command)
{
    std::erase(commands_, &command);
    removeChildComponent(&command);
    resized();
    repaint();
}

void PanelHeader::resized()
{
    // Right to left, `itemGap` apart. The last one's label ends at the padding, where a caption
    // would (the option's own padding is half the shell's).
    const auto& s = Theme::current().shell;
    int right = getWidth() - s.padding / 2;
    bool options = false;
    for (auto it = options_.rbegin(); it != options_.rend(); ++it)
        if (it->component->isVisible())
        {
            const int w = it->width();
            it->component->setBounds(right - w, 0, w, getHeight());
            right -= w + s.itemGap;
            options = true;
        }
    // Commands left of them, a group apart (from the padding when there are none), centred.
    const bool commands = std::any_of(commands_.begin(), commands_.end(), [](auto* c) { return c->isVisible(); });
    if (commands)
    {
        right = !options ? getWidth() - s.padding : right + s.itemGap - s.groupGap;
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            if ((*it)->isVisible())
            {
                const int w = (*it)->getWidth(), h = (*it)->getHeight();
                (*it)->setBounds(right - w, (getHeight() - h) / 2, w, h);
                right -= w + s.itemGap;
            }
    }
    optionsLeft_ = !options && !commands ? getWidth() : right;
    // The tabs' text starts at the padding, where a title's would.
    tabs_.setBounds(s.padding - s.buttonTextPadding, 0, tabs_.preferredWidth(), getHeight());
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
    if (!tabs_.isVisible())
    {
        const int titleWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(capsLabelFont(), title_.toUpperCase())));
        paintCapsLabel(g, title_, r.removeFromLeft(titleWidth).toFloat());
    }
    else
        r.setLeft(tabs_.getRight());
    if (context_.isNotEmpty())
    {
        g.setColour(t.textSecondary);
        g.setFont(font(TextStyle::Name));
        g.drawText(context_, r.withTrimmedLeft(t.shell.padding).withTrimmedRight(120), juce::Justification::centredLeft, true);
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

juce::Rectangle<int> treeRowMark(juce::Rectangle<int> row, const TreeItem& item)
{
    const auto& s = Theme::current().shell;
    return {row.getX() + s.listTextInset + item.depth * s.browserIndent, row.getY(),
            item.flat ? 0 : s.listIconSize, row.getHeight()};
}

juce::Rectangle<int> treeRowStar(juce::Rectangle<int> row)
{
    const auto& s = Theme::current().shell;
    return {row.getRight() - s.listTextInset - s.listIconSize, row.getY(), s.listIconSize, row.getHeight()};
}

namespace
{
/// A tree row's text: after its disclosure, before the star's column (kept on every row).
juce::Rectangle<int> treeRowText(juce::Rectangle<int> row, const TreeItem& item)
{
    const auto& s = Theme::current().shell;
    const auto mark = treeRowMark(row, item);
    auto text = row.withLeft(item.flat ? row.getX() + s.listTextInset : mark.getRight() + s.itemGap)
                    .withTrimmedRight(s.listTextInset);
    text.removeFromRight(s.listIconSize + s.itemGap);
    return text;
}
} // namespace

juce::Rectangle<int> treeRowCaption(juce::Rectangle<int> row, const TreeItem& item)
{
    if (item.caption.isEmpty())
        return {};
    auto text = treeRowText(row, item);
    const int caption = juce::GlyphArrangement::getStringWidthInt(font(TextStyle::Caption), item.caption);
    const int name = juce::GlyphArrangement::getStringWidthInt(font(TextStyle::Name), item.name);
    if (name + Theme::current().shell.itemGap + caption > text.getWidth())
        return {};
    return text.removeFromRight(caption);
}

void paintTreeRow(juce::Graphics& g, juce::Rectangle<int> row, const TreeItem& item)
{
    const auto& t = Theme::current();
    const auto& s = t.shell;
    if (item.selected || item.hovered)
    {
        g.setColour(item.selected ? t.surface3 : t.roles.hoverWash);
        g.fillRoundedRectangle(row.toFloat(), t.radiusControl);
    }
    const auto icon = [](juce::Rectangle<int> area)
    {
        const auto size = static_cast<float>(Theme::current().shell.listIconSize);
        return area.toFloat().withSizeKeepingCentre(size, size);
    };
    const auto mark = treeRowMark(row, item);
    if (item.mark == TreeItem::Mark::Closed || item.mark == TreeItem::Mark::Open)
        drawIcon(g, item.mark == TreeItem::Mark::Open ? Icon::ChevronDown : Icon::ChevronRight, icon(mark),
                 t.textTertiary);
    if (item.mark == TreeItem::Mark::Add)
        drawIcon(g, Icon::Plus, icon(mark), t.textSecondary);
    auto text = treeRowText(row, item);
    if (item.starrable && (item.starred || item.hovered))
        drawIcon(g, item.starred ? Icon::StarFilled : Icon::Star, icon(treeRowStar(row)),
                 item.starred ? t.active : t.textTertiary);
    if (const auto caption = treeRowCaption(row, item); !caption.isEmpty())
    {
        g.setFont(font(TextStyle::Caption));
        g.setColour(t.textTertiary);
        g.drawText(item.caption, caption, juce::Justification::centredRight, false);
        text.setRight(caption.getX() - s.itemGap);
    }
    g.setFont(font(TextStyle::Name));
    g.setColour(item.dim ? t.textTertiary : item.secondary ? t.textSecondary : t.textPrimary);
    g.drawText(item.name, text, juce::Justification::centredLeft, true);
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
