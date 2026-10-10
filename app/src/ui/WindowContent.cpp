#include "ui/WindowContent.h"

namespace fd::app
{
WindowContent::WindowContent(engine::Session& session, mcp::McpServer* assistants, bool titleBar)
    : shell_(session, assistants)
{
    if (titleBar)
    {
        titleBar_ = std::make_unique<TitleBar>(shell_.menus());
        addAndMakeVisible(*titleBar_);
        shell_.onOpenMenu = [this](int index) { titleBar_->showMenu(index); };
        shell_.onMnemonicsShown = [this](bool shown) { titleBar_->setMnemonicsShown(shown); };
        titleBar_->setTitle(shell_.projectName(), shell_.hasUnsavedChanges());
    }
    shell_.onTitleChanged = [this](const juce::String& title)
    {
        if (titleBar_ != nullptr)
            titleBar_->setTitle(shell_.projectName(), shell_.hasUnsavedChanges());
        if (onTitleChanged)
            onTitleChanged(title);
    };
    addAndMakeVisible(shell_);
    setSize(shell_.getWidth(),
            shell_.getHeight() + (titleBar_ != nullptr ? ui::Theme::current().shell.titleBarHeight : 0));
}

void WindowContent::setOwnsWindow(bool owns)
{
    if (titleBar_ != nullptr)
        titleBar_->setOwnsWindow(owns);
}

void WindowContent::setWindowState(bool maximised, bool fullScreen)
{
    if (titleBar_ != nullptr)
        titleBar_->setWindowState(maximised, fullScreen);
}

juce::Component::WindowControlKind WindowContent::controlAt(juce::Point<float> p) const
{
    using K = juce::Component::WindowControlKind;
    if (titleBar_ == nullptr || !titleBar_->ownsWindow() || !titleBar_->getBounds().toFloat().contains(p))
        return K::client;
    switch (titleBar_->partAt((p - titleBar_->getPosition().toFloat()).toInt()))
    {
    case TitleBar::Part::Caption:
        return K::caption;
    case TitleBar::Part::Minimise:
        return K::minimise;
    case TitleBar::Part::Maximise:
        return K::maximise;
    case TitleBar::Part::Close:
        return K::close;
    case TitleBar::Part::Client:
        break;
    }
    return K::client;
}

void WindowContent::resized()
{
    auto r = getLocalBounds();
    if (titleBar_ != nullptr)
        titleBar_->setBounds(r.removeFromTop(ui::Theme::current().shell.titleBarHeight));
    shell_.setBounds(r);
}
} // namespace fd::app
