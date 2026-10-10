# Bounce: design (M2 step C7)

How Segno turns clips into audio: **Bounce in place** renders a part on its own track and keeps what it came from, and **Bounce to new track** prints what a track's modules make of it. The decision is [ADR 0035](../decisions/0035-bounce-keeps-its-source.md). It builds on one kind of track and its signal flow ([ADR 0019](../decisions/0019-one-kind-of-track.md)), projects as folders ([ADR 0030](../decisions/0030-projects-are-folders.md)), takes on clips ([ADR 0032](../decisions/0032-takes-belong-to-clips.md)), no instrument added by itself ([ADR 0033](../decisions/0033-no-instrument-by-itself.md)), lanes that drive parameters on any channel ([ADR 0029](../decisions/0029-independent-automation-lanes.md)), and the offline render the MCP `render` tool already uses (`Session::render`, [mcp.md](mcp.md) §7).

**Status:** approved by the product owner (2026-10-10), with every recommendation of §14. Built in two parts (§15): C7a, then C7b.

**Where the field is (2026):** every current DAW renders MIDI to audio, in two families.
- **Commit in place.** The part becomes audio on its own track:
  - Bitwig's Bounce In Place renders only the instrument ("Pre-FX") and leaves a hybrid track whose effects stay live, but it deletes the source clip;
  - Reaper's "apply FX as new take" keeps the original as another take of the item, though the track's effects then process the result a second time unless bypassed by hand;
  - Studio One's transform (now "Freeze to audio track" in Fender Studio Pro) can transform back;
  - Live's Bounce Track in Place drops the track's devices.
- **Print to a new track.** The part is rendered through the channel onto a new track below, and the source is muted:
  - Live 12.2's Bounce to New Track, which always renders after the devices and before the mixer, and carries volume, pan and sends over;
  - Logic's Bounce Regions in Place, which opens a dialog of options;
  - Cubase's Render in Place, with four signal points;
  - Studio One's Bounce to New Track.

Most DAWs also have freeze. Tails are on by default, either until silent (Logic, Studio One) or a fixed length (Cubase, Reaper). Device automation is baked, while volume, pan and sends stay live. Files go into the project's folder.

Segno offers both families, as two commands without a dialog. Its tracks are hybrid, like Bitwig's, so bounce in place renders the instrument alone. It goes further in three ways:
- the source of a bounce in place is a take, so the MIDI comes back with one click at any time, not only through undo;
- a bounce renders on a copy of the project, so playback never stops;
- tails over the next clip have a rule (§7). No vendor documents one, except Live, which splits them where a muted clip follows.

## 1. Principles

1. **A bounce in place sounds the same.** It renders only what the clip adds before the modules that keep running, so the track plays exactly as before (it nulls), and those modules stay editable. The one exception is a tail cut short by a clip that isn't bounced, which is faded and reported (§7).
2. **Nothing is lost.** The source of a bounce in place becomes a take of the new clip, and the source of a bounce to a new track stays, muted. Undo never deletes a bounced file (ADR 0030).
3. **Never in the way.** A bounce renders offline on a copy of the project. Playback and editing go on, progress shows on the clips, and nothing is modal.
4. **Afterwards, ordinary clips.** A bounce is a plain audio clip: it can be moved, trimmed, split, faded and exported like any other.
5. **No dialog of options.** There are two commands, each doing one thing with the right defaults, instead of one command with a page of switches.

## 2. Two commands

| | **Bounce in place** (Ctrl+B) | **Bounce to new track** (Ctrl+Shift+B) |
|---|---|---|
| **For** | Turning a part into audio on its own track: to edit the part as audio, to commit to it, to lighten the load. An instrument playing nothing costs little, and once a track's MIDI is bounced, turning the instrument off frees it all; audio still passes (ADR 0019) | Printing what the track's modules make of a part: to resample, chop or reverse it, or to commit the effects |
| **Renders** | A MIDI clip: its notes through the track's modules up to and including its instrument. An audio clip: the clip as it plays, before any module | The clips through the track's whole chain, before the fader |
| **Result** | An audio clip in the same place on the same track, with the source's name and colour. The modules after the instrument, the mixer and the sends keep processing it live | A new track right after the source track, with one audio clip per bounced clip. It has no modules; it copies the source's volume, pan and sends, and joins its automation lanes (§5) |
| **The source** | Kept as a take of the new clip (§4) | Muted in place (clip mute, §6) |
| **Sounds** | The same: the track nulls against itself before the bounce | The same while the source stays muted: the mix nulls |

Both work on the selected clips, on one track or several. A clip with takes counts as one clip (ADR 0032). Time ranges across clips and gaps come with range selection (later, ADR 0028); until then, select the clips.

## 3. What a bounce renders

**In place: up to where the clip enters the chain.** The signal flow of ADR 0019 decides it:
- **MIDI clips** feed the track's first instrument. The render covers the modules up to and including it. From M4, MIDI modules before the instrument (Arpeggiator, Chord) shape the notes and are part of the render. The result is the instrument's sound alone; everything after the instrument keeps running on the bounce exactly as it ran on the instrument.
- **Audio clips** enter at the start of the chain, so nothing is rendered but the clip itself. Today that copies the audio as it plays. With clip fades (C6) it bakes the fades, and with comping (C5c) it joins a comp's parts into one file. One rule covers every clip, and the command grows more useful as clips gain properties.
- **Automation** of the instrument's own parameters is baked in, because the instrument played it. The lanes stay: they now drive an instrument that plays nothing there, and they apply again if the MIDI comes back (§4). Automation of later modules and of the mixer stays live.

**When bounce in place isn't offered** (the menu item is dimmed; the shortcut shows a toast saying why):
- **A MIDI clip on a track without an instrument.** It is silent (ADR 0033): "Add an instrument to bounce this clip."
- **A MIDI clip on a track with audio modules before its instrument.** Those modules process only the audio clips (ADR 0019). A bounce enters at the start of the chain, so it would pass through them and sound different: "Modules before the instrument would change the bounce: move them after it, or bounce to a new track." This setup is rare and deliberate; changing its sound without asking is worse than declining.
- **A muted clip** (§6): it plays nothing, so there is nothing to bounce.

**To a new track: the whole chain, before the fader.** Every module with its automation is rendered. Tracks feeding the modules' sidechains play into the render muted, as in `render` (ADR 0025). Volume, pan and sends are left out, because the new track applies them itself (§5). Live's bounces always use this point (after the devices, before the mixer), and so does Logic when volume and pan aren't baked in.

## 4. In place: the source becomes a take

The bounced clip's takes follow ADR 0032:
- **A plain clip** gets two takes: "Before bounce" (the source, MIDI or audio) and "Bounce". The clip plays "Bounce", and its badge reads "2/2 ▾".
- **A clip with takes** gets "Bounce" as its newest take and plays it. Its other takes, MIDI or audio, stay as they were. A second bounce of the same clip is "Bounce 2".
- **A comp** (C5c) keeps its parts as one take, "Before bounce", so the comp can come back too.

**Going back** is choosing a take: click the "Before bounce" strip, or press Alt+↑. The clip is MIDI again and the instrument plays it, with its automation. To commit for good, use "Keep only Bounce" in the take menu; the files stay (ADR 0030). A clip's takes can now be of both kinds. The take code copies clip states whatever their kind, and the strips already draw notes and waveforms.

**The range** of the clip's takes grows to cover the tail (§7). The "Before bounce" take is shorter than the range, as a partial pass already is in recording.

## 5. To a new track: the mix follows

- **Placement:** the new track goes right after the source track, named after it ("Bass bounce"), in its colour, with no modules. One new track is made per source track, holding one audio clip per bounced clip, each where its source was.
- **Mix:** it copies the source's volume, pan and sends (level and pre/post), so it sits in the mix where the source did and still feeds the returns.
- **Automation:** where the source's volume, pan or a send is assigned to a lane, the new track's same parameter is assigned to that lane through the same range. A lane can drive any number of parameters on any channel (ADR 0029), so a volume ride keeps riding the bounce. Module automation is in the print already.
- **The source** clips are muted (§6), so the mix sounds the same; unmuting them layers the two or compares them. The source track and its modules are untouched.

## 6. Clip mute

Clips can be muted. Bounce to new track needs it, and every current DAW has it.
- **A muted clip stays where it is and plays nothing.** It's drawn dimmed (`clipDimmed`, as unused take strips are) with a small mute mark in its title strip.
- **0** mutes or unmutes the selected clips (one undo step, "Mute clips"). The same command is in the clip's menu and in the CLIP section of the inspector, as a Muted toggle.
- **Placement** rules don't change: a muted clip still takes its place, and a placed clip still cuts it (ADR 0019).
- **Storage:** tracktion's own clip mute, saved with the project; muted take clips keep their state.

## 7. Tails

- **How long:** a bounce runs past its clip's end until the sound stays below −90 dBFS, for at most 10 s, and never past the next clip on its track. The clip ends there, and never before its source's end, so the arrangement keeps its shape. tracktion's own early stop (at about −78 dBFS) isn't used: it cuts quiet release tails. Segno renders the allowance and trims the clip itself.
- **Clips bounced together on a track render in one pass.** Each bounce runs from its clip's start to the next bounced clip's start, so a tail that rings into the next clip is part of that clip's bounce. The bounces join without a seam and the run nulls. The clips point into one file per run, at offsets, as recorded loop laps do.
- **A tail cut by a clip that isn't bounced:** the clip ends with a 4 ms fade-out, so the cut doesn't click. The fade is tracktion's own clip fade, which C6 will show and make editable, at the length C6 uses for its automatic edge fades. The file keeps the whole tail, as Logic's do, so moving the next clip away and trimming the bounce out brings the tail back. When what's cut is audible (above −60 dBFS), a toast says so: "Pad 1's tail stops where Pad 2 starts. Bounce both to keep it."
- **To a new track,** the tail can run under the source track's next clip: the bounce is on another track, so nothing overlaps. Only the next bounced clip, or 10 s, ends it.

## 8. Running a bounce

- **Offline, on a copy** (`Session::render`): the project, its undo history and playback are untouched while it renders. Several bounces can run at once.
- **When it's done,** the result is applied as one undo step ("Bounce in place", "Bounce to new track"), and the bounced clips are selected. Undo takes the clips back and leaves the files; redo uses them again.
- **Progress** shows as a thin line along the bottom of each clip being bounced: the new shared `Progress` component, which export (C8) will use too. It appears only after a short delay (a motion token, 250 ms), so quick bounces don't flicker.
- **Escape** cancels the bounces in progress, unless a popover is open, which closes first. Nothing changes.
- **Editing while it renders** is allowed. If a clip being bounced is moved, edited or deleted, or a module the bounce renders changes, that bounce is dropped and a toast says why. Its file is removed, since nothing ever referred to it. Other edits don't affect it.

## 9. Files

- **Where:** `Bounces/` in the project's folder (ADR 0030). Before the first save they go to the unsaved project folder and move in when it's saved, as recordings do.
- **Format:** 32-bit float WAV at the engine's sample rate. Signals before the fader can pass 0 dBFS, and float keeps them whole. No dither and no normalising: a bounce must sound the same.
- **Names:** after the track and clip, e.g. `Bass - Bass 1 - Bounce.wav`, made unique with a number.
- **Never deleted implicitly:** undo, deleting the clip and "Keep only" leave the files. "Remove unused recordings" (M3) covers `Bounces/` too.

## 10. Menus, keys and selection

- **A clip's menu** (right-click on a clip; clips have had none so far) is built from the action registry, so each item shows its shortcut and dims when it can't run: Bounce in place, Bounce to new track, Mute, then Split at playhead, Duplicate, Delete. The take badge keeps its own take menu (ADR 0032).
- **Every command is in the command palette** (C1).
- **Afterwards:** a bounce in place selects the bounced clips; a bounce to a new track selects the new clips, so their track becomes the selected channel (ADR 0027).

| Action | ID | Shortcut |
|---|---|---|
| Bounce in place | `clip.bounce` | Ctrl+B |
| Bounce to new track | `clip.bounceToTrack` | Ctrl+Shift+B |
| Mute clips | `clip.mute` | 0 |

Ctrl+B bounces in Live (to a new track) and in Logic (regions in place) too, and none of the three is taken. A plain 0 is safe: the computer keyboard plays letters only, and Ctrl+0 (zoom to fit) stays.

## 11. Assistants (MCP)

Every feature comes with its tools and a test through MCP (ADR 0021).

| Tool | Arguments | Result |
|---|---|---|
| `bounce_clips` | `clips`, `newTrack?` (false) | Waits for the render. The new clips (id, track, start and length in beats, file), the new tracks if any, and any tails cut. One undo step, "Assistant: bounce clips". Refused, with the reason, for MIDI clips without an instrument or with audio modules before it, and for muted clips |
| `mute_clips` | `clips`, `muted?` (true) | The clips and their state. One undo step |

Clip results gain `muted`, and `get_takes` lists bounce takes like any other. **Checking a bounce:** `render` the track before and after, and compare. An assistant can bounce a part, measure that it nulls, and report.

## 12. Model and engine

- **`model::Bounce`** (JUCE-free, `[fast]` tests): whether a clip can be bounced in place or to a new track, and why not; how the selected clips form runs per track; each bounce's range (to the next bounced clip; the tail to the next clip, 10 s at most); where a tail is cut and needs its fade; the take names ("Before bounce", "Bounce N"); the new track's name.
- **`Session`:** `bounce(clips, BounceMode)` returns a job (progress, cancel), like `render`. When the job ends, the Session applies the result in one undo step:
  - in place: an audio clip for each source (file, offset, range), the source moved into the clip's takes;
  - to a new track: the track after the source, its mix copied and its lanes assigned, the clips placed, the sources muted.
- **The render** extends `Session::render` on its Edit copy:
  - only the bounced clips play (`Renderer::Parameters::allowedClips`);
  - for in place, the modules after the instrument and the mixer's own plugins are turned off in the copy (and every module, for audio clips);
  - the range runs to the tail allowance, and the clip ends are trimmed afterwards at −90 dBFS; a tail cut by the next clip gets its fade-out through tracktion's clip fade (`AudioClipBase::setFadeOut`);
  - the file is written at the engine's sample rate into `Bounces/`.
- **Watching for changes:** the Session's existing notifications (snapshot and module state changes) tell a running bounce when one of its clips or rendered modules changed; it then cancels.
- **Clip mute** is tracktion's `Clip::setMuted`, with `ClipInfo::muted` in the snapshot and the dimmed drawing in the arrangement's one clip painter (`clipBox`, `titleOf`).

## 13. Tests (Done when)

- **Nulls:** a Synth part on a track with an EQ, a Compressor and a volume lane, bounced in place, renders the same as before the bounce, to below −100 dBFS; likewise our VST3 as a third-party instrument (the stand-in used by the chain tests). A bounce to a new track nulls against the mix before it, with a send to a return and a lane on the source's volume.
- **Tails:** a long-release pad's bounce runs until silent; a tail ends at the next clip with its fade and the toast; two touching clips bounced together null as a run; 10 s at most.
- **Takes:** a plain clip gets "Before bounce" and "Bounce", plays the bounce, and plays the MIDI again after choosing it (one undo step); a recorded clip with takes gains "Bounce"; "Keep only" keeps the file.
- **When it isn't offered:** no instrument; audio modules before the instrument; a muted clip. Each is refused with its reason, in the UI and through MCP.
- **Running:** playback isn't interrupted; Escape cancels; editing a clip being bounced, or a module it renders, drops the bounce; one undo step when it lands; undo keeps the file, redo reuses it.
- **Files:** in `Bounces/`, 32-bit float at the engine's rate; an unsaved project's bounces move in when it's saved; a moved project folder still plays them.
- **Clip mute:** muting and unmuting (undo, saved and reopened); a muted clip is silent and drawn dimmed; it can't be bounced.
- **Model:** runs, ranges, tail cuts, availability and names (`[fast]`).
- **UI:** the clip menu, the shortcuts, the progress line (screenshots, standard and compact widths); the shell test bounces a clip end to end.
- **MCP:** `bounce_clips` in both modes and refused cases; `mute_clips`.
- **Manual:** `manual-tests/M2.md`, "Bounce (C7)".

## 14. Decisions from the review (product owner, 2026-10-10: every recommendation agreed)

| # | Question | Recommendation |
|---|---|---|
| Q1 | **What bounce in place renders** | The clip up to where it enters the chain: a MIDI clip through its instrument, an audio clip as it plays. The modules after it stay live, so the track sounds the same and stays editable (§3) |
| Q2 | **Where the source goes** | It becomes a take of the bounced clip ("Before bounce"), so going back is one click at any time (§4). This replaces "the MIDI is kept, muted" in the M2 plan: on one lane without overlaps (ADR 0019), a take is where a kept clip belongs |
| Q3 | **Audio modules before the instrument** | Bounce in place isn't offered there, with the reason, because the bounce would pass through them and sound different; bounce to a new track still works (§3) |
| Q4 | **Several clips** | One bounce per clip, keeping the arrangement's clips. Clips bounced together on a track render in one pass, so tails carry over and the run nulls (§7) |
| Q5 | **Tails** | Until silent (−90 dBFS), 10 s at most, never past the next clip. Where a clip that isn't bounced cuts a tail: a 4 ms fade on the clip, the whole tail kept in the file, and a toast when the cut is audible (§7) |
| Q6 | **Printing effects** | A second command, Bounce to new track: the whole chain before the fader, onto a new track that copies the mix and joins the source's lanes, with the source clips muted (§5) |
| Q7 | **Clip mute** | Added with C7 (key 0, the clip menu, the inspector), since printing to a new track needs it (§6) |
| Q8 | **Time ranges** | Clips only in M2; ranges with range selection, when range editing comes (ADR 0028) |
| Q9 | **Freeze** | Not in M2. Bounce in place covers its main use for instruments (CPU, reversible). Revisit with racks in M4, when chains get heavy (§15) |
| Q10 | **Files** | `Bounces/`, 32-bit float WAV at the engine's rate, no dither or normalising (§9) |
| Q11 | **While it renders** | Offline on a copy: playback continues; progress on the clips after 250 ms; Escape cancels; a change to what it renders drops it (§8) |
| Q12 | **Shortcuts** | Ctrl+B, Ctrl+Shift+B, and 0 for clip mute (§10) |

## 15. What comes when

| Part | Scope | When |
|---|---|---|
| **C7a Bounce in place** | MIDI and audio clips in place; the source as a take; tails and runs; files; progress and cancelling; the clip menu; MCP `bounce_clips` | M2 |
| **C7b Bounce to new track** | The whole chain before the fader; the new track with the mix and lanes; clip mute; MCP `bounce_clips` with `newTrack`, `mute_clips` | M2, after C7a |
| Bounce a time range | Across clips and gaps, with range selection | With range editing |
| Freeze | A track's chain rendered and asleep until thawed | Revisit in M4 (racks) |
| Resampling | A track's or the master's output as a recording input | M4 ([recording.md](recording.md) §15) |
| Bounce up to a module | A module's menu item "Bounce up to here": print the chain up to that module and keep the rest live (Bitwig's custom point, Reaper's "freeze up to selected FX") | With racks (M4) |
| Loop tails | Fold the tail onto the clip's start so a loop repeats seamlessly (tracktion's `wrapRemainder`; FL Studio's "Wrap") | When asked |
| Options people ask for | Mono, normalise, a bounce after the fader, several clips joined into one (Cubase's "block events") | When asked |
