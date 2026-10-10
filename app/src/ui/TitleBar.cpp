#include "ui/TitleBar.h"

#include "model/TopBarLayout.h"

namespace fd::app
{
namespace
{
const juce::String& unsavedMark()
{
    static const auto mark = juce::String::fromUTF8(" \xE2\x80\xA2"); // " •"
    return mark;
}
} // namespace

TitleBar::TitleBar(juce::MenuBarModel& menus) : menuBar_(&menus)
{
    addAndMakeVisible(menuBar_);
    buttons_.onClick = [this](ui::WindowButtons::Part part)
    {
        using P = ui::WindowButtons::Part;
        const auto& run = part == P::Minimise ? onMinimise : part == P::Maximise ? onMaximise : onClose;
        if (run)
            run();
    };
    addAndMakeVisible(buttons_);
}

void TitleBar::setTitle(const juce::String& name, bool unsaved)
{
    if (name == name_ && unsaved == unsaved_)
        return;
    name_ = name;
    unsaved_ = unsaved;
    resized();
    repaint();
}

void TitleBar::setOwnsWindow(bool owns)
{
    if (owns_ == owns)
        return;
    owns_ = owns;
    resized();
    repaint();
}

void TitleBar::setWindowState(bool maximised, bool fullScreen)
{
    buttons_.setMaximised(maximised);
    if (fullScreen_ == fullScreen)
        return;
    fullScreen_ = fullScreen;
    resized();
    repaint();
}

void TitleBar::showMenu(int index)
{
    menuBar_.showMenu(index);
}

void TitleBar::setMnemonicsShown(bool shown)
{
    if (static_cast<bool>(menuBar_.getProperties()[ui::mnemonicsShown]) == shown)
        return;
    menuBar_.getProperties().set(ui::mnemonicsShown, shown);
    menuBar_.repaint();
}

int TitleBar::menusWidth()
{
    int w = 0;
    if (auto* model = menuBar_.getModel())
    {
        const auto names = model->getMenuBarNames();
        for (int i = 0; i < names.size(); ++i)
            w += getLookAndFeel().getMenuBarItemWidth(menuBar_, i, names[i]);
    }
    return w;
}

TitleBar::Part TitleBar::partAt(juce::Point<int> p) const
{
    if (buttons_.isVisible() && buttons_.getBounds().contains(p))
    {
        using P = ui::WindowButtons::Part;
        switch (buttons_.partAt(p - buttons_.getPosition()))
        {
        case P::Minimise:
            return Part::Minimise;
        case P::Maximise:
            return Part::Maximise;
        case P::Close:
            return Part::Close;
        case P::None:
            break;
        }
    }
    if (menuBar_.getBounds().contains(p))
        return Part::Client;
    return Part::Caption;
}

void TitleBar::resized()
{
    // Three parts (menu-bar.md §4): the brand and the menus from the left on the bar's gaps, the
    // window buttons at the right edge, the name centred between, narrowed before the wordmark goes.
    const auto& t = ui::Theme::current();
    const auto& sh = t.shell;
    const bool buttons = owns_ && !fullScreen_;
    buttons_.setVisible(buttons);
    const int buttonsWidth = buttons ? ui::WindowButtons::widthFor() : 0;
    buttons_.setBounds(getWidth() - buttonsWidth, 0, buttonsWidth, getHeight());

    const int h = getHeight() - 2 * sh.barInset; // the brand's height, as in the top bar
    const int mark = ui::brandMarkWidth(h);
    using S = model::BarSection;
    const std::vector<model::BarItem> items{
        {S::Left, static_cast<double>(mark)},
        {S::Left, static_cast<double>(ui::brandWidth(h) - mark - sh.itemGap), false, 1},
        {S::Left, static_cast<double>(menusWidth()), true},
    };
    const auto slots = model::layoutTopBar(getWidth() - buttonsWidth, items,
                                           {.padding = static_cast<double>(sh.barPadding),
                                            .itemGap = static_cast<double>(sh.itemGap),
                                            .groupGap = static_cast<double>(sh.groupGap),
                                            .sectionGap = 2.0 * sh.groupGap});
    auto x = [](const model::BarSlot& s) { return juce::roundToInt(s.x); };
    auto w = [](const model::BarSlot& s) { return juce::roundToInt(s.width); };
    markOnly_ = !slots[1].shown;
    brand_ = juce::Rectangle<int>(x(slots[0]), sh.barInset,
                                  (markOnly_ ? w(slots[0]) : x(slots[1]) + w(slots[1]) - x(slots[0])), h);
    menuBar_.setBounds(x(slots[2]), 0, w(slots[2]), getHeight());

    title_ = {};
    if (owns_ && name_.isNotEmpty())
    {
        const auto f = ui::font(ui::TextStyle::Body);
        const auto text =
            juce::GlyphArrangement::getStringWidth(f, name_ + (unsaved_ ? unsavedMark() : juce::String()));
        if (const auto slot = model::placeTitle(getWidth(), menuBar_.getRight(), getWidth() - buttonsWidth,
                                                std::ceil(text), 2.0 * sh.groupGap);
            slot.shown)
            title_ = {x(slot), 0, w(slot), getHeight()};
    }
}

void TitleBar::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);
    if (markOnly_)
        ui::paintMark(g, brand_.toFloat(), t.active);
    else
        ui::paintBrand(g, brand_);
    if (title_.isEmpty())
        return;
    // The name, elided to its slot, then the unsaved dot (never elided away).
    const auto f = ui::font(ui::TextStyle::Body);
    g.setFont(f);
    g.setColour(t.textSecondary);
    const int dot =
        unsaved_ ? static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(f, unsavedMark()))) : 0;
    auto area = title_;
    const auto dotArea = area.removeFromRight(dot);
    g.drawText(name_, area, juce::Justification::centredLeft, true);
    if (unsaved_)
        g.drawText(unsavedMark(), dotArea, juce::Justification::centredLeft, false);
}

void TitleBar::mouseDown(const juce::MouseEvent& e)
{
    dragStarted_ = false;
    if (partAt(e.getPosition()) != Part::Caption)
        return;
    if (e.mods.isPopupMenu())
    {
        if (onCaptionMenu)
            onCaptionMenu();
    }
    else if (e.getNumberOfClicks() == 2 && onCaptionDoubleClick)
        onCaptionDoubleClick();
}

void TitleBar::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStarted_ || !e.mods.isLeftButtonDown() || partAt(e.getMouseDownPosition()) != Part::Caption ||
        e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        return;
    dragStarted_ = true;
    if (onCaptionDrag)
        onCaptionDrag(e);
}
} // namespace fd::app
