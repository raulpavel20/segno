#include "ui/DrumKitPanel.h"

#include "engine/Session.h"
#include "model/DrumKit.h"
#include "model/Taper.h"
#include "ui/ChannelControls.h"

namespace fd::app
{
namespace
{
juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

/// Plays's travel: a note each step, 0 … 127.
constexpr float playsPosition(int note) noexcept
{
    return static_cast<float>(std::clamp(note, 0, 127)) / 127.0f;
}

int playsAt(float position) noexcept
{
    return std::clamp(juce::roundToInt(position * 127.0f), 0, 127);
}
} // namespace

DrumKitPanel::DrumKitPanel(ui::DeviceUiContext& context) : DevicePanel(context)
{
    // PADS: the 16 shown over six columns, all 128 in the seventh.
    auto& pads = addSection("pads", "Pads", 7);
    pads.place(pads_, 0, 0, 6, ui::Theme::current().deviceGrid.rows);
    pads.place(overview_, 6, 0, 1, ui::Theme::current().deviceGrid.rows);
    pads_.onPress = [this](int note)
    {
        if (onSelectPad)
            onSelectPad(note);
        if (onPlayPad)
            onPlayPad(note);
    };
    pads_.onRelease = [this](int note)
    {
        if (onStopPad)
            onStopPad(note);
    };
    pads_.onDoubleClick = [this](int note) { renamePad(note); };
    pads_.onMenu = [this](int note)
    {
        if (onPadMenu)
            onPadMenu(note);
    };
    pads_.onMovePad = [this](int from, int to, bool copy)
    {
        if (onMovePad)
            onMovePad(from, to, copy);
    };
    pads_.onScroll = [this](int rows)
    {
        if (onShowPads)
            onShowPads(model::kit::shownFrom(kit_.padsFrom + rows * model::kit::columns));
    };
    overview_.onShow = [this](int from)
    {
        if (onShowPads)
            onShowPads(model::kit::shownFrom(from));
    };

    // PAD: the selected pad; its power in the title is the pad's on.
    padSection_ = &addSection("pad", "Pad", 2);
    padSection_->setPowerSwitch(
        [this]
        {
            const auto* pad = selectedPad();
            return pad != nullptr && pad->on;
        },
        [this](bool on)
        {
            if (onPadOn && selectedPad() != nullptr)
                onPadOn(kit_.selectedPad, on);
        },
        "The pad on or off: off, it makes no sound");
    for (auto* bar : {&volume_, &pan_, &plays_})
    {
        bar->setStyle(ui::Knob::Style::Bar);
        bar->onGestureStart = [this]
        {
            padGesture_ = true;
            if (onPadGesture)
                onPadGesture(true);
        };
        bar->onGestureEnd = [this]
        {
            padGesture_ = false;
            if (onPadGesture)
                onPadGesture(false);
        };
    }
    volume_.setName("Vol");
    volume_.setTooltip("The pad's volume (double-click: 0 dB)");
    volume_.setDefaultValue(static_cast<float>(model::volumePosition(0.0)));
    volume_.textFromValue = [](float v) { return levelText(static_cast<float>(model::volumeDbAt(v))); };
    volume_.onValueChange = [this](float v)
    {
        if (onPadVolume)
            onPadVolume(kit_.selectedPad, static_cast<float>(model::volumeDbAt(v)));
    };
    pan_.setName("Pan");
    pan_.setTooltip("The pad's pan, a balance (double-click: centre)");
    pan_.setArcOrigin(0.5f);
    pan_.setDefaultValue(0.5f);
    pan_.textFromValue = [](float v) { return panText(v * 2.0f - 1.0f); };
    pan_.onValueChange = [this](float v)
    {
        if (onPadPan)
            onPadPan(kit_.selectedPad, v * 2.0f - 1.0f);
    };
    plays_.setName("Plays");
    plays_.setTooltip("The note the pad plays into its instrument, whatever note it's hit with "
                      "(double-click: C3, where a sample plays at its own pitch)");
    plays_.setNumSteps(128);
    plays_.setDefaultValue(playsPosition(60));
    plays_.textFromValue = [](float v) { return utf8(model::kit::noteName(playsAt(v))); };
    plays_.onValueChange = [this](float v)
    {
        if (onPadPlays)
            onPadPlays(kit_.selectedPad, playsAt(v));
    };
    choke_.setInlineName("Choke");
    choke_.setTooltip("Its choke group: a pad hit stops the others in its group (the closed hat cuts the "
                      "open one off)");
    for (int group = 0; group <= model::kit::chokeGroups; ++group)
        choke_.addItem(utf8(model::kit::chokeName(group)), group + 1);
    choke_.onChange = [this]
    {
        if (onPadChoke && selectedPad() != nullptr)
            onPadChoke(kit_.selectedPad, choke_.getSelectedId() - 1);
    };
    solo_.setClickingTogglesState(true);
    solo_.setTooltip("Solo: hear this pad alone while you set the kit up");
    solo_.onClick = [this]
    {
        if (onPadSolo && selectedPad() != nullptr)
            onPadSolo(kit_.selectedPad, solo_.getToggleState());
    };
    using Kind = ui::PanelSection::Kind;
    padSection_->place(volume_, 0, 0, 2, 1, Kind::Line);
    padSection_->place(pan_, 0, 1, 2, 1, Kind::Line);
    padSection_->place(plays_, 0, 2, 2, 1, Kind::Line);
    padSection_->place(choke_, 0, 3, 2, 1, Kind::Line);
    padSection_->place(solo_, 0, 4, 1, 1, Kind::Line);

    // MACROS, from the first mapping: a rack's eight.
    macrosSection_ = &addSection("macros", "Macros", 4);
    showSectionWhen(*macrosSection_,
                    [this]
                    {
                        return std::any_of(kit_.macros.begin(), kit_.macros.end(),
                                           [](const model::RackMacro& m) { return !m.mappings.empty(); });
                    });
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
    {
        auto& knob = macrosSection_->knob(static_cast<size_t>(i), i % 4, i < 4 ? 0 : 3);
        knob.setSourceHandle(true);
        macros_[static_cast<size_t>(i)] = &knob;
    }
    updatePads();
    updatePad();
}

DrumKitPanel::~DrumKitPanel() = default;

const model::RackChain* DrumKitPanel::selectedPad() const
{
    return model::kit::padOn(kit_, kit_.selectedPad);
}

void DrumKitPanel::setKit(const model::RackInfo& kit)
{
    kit_ = kit;
    kit_.padsFrom = model::kit::shownFrom(kit_.padsFrom);
    setFoldedDetail(utf8(model::kit::contents(kit_)));
    updatePads();
    updatePad();
    updateMacros();
    if (!updateShownSections())
        resized();
}

void DrumKitPanel::setPlaying(const std::bitset<128>& notes)
{
    pads_.setPlaying(notes);
}

void DrumKitPanel::updatePads()
{
    std::array<ui::PadGrid::Pad, ui::PadGrid::shown> shown{};
    for (int i = 0; i < ui::PadGrid::shown; ++i)
    {
        const int note = kit_.padsFrom + i;
        auto& p = shown[static_cast<size_t>(i)];
        p.note = utf8(model::kit::noteName(note));
        if (const auto* pad = model::kit::padOn(kit_, note))
        {
            p.sound = true;
            p.name = utf8(pad->name);
            p.on = pad->on;
            p.solo = pad->solo;
            p.choke = pad->choke;
        }
    }
    pads_.setPads(kit_.padsFrom, shown, kit_.selectedPad);
    overview_.setPads(model::kit::padsWithSounds(kit_), kit_.padsFrom, kit_.selectedPad);
}

void DrumKitPanel::updatePad()
{
    const auto& t = ui::Theme::current();
    const auto* pad = selectedPad();
    padSection_->setSectionTitle("Pad " + juce::String::fromUTF8("\xc2\xb7 ") +
                                 utf8(model::kit::noteName(kit_.selectedPad)));
    // An empty pad has no settings: they're shown, dimmed, until it has a sound.
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&volume_, &pan_, &plays_, &choke_, &solo_})
    {
        c->setEnabled(pad != nullptr);
        c->setAlpha(pad != nullptr ? 1.0f : t.roles.dimAlpha);
    }
    // An empty pad's settings show where a new pad's start: 0 dB, centre, C3, no group.
    const model::RackChain fresh;
    const auto& shown = pad != nullptr ? *pad : fresh;
    if (!padGesture_)
    {
        volume_.setValue(static_cast<float>(model::volumePosition(shown.volumeDb)),
                         juce::dontSendNotification);
        pan_.setValue(0.5f + 0.5f * shown.pan, juce::dontSendNotification);
        plays_.setValue(playsPosition(shown.plays), juce::dontSendNotification);
    }
    choke_.setSelectedId(std::clamp(shown.choke, 0, model::kit::chokeGroups) + 1, juce::dontSendNotification);
    solo_.setToggleState(shown.solo, juce::dontSendNotification);
    padSection_->refresh();
}

void DrumKitPanel::updateMacros()
{
    const auto& t = ui::Theme::current();
    const auto& specs = engine::Session::drumKit().params;
    for (size_t i = 0; i < macros_.size(); ++i)
    {
        const auto& macro = kit_.macros[i];
        auto& knob = *macros_[i];
        knob.setName(macro.name.empty() ? juce::String(std::string(specs[i].name)) : utf8(macro.name));
        // One that maps nothing yet recedes: turning it does nothing (as a rack's, racks.md §5a).
        knob.setAlpha(macro.mappings.empty() ? t.roles.fadedAlpha : 1.0f);
    }
}

void DrumKitPanel::renamePad(int note)
{
    const auto* pad = model::kit::padOn(kit_, note);
    if (pad == nullptr || isFolded())
        return;
    nameEditor_.show(getLocalArea(&pads_, pads_.nameArea(note)), utf8(pad->name), ui::TextStyle::Control,
                     [this, note](const juce::String& name)
                     {
                         if (onRenamePad)
                             onRenamePad(note, name);
                     });
}
} // namespace fd::app
