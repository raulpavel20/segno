namespace fd::ui
{
namespace
{
/// The component keeping the focus (one at a time: the latest ask). A field that's told it lost
/// the focus lost it for real (a click, Tab): the window's hand-back never tells it.
class KeptFocus final : private juce::FocusChangeListener, private juce::TextEditor::Listener
{
public:
    void keep(juce::Component& component)
    {
        stop();
        target_ = &component;
        since_ = juce::Time::getCurrentTime();
        press_ = lastPress(); // the press that asked, if any, is in already
        juce::Desktop::getInstance().addFocusChangeListener(this);
        listening_ = true;
        component.grabKeyboardFocus();
        if (auto* field = dynamic_cast<juce::TextEditor*>(&component); field != nullptr && target_ != nullptr)
        {
            field_ = field;
            field->addListener(this);
        }
    }

    [[nodiscard]] bool tookOnThisPress(const juce::Component& component) const
    {
        return target_.getComponent() == &component && press_ == lastPress();
    }

    void stop()
    {
        target_ = nullptr;
        if (auto* field = field_.getComponent())
            field->removeListener(this);
        field_ = nullptr;
        if (listening_)
            juce::Desktop::getInstance().removeFocusChangeListener(this);
        listening_ = false;
    }

private:
    void globalFocusChanged(juce::Component* focused) override
    {
        auto* target = target_.getComponent();
        if (target == nullptr || !target->isShowing() || lastPress() != press_ ||
            juce::Time::getCurrentTime() > since_ + juce::RelativeTime::milliseconds(focusKeptFor))
            return stop(); // gone, or the user moves the focus from here on
        if (focused == nullptr || focused == target || target->isParentOf(focused) ||
            focused->getTopLevelComponent() != target->getTopLevelComponent())
            return; // the window lost it, or another window (a menu) has it: wait
        target->grabKeyboardFocus();
    }

    void textEditorFocusLost(juce::TextEditor& field) override
    {
        if (!field.hasKeyboardFocus(false)) // told late: it may have it again
            stop();
    }

    static juce::Time lastPress()
    {
        return juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
    }

    juce::Component::SafePointer<juce::Component> target_;
    juce::Component::SafePointer<juce::TextEditor> field_;
    juce::Time since_, press_;
    bool listening_ = false;
};

KeptFocus& keptFocus()
{
    static KeptFocus kept; // the desktop outlives every wait (they stop with their window)
    return kept;
}
} // namespace

void takeFocus(juce::Component& component)
{
    if (component.isShowing())
        keptFocus().keep(component);
}

bool tookFocusOnThisPress(const juce::Component& component)
{
    return keptFocus().tookOnThisPress(component);
}

EndEditingOnClickAway::EndEditingOnClickAway(juce::Component& home) : home_(home)
{
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    juce::Desktop::getInstance().addFocusChangeListener(this);
}

EndEditingOnClickAway::~EndEditingOnClickAway()
{
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void EndEditingOnClickAway::clickedOn(juce::Component& component)
{
    auto* field = dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent());
    if (field == nullptr || !home_.isParentOf(field))
        return; // nothing being typed here
    if (field != settled_.getComponent() || tookFocusOnThisPress(*field))
        return; // this press put the focus there
    if (&component == field || field->isParentOf(&component))
        return;
    if (component.getTopLevelComponent() != field->getTopLevelComponent())
        return; // another window: a menu, a plugin's editor
    home_.grabKeyboardFocus();
}

void EndEditingOnClickAway::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent != nullptr)
        clickedOn(*e.eventComponent);
}

void EndEditingOnClickAway::globalFocusChanged(juce::Component* focused)
{
    settled_ = focused;
}
} // namespace fd::ui
