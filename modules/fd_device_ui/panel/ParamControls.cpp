namespace fd::ui
{
namespace
{
/// A control's tooltip: the parameter's full name and what it does (its description, B22).
juce::String tooltipFor(const ParamSpec& spec)
{
    const juce::String name(std::string(spec.name));
    return spec.description.empty() ? name : name + ": " + juce::String(std::string(spec.description));
}
} // namespace

void addModulationItems(ModulationModel& m, size_t index, juce::PopupMenu& menu,
                        const std::function<std::function<void()>(std::function<void()>)>& whileAlive)
{
    // Its routes, each with Remove.
    for (const auto& r : m.routes())
        if (r.param == index)
        {
            juce::PopupMenu route;
            route.addItem("Remove", whileAlive([&m, r] { m.removeRoute(r.index); }));
            menu.addSubMenu(m.sourceName(r.source) + juce::String::fromUTF8(" \u2192 ") +
                                juce::String(juce::roundToInt(r.amount * 100.0f)) + " %",
                            route);
        }
    // A route's own control (a matrix row's): remove the route.
    if (const auto route = m.routeOfParam(index))
        menu.addItem("Remove route", whileAlive([&m, r = *route] { m.removeRoute(r); }));
    // Modulate with: the sources by group.
    if (!m.canModulate(index))
        return;
    const auto sources = m.sources();
    if (sources.empty())
        return;
    juce::PopupMenu with;
    if (m.isFull())
        with.addItem("The matrix is full: remove a route first", false, false, [] {});
    else
        for (size_t i = 0; i < sources.size();)
        {
            juce::PopupMenu group;
            const auto name = sources[i].group;
            for (; i < sources.size() && sources[i].group == name; ++i)
                group.addItem(sources[i].name,
                              whileAlive([&m, id = sources[i].id, index] { m.addRoute(id, index); }));
            with.addSubMenu(name, group);
        }
    menu.addSubMenu("Modulate with", with);
}

void showParamMenu(DeviceUiContext& context, size_t index, juce::Component& control,
                   const std::function<void(juce::PopupMenu&)>& extra)
{
    auto& params = context.params;
    juce::Component::SafePointer<juce::Component> alive(&control); // the menu outlives a closed panel
    auto whileAlive = [alive](std::function<void()> action) {
        return [safe = alive, run = std::move(action)] {
            if (safe != nullptr)
                run();
        };
    };
    juce::PopupMenu menu;
    // The panel's own items first (a macro's Rename).
    if (extra)
    {
        extra(menu);
        if (menu.getNumItems() > 0)
            menu.addSeparator();
    }
    if (params.canAutomate(index))
    {
        params.addAutomationItems(index, menu); // they act through the session, not the panel
        if (menu.getNumItems() > 0)
            menu.addSeparator();
    }
    // A rack's macros (racks.md §4): Map to macro, or Unmap.
    {
        const int before = menu.getNumItems();
        params.addMacroItems(index, menu);
        if (menu.getNumItems() > before)
            menu.addSeparator();
    }
    // Its modulation (device-panels.md, "Modulation").
    if (context.modulation != nullptr)
    {
        const int before = menu.getNumItems();
        addModulationItems(*context.modulation, index, menu, whileAlive);
        if (menu.getNumItems() > before)
            menu.addSeparator();
    }
    const auto def = context.info.params[index].def;
    menu.addItem("Reset to default", whileAlive([&params, index, def] {
                     params.beginGesture(index);
                     params.setPlain(index, def);
                     params.endGesture(index);
                 }));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&control));
}

ParamKnob::ParamKnob(DeviceUiContext& context, size_t paramIndex) : context_(context), index_(paramIndex)
{
    // Its ring sets the amount of a route that moves it (device-panels.md, "Modulation").
    onRingStart = [this]
    {
        auto* m = context_.modulation;
        const auto route = m != nullptr ? m->routeToDrag(index_) : std::nullopt;
        ringRoute_ = route ? std::optional<int>(route->index) : std::nullopt;
        if (route)
        {
            ringAmount_ = route->amount;
            m->beginAmount(route->index);
        }
    };
    onRingDrag = [this](float delta)
    {
        if (ringRoute_ && context_.modulation != nullptr)
        {
            ringAmount_ = std::clamp(ringAmount_ + delta, -1.0f, 1.0f);
            context_.modulation->setAmount(*ringRoute_, ringAmount_);
            refresh();
        }
    };
    onRingEnd = [this]
    {
        if (ringRoute_ && context_.modulation != nullptr)
            context_.modulation->endAmount(*ringRoute_);
        ringRoute_.reset();
    };
    bind(paramIndex);
}

void ParamKnob::setModulationSource(int source)
{
    source_ = source;
    auto panel = [this] { return findParentComponentOfClass<DevicePanel>(); };
    onHandleHover = [this](bool hovered)
    {
        if (auto* m = context_.modulation)
        {
            if (hovered)
                m->hoveredSource = m->lastSource = source_;
            else if (m->hoveredSource == source_)
                m->hoveredSource = -1;
        }
    };
    onHandleDrag = [this, panel](const juce::MouseEvent& e)
    {
        if (auto* p = panel())
            p->dragSource(source_, e.getEventRelativeTo(p).getPosition());
    };
    onHandleRelease = [this, panel](const juce::MouseEvent& e)
    {
        if (auto* p = panel())
            p->dropSource(source_, e.getEventRelativeTo(p).getPosition(), !e.mouseWasDraggedSinceMouseDown());
    };
    refresh();
}

void ParamKnob::mouseEnter(const juce::MouseEvent& e)
{
    if (auto* m = context_.modulation)
        m->hoveredParam = index_;
    Knob::mouseEnter(e);
}

void ParamKnob::mouseExit(const juce::MouseEvent& e)
{
    if (auto* m = context_.modulation; m != nullptr && m->hoveredParam == index_)
        m->hoveredParam.reset();
    Knob::mouseExit(e);
}

void ParamKnob::bind(size_t paramIndex)
{
    index_ = paramIndex;
    const ParamSpec& spec = context_.info.params[index_];
    setName(juce::String(std::string(spec.shortName.empty() ? spec.name : spec.shortName)));
    setTooltip(tooltipFor(spec));
    setDefaultValue(spec.toNormalized(spec.def));
    setNumSteps(spec.numSteps());
    // Bipolar arcs (from the centre) for ranges around zero: pan, bias, and symmetric gains
    // such as +-12 dB. Asymmetric dB ranges (e.g. -70..+35) fill from the minimum.
    const bool symmetric = std::abs(spec.min + spec.max) < 1e-6f;
    const bool bipolar = spec.min < 0.0f && spec.max > 0.0f && (spec.unit != Unit::Decibels || symmetric);
    setArcOrigin(bipolar ? spec.toNormalized(0.0f) : 0.0f);

    textFromValue = [&spec](float n) { return juce::String(spec.toText(spec.fromNormalized(n))); };
    valueFromText = [&spec](const juce::String& text) -> std::optional<float> {
        if (auto v = spec.fromText(text.toStdString()))
            return spec.toNormalized(*v);
        return std::nullopt;
    };
    onValueChange = [this, &spec](float n) { context_.params.setPlain(index_, spec.fromNormalized(n)); };
    onGestureStart = [this] { context_.params.beginGesture(index_); };
    onGestureEnd = [this] { context_.params.endGesture(index_); };
    onPopupMenu = [this] { showParamMenu(context_, index_, *this); };
    refresh();
    repaint(); // the name and value text, where the value hasn't moved
}

void ParamKnob::follow(std::function<size_t()> which)
{
    which_ = std::move(which);
    refresh();
}

void ParamKnob::refresh()
{
    if (const size_t now = which_ ? which_() : index_; now != index_)
    {
        bind(now); // which refreshes
        return;
    }
    const ParamSpec& spec = context_.info.params[index_];
    setValue(spec.toNormalized(context_.params.getPlain(index_)), juce::dontSendNotification);
    setAutomationState(context_.params.automationState(index_));
    // Set by a rack's macro (racks.md §4): its range as a ring, and no dragging by hand.
    const auto mapping = context_.params.macroMapping(index_);
    // Moved by the track's modulators (modulators.md §7.4): their range, and where it is now.
    const auto moved = context_.params.modulated(index_);
    setFollowing(mapping.has_value());
    juce::String tooltip = tooltipFor(spec);
    if (moved)
        tooltip = "Moved by " + moved->by + ". " + tooltip;
    if (mapping)
        tooltip = "Set by " + mapping->macro + ". " + tooltip;
    setTooltip(tooltip);
    if (mapping)
        setRing(Ring{std::min(mapping->from, mapping->to), std::max(mapping->from, mapping->to),
                     mapping->dimmed});
    else if (moved)
        setRing(Ring{moved->from, moved->to, moved->dimmed, moved->now});
    else if (context_.modulation == nullptr)
        setRing(std::nullopt);
    // A rack's macro dragged over the module (racks.md §4), as a modulation source is; a
    // modulator's handle lands on any control, a bar too (modulators.md §7.1).
    const auto macro = context_.params.macroDrag(index_);
    const auto modulator = context_.params.modulatorDrag(index_);
    if (context_.modulation == nullptr)
    {
        setDropTarget(macro.target || modulator.target);
        setDropCandidate((macro.candidate && style() == Style::Rotary) || modulator.candidate);
    }
    if (const auto* m = context_.modulation)
    {
        if (!mapping && !moved) // the track's modulators' ring over the module's own routes'
            setRing(m->ringFor(index_, getValue()));
        setDropTarget(m->dropParam == index_ || macro.target);
        setDropCandidate(style() == Style::Rotary &&
                         ((m->draggedSource >= 0 && m->canModulate(index_)) || macro.candidate));
        if (source_ >= 0)
            setSourceHandle(true, m->hoveredSource == source_ || m->draggedSource == source_ ||
                                      m->movesHovered(source_));
    }
}

ParamToggle::ParamToggle(DeviceUiContext& context, size_t paramIndex)
    : Toggle(juce::String(std::string(context.info.params[paramIndex].shortName.empty()
                                          ? context.info.params[paramIndex].name
                                          : context.info.params[paramIndex].shortName))),
      context_(context), index_(paramIndex)
{
    setTooltip(tooltipFor(context.info.params[paramIndex]));
    onPopupMenu = [this] { showParamMenu(context_, index_, *this); };
    refresh();
}

void ParamToggle::refresh()
{
    setToggleState(context_.params.getPlain(index_) >= 0.5f, juce::dontSendNotification);
    setAutomationState(context_.params.automationState(index_));
    // Moved by the track's modulators (modulators.md §7.3): turned while they have it the other way.
    const auto moved = context_.params.modulated(index_);
    setModulation(
        moved ? std::optional<Modulation>(Modulation{(moved->now >= 0.5f) != getToggleState(), moved->dimmed})
              : std::nullopt);
    const auto drag = context_.params.modulatorDrag(index_);
    setDropTarget(drag.target);
    setDropCandidate(drag.candidate);
    const auto& spec = context_.info.params[index_];
    setTooltip(moved ? "Moved by " + moved->by + ". " + tooltipFor(spec) : tooltipFor(spec));
}

void ParamToggle::clicked()
{
    context_.params.beginGesture(index_);
    context_.params.setPlain(index_, getToggleState() ? 1.0f : 0.0f);
    context_.params.endGesture(index_);
}

ParamSegmented::ParamSegmented(DeviceUiContext& context, size_t paramIndex, int columns)
    : context_(context), index_(paramIndex), columns_(std::max(1, columns))
{
    const ParamSpec& spec = context_.info.params[index_];
    constexpr int groupId = 1; // radio groups are scoped to siblings, i.e. this component
    for (size_t i = 0; i < spec.choices.size(); ++i)
    {
        auto* b = buttons_.add(new Toggle(juce::String(std::string(spec.choices[i]))));
        b->setRadioGroupId(groupId);
        b->setTooltip(tooltipFor(spec));
        b->onClick = [this, i] {
            context_.params.beginGesture(index_);
            context_.params.setPlain(index_, static_cast<float>(i));
            context_.params.endGesture(index_);
        };
        b->onPopupMenu = [this] { showParamMenu(context_, index_, *this); };
        addAndMakeVisible(b);
    }
    refresh();
}

void ParamSegmented::refresh()
{
    const int selected = static_cast<int>(context_.params.getPlain(index_));
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setToggleState(i == selected, juce::dontSendNotification);
    if (const auto state = context_.params.automationState(index_); state != automation_)
    {
        automation_ = state;
        repaint();
    }
}

void ParamSegmented::paintOverChildren(juce::Graphics& g)
{
    paintAutomationIndicator(g, {3.5f, 3.5f}, automation_);
}

void layoutSegments(const juce::OwnedArray<Toggle>& buttons, int columns, juce::Rectangle<int> area)
{
    // The grid's own gaps (docs/design/device-panels.md): segments fill whole cells when the
    // span is a multiple of the buttons per row, and each line of buttons is one row tall.
    const auto& grid = Theme::current().deviceGrid;
    columns = std::max(1, columns);
    const int rows = (buttons.size() + columns - 1) / columns;
    const int w = (area.getWidth() - grid.columnGap * (columns - 1)) / columns;
    const int h = (area.getHeight() - grid.rowGap * (rows - 1)) / std::max(1, rows);
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds(area.getX() + (i % columns) * (w + grid.columnGap),
                              area.getY() + (i / columns) * (h + grid.rowGap), w, h);
}

void ParamSegmented::resized()
{
    layoutSegments(buttons_, columns_, getLocalBounds());
}

Selector::Selector(juce::StringArray choices, int columns) : columns_(std::max(1, columns))
{
    constexpr int groupId = 1; // radio groups are scoped to siblings, i.e. this component
    for (int i = 0; i < choices.size(); ++i)
    {
        auto* b = buttons_.add(new Toggle(choices[i]));
        b->setRadioGroupId(groupId);
        b->onClick = [this, i]
        {
            if (i != selected_)
            {
                selected_ = i;
                if (onSelect)
                    onSelect(i);
            }
        };
        addAndMakeVisible(b);
    }
    setSelected(0);
}

void Selector::setSelected(int index, juce::NotificationType notification)
{
    selected_ = std::clamp(index, 0, std::max(0, buttons_.size() - 1));
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setToggleState(i == selected_, juce::dontSendNotification);
    if (notification != juce::dontSendNotification && onSelect)
        onSelect(selected_);
}

void Selector::resized()
{
    layoutSegments(buttons_, columns_, getLocalBounds());
}

ParamComboBox::ParamComboBox(DeviceUiContext& context, size_t paramIndex) : context_(context), index_(paramIndex)
{
    const ParamSpec& spec = context_.info.params[index_];
    for (size_t i = 0; i < spec.choices.size(); ++i)
        addItem(juce::String(std::string(spec.choices[i])), static_cast<int>(i) + 1);
    setTooltip(tooltipFor(spec));
    onChange = [this] {
        context_.params.beginGesture(index_);
        context_.params.setPlain(index_, static_cast<float>(getSelectedId() - 1));
        context_.params.endGesture(index_);
    };
    refresh();
}

void ParamComboBox::refresh()
{
    setSelectedId(static_cast<int>(context_.params.getPlain(index_)) + 1, juce::dontSendNotification);
    if (const auto state = context_.params.automationState(index_); state != automation_)
    {
        automation_ = state;
        repaint();
    }
}

void ChoiceBox::setInlineName(juce::String name)
{
    inlineName_ = std::move(name);
    resized();
    repaint();
}

void ChoiceBox::paintOverChildren(juce::Graphics& g)
{
    if (const auto name = inlineName(); name.isNotEmpty())
    {
        // Its name where a bar's is, its choice after it.
        const auto& t = Theme::current();
        g.setColour(t.textSecondary);
        g.setFont(font(TextStyle::Label));
        g.drawText(name, getLocalBounds().withTrimmedLeft(t.shell.fieldTextInset),
                   juce::Justification::centredLeft, false);
    }
}

void ChoiceBox::resized()
{
    juce::ComboBox::resized();
    const auto name = inlineName();
    if (name.isEmpty())
        return;
    // The choice moves over by the name and a text button's padding.
    const auto& t = Theme::current();
    const int width =
        static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Label), name)));
    for (auto* child : getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child))
            label->setBounds(label->getBounds().withTrimmedLeft(width + t.shell.buttonTextPadding));
}

void ParamComboBox::paintOverChildren(juce::Graphics& g)
{
    ChoiceBox::paintOverChildren(g);
    paintAutomationIndicator(g, {3.5f, 3.5f}, automation_);
}

void ParamComboBox::setInlineName(bool shown)
{
    const auto& spec = context_.info.params[index_];
    ChoiceBox::setInlineName(
        shown ? juce::String(std::string(spec.shortName.empty() ? spec.name : spec.shortName))
              : juce::String());
}

void ParamComboBox::showPopup()
{
    if (!menuText_)
    {
        juce::ComboBox::showPopup();
        return;
    }
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    for (int i = 0; i < getNumItems(); ++i)
        menu.addItem(i + 1, menuText_(i), true, getSelectedId() == i + 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                       [safe = juce::Component::SafePointer<ParamComboBox>(this)](int chosen)
                       {
                           if (safe != nullptr)
                               safe->menuClosed(chosen);
                       });
}

void ParamComboBox::menuClosed(int chosen)
{
    // As juce::ComboBox's own menu does: until hidePopup(), the box thinks its menu is still
    // open and ignores every click.
    hidePopup();
    if (chosen > 0)
        setSelectedId(chosen, juce::sendNotificationSync);
}

// ---- the latency badge --------------------------------------------------------------------

LatencyBadge::LatencyBadge(juce::String moduleName, std::function<double()> latencyMs)
    : moduleName_(std::move(moduleName)), latencyMs_(std::move(latencyMs))
{
    setName("latency");
    refresh();
}

void LatencyBadge::refresh()
{
    const int ms = latencyMs_ ? juce::roundToInt(latencyMs_()) : 0;
    const auto text = ms > 0 ? "+" + juce::String(ms) + " ms" : juce::String();
    if (text == text_)
        return;
    text_ = text;
    const auto delay = juce::String(ms) + " ms";
    setTooltip(text_.isEmpty()
                   ? juce::String()
                   : "This " + moduleName_ + " delays its track by " + delay +
                         ". The DAW keeps the tracks in time; what you play live on this track is "
                         "heard " +
                         delay + " late.");
    repaint();
    if (onChange)
        onChange();
}

int LatencyBadge::preferredWidth() const
{
    return text_.isEmpty() ? 0 : widthFor(text_);
}

int LatencyBadge::widthFor(const juce::String& text)
{
    const auto& grid = Theme::current().deviceGrid;
    return static_cast<int>(
               std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), text))) +
           2 * grid.displayInset;
}

void LatencyBadge::paint(juce::Graphics& g)
{
    if (text_.isEmpty())
        return;
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    g.setColour(t.roles.warningFill);
    g.fillRoundedRectangle(r, t.radiusControl);
    g.setColour(t.roles.warning);
    g.setFont(font(TextStyle::Control));
    g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
}
HeaderReadout::HeaderReadout(std::function<juce::String()> text, juce::String widest)
    : textOf_(std::move(text)), widest_(std::move(widest))
{
    setName("readout");
    refresh();
}

void HeaderReadout::refresh()
{
    if (auto text = textOf_ ? textOf_() : juce::String(); text != text_)
    {
        text_ = std::move(text);
        repaint();
    }
}

int HeaderReadout::preferredWidth() const
{
    return static_cast<int>(
        std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::ValueSmall), widest_)));
}

void HeaderReadout::paint(juce::Graphics& g)
{
    g.setColour(Theme::current().textSecondary);
    g.setFont(font(TextStyle::ValueSmall));
    g.drawText(text_, getLocalBounds(), juce::Justification::centredRight, false);
}
} // namespace fd::ui
