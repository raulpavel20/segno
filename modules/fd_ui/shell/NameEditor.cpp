namespace fd::ui
{
NameEditor::NameEditor(juce::Component& host) : host_(host)
{
    const auto& t = Theme::current();
    field_.setColour(juce::TextEditor::backgroundColourId, t.surface2);
    field_.setColour(juce::TextEditor::textColourId, t.textPrimary);
    field_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    field_.setColour(juce::TextEditor::focusedOutlineColourId, t.roles.focusRing);
    field_.setColour(juce::TextEditor::highlightColourId, t.roles.textHighlight);
    field_.setIndents(t.shell.fieldTextInset, 0);
    field_.setJustification(juce::Justification::centredLeft);
    field_.setMultiLine(false);
    field_.setReturnKeyStartsNewLine(false);
    field_.setTabKeyUsedAsCharacter(false);
    field_.onReturnKey = [this] { finish(true); };
    field_.onEscapeKey = [this] { finish(false); };
    field_.onFocusLost = [this] { finish(true); };
    field_.addKeyListener(this);
    host_.addChildComponent(field_);
}

NameEditor::~NameEditor()
{
    field_.removeKeyListener(this);
    field_.onFocusLost = nullptr;
}

void NameEditor::show(juce::Rectangle<int> area, const juce::String& name, TextStyle style,
                      std::function<void(const juce::String&)> commit, std::function<void(int)> tab)
{
    if (isEditing())
        finish(true);
    original_ = name;
    commit_ = std::move(commit);
    tab_ = std::move(tab);
    field_.setFont(font(style));
    field_.setText(name, false);
    field_.setBounds(area);
    field_.setVisible(true);
    field_.toFront(false);
    if (field_.isShowing())
        field_.grabKeyboardFocus();
    field_.selectAll();
}

void NameEditor::cancel()
{
    finish(false);
}

bool NameEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key.getKeyCode() != juce::KeyPress::tabKey)
        return false;
    finish(true, key.getModifiers().isShiftDown() ? -1 : 1);
    return true;
}

void NameEditor::finish(bool keep, int tab)
{
    if (finishing_ || !field_.isVisible())
        return;
    finishing_ = true; // hiding it moves the focus, which would finish it again
    const auto name = field_.getText().trim();
    auto commit = std::move(commit_);
    auto next = std::move(tab_);
    field_.setVisible(false);
    if (host_.isShowing())
        host_.grabKeyboardFocus();
    finishing_ = false;
    if (keep && name.isNotEmpty() && name != original_ && commit)
        commit(name);
    if (tab != 0 && next)
        next(tab);
}
} // namespace fd::ui
