#pragma once

namespace fd::ui
{
/// The icon set (ui-principles §3): line icons drawn in code on a 16 px grid with a 1.5 px
/// stroke; transport glyphs are filled. Add icons here only; every icon needs a tooltip.
enum class Icon
{
    Play,
    Stop,
    Record,
    Loop,
    Metronome,
    PanelLeft,   // browser
    PanelRight,  // inspector
    PanelBottom, // detail panel
    Plus,
    Settings,
    Undo,
    Redo,
    Keyboard,      // computer keyboard as MIDI input
    ChevronLeft,   // fold a module
    ChevronRight,  // unfold
    Assistant,     // assistants connected over MCP (a spark)
    AutomationArm, // automation arm: a lane's line with a point (C4)
    AutomationOn,  // turn automation back on: the line with a turning arrow
    Close,         // remove or close: a cross
    Capture,       // capture MIDI (C5b): a turning arrow back round a dot, recording what was played
    Follow,        // follow the playhead: the playhead line with an arrow going on
    CountIn,       // count-in: beats leading into the downbeat
    ChevronDown,   // opens a menu (a clip's takes)
    Audition,      // hear a take in place of its clip (ADR 0038): a speaker with a sound wave
    Minimise,      // the window buttons (ADR 0040): a line,
    Maximise,      // a square,
    Restore,       // two squares (a maximised window back to its size); Close closes
    Search,        // the command palette (ADR 0040): a magnifier
    Star,          // a preset not starred (ADR 0044): an outline
    StarFilled,    // a favourite preset
    Note,          // instruments (the browser's rail, ADR 0046): an eighth note
    Knob,          // audio effects: a knob with its pointer
    Plug,          // plugins: a plug with two prongs
    Folder,        // places and folders: a folder with its tab
    Move,          // a modulation source's handle, dragged onto a knob: four ways
    Rack,          // racks (the browser, racks.md §7): two modules stacked in a frame
    Lock,          // a rack's macro that recall, Morph and Vary leave alone (racks.md §18.4): a padlock
    LockOpen,      // one that they move: the padlock open, its shackle lifted on one side
    // The clip view's tools (piano-roll.md §25): the Tools pane's tabs and each tool's tile.
    Transform,  // notes changed: a low bar turned into a high one
    Generate,   // notes made: a plus over lines
    Quantise,   // a note between two grid lines
    Humanize,   // a wavering line
    Strum,      // three notes, each a little later
    Arpeggiate, // notes climbing one after another
    Chop,       // a note in pieces
    Ornament,   // a short note before a long one
    Velocity,   // stems of different heights
    Span,       // a note stretched to a line
    Randomise,  // a die
    MakeChords, // one note, then a stack
    Rhythm,     // beats along a line
    Melody,     // a line of notes up and down
    Chords,     // a stack of notes
    Bassline,   // low notes, stepping
    // The audio editor's tools (audio-editor.md §11).
    Normalise, // two levels raised to a line
    Tempo      // beats found in a sound: hits along a line, a tick over each
};

/// Draws `icon` fitted (aspect kept) into `area` in `colour`.
void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

/// A mouse cursor for a gesture of its own (looping a clip by its edge, clip-loops.md §5): `icon`
/// at `input.cursorIconSize` in textPrimary, outlined in surface0 so it reads on any background,
/// its hotspot at the centre. Make it once and keep it.
[[nodiscard]] juce::MouseCursor cursorFor(Icon icon);
/// The image `cursorFor` shows, drawn at twice its size (sharp on a 2x screen).
[[nodiscard]] juce::ScaledImage cursorImage(Icon icon);

/// Flat icon button (ui-principles §4): dim when off, `active` fill when on (toggles),
/// `surface3` on hover. Tooltip text should include the shortcut.
class IconButton : public juce::Button
{
public:
    explicit IconButton(Icon icon, const juce::String& name = {});

    void setIcon(Icon icon);
    /// Its icon at `size` px, centred (the browser's rail draws its parts' icons as its tree
    /// does); 0: as large as the button allows.
    void setIconSize(int size);
    /// On in the `record` colour instead of `active` (the transport's Record button).
    void setRecordAccent(bool record);
    /// A right-click: the button's menu (it doesn't click).
    std::function<void()> onPopupMenu;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    Icon icon_;
    int iconSize_ = 0;
    bool record_ = false, popup_ = false;
};
} // namespace fd::ui
