# Ableton Push 2: design

Segno played, mixed and shaped from an Ableton Push 2, with its display: the pads play the selected track's instrument in the project's key, the encoders set the selected module's controls bank by bank, and the display shows what they do, drawn as Segno's own UI. Other controllers keep to generic MIDI learn (M3 C8). The decision is [ADR 0054](../decisions/0054-push-2-support.md) (proposed). It builds on:
- the Session as the one way in ([ADR 0018](../decisions/0018-daw-app-structure.md)), as the assistants use it ([ADR 0021](../decisions/0021-mcp-server.md));
- one selection ([ADR 0027](../decisions/0027-selection-and-context-panels.md)), the module panels' sections and pages ([ADR 0022](../decisions/0022-module-panels.md), [ADR 0051](../decisions/0051-pages-for-very-wide-modules.md));
- the song's sections ([ADR 0049](../decisions/0049-the-master-row-is-the-song-row.md)) and the project's key and scale (M3 P3);
- the menu bar ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)), the settings page ([ADR 0045](../decisions/0045-settings-are-one-page.md)), the UI foundations ([ADR 0023](../decisions/0023-ui-foundations.md)).

**Status:** proposed for review (2026-10-14).

## 1. Why

- **The product owner plays on a Push 2** and wants it with Segno, display included; no Push 3, no other controller of their own (2026-10-14).
- **MIDI learn alone** (C8) would make it a box of knobs: no names or values on the display, no lights that follow the song, pads that don't know the key.
- **The vision ruled such integrations out;** ADR 0054 narrows that to everything but Push 2.

## 2. The hardware

Facts from Ableton's [MIDI and display interface manual](https://github.com/Ableton/push-interface), in our own words; the manual and its control map stay out of the repository (ADR 0054). Our table of the controls is written from it.

| Part | What it is |
|---|---|
| **Pads** | 64, 8 × 8, lit in colour. Velocity, and pressure as channel pressure or per pad. The firmware's velocity curve, pressure range and sensitivity can be set |
| **Encoders** | 8 over the display and a master encoder at the right: endless, touch-sensitive, about 210 steps a turn, faster turns sending bigger steps. A tempo encoder (18 detents a turn) and a swing encoder at the left |
| **Display** | 960 × 160 pixels, 16-bit colour, 60 frames a second, double-buffered; it goes black after 2 s without a frame. Adjustable backlight |
| **Display buttons** | 8 above the display and 8 below it, lit in colour, one over and one under each encoder's column |
| **Scene buttons** | 8 down the right of the pads, lit in colour, printed with note values (1/4 … 1/32t) |
| **Labelled buttons** | Play, Record, Automate, Metronome, Tap Tempo, Undo, Delete, Duplicate, Quantize, Double Loop, Convert, New, Fixed Length, Stop Clip, Mute, Solo; Device, Mix, Clip, Browse, Note, Session, Add Device, Add Track, Master, Setup, User; Scale, Layout, Repeat, Accent, Octave up and down, Page left and right, four arrows, Shift, Select. White or colour lights |
| **Touch strip** | Beside the pads, 31 lights: pitch bend or mod wheel, returning to the centre or not, its lights drawn by the Push or by the host |
| **Pedal jacks** | 2: switches by default (sustain, hold), expression pedals once calibrated |
| **USB** | Two MIDI ports (the *Live port* and the *User port*), and a bulk interface for the display. Live mode (the default) speaks on the Live port; User mode on the User port. On USB power alone, the lights and the backlight are kept dim; the power supply gives them full brightness |

## 3. What the others do (research, October 2026)

Live is the Push's home; two free projects drive it elsewhere. From their code (Ardour's `libs/surfaces/push2`, DrivenByMoss's `controller/ableton/push`), their manuals and their issue trackers.

| | Ardour (linear, like Segno) | DrivenByMoss (Bitwig; Reaper, linear) |
|---|---|---|
| **Pads** | Notes in key or chromatic for the selected MIDI track (rows in 3rds, 4ths, 5ths or in sequence; 35 scales); a "percussive" map from note 36; its clip launcher on Session | Live's views: in key, chords, piano, drums (4 × 4 pads, steps above, loop pages), step sequencers; on Reaper, which has no launcher, a "clip" is a track's item and a "scene" a region |
| **Encoders** | The mix only: volumes, pans, widths, sends A–E across 8 tracks; one track's gain, pan, width and trim. No plugin parameters | Device pages (the host's remote pages), the mix, sends; Delete + touch resets; Shift is finer |
| **Button rows** | Above: what the encoders set (Volumes, Pans, Sends); below: the tracks | Below: devices or pages; above: device functions (on, chains, window) |
| **Display** | Cairo, 8 columns of 120 px, labels top and bottom, the selected one filled in its track's colour; values always shown; 25 frames a second | Java drawing, the same columns; a parameter's value only while touched; notices for about a second; drawn again only on a change |
| **Transport** | Play (Shift: to the start), Record, Repeat as the loop, Undo (Shift: redo); scene buttons go to markers | Play (Shift: loop), Record, Repeat as note repeat (Live's meaning), Undo (Shift: redo); a marker mode |
| **Unbound** | Delete, Double Loop, Convert, Automate, Tap Tempo, Device, Add Device, Setup, User, Accent | Almost nothing: every label keeps Live's meaning |

- **What they share with Live:** the pads play in key by default, rows a fourth apart, with a scale page; the display has 8 columns, one per encoder, labelled for the buttons above and below; Undo with Shift redoes; the Push stays in Live mode on the Live port, and pads reach the track through a port of their own ("Push 2 Pads" in Ardour), remapped on the way.
- **What went wrong for them,** and Segno avoids:
  - Ardour sends a display header the manual calls reserved and skips the XOR it requires; one failed transfer stops its display until the surface is restarted. Segno follows the manual to the byte (tested), and a failed frame is simply sent again.
  - Unplugging during a frame crashed Bitwig (DrivenByMoss #202, fixed). Unplugging mid-frame is one of Segno's tests.
  - A whole grid of lights sent at once can drop MIDI on Linux (push-interface #11): Segno sends lights in batches (§9).
  - Ardour warns that switching tracks while a pad is held can leave a note hanging. A pad's release ends the note its press started, on the track it went to.
  - Lights are unreadably dim on USB power (DrivenByMoss #181): Segno says so (§5).
  - Ardour's encoders write automation to the volume whatever they're set to, and its encoder speeds differ between pages. Segno's encoders touch what they move, one speed everywhere.
- **Where Segno differs:**
  - **No clip launching** (ADR 0001): the Session button shows the song instead (§7.4).
  - **Banks are never curated per module:** every module's banks come from its panel's sections, so a new module works on the Push the day it lands, as it does for assistants.
  - **The display is the product's own UI,** built from the same parts as the screen.

## 4. Principles

1. **A client of the Session.** Buttons run the `ActionRegistry`'s actions; encoders edit through the host binding with a gesture from touch to release. Undo, automation and "devices never write their own parameters" hold unchanged; undo steps are named as the mouse's are.
2. **One selection.** The Push shows Segno's selection and changes it; it never keeps one of its own. Selecting a track on the Push selects it on screen, and the other way round.
3. **A button does what its label says, or nothing.** A label with no meaning in Segno (Stop Clip, Convert) stays dark and does nothing, rather than taking a job its label doesn't name.
4. **Nothing only the Push can do.** Whatever it changes in the project, the mouse can change too (ADR 0040). A new action the Push needs comes with its menu item.
5. **Lights tell the truth.** A button is lit only while pressing it would do something; brighter while what it toggles is on. A track's colour on a pad is its colour on screen.
6. **Playing comes first.** Pads reach the instrument as fast as a MIDI keyboard's keys, never waiting on the screen.
7. **The display is Segno's UI** (ADR 0023): colour roles, text styles, metrics and shared drawing; the UI lint and the layout checklist apply; reviewed in zoomed screenshots like any view.

## 5. Connecting

- **Found by itself:** turned on or plugged in at any time, it's taken within 2 s; unplugged, it's let go quietly. No dialogs. One Push 2 at a time (the first found). Segno watches for its MIDI ports to appear (the system's MIDI change notice), then opens its USB device: libusb's own hot-plug isn't there on Windows, and the ports are what matter first.
- **What Segno opens:** the Live port (MIDI in and out) and the display interface, after checking it is a Push 2 (the MIDI identity reply: Ableton, Push, model 2). Live mode, so the User port stays free for other programs.
- **The pads, the touch strip and the pedals** reach Segno as one MIDI input named **"Push 2"**, beside "Computer keyboard": in "All MIDI inputs", in a track's MIDI input choices, in Capture MIDI. The Push's buttons and encoders never reach a track or MIDI learn.
- **Only one program can hold it.** With Live running and using it, Segno can't open the ports; Settings says "In use by another program". Segno never fights for it.
- **Linux:** MIDI works as plugged in. The display needs permission to open the USB device: a udev rule for Ableton's Push 2 (vendor 2982, product 1967) giving the logged-in user access (`TAG+="uaccess"`, rather than Ardour's world-writable `MODE="0666"`). `tools/install-push2-udev-rule.sh` installs it and reloads udev, asking for your password; Linux packages install it themselves (M7). Without it, the Push works with a dark display, and Settings says why, with the command to copy.
- **macOS:** nothing to install.
- **Windows:** the display needs the WinUSB driver. Live installs it the first time a Push 2 is connected (or from its `Push2-display-driver-installer`); whether it can be had without Live is open (B78), to settle before the first Windows release. MIDI works either way.
- **On USB power alone,** Settings says "Plug in the Push's power supply for full brightness" (the Push reports how it's powered).
- **Quitting Segno,** or turning the Push off in Settings, darkens its display and lights.

## 6. Buttons in every mode

Each runs the action named, so it does what the menu item does and is undone as one.

| Button | Does | The same as |
|---|---|---|
| **Play** | Play or stop; with Shift, stop and return to the start (as in Ardour) | Transport > Play / stop, Stop and return to start |
| **Repeat** | With Shift, the loop on or off. Alone: note repeat, with step 2 | Transport > Loop |
| **Record** | Record; with Shift, Capture MIDI | Transport > Record, Capture MIDI |
| **Automate** | Automation arm | Transport > Automation arm |
| **Metronome** | The metronome | Transport > Metronome |
| **Tap Tempo** | The tempo from your taps (four set it); one undo step | The tempo field in the top bar |
| **Tempo encoder** | The tempo, 1 BPM a detent, 0.1 with Shift; turns close together are one undo step | The tempo field |
| **Master encoder** | The master's volume; touched, the display shows it | The master's volume |
| **Undo** | Undo; with Shift, Redo. The display names the step ("Undo Set Cutoff") | Edit > Undo, Redo |
| **Delete** | Alone: deletes the selection. Held: deletes what you press (a module's button, a section's scene button) or sets a touched encoder to its default | Edit > Delete; a control's double-click to its default |
| **Duplicate** | Duplicates the selection | Edit > Duplicate |
| **Quantize** | Quantises the selected clips or notes | Clip > Notes > Quantise |
| **New** | A new MIDI clip on the selected track at the playhead | Clip > New MIDI clip |
| **Mute, Solo** | Alone: the selected track. Held: the track whose button you press | Track > Mute, Solo selected track; the header's buttons |
| **Add Track** | A track; with Shift, a return track | Track > Add track, Add return track |
| **Master** | Selects the master | A click on the song row's header |
| **← →** | The previous or next track (in Song, they move the pads' window: §7.4) | Selecting it |
| **↑ ↓** | The previous or next section, the playhead going there (in Song, as ← →) | Song > Previous section, Next section |
| **Note, Session; Device, Mix; Scale, Setup, User** | The modes (§7) | – |
| **Shift** | Finer encoders; the second job of a button, as listed. While it's held, the buttons light by their second jobs | – |
| **Select** | Held: a pad or button selects what it names without doing its job (in the song grid, a clip without moving the playhead) | – |
| **Swing encoder** | Nothing until the groove (M4) | – |
| **Stop Clip, Convert, Fixed Length** | Nothing: dark (no clip launching, no conversion, no fixed-length recording) | – |
| **Session, Browse, Add Device, Clip, Double Loop** | Step 2 (§16); dark until then | – |

## 7. Modes

Two choices, made apart, as the buttons sit on the Push:
- **the pads:** Note (§7.1) or Song (the Session button, §7.4), beside the pads;
- **the encoders and the display:** Device (§7.2) or Mix (§7.3), beside the display; Clip and Browse join them with step 2. Scale and Setup take the display while they're lit, and give it back.

Segno starts in Note and Device. In every display mode, the buttons above the display choose **what** the encoders act on, and the buttons below choose **which** thing (a module, a track); the display's top and bottom rows label them. **Page ◀ ▶** shows the next or previous 8 of the row above; with Shift, of the row below.

### 7.1 Note (the Note button)

The pads play the selected track's instrument, through the "Push 2" input, as a MIDI keyboard would: to the armed tracks, or the selected one, softly armed (recording.md §4).

- **Layouts** (Layout steps through them; the display names each for a moment):
  - **In key** (the default): only the key's notes, rows a fourth apart, so a chord's shape plays the same anywhere;
  - **Chromatic:** every note, rows a fourth apart, notes outside the key dark;
  - **Drums:** four blocks of 4 × 4 from C1 (MIDI 36), the General MIDI drum notes, the bottom-left block first.
- **The key and scale are the project's** (M3 P3), one source for the pads, the piano roll and the MIDI effects. **Scale** shows them on the display: the upper buttons choose the root, in the circle of fifths as Live and Ardour order them (Page for the other four), an encoder the scale. Changing them changes the project's key, one undo step, as in the top bar. With no key set (a new project), the pads play C major and the display says so ([key-and-scale.md](key-and-scale.md) §8).
- **Colours:** the root in the track's colour, the key's other notes dim white, a sounding note bright on every pad that plays it.
- **Octave up and down** move the range; the display shows it ("C2 – D#5") for a moment.
- **Accent:** while it's lit, every note plays at full velocity.
- **Pressure** goes to the instrument per pad (polyphonic pressure) by default, or as channel pressure (Settings). **Velocity** follows the pads' curve (Settings).
- **The touch strip** bends the pitch, returning to the centre; or it's the mod wheel (Settings). Its lights follow the Push.
- **Pedals** sustain and hold, into the same input.
- **A track without an instrument** (ADR 0033) leaves the pads dim, and the display says "Bass has no instrument" for a moment.
- **The display stays in its own mode** (Device, so the sound can be shaped while playing, or Mix); a change of layout, key or octave shows as a notice.

### 7.2 Device

The 8 encoders set the selected module's controls (the detail panel's), or the selected track's first module when none is selected.

- **Banks are the panel's sections,** in the panel's order and page by page (ADR 0051), each section's controls in reading order (down each column, then the next column), 8 to a bank. A section with more than 8 is split ("OSC 1 · 1/2"); collapsed sections are banked too. A module that isn't ours (a plugin) gets its parameters 8 at a time, in its own order.
- **Above the display:** the banks, by their sections' titles; Page ◀ ▶ for more. Pressing the shown bank's button again switches its section on or off, where the section has a power switch (ADR 0022).
- **Below the display:** the selected track's modules, in chain order. A press selects one, and the panel on screen scrolls to it. With Shift, it switches the module on or off; held with Delete, it removes it.
- **Encoders:**
  - **a turn** sets the control as dragging its knob does: its whole range in one turn (about `input.valueTravel`), finer with Shift (`input.fineFactor`); a choice steps once every few steps; a switch flips;
  - **a touch** shows the control's value large and starts its gesture; letting go ends it, so a touch is one undo step;
  - **with Automate on and playing,** a touch writes automation while held, as the mouse's touch does (automation.md: touch mode; B31's latch is for controllers without touch);
  - **an automated control** turned while not writing turns its lane off, as with the mouse (ADR 0028).
- **Display, per column:** the control's label (the panel's), its value as the panel writes it (always, as the panel shows it; larger while touched), and its knob drawn by the panel's own knob drawing at the display's size, modulation rings included; a switch as on or off; a choice with its neighbours while touched.

### 7.3 Mix

- **Above the display:** Volume, Pan, then a send per return (A, B, …): what the encoders set.
- **Below the display:** 8 tracks, in the arrangement's order, the returns after them. A press selects the track; held with Mute or Solo, mutes or solos it. Each button lit in its track's colour, the selected one bright.
- **Encoders:** the 8 tracks' volumes (or pans, or sends), through the Session's mix gesture: a touch is one undo step; the volume taper is the mixer's (ADR 0026).
- **Master** selects the master, whose strip takes the last column.
- **Display, per column:** the track's name on its colour, its meter (live), the value (dB, or the pan as a centred bar), mute and solo states.

### 7.4 Song (the Session button)

There's no clip launching (ADR 0001). The Session button shows the song instead: where everything is, to move around it and select it.

```
              track 1  track 2  …  track 8      scene buttons
 Intro        ■ blue   □         …  ■ green      ▶ Intro
 Verse        ■ blue   ■ red     …  ■ green      ▶ Verse
 Chorus       ■ blue   ■ red     …  □            ▶ Chorus   (pulsing: under the playhead)
 …
```

- **Columns are tracks** (the same 8 as Mix's buttons below the display), **rows are the song's sections,** first at the top. A pad is lit in the colour of the clips on that track in that section (each clip's own colour, ADR 0043), dark where it has none; the section under the playhead pulses in tempo. A song without sections has a row every 8 bars.
- **The scene buttons are the sections,** each lit in its section's colour (ADR 0049). One moves the playhead to its section; with Shift, loops it (Song > Loop section); held with Duplicate or Delete, duplicates or deletes the section (the Song menu's items). While one is held, the display names its section, its bars and its length.
- **A pad** selects those clips (the arrangement scrolls to them, the detail panel shows them) and moves the playhead to the section; with Select held, it only selects.
- **Arrows** move the 8 × 8 window over the tracks and sections, by one, or by 8 with Shift; the arrangement on screen follows.
- **The display stays in its mode.** Mix suits the song best: its columns are the pads' columns, named.

### 7.5 Setup

The Push's own settings on its display, the same ones as Settings > Controllers (§10): the pads' velocity curve and pressure, the touch strip, the lights' and display's brightness. Changing one here changes it there.

### 7.6 User

User gives the Push to MIDI learn and other programs: it switches to User mode, so its pads, buttons and encoders arrive on the User port as plain MIDI ("Ableton Push 2 User Port", an ordinary MIDI input). The display says so, and User again brings it back.

## 8. The display

```
0    ┌──────────┬──────────┬──────────┬───   8 columns of push.column   ───┬──────────┐
     │ OSC 1    │ OSC 2    │ FILTER   │ AMP ENV  │ …                      │          │  push.band: the buttons above
24   ├──────────┼──────────┼──────────┼──────────┼────────────────────────┼──────────┤
     │ Table    │ Position │ Warp     │ Amount   │ …                      │          │
     │   ◜◝     │   ◜◝     │   ◜◝     │   ◜◝     │                        │          │  content: one control per column
     │ Vowels   │  34 %    │ Sync     │  50 %    │                        │          │
136  ├──────────┼──────────┼──────────┼──────────┼────────────────────────┼──────────┤
     │●Wavetable│ EQ       │ Delay    │          │                        │          │  push.band: the buttons below
160  └──────────┴──────────┴──────────┴──────────┴────────────────────────┴──────────┘
```

- **The grid:** 8 columns of `push.column` (120 px), one over each encoder and between its two buttons; a band of `push.band` (24 px) at the top and the bottom labelling those buttons; `push.inset` (8 px) inside every column. New metrics in `fd_ui`, listed in ui-foundations.md when built.
- **Text** uses the shell's text styles, drawn at `push.scale` (125 % to start, set on the hardware at review): the display's pixels are smaller than a monitor's and it's read from further away (Ardour's text is about 13 px). The labels are the panels' own, already short.
- **Colours** are the theme's roles; a chosen button's label sits on `active` (in Mix, on the track's colour), and its light shows the same colour.
- **Nothing touches:** text is elided to its column, never into the next. A layout test checks every page at the longest names.
- **Notices** (the undo step, a layout's name, the octave range, "In use by another program") show for a moment over the content, as a toast does on screen.
- **Frames** are drawn from the snapshots, on the message thread: at 60 a second while something moves (meters, a value), and once a second while nothing does, which keeps the display on. A frame takes under 1 ms on the reference machine (a benchmark); a separate thread sends it.

## 9. Lights

- **Segno's colours:** when the Push connects, Segno loads its own palette into it: the clip palette (tracks, clips, sections) and the colour roles a light can need (`active`, `record`, `warning`), so a track's pad is the colour of its header (as near as the LEDs' colour allows).
- **White buttons:** off while they can't act, dim while they can, bright while what they toggle is on.
- **In time:** Segno sends the Push its tempo (MIDI clock while playing), so Record blinks through a count-in and the playing section pulses with the beat. (Neither Ardour nor DrivenByMoss sends it, so their lights blink at the Push's own 120 BPM.)
- **Only changes are sent:** Segno keeps every light's state and sends what differs, in batches with a SysEx round trip between them, so a whole grid changing at once never floods the Push's MIDI (push-interface #11).
- **The palette is checked at connect:** read back and rewritten where it differs, as DrivenByMoss does, so another program's palette never shows.
- **Brightness** of the lights and the display is in Settings.

## 10. Settings > Controllers

A new section of the settings page (settings.md §7), after MIDI. MIDI learn's mappings join it with C8.

| Setting | Control | Default |
|---|---|---|
| **Ableton Push 2** | On or off, with its state under it: Connected; Not connected; In use by another program; Display unavailable: no permission (Linux), with the command to copy | On |
| Pad velocity | Soft, Medium, Hard (the firmware's curve) | Medium |
| Pad pressure | Per pad, One for all pads, Off | Per pad |
| Touch strip | Pitch bend, Mod wheel | Pitch bend |
| Light brightness / Display brightness | 10 – 100 % | 100 % |

- The Push's Setup button (§7.5) shows the same settings on its display.
- Kept in `settings.json` with the rest (ADR 0045).

## 11. Menus and keys (ADR 0040)

- **No new menu items for the Push itself:** turning it on or off and its settings are in Settings (Options > Settings…), where settings live.
- **Every button runs an action already in the menu bar** (§6). Tap tempo and the tempo encoder set the tempo the tempo field sets.
- **New actions with step 2** come with their menu items: Clip > Double loop (the Double Loop button).

## 12. Assistants (MCP)

| Tool | Arguments | Returns |
|---|---|---|
| `get_push` | `image?` (false) | Whether a Push 2 is connected (and if not, why), the mode, what each button and pad shows (its label, colour, lit or not), the display's columns as text; with `image: true`, the display as a PNG. Read-only |
| `push_input` | `events`: presses (`{"press": "play", "shift": true}`), turns (`{"encoder": 1–8, "master" or "tempo", "by": steps, "touch": true}`), pads (`{"pad": [row, column], "velocity": 100}`, then `"release"`) | What the display shows after them. Acts as the hardware would, through the same code. Refused while no Push 2 is connected (and in read-only mode), except with the virtual Push the tests and `--virtual-push` run |

- **Settings:** `get_settings` and `set_settings` gain `controllers` (§10).

## 13. Code

```
app/src/control/push2/        fd_app_push2: DAW only
  Protocol    JUCE-free, [fast]: our table of the controls; decoding (encoder steps, touches,
              pads, pressure, the strip); the SysEx we send (palette, brightness, MIDI mode,
              pressure mode, the strip, velocity curve, identity, power); the display frame
              (header, 16-bit pixels with each line padded, the XOR pattern)
  Lights      JUCE-free: every light's wanted state; what to send
  Layouts     JUCE-free: pads to notes for each layout, key and octave; banks from sections
  Port        the hardware behind an interface: MIDI in and out, a display sink.
              Hardware (JUCE MIDI, libusb) and VirtualPush2 (tests, --virtual-push)
  Display     a mode's page drawn into a 960 × 160 juce::Image with fd_ui
  Controller  the modes over the Session, the selection and the ActionRegistry
```

- **Threads:**
  - **MIDI thread:** pads, pressure, the strip and pedals are turned into notes by the current layout (a table swapped atomically) and passed straight to the "Push 2" input and Capture MIDI's tap, as a keyboard's are;
  - **message thread:** buttons, encoders and touches (from a lock-free queue) run actions and edits; frames are drawn and lights worked out;
  - **USB thread:** sends the newest frame, dropping one that's been overtaken; never blocks the message thread;
  - **the audio thread** is untouched.
- **Dependency:** libusb 1.0.30 (LGPL-2.1-or-later), through libusb-cmake, pinned (cmake/Dependencies.cmake), built into the DAW only.
- **Boundaries:** a CTest `push_only_in_the_daw`, like `mcp_only_in_the_daw`: nothing outside `app/` includes the Push code or links libusb; the Push code includes no tracktion header (it reaches the engine through the Session).

## 14. Tests (Done when)

- **`[fast]`:**
  - the protocol, byte for byte against the manual's examples: encoder steps both ways, touches, pressure, every SysEx we send, a frame's header, padding and XOR;
  - lights: only changes are sent; a reconnect sends everything;
  - layouts: every pad's note in each layout, key and octave; roots where they should be;
  - banks: 8 to a bank, sections split, pages in order, a plugin's parameters.
- **Integration** (the virtual Push and a real Session):
  - each button runs its action; Undo names its step on the display;
  - a touched encoder's turns are one undo step, written as automation while armed;
  - pads reach an armed track's instrument and are recorded; Capture MIDI takes them;
  - selection both ways: a track or module chosen on the Push is selected on screen, and the other way round;
  - the song grid's lights match the clips' colours and sections;
  - unplugged and plugged back, it carries on, also in the middle of a frame; a failed frame is sent again and the display carries on; with none connected, nothing happens;
  - a held pad's note ends on its track when the selection moves to another track.
- **Display:** every page dumped as an image (`FD_DUMP_DIR`) and reviewed zoomed; no text crosses its column at the longest names; the UI lint over the display code; a frame under 1 ms.
- **MCP:** `get_push` and `push_input`.
- **Manual** (on the product owner's Push 2): playing feel (velocity, pressure), the encoders' feel, the display read from where you sit, the pads' latency against a keyboard's, installing the udev rule.

## 15. Legal

- **The interface is documented by Ableton** for people building their own applications; reading it and talking to the hardware is what it's for. Ableton's manual is used, not copied: our table is written from it; the manual, its pictures and its JSON map aren't in the repository.
- **Names:** "Ableton" and "Push" are Ableton's trademarks. Segno names the Push only to say what it works with (Settings, the docs); no logos, nothing that suggests Ableton endorses Segno, and no "Push" in the name of a feature, mode or product of ours (ADR 0054; ADRs 0008 and 0052 hold for ours).
- **The display's pages are our own design,** not copies of Live's (ADR 0008).
- **Code:** ours. libusb is LGPL-2.1-or-later, which AGPL-3.0 can include; it's listed in `REUSE.toml` and the About box's credits.

## 16. What comes when

- **Step 1:** connecting, lights, the display, Settings > Controllers and the udev script; the buttons of §6; Note, Device, Mix, Setup and User; `get_push` and `push_input`.
- **Step 2:** Song (the Session button); Browse and Add Device (modules and presets on the display); Clip (the inspector's CLIP section on the encoders); Repeat (notes repeated in time, the scene buttons' note values); Double Loop with Clip > Double loop; a section's display (the EQ's curve, an envelope) on the Push.
- **Step 3,** with the Drum Kit ([drum-kit.md](drum-kit.md)) and groove: step sequencing on the pads, melodic and drums; the swing encoder.

## 17. Questions for review

| # | Question | Recommendation |
|---|---|---|
| Q1 | **Which Push** | Push 2 only (ADR 0054); other controllers through MIDI learn (C8), Push 2 too in User mode |
| Q2 | **Where it lives** | In the DAW only, a client of the Session like the assistants; buttons run actions, encoders edit with gestures (§4) |
| Q3 | **The pads without a Session view** | Note mode by default; the Session button shows the song, tracks by sections, to move around and select (§7.4). No clip launching |
| Q4 | **Note layouts** | In key (default) and Chromatic, rows a fourth apart, and Drums, 4 × 4 blocks from C1 (§7.1) |
| Q5 | **The pads' key** | The project's key and scale (P3); changing it on the Push changes the project's, one undo step |
| Q6 | **Encoder banks** | The panel's sections in order, 8 controls to a bank, page by page; no banks curated per module (§7.2) |
| Q7 | **Encoder feel** | A control's whole range in one turn, finer with Shift; a touch is one undo step, and writes automation while armed (§7.2) |
| Q8 | **Modes and the two button rows** | The pads (Note, Song) and the display (Device, Mix) chosen apart, as on the hardware; above the display: what the encoders act on (banks, Volume / Pan / sends); below: which thing (modules, tracks) (§7) |
| Q9 | **Labels without a meaning in Segno** (Stop Clip, Convert, Fixed Length; the swing encoder until M4) | Dark, doing nothing (§6) |
| Q10 | **The display's look** | Segno's colour roles and text styles at 125 %, on an 8-column grid of new `push.*` metrics; our own pages, reviewed in zoomed dumps (§8) |
| Q11 | **Lights** | Segno's palette loaded into the Push; off, dim, bright by what a button can do; blinking in tempo (§9) |
| Q12 | **Settings** | A Controllers section after MIDI: Push 2 on or off with its state, the pads, the strip, brightness; the Setup button shows the same (§10) |
| Q13 | **User mode** | The User button hands the Push to MIDI learn and other programs, and back (§7.6) |
| Q14 | **The display's access, per system** | Linux: a udev rule giving the logged-in user access, installed by `tools/install-push2-udev-rule.sh` now and by the packages at M7; without it, a dark display and Settings saying why. macOS: nothing. Windows: the WinUSB driver Live installs; without Live, open (B78) (§5) |
| Q15 | **Assistants** | `get_push` (state and the display as an image) and `push_input` (presses, turns, pads); `controllers` in the settings tools (§12) |
| Q16 | **When** | Step 1 as M4's first DAW step, once M3 is done (Note mode needs P3's key); step 2 next in M4; step 3 with the Drum Kit. If you want it sooner, step 1 fits right after P4 |
