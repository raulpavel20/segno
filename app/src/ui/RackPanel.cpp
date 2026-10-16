#include "ui/RackPanel.h"

#include "model/Racks.h"
#include "model/Taper.h"
#include "ui/ChannelControls.h"

namespace fd::app
{
// ---- the panel ------------------------------------------------------------------------------

RackPanel::RackPanel(ui::DeviceUiContext& context) : MacroPanel(context)
{
    addMacroKnobs(addSection("macros", "Macros", 4));

    // SNAPSHOTS, from the first (racks.md §18.2): a click recalls one, a double-click renames it.
    snapshotsSection_ = &addSection("snapshots", "Snapshots", 4);
    showSectionWhen(*snapshotsSection_, [this] { return !rack_.snapshots.empty(); });
    for (int i = 0; i < maxSnapshots; ++i)
    {
        auto& snapshot = snapshots_[static_cast<size_t>(i)];
        snapshot.setClickingTogglesState(false); // lit while it's the current one
        snapshot.setCutsText(true);
        snapshot.onClick = [this, i]
        {
            if (onRecallSnapshot)
                onRecallSnapshot(i);
        };
        snapshot.onPopupMenu = [this, i]
        {
            if (onSnapshotMenu)
                onSnapshotMenu(i);
        };
        snapshot.onDoubleClick = [this, i] { renameSnapshot(i); };
    }
    store_.setClickingTogglesState(false);
    store_.setTooltip("Store a snapshot: where the eight macros are now");
    store_.onClick = [this]
    {
        if (onStoreSnapshot)
            onStoreSnapshot();
    };

    // MORPH, from the second (§18.3): on or off, the knob, the snapshots it goes between.
    morphSection_ = &addSection("morph", "Morph", 2);
    showSectionWhen(*morphSection_, [this] { return rack_.snapshots.size() >= 2; });
    morphSection_->setPowerSwitch([this] { return rack_.morph.on; },
                                  [this](bool on)
                                  {
                                      rack_.morph.on = on; // shown at once; the host's rack follows
                                      if (onMorphOn)
                                          onMorphOn(on);
                                  },
                                  "Morph on or off: while it's on, the macros follow it from A to B");
    morph_ = &morphSection_->knob(static_cast<size_t>(model::RackInfo::morphParam), 0, 0, 2); // over A and B
    // Its name lights the macros it moves while hovered, and is lit while a macro it moves is, as
    // a macro's does with its knobs; it isn't dragged anywhere.
    morph_->setSourceHandle(true);
    morph_->onHandleHover = [this](bool hovered)
    {
        morphHovered_ = hovered;
        updateMacros();
    };
    for (const auto& [box, letter, tip] : {std::tuple{&morphA_, "A", "A: where Morph starts, at 0 %"},
                                           std::tuple{&morphB_, "B", "B: where Morph ends, at 100 %"}})
    {
        box->setInlineName(letter);
        box->setTooltip(tip);
    }
    morphA_.onChange = [this]
    {
        if (onMorphBetween)
            onMorphBetween(morphA_.getSelectedId() - 1, rack_.morph.b);
    };
    morphB_.onChange = [this]
    {
        if (onMorphBetween)
            onMorphBetween(rack_.morph.a, morphB_.getSelectedId() - 1);
    };
    morphSection_->place(morphA_, 0, 3, 2, 1, ui::PanelSection::Kind::Line);
    morphSection_->place(morphB_, 0, 4, 2, 1, ui::PanelSection::Kind::Line);

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
    placeSnapshots();
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

void RackPanel::placeSnapshots()
{
    const int count = std::min(static_cast<int>(rack_.snapshots.size()), maxSnapshots);
    if (count != placedSnapshots_)
    {
        // Two a row, then "+ Store" in the next place while there's room.
        snapshotsSection_->clear();
        for (int i = 0; i < maxSnapshots; ++i)
        {
            snapshots_[static_cast<size_t>(i)].setVisible(i < count);
            if (i < count)
                snapshotsSection_->place(snapshots_[static_cast<size_t>(i)], (i % 2) * 2, i / 2, 2, 1,
                                         ui::PanelSection::Kind::Line);
        }
        store_.setVisible(count < maxSnapshots);
        if (count < maxSnapshots)
            snapshotsSection_->place(store_, (count % 2) * 2, count / 2, 2, 1, ui::PanelSection::Kind::Line);
        placedSnapshots_ = count;
    }
    // The current one lit, marked once the macros have moved from it; none while Morph sets them.
    const bool changed = model::snapshotChanged(rack_);
    const bool morphing = model::morphing(rack_);
    juce::StringArray names;
    for (int i = 0; i < count; ++i)
    {
        auto& button = snapshots_[static_cast<size_t>(i)];
        const auto name = juce::String::fromUTF8(rack_.snapshots[static_cast<size_t>(i)].name.c_str());
        names.add(name);
        const bool current = i == rack_.currentSnapshot && !morphing;
        button.setButtonText(name);
        button.setToggleState(current, juce::dontSendNotification);
        button.setModified(current && changed);
        button.setTooltip(name + (current && changed ? ", changed" : "") +
                          ": click to put the macros where it has them; right-click to rename, update or "
                          "remove it");
    }
    // MORPH's choices: the snapshots by name.
    if (names != morphNames_)
    {
        for (auto* box : {&morphA_, &morphB_})
        {
            box->clear(juce::dontSendNotification);
            for (int i = 0; i < names.size(); ++i)
                box->addItem(names[i], i + 1);
        }
        morphNames_ = names;
    }
    morphA_.setSelectedId(rack_.morph.a + 1, juce::dontSendNotification);
    morphB_.setSelectedId(rack_.morph.b + 1, juce::dontSendNotification);
}

void RackPanel::renameSnapshot(int snapshot)
{
    if (snapshot < 0 || snapshot >= placedSnapshots_ || isFolded())
        return;
    auto& button = snapshots_[static_cast<size_t>(snapshot)];
    nameEditor_.show(getLocalArea(&button, button.getLocalBounds()), button.getButtonText(),
                     ui::TextStyle::Control,
                     [this, snapshot](const juce::String& name)
                     {
                         if (onRenameSnapshot)
                             onRenameSnapshot(snapshot, name);
                     });
}

void RackPanel::setMorphLit(bool lit)
{
    morph_->setSourceHandle(true, lit);
}

void RackPanel::updateMacros()
{
    updateMacroKnobs(rack_.macros);
    for (size_t i = 0; i < macros_.size(); ++i)
        macros_[i]->setLocked(rack_.macros[i].locked);
}

void RackPanel::addMacroItems(int macro, juce::PopupMenu& menu)
{
    // Snapshots and locks (racks.md §18).
    menu.addItem("Store snapshot",
                 [this]
                 {
                     if (onStoreSnapshot)
                         onStoreSnapshot();
                 });
    const bool locked = rack_.macros[static_cast<size_t>(macro)].locked;
    menu.addItem("Lock macro", true, locked,
                 [this, macro, locked]
                 {
                     if (onLockMacro)
                         onLockMacro(macro, !locked);
                 });
}

bool RackPanel::macroLit(int macro) const
{
    return morphHovered_ && model::morphSets(rack_, macro); // Morph moving it
}

void RackPanel::macroHovered(int macro, bool hovered)
{
    setMorphLit(hovered && model::morphSets(rack_, macro)); // Morph lit while one it moves is
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
