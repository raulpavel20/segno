#include "ui/RackPanel.h"

#include "model/Taper.h"
#include "ui/ChannelControls.h"

namespace fd::app
{
// ---- the panel ------------------------------------------------------------------------------

RackPanel::RackPanel(ui::DeviceUiContext& context) : DevicePanel(context)
{
    auto& macros = addSection("macros", "Macros", 4);
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
    {
        auto& knob = macros.knob(static_cast<size_t>(i), i % 4, i < 4 ? 0 : 3);
        knob.setSourceHandle(true);
        knob.onHandleDrag = [this, i](const juce::MouseEvent& e)
        {
            if (onMacroDrag)
                onMacroDrag(i, e);
        };
        knob.onHandleRelease = [this, i](const juce::MouseEvent& e)
        {
            if (onMacroDrop)
                onMacroDrop(i, e);
        };
        knob.onHandleHover = [this, i](bool hovered)
        {
            if (onMacroHover)
                onMacroHover(i, hovered);
        };
        knob.onHandleDoubleClick = [this, i] { renameMacro(i); };
        // Its right-click: rename it, edit the macros, then its automation as any parameter's.
        knob.onPopupMenu = [this, &knob, i]
        {
            ui::showParamMenu(this->context(), static_cast<size_t>(i), knob,
                              [this, i](juce::PopupMenu& menu)
                              {
                                  menu.addItem(juce::String::fromUTF8("Rename macro\xe2\x80\xa6"),
                                               [this, i] { renameMacro(i); });
                                  menu.addItem("Edit macros",
                                               [this, i]
                                               {
                                                   if (onEditMacros)
                                                       onEditMacros(i);
                                               });
                              });
        };
        macros_[static_cast<size_t>(i)] = &knob;
    }

    chains_ = &addSection("chains", "Chains", 4);
    showSectionWhen(*chains_, [this] { return rack_.chains.size() > 1; });
    for (int i = 0; i < maxChains; ++i)
    {
        auto& on = on_[static_cast<size_t>(i)];
        on.setClickingTogglesState(true);
        on.setTooltip("Turn the chain on or off");
        on.onClick = [this, i, &on]
        {
            if (onChainOn)
                onChainOn(i, on.getToggleState());
        };
        auto& solo = solo_[static_cast<size_t>(i)];
        solo.setButtonText("S");
        solo.setClickingTogglesState(true);
        solo.setTooltip("Solo: hear this chain alone while you set the rack up");
        solo.onClick = [this, i, &solo]
        {
            if (onChainSolo)
                onChainSolo(i, solo.getToggleState());
        };
    }
    addChain_.setTooltip("Add a chain beside these: an empty chain passes the sound through");
    addChain_.onClick = [this]
    {
        if (onAddChain)
            onAddChain();
    };

    volume_.setStyle(ui::Knob::Style::Bar);
    volume_.setName("Vol");
    volume_.setTooltip("The shown chain's volume (double-click: 0 dB)");
    volume_.setDefaultValue(static_cast<float>(model::volumePosition(0.0)));
    volume_.textFromValue = [](float v) { return levelText(static_cast<float>(model::volumeDbAt(v))); };
    volume_.onGestureStart = pan_.onGestureStart = [this]
    {
        chainGesture_ = true;
        if (onChainGesture)
            onChainGesture(true);
    };
    volume_.onGestureEnd = pan_.onGestureEnd = [this]
    {
        chainGesture_ = false;
        if (onChainGesture)
            onChainGesture(false);
    };
    volume_.onValueChange = [this](float v)
    {
        if (onChainVolume)
            onChainVolume(rack_.shownChain, static_cast<float>(model::volumeDbAt(v)));
    };
    pan_.setStyle(ui::Knob::Style::Bar);
    pan_.setName("Pan");
    pan_.setTooltip("The shown chain's pan, a balance (double-click: centre)");
    pan_.setArcOrigin(0.5f);
    pan_.setDefaultValue(0.5f);
    pan_.textFromValue = [](float v) { return panText(v * 2.0f - 1.0f); };
    pan_.onValueChange = [this](float v)
    {
        if (onChainPan)
            onChainPan(rack_.shownChain, v * 2.0f - 1.0f);
    };
    placeChains();
}

RackPanel::~RackPanel() = default;

void RackPanel::setRack(const juce::String& name, const model::RackInfo& rack)
{
    rack_ = rack;
    rackName_ = name;
    setDisplayName(name);
    setFoldedDetail(rackContents(rack_));
    placeChains();
    updateMacros();
    if (!updateShownSections())
        resized();
}

void RackPanel::placeChains()
{
    const int count = std::min(static_cast<int>(rack_.chains.size()), maxChains);
    juce::StringArray names;
    for (int i = 0; i < count; ++i)
        names.add(juce::String::fromUTF8(rack_.chains[static_cast<size_t>(i)].name.c_str()));
    if (names != placedNames_ || names_ == nullptr || rack_.shownChain != placedShown_)
    {
        // A row a chain, "+ Chain" under the names while there's room, the shown chain's mixer
        // at the bottom.
        chains_->clear();
        names_ = std::make_unique<ui::Selector>(names, 1);
        names_->onSelect = [this](int i)
        {
            if (onShowChain)
                onShowChain(i);
        };
        for (int i = 0; i < count; ++i)
        {
            auto* name = names_->buttons()[i];
            name->setTooltip(chainLabel(rack_, i) + ": choose it to show its modules in the rack's frame");
            name->onPopupMenu = [this, i]
            {
                if (onChainMenu)
                    onChainMenu(i);
            };
        }
        for (int i = 0; i < maxChains; ++i)
        {
            on_[static_cast<size_t>(i)].setVisible(i < count);
            solo_[static_cast<size_t>(i)].setVisible(i < count);
            if (i < count)
            {
                chains_->place(on_[static_cast<size_t>(i)], 0, i, 1, 1, ui::PanelSection::Kind::Line);
                chains_->place(solo_[static_cast<size_t>(i)], 3, i, 1, 1, ui::PanelSection::Kind::Line);
            }
        }
        if (count > 0)
            chains_->place(*names_, 1, 0, 2, count, ui::PanelSection::Kind::Line);
        addChain_.setVisible(count < maxChains);
        if (count < maxChains)
            chains_->place(addChain_, 1, count, 2, 1, ui::PanelSection::Kind::Line);
        // The shown chain's mixer, under its name.
        const int mixer = ui::Theme::current().deviceGrid.rows - 1;
        if (count > 0)
            chains_->label(names[std::clamp(rack_.shownChain, 0, count - 1)], 0, mixer - 1, 4);
        chains_->place(volume_, 0, mixer, 2, 1, ui::PanelSection::Kind::Line);
        chains_->place(pan_, 2, mixer, 2, 1, ui::PanelSection::Kind::Line);
        placedNames_ = names;
        placedShown_ = rack_.shownChain;
    }
    const auto& t = ui::Theme::current();
    names_->setSelected(rack_.shownChain);
    for (int i = 0; i < count; ++i)
    {
        const auto& chain = rack_.chains[static_cast<size_t>(i)];
        on_[static_cast<size_t>(i)].setToggleState(chain.on, juce::dontSendNotification);
        solo_[static_cast<size_t>(i)].setToggleState(chain.solo, juce::dontSendNotification);
        // An off chain recedes, here and on its frame (racks.md §5a).
        names_->buttons()[i]->setAlpha(chain.on ? 1.0f : t.roles.dimAlpha);
    }
    if (!chainGesture_ && rack_.shownChain >= 0 && rack_.shownChain < static_cast<int>(rack_.chains.size()))
    {
        const auto& shown = rack_.chains[static_cast<size_t>(rack_.shownChain)];
        volume_.setValue(static_cast<float>(model::volumePosition(shown.volumeDb)),
                         juce::dontSendNotification);
        pan_.setValue(0.5f + 0.5f * shown.pan, juce::dontSendNotification);
    }
}

void RackPanel::updateMacros()
{
    const auto& t = ui::Theme::current();
    for (size_t i = 0; i < macros_.size(); ++i)
    {
        const auto& macro = rack_.macros[i];
        auto& knob = *macros_[i];
        knob.setName(macro.name.empty()
                         ? juce::String(std::string(engine::Session::rackMacros().params[i].name))
                         : juce::String::fromUTF8(macro.name.c_str()));
        knob.setSourceHandle(true, static_cast<int>(i) == litMacro_);
        // One that maps nothing yet recedes: turning it does nothing (racks.md §5a).
        knob.setAlpha(macro.mappings.empty() && static_cast<int>(i) != litMacro_ ? t.roles.fadedAlpha : 1.0f);
    }
}

void RackPanel::renameMacro(int macro)
{
    if (macro < 0 || macro >= model::RackInfo::macroCount || isFolded())
        return;
    auto& knob = *macros_[static_cast<size_t>(macro)];
    const auto strip = knob.nameStrip().toNearestInt() + getLocalPoint(&knob, juce::Point<int>());
    nameEditor_.show(strip, knob.getName(), ui::TextStyle::Label,
                     [this, macro](const juce::String& name)
                     {
                         if (onRenameMacro)
                             onRenameMacro(macro, name);
                     });
}

void RackPanel::renameChain(int chain)
{
    if (names_ == nullptr || chain < 0 || chain >= names_->buttons().size() || isFolded())
        return;
    auto* button = names_->buttons()[chain];
    nameEditor_.show(getLocalArea(button, button->getLocalBounds()), button->getButtonText(),
                     ui::TextStyle::Control,
                     [this, chain](const juce::String& name)
                     {
                         if (onRenameChain)
                             onRenameChain(chain, name);
                     });
}

void RackPanel::renameRack()
{
    if (isFolded())
        return;
    // The header's name, as wide as the room it has.
    nameEditor_.show(
        nameArea().withWidth(std::max(nameArea().getWidth(), ui::Theme::current().deviceGrid.width(3))),
        rackName_, ui::TextStyle::Title,
        [this](const juce::String& name)
        {
            if (onRenameRack)
                onRenameRack(name);
        });
}

void RackPanel::setLitMacro(int macro)
{
    if (macro == litMacro_)
        return;
    litMacro_ = macro;
    updateMacros();
}

// ---- the frame ------------------------------------------------------------------------------

juce::String chainLabel(const model::RackInfo& rack, int chain)
{
    const auto number = "Chain " + juce::String(chain + 1);
    if (chain < 0 || chain >= static_cast<int>(rack.chains.size()))
        return number;
    const auto name = juce::String::fromUTF8(rack.chains[static_cast<size_t>(chain)].name.c_str());
    return name.isEmpty() || name == number ? number : number + juce::String::fromUTF8(" \xc2\xb7 ") + name;
}

juce::String rackContents(const model::RackInfo& rack)
{
    if (rack.chains.size() > 1)
        return juce::String(static_cast<int>(rack.chains.size())) + " chains";
    const size_t modules = rack.chains.empty() ? 0 : rack.chains.front().devices.size();
    return modules == 1 ? juce::String("1 module") : juce::String(static_cast<int>(modules)) + " modules";
}

void paintRackFrame(juce::Graphics& g, const RackFrame& frame)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.roles.rackFrame);
    g.fillRoundedRectangle(frame.bounds.toFloat(), t.radiusPanel);
    // Its edge: in `active` while a drop would land in it.
    g.setColour(frame.dropInside ? t.active : t.line);
    g.drawRoundedRectangle(frame.bounds.toFloat().reduced(0.5f), t.radiusPanel, 1.0f);
    if (frame.text.isEmpty())
        return;
    auto text = frame.text;
    if (frame.chainOff)
        text += juce::String::fromUTF8(" \xc2\xb7 off");
    ui::paintCapsLabel(g, text, frame.label.toFloat());
    if (frame.soloed)
    {
        const auto width = juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), text.toUpperCase());
        ui::paintCapsLabel(
            g, "Solo",
            frame.label.toFloat().withTrimmedLeft(std::ceil(width) + static_cast<float>(t.shell.groupGap)),
            juce::Justification::centredLeft, t.active);
    }
}
} // namespace fd::app
