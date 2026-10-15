// The piano roll's commands in the shell (M3 step P4, ADR 0058, docs/design/piano-roll.md §13):
// the Notes menu, the clip view's options in the View menu, the notes' right-click menu and the
// Chord and Quantise… popovers. Edit's Delete, Duplicate, Cut, Copy, Paste, Select all and Split
// at playhead, Clip's Mute and the arrows act on notes too, when the clip view was used last
// (Shell.cpp).

#include "model/Chance.h"
#include "model/NoteToolkit.h"
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
    const auto whyNoNotes = [this]
    {
        return juce::String(detail_.showsPianoRoll()
                                ? "The clip has no notes yet: double-click in the clip view to draw some"
                                : "Open a MIDI clip in the clip view first: double-click it");
    };
    const auto hasKey = [hasNotes, &roll] { return hasNotes() && roll.key().has_value(); };
    const auto whyNoKey = [this, whyNoNotes, &roll]
    {
        return roll.key() || !detail_.showsPianoRoll()
                   ? whyNoNotes()
                   : juce::String("The song has no key: Song > Key sets one");
    };
    const auto tool = [&](std::string id, juce::String name, std::function<void()> perform,
                          std::vector<juce::KeyPress> keys = {})
    {
        actions_.add({.id = std::move(id),
                      .name = std::move(name),
                      .category = "Notes",
                      .keys = std::move(keys),
                      .perform = std::move(perform),
                      .isEnabled = hasNotes,
                      .whyNot = whyNoNotes});
    };

    // Its keys, and Transpose's, go to whichever kind of clip the clip view shows: the Audio menu
    // lists the audio's with the same keys (audio-editor.md §13).
    actions_.add({.id = "notes.quantiseSettings",
                  .name = asks("Quantise"),
                  .category = "Notes",
                  .keys = {key('u', kCmd | kShift)},
                  .perform =
                      [this]
                  {
                      if (detail_.showsAudioClip())
                          actions_.perform("audio.quantiseSettings");
                      else
                          openTool("quantise");
                  },
                  .isEnabled = [this, hasNotes] { return hasNotes() || detail_.showsAudioClip(); },
                  .whyNot = whyNoNotes});

    // Transpose ▸: a step (of the scale with Key, §6), a semitone, an octave.
    const auto selected = [this, &roll] { return notesActive() && roll.hasSelection(); };
    const auto semitones = [this, &roll, selected](int by)
    {
        return [this, &roll, selected, by]
        {
            if (selected())
                roll.transpose(by);
            else
                actions_.perform(by > 0 ? "audio.semitoneUp" : "audio.semitoneDown");
        };
    };
    const auto canTranspose = [this, selected]
    { return selected() || actions_.isEnabled("audio.semitoneUp"); };
    actions_.add({.id = "notes.semitoneUp",
                  .name = "Up a semitone",
                  .category = "Notes",
                  .keys = {key(juce::KeyPress::upKey, kCmd)},
                  .perform = semitones(1),
                  .isEnabled = canTranspose});
    actions_.add({.id = "notes.semitoneDown",
                  .name = "Down a semitone",
                  .category = "Notes",
                  .keys = {key(juce::KeyPress::downKey, kCmd)},
                  .perform = semitones(-1),
                  .isEnabled = canTranspose});
    actions_.add({.id = "notes.fitToKey",
                  .name = "Fit to key",
                  .category = "Notes",
                  .perform = [&roll] { roll.fitToKey(); },
                  .isEnabled = hasKey,
                  .whyNot = whyNoKey});

    // Chords (§7.4, §20.3, §20.4): Make chords… chord by chord; Change chord… for the selected chord.
    tool("notes.makeChords", asks("Make chords"), [this] { openTool("makeChords"); });
    const auto selectedChord = [this, &roll]
    {
        if (!detail_.showsPianoRoll() || !roll.hasSelection())
            return false;
        std::vector<int> pitches;
        for (auto i : roll.selection())
            pitches.push_back(roll.notes()[i].pitch);
        return music::nameChord(pitches).has_value();
    };
    actions_.add(
        {.id = "notes.changeChord",
         .name = asks("Change chord"),
         .category = "Notes",
         .perform = [this, &roll] { openChordPopover(roll.placeForSelection()); },
         .isEnabled = selectedChord,
         .whyNot = []
         { return juce::String("Select a chord first: click its name in the chord lane, or its notes"); }});
    tool("notes.invertUp", "Invert up", [&roll] { roll.invertChords(true); },
         {key(juce::KeyPress::upKey, kAlt | kShift)});
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
    // The tools that transform and generate (§12, §23): each opens the Tools pane at it (§25). Transforms
    // need notes; a generator a MIDI clip in the clip view (the bassline, its chords too).
    for (const auto& t : model::noteTools())
    {
        if (t.id == "quantise")
            continue; // Quantise… (above) opens the pane at it
        const auto* spec = &t;
        actions_.add(
            {.id = "notes.tool." + std::string(t.id),
             .name = asks(std::string(t.name).c_str()),
             .category = "Notes",
             .perform = [this, spec] { openTool(spec->id); },
             .isEnabled = [this, &roll, spec]
             { return detail_.showsPianoRoll() && model::toolUnavailable(*spec, roll.notes()).empty(); },
             .whyNot =
                 [this, &roll, spec, whyNoNotes]
             {
                 if (!detail_.showsPianoRoll())
                     return whyNoNotes();
                 return juce::String::fromUTF8(model::toolUnavailable(*spec, roll.notes()).c_str());
             }});
    }

    // Re-roll (§10): the clip's notes left to chance decided again.
    actions_.add({.id = "notes.reroll",
                  .name = "Re-roll chance",
                  .category = "Notes",
                  .perform = [&roll] { roll.rerollChance(); },
                  .isEnabled = [hasNotes, &roll] { return hasNotes() && model::usesChance(roll.notes()); },
                  .whyNot =
                      [hasNotes, whyNoNotes]
                  {
                      return hasNotes() ? juce::String("No note in the clip is left to chance: set a note's "
                                                       "Chance or Spread in the inspector")
                                        : whyNoNotes();
                  }});

    // Select ▸ (§5).
    tool("notes.selectSamePitch", "Select same pitch", [&roll] { roll.selectSamePitch(); });
    actions_.add({.id = "notes.selectOutsideKey",
                  .name = "Select outside the key",
                  .category = "Notes",
                  .perform = [&roll] { roll.selectOutsideKey(); },
                  .isEnabled = hasKey,
                  .whyNot = whyNoKey});
    tool("notes.selectInLoop", "Select in the loop", [&roll] { roll.selectInLoop(); });
    tool("notes.invertSelection", "Invert selection", [&roll] { roll.invertSelection(); });

    // The clip view's options in the View menu (§13): each reachable without the header.
    const auto clipView = [this] { return detail_.showsPianoRoll(); };
    const auto whyNoClip = []
    { return juce::String("Open a MIDI clip in the clip view first: double-click it"); };
    actions_.add({.id = "view.clipInKey",
                  .name = "Show only the key's notes",
                  .category = "View",
                  .keys = {key('n', kCmd | kAlt)},
                  .perform = [&roll] { roll.setInKey(!roll.inKey()); },
                  .isEnabled = [clipView, &roll] { return clipView() && roll.key().has_value(); },
                  .isToggled = [&roll] { return roll.inKey(); },
                  .whyNot =
                      [this, &roll]
                  {
                      return detail_.showsPianoRoll() && !roll.key()
                                 ? juce::String("The song has no key: Song > Key sets one")
                                 : juce::String("Open a MIDI clip in the clip view first: double-click it");
                  }});
    // The Tools pane (§25): on a tab, or closed, as the header's Transform and Generate do.
    for (const auto& [id, name, tab] :
         {std::tuple{"view.toolsTransform", "Transform tools", model::ToolKind::Transform},
          std::tuple{"view.toolsGenerate", "Generate tools", model::ToolKind::Generate}})
        actions_.add({.id = id,
                      .name = name,
                      .category = "View",
                      .perform = [this, kind = tab]
                      { detail_.showTools(detail_.toolsShown(kind) ? std::nullopt : std::optional(kind)); },
                      .isEnabled = clipView,
                      .isToggled = [this, kind = tab] { return detail_.toolsShown(kind); },
                      .whyNot = whyNoClip});
    actions_.add({.id = "view.clipOthers",
                  .name = "Show other tracks' notes",
                  .category = "View",
                  .perform = [&roll] { roll.setOthers(!roll.options().others); },
                  .isEnabled = clipView,
                  .isToggled = [&roll] { return roll.options().others; },
                  .whyNot = whyNoClip});
    for (const auto& [id, mode] : {std::pair{"view.drawNote", model::DrawMode::Note},
                                   std::pair{"view.drawChord", model::DrawMode::Chord},
                                   std::pair{"view.drawChordUnder", model::DrawMode::ChordUnder}})
        actions_.add({.id = id,
                      .name = model::drawModeName(mode),
                      .category = "View",
                      .perform = [&roll, mode] { roll.setDraw(mode); },
                      .isEnabled = clipView,
                      .isToggled = [&roll, mode] { return roll.options().draw == mode; },
                      .whyNot = whyNoClip});
    // The kind and voicing the chords are drawn with (§20.2), as the header has them.
    const auto setStyle = [&roll](auto change)
    {
        return [&roll, change]
        {
            auto style = roll.options().chord;
            change(style);
            roll.setChordStyle(style);
        };
    };
    for (const auto& [id, name, shape] :
         {std::tuple{"view.drawTriads", "Draw triads", music::ChordShape::Triad},
          std::tuple{"view.drawSevenths", "Draw 7th chords", music::ChordShape::Seventh},
          std::tuple{"view.drawNinths", "Draw 9th chords", music::ChordShape::Ninth},
          std::tuple{"view.drawSus2", "Draw sus2 chords", music::ChordShape::Sus2},
          std::tuple{"view.drawSus4", "Draw sus4 chords", music::ChordShape::Sus4},
          std::tuple{"view.drawAdd9", "Draw add9 chords", music::ChordShape::Add9}})
        actions_.add({.id = id,
                      .name = name,
                      .category = "View",
                      .perform = setStyle([shape](model::ChordStyle& s) { s.shape = shape; }),
                      .isEnabled = clipView,
                      .isToggled = [&roll, shape] { return roll.options().chord.shape == shape; },
                      .whyNot = whyNoClip});
    for (const auto& [id, voicing] : {std::pair{"view.voicingSmooth", music::Voicing::Smooth},
                                      std::pair{"view.voicingClose", music::Voicing::Close},
                                      std::pair{"view.voicingOpen", music::Voicing::Open},
                                      std::pair{"view.voicingDrop2", music::Voicing::Drop2},
                                      std::pair{"view.voicingDrop3", music::Voicing::Drop3},
                                      std::pair{"view.voicingSpread", music::Voicing::Spread}})
        actions_.add(
            {.id = id,
             .name = juce::String("Voiced ") + juce::String(model::voicingName(voicing)).toLowerCase(),
             .category = "View",
             .perform = setStyle([voicing](model::ChordStyle& s) { s.voicing = voicing; }),
             .isEnabled = clipView,
             .isToggled = [&roll, voicing] { return roll.options().chord.voicing == voicing; },
             .whyNot = whyNoClip});

    // The header's Notes button opens the menu bar's Notes menu: one builder (§4).
    detail_.notesMenu = [this]
    {
        const auto& menus = menus_->menus();
        for (std::size_t i = 0; i < menus.size(); ++i)
            if (menus[i].title == "Notes")
                return menus_->menu(static_cast<int>(i));
        return juce::PopupMenu();
    };
    roll.onChordPopover = [this](const PianoRoll::ChordPlace& place, juce::Rectangle<int>)
    { openChordPopover(place); };
}

juce::PopupMenu Shell::notesContextMenu()
{
    // The most used, on the notes (§13): the rest are in the Notes menu, which opens from here too.
    juce::PopupMenu menu;
    actions_.addMenuItem(menu, "notes.quantise");
    actions_.addMenuItem(menu, "notes.fitToKey");
    actions_.addMenuItem(menu, "notes.makeChords");
    juce::PopupMenu chords;
    actions_.addMenuItem(chords, "notes.changeChord");
    chords.addSeparator();
    for (const char* id : {"notes.invertUp", "notes.invertDown"})
        actions_.addMenuItem(chords, id);
    chords.addSeparator();
    for (const char* id : {"notes.voiceSmooth", "notes.voiceClose", "notes.voiceOpen", "notes.voiceDrop2",
                           "notes.voiceDrop3", "notes.voiceSpread"})
        actions_.addMenuItem(chords, id);
    menu.addSubMenu("Chords", chords);
    juce::PopupMenu transform; // §12
    actions_.addMenuItem(transform, "notes.quantiseSettings");
    for (const auto& t : model::noteTools())
        if (t.kind == model::ToolKind::Transform && t.id != "quantise")
            actions_.addMenuItem(transform, "notes.tool." + std::string(t.id));
    menu.addSubMenu("Transform", transform);
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
    for (const char* id :
         {"notes.selectSamePitch", "notes.selectOutsideKey", "notes.selectInLoop", "notes.invertSelection"})
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
    detail_.toolsPane().disarm(); // one thing at a time shows on the notes
    // Beside the place in the lane (the lane's top when it isn't shown yet).
    auto anchor = roll.laneAreaWithBefore(place);
    if (anchor.isEmpty())
        anchor = roll.rulerArea();
    chordPopover_.open(*this, getLocalArea(&roll, anchor), place);
}

void Shell::openTool(std::string_view id)
{
    if (!detail_.showsPianoRoll())
        return;
    lastArea_ = Area::Clip;
    chordPopover_.close();
    detail_.showTool(id); // the Tools pane at it (§25)
}
} // namespace fd::app
