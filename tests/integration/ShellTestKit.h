#pragma once

// A shell built offscreen around a Session, and mouse and screenshot helpers, for UI tests of
// the shell's views (AppSmokeTests.cpp keeps its own copies).

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "ui/Shell.h"

#include <tracktion_engine/tracktion_engine.h>

namespace fd::testing
{
struct ShellFixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<fd::engine::Session> session;
    std::unique_ptr<fd::app::Shell> shell;

    ShellFixture()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
        shell = std::make_unique<fd::app::Shell>(*session);
    }

    ~ShellFixture()
    {
        shell.reset();
        session.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
};

inline juce::MouseEvent mouseEvent(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged,
                                   juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
{
    return {juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, dragged};
}

/// Writes a screenshot for review into `dir`, replacing any earlier one.
inline void dumpPng(const char* dir, const juce::String& name, const juce::Image& image)
{
    const auto file = juce::File(dir).getChildFile(name);
    file.deleteFile();
    juce::PNGImageFormat png;
    if (auto out = file.createOutputStream())
        png.writeImageToStream(image, *out);
}

/// A click (or, with clicks = 2, the second click of a double-click) at `pos`, with modifiers.
inline void click(juce::Component& c, juce::Point<float> pos, int clicks = 1, int mods = 0)
{
    const juce::ModifierKeys m(juce::ModifierKeys::leftButtonModifier | mods);
    c.mouseDown(mouseEvent(c, pos, pos, false, m, clicks));
    c.mouseUp(mouseEvent(c, pos, pos, false, m, clicks));
}

/// A double-click at `pos`.
inline void doubleClick(juce::Component& c, juce::Point<float> pos, int mods = 0)
{
    click(c, pos, 1, mods);
    click(c, pos, 2, mods);
}

/// Press at `from`, drag to `to` in a few steps, release; with modifiers held throughout.
inline void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to, int mods = 0)
{
    const juce::ModifierKeys m(juce::ModifierKeys::leftButtonModifier | mods);
    c.mouseDown(mouseEvent(c, from, from, false, m));
    for (float k : {0.25f, 0.5f, 1.0f})
        c.mouseDrag(mouseEvent(c, from + (to - from) * k, from, true, m));
    c.mouseUp(mouseEvent(c, to, from, true, m));
}

/// Runs a menu item by its text, in submenus too; false if there's none (or it's dimmed).
inline bool invoke(const juce::PopupMenu& menu, const juce::String& text, const juce::String& in = {})
{
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        auto& item = it.getItem();
        if (item.subMenu != nullptr)
        {
            if ((in.isEmpty() || item.text == in) && invoke(*item.subMenu, text))
                return true;
            continue;
        }
        if (in.isEmpty() && item.text == text && item.isEnabled && item.action)
        {
            item.action();
            return true;
        }
    }
    return false;
}

/// A menu's items as text, submenus as "Title ▸ item", separators as "—".
inline juce::StringArray itemsOf(const juce::PopupMenu& menu, const juce::String& prefix = {})
{
    juce::StringArray out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        const auto& item = it.getItem();
        if (item.isSeparator)
            out.add(prefix + juce::String::fromUTF8("—"));
        else if (item.subMenu != nullptr)
            out.addArray(itemsOf(*item.subMenu, prefix + item.text + juce::String::fromUTF8(" ▸ ")));
        else
            out.add(prefix + item.text);
    }
    return out;
}

/// Whether any two of the visible children of `c` overlap, or touch closer than `gap`.
inline juce::StringArray crowded(juce::Component& c, int gap)
{
    juce::StringArray out;
    std::vector<juce::Component*> shown;
    for (auto* child : c.getChildren())
        if (child->isVisible() && !child->getBounds().isEmpty())
            shown.push_back(child);
    for (std::size_t i = 0; i < shown.size(); ++i)
        for (std::size_t j = i + 1; j < shown.size(); ++j)
            if (shown[i]->getBounds().expanded(gap, 0).intersects(shown[j]->getBounds()))
                out.add(shown[i]->getTitle() + "/" + shown[i]->getName() + " near " + shown[j]->getTitle() + "/" +
                        shown[j]->getName());
    return out;
}
} // namespace fd::testing
