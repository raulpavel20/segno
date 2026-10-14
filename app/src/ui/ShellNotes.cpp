// The piano roll's commands in the shell (M3 step P4, ADR 0058, docs/design/piano-roll.md §13):
// the Notes menu, the clip view's options in the View menu, the notes' right-click menu and the
// Chord and Quantise… popovers. Edit's Delete, Duplicate, Cut, Copy, Paste, Select all and Split
// at playhead, Clip's Mute and the arrows act on notes too, when the clip view was used last
// (Shell.cpp).

#include "ui/Shell.h"

namespace fd::app
{
namespace
{
juce::KeyPress key(int code, int mods = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(mods), 0);
}

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

juce::String asks(const char* name)
{
    return juce::String(name) + juce::String::fromUTF8("\xE2\x80\xA6");
}
} // namespace

void Shell::registerNotesActions()
{
    auto& roll = detail_.pianoRoll();
    // Every Notes item acts on the selected notes, or on all of them, while the clip view shows a
    // MIDI clip; dimmed, saying why, otherwise (§13).
    const auto hasNotes = [this, &roll] { return detail_.showsPianoRoll() && !roll.notes().empty(); };
    const auto whyNoNotes = [this] {
        return juce::String(detail_.showsPianoRoll() ? "The clip has no notes yet: double-click in the clip view to draw some"
                                                     : "Open a MIDI clip in the clip view first: double-click it");
    };
    const auto hasKey = [hasNotes, &roll] { return hasNotes() && roll.key().has_value(); };
    const auto whyNoKey = [this, whyNoNotes, &roll] {
        return roll.key() || !detail_.showsPianoRoll() ? whyNoNotes()
                                                       : juce::String("The song has no key: Song > Key sets one");
    };
    const auto tool = [&](std::string id, juce::String name, std::function<void()> perform, std::vector<juce::KeyPress> keys = {})
    {
        actions_.add({.id = std::move(id), .name = std::move(name), .category = "Notes", .keys = std::move(keys),
                      .perform = std::move(perform), .isEnabled = hasNotes, .whyNot = whyNoNotes});
    };

    actions_.add({.id = "notes.quantiseSettings", .name = asks("Quantise"), .category = "Notes",
                  .keys = {key('u', kCmd | kShift)}, .perform = [this] { openQuantisePopover(); },
                  .isEnabled = hasNotes, .whyNot = whyNoNotes});

    // Transpose ▸: a step (of the scale with Key, §6), a semitone, an octave.
    const auto selected = [this, &roll] { return notesActive() && roll.hasSelection(); };
    actions_.add({.id = "notes.semitoneUp", .name = "Up a semitone", .category = "Notes",
                  .keys = {key(juce::KeyPress::upKey, kCmd)}, .perform = [&roll] { roll.transpose(1); },
                  .isEnabled = selected});
    actions_.add({.id = "notes.semitoneDown", .name = "Down a semitone", .category = "Notes",
                  .keys = {key(juce::KeyPress::downKey, kCmd)}, .perform = [&roll] { roll.transpose(-1); },
                  .isEnabled = selected});
    actions_.add({.id = "notes.fitToKey", .name = "Fit to key", .category = "Notes", .perform = [&roll] { roll.fitToKey(); },
                  .isEnabled = hasKey, .whyNot = whyNoKey});

    // Chords (§7.4).
    tool("notes.makeChords", "Make chords", [&roll] { roll.makeChords(); });
    actions_.add({.id = "notes.makeChord", .name = asks("Make chord"), .category = "Notes",
                  .perform = [this, &roll] { openChordPopover(roll.placeForSelection()); },
                  .isEnabled = [this] { return detail_.showsPianoRoll(); },
                  .whyNot = [] { return juce::String("Open a MIDI clip in the clip view first: double-click it"); }});
    tool("notes.invertUp", "Invert up", [&roll] { roll.invertChords(true); }, {key(juce::KeyPress::upKey, kAlt | kShift)});
    tool("notes.invertDown", "Invert down", [&roll] { roll.invertChords(false); },
         {key(juce::KeyPress::downKey, kAlt | kShift)});
    tool("notes.voiceSmooth", "Smooth voicing", [&roll] { roll.voiceChords(music::Voicing::Smooth); });
    tool("notes.voiceClose", "Close voicing", [&roll] { roll.voiceChords(music::Voicing::Close); });
    tool("notes.voiceOpen", "Open voicing", [&roll] { roll.voiceChords(music::Voicing::Open); });
    tool("notes.voiceDrop2", "Drop 2 voicing", [&roll] { roll.voiceChords(music::Voicing::Drop2); });
    tool("notes.voiceDrop3", "Drop 3 voicing", [&roll] { roll.voiceChords(music::Voicing::Drop3); });
    tool("notes.voiceSpread", "Spread voicing", [&roll] { roll.voiceChords(music::Voicing::Spread); });
    tool("notes.addBass", "Add bass", [&roll] { roll.addBass(); });

    // Editing (§9.1).
    tool("notes.join", "Join", [&roll] { roll.join(); }, {key('j', kCmd)});
    tool("notes.legato", "Legato", [&roll] { roll.legato(); });
    tool("notes.fixedLength", "Fixed length", [&roll] { roll.fixedLength(); });
    tool("notes.doubleSpeed", "Double speed", [&roll] { roll.stretch(0.5); });
    tool("notes.halfSpeed", "Half speed", [&roll] { roll.stretch(2.0); });
    tool("notes.reverse", "Reverse", [&roll] { roll.reverse(); });
    tool("notes.mirror", "Mirror pitches", [&roll] { roll.mirrorPitches(); });

    // Select ▸ (§5).
    tool("notes.selectSamePitch", "Select same pitch", [&roll] { roll.selectSamePitch(); });
    actions_.add({.id = "notes.selectOutsideKey", .name = "Select outside the key", .category = "Notes",
                  .perform = [&roll] { roll.selectOutsideKey(); }, .isEnabled = hasKey, .whyNot = whyNoKey});
    tool("notes.selectInLoop", "Select in the loop", [&roll] { roll.selectInLoop(); });
    tool("notes.invertSelection", "Invert selection", [&roll] { roll.invertSelection(); });

    // The clip view's options in the View menu (§13): each reachable without the header.
    const auto clipView = [this] { return detail_.showsPianoRoll(); };
    const auto whyNoClip = [] { return juce::String("Open a MIDI clip in the clip view first: double-click it"); };
    actions_.add({.id = "view.clipKey", .name = "Snap to key", .category = "View",
                  .perform = [&roll] { roll.setKeySnap(!roll.options().keySnap); },
                  .isEnabled = [clipView, &roll] { return clipView() && roll.key().has_value(); },
                  .isToggled = [&roll] { return roll.options().keySnap; },
                  .whyNot = [this, &roll] {
                      return detail_.showsPianoRoll() && !roll.key()
                                 ? juce::String("The song has no key: Song > Key sets one")
                                 : juce::String("Open a MIDI clip in the clip view first: double-click it");
                  }});
    actions_.add({.id = "view.clipOthers", .name = "Show other tracks' notes", .category = "View",
                  .perform = [&roll] { roll.setOthers(!roll.options().others); }, .isEnabled = clipView,
                  .isToggled = [&roll] { return roll.options().others; }, .whyNot = whyNoClip});
    for (const auto& [id, fold] : {std::pair{"view.foldAll", model::Fold::All}, std::pair{"view.foldKey", model::Fold::Key},
                                   std::pair{"view.foldNotes", model::Fold::Notes}})
        actions_.add({.id = id, .name = model::foldName(fold), .category = "View",
                      .perform = [&roll, fold] { roll.setFold(fold); },
                      .isEnabled = [clipView, &roll, fold] { return clipView() && (fold != model::Fold::Key || roll.key()); },
                      .isToggled = [&roll, fold] { return roll.options().fold == fold; }, .whyNot = whyNoClip});
    for (const auto& [id, mode] : {std::pair{"view.drawNote", model::DrawMode::Note},
                                   std::pair{"view.drawChord", model::DrawMode::Chord},
                                   std::pair{"view.drawChordUnder", model::DrawMode::ChordUnder}})
        actions_.add({.id = id, .name = model::drawModeName(mode), .category = "View",
                      .perform = [&roll, mode] { roll.setDraw(mode); }, .isEnabled = clipView,
                      .isToggled = [&roll, mode] { return roll.options().draw == mode; }, .whyNot = whyNoClip});

    // The header's Notes button opens the menu bar's Notes menu: one builder (§4).
    detail_.notesMenu = [this]
    {
        const auto& menus = menus_->menus();
        for (std::size_t i = 0; i < menus.size(); ++i)
            if (menus[i].title == "Notes")
                return menus_->menu(static_cast<int>(i));
        return juce::PopupMenu();
    };
    roll.onChordPopover = [this](const PianoRoll::ChordPlace& place, juce::Rectangle<int>) { openChordPopover(place); };
    roll.onChordSettings = [this, &roll]
    {
        chordPopover_.openSettings(*this, getLocalArea(&detail_, detail_.drawChoice().getBounds()));
        (void)roll;
    };
}

juce::PopupMenu Shell::notesContextMenu()
{
    // The most used, on the notes (§13): the rest are in the Notes menu, which opens from here too.
    juce::PopupMenu menu;
    actions_.addMenuItem(menu, "notes.quantise");
    actions_.addMenuItem(menu, "notes.fitToKey");
    actions_.addMenuItem(menu, "notes.makeChords");
    actions_.addMenuItem(menu, "notes.makeChord");
    juce::PopupMenu chords;
    for (const char* id : {"notes.invertUp", "notes.invertDown"})
        actions_.addMenuItem(chords, id);
    chords.addSeparator();
    for (const char* id : {"notes.voiceSmooth", "notes.voiceClose", "notes.voiceOpen", "notes.voiceDrop2",
                           "notes.voiceDrop3", "notes.voiceSpread"})
        actions_.addMenuItem(chords, id);
    menu.addSubMenu("Chords", chords);
    actions_.addMenuItem(menu, "clip.mute");
    menu.addSeparator();
    actions_.addMenuItem(menu, "edit.cut");
    actions_.addMenuItem(menu, "edit.copy");
    actions_.addMenuItem(menu, "edit.paste");
    menu.addSeparator();
    actions_.addMenuItem(menu, "edit.duplicate");
    actions_.addMenuItem(menu, "edit.delete");
    menu.addSeparator();
    juce::PopupMenu select;
    for (const char* id : {"notes.selectSamePitch", "notes.selectOutsideKey", "notes.selectInLoop", "notes.invertSelection"})
        actions_.addMenuItem(select, id);
    menu.addSubMenu("Select", select);
    menu.addSubMenu("Notes", detail_.notesMenu ? detail_.notesMenu() : juce::PopupMenu());
    menu.addSubMenu("Grid", detail_.pianoRoll().gridMenu()); // where you edit (ADR 0047)
    return menu;
}

void Shell::openChordPopover(const PianoRoll::ChordPlace& place)
{
    auto& roll = detail_.pianoRoll();
    if (!detail_.showsPianoRoll())
        return;
    lastArea_ = Area::Clip;
    quantisePopover_.close();
    // Beside the place in the lane (the lane's top when it isn't shown yet).
    auto anchor = roll.laneAreaWithBefore(place);
    if (anchor.isEmpty())
        anchor = roll.rulerArea();
    chordPopover_.open(*this, getLocalArea(&roll, anchor), place);
}

void Shell::openQuantisePopover()
{
    if (!detail_.showsPianoRoll())
        return;
    lastArea_ = Area::Clip;
    chordPopover_.close();
    // Under the header's Quantise: the header's right end when it's hidden.
    auto& header = detail_.header();
    juce::Rectangle<int> anchor = header.getBounds().removeFromRight(1);
    for (auto* child : header.getChildren())
        if (auto* button = dynamic_cast<ui::Toggle*>(child); button != nullptr && button->getButtonText() == "Quantise" &&
                                                             button->isVisible() && !button->getBounds().isEmpty())
            anchor = button->getBounds().translated(header.getX(), header.getY());
    quantisePopover_.open(*this, getLocalArea(&detail_, anchor));
}
} // namespace fd::app
