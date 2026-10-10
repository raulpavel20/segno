#include "ui/AboutPopover.h"

namespace fd::app
{
namespace
{
juce::AttributedString text(const juce::String& s, juce::Colour colour)
{
    juce::AttributedString a;
    a.append(s, ui::font(ui::TextStyle::Body), colour);
    a.setWordWrap(juce::AttributedString::byWord);
    return a;
}

int textHeight(const juce::String& s, int width)
{
    juce::TextLayout layout;
    layout.createLayout(text(s, ui::Theme::current().textPrimary), static_cast<float>(width));
    return static_cast<int>(std::ceil(layout.getHeight()));
}
} // namespace

AboutPopover::AboutPopover()
{
    setWantsKeyboardFocus(true);
}

void AboutPopover::setVersion(const juce::String& version)
{
    version_ = version;
    repaint();
}

juce::StringArray AboutPopover::paragraphs() const
{
    return {
        "Version " + (version_.isEmpty() ? juce::String("(development)") : version_),
        "Segno is free software: you can redistribute it and modify it under the terms of the GNU Affero "
        "General "
        "Public License, version 3 or later. It comes with absolutely no warranty.",
        "The licence: https://www.gnu.org/licenses/agpl-3.0.html",
        "Built with JUCE (AGPL-3.0) and tracktion_engine (GPL-3.0); the Inter and JetBrains Mono fonts (SIL "
        "Open "
        "Font License 1.1); nlohmann/json, cpp-httplib, elliptic-blep and dr_libs (MIT and public domain).",
    };
}

std::vector<juce::Rectangle<int>> AboutPopover::layout(int width) const
{
    const auto& sh = ui::Theme::current().shell;
    const int inner = width - 2 * sh.padding;
    std::vector<juce::Rectangle<int>> out;
    // The brand on the first row; then each paragraph, a group gap apart.
    int y = sh.padding + sh.rowHeight + sh.itemGap;
    for (const auto& p : paragraphs())
    {
        const int h = textHeight(p, inner);
        out.emplace_back(sh.padding, y, inner, h);
        y += h + sh.groupGap;
    }
    return out;
}

int AboutPopover::preferredHeight() const
{
    const auto& sh = ui::Theme::current().shell;
    const auto areas = layout(sh.formPopoverWidth);
    return areas.empty() ? 2 * sh.padding : areas.back().getBottom() + sh.padding;
}

void AboutPopover::open(juce::Component& parent)
{
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    const auto& sh = ui::Theme::current().shell;
    setBounds(juce::Rectangle<int>(sh.formPopoverWidth, preferredHeight())
                  .withCentre({parent.getWidth() / 2, 0})
                  .withY(sh.popoverTop)
                  .constrainedWithin(parent.getLocalBounds()));
    setVisible(true);
    toFront(true);
    outside_.attach();
    if (isShowing())
        grabKeyboardFocus(); // Escape closes it
}

void AboutPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

bool AboutPopover::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void AboutPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& sh = t.shell;
    ui::paintPopover(g, getLocalBounds());
    ui::paintBrand(g, {sh.padding, sh.padding, getWidth() - 2 * sh.padding, sh.rowHeight});
    const auto texts = paragraphs();
    const auto areas = layout(getWidth());
    for (int i = 0; i < texts.size(); ++i)
    {
        juce::TextLayout l;
        l.createLayout(text(texts[i], i == 0 ? t.textPrimary : t.textSecondary),
                       static_cast<float>(areas[static_cast<size_t>(i)].getWidth()));
        l.draw(g, areas[static_cast<size_t>(i)].toFloat());
    }
}
} // namespace fd::app
