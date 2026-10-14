# MCP server: design (M2 step C9)

The DAW's built-in MCP server ([ADR 0021](../decisions/0021-mcp-server.md)). An assistant the user connects can do what the user can do, read what the user can see, and render and measure the result.

**Status:** approved by the product owner (2026-10-09), with the recommendations in §9. Built in M2 step C9; what differs from this design, and what comes later, is in §10. Tool names and arguments become a public interface once released (ADR 0021).

## 1. Connecting

| Item | Design |
|---|---|
| **Off by default** | Turned on in the DAW's **Assistants** popover, opened from a new icon in the transport bar. The popover also shows the token, the port, the read-only switch, the connected clients, and a ready-to-copy setup line per client. |
| **HTTP** (Claude Code and other HTTP clients) | Streamable HTTP on `http://127.0.0.1:<port>/mcp`. Only the loopback interface; the `Origin` header is checked (DNS rebinding); every request needs `Authorization: Bearer <token>`. The default port is 7377; if it's taken, the next free one is used and shown. |
| **stdio** (clients that launch a process, like Claude Desktop) | `Segno --mcp-stdio` relays standard input and output to the running app's endpoint. It finds the app through a small file in the user data folder holding the port and the token. With no app running it starts one **headless** (see below). |
| **Headless** | `Segno --headless [--mcp-stdio] [project.segno]` runs a Session without a window, with the same tools. It is for batch work and tests. |
| **Token** | Generated per install, stored in the user data folder, and can be regenerated in the popover (which disconnects every client). |
| **Indicator** | A light beside the transport icon is lit for 3 s after a client's request; the popover lists the clients by name (`clientInfo`) and when each was last seen. |
| **Read-only mode** | Only tools marked read-only run (reading, rendering, measuring); the others return "The DAW is in read-only mode for assistants". |

Setup lines the popover offers:

```
claude mcp add --transport http segno http://127.0.0.1:7377/mcp --header "Authorization: Bearer <token>"
{"mcpServers": {"segno": {"command": "/path/to/Segno", "args": ["--mcp-stdio"]}}}
```

## 2. Protocol

| Item | Design |
|---|---|
| **Revisions** | 2026-07-28 (current: stateless, `server/discover`, no handshake) and 2025-11-25 (clients that still send `initialize`; the server answers both). 2025-06-18 and 2025-03-26 clients are served like 2025-11-25 ones. |
| **Capabilities** | Tools (with input and output schemas and annotations), resources and resource templates, `subscriptions/listen` change notifications (2025-11-25 clients: `resources/subscribe`), prompts, progress on long calls. The `tasks` extension is for renders and plugin scans that take longer than a few seconds (later, §10). |
| **Confirmations** | Multi round-trip requests (`input_required`) when a step can't be undone: discarding unsaved changes (new or open project), and overwriting another file when saving. 2025-11-25 clients get `elicitation/create`. Clients that can't ask the user (no elicitation capability) get an error telling the assistant to ask, then call again with `"confirm": true`. |
| **Left out** | Deprecated features (roots, sampling, logging), and MCP Apps. |
| **Errors** | Bad requests are JSON-RPC errors. A tool that can't do what was asked returns `isError: true` with a sentence saying why and what to do instead, e.g. "Track t:12 doesn't exist; read segno://project for the current tracks". |

## 3. Conventions (every tool)

- **One call, one undo step,** named after the tool: "Assistant: add notes". Ctrl+Z in the DAW takes back exactly what one call did. Read-only tools never touch the undo history.
- **The same paths as the user's edits.** Tools call `engine::Session` and the module host binding (ADR 0021): automation, undo and dirty state behave as for the user's own edits.
- **Ids** are strings with a type prefix: `t:` track, `c:` clip, `m:` module (an instrument or effect in a chain). They come from snapshots, and they stay the same within a project, across saves.
- **Time is musical.**
  - Positions and lengths are in **beats** (quarter notes) from the song's start; results also give the `bar.beat.sixteenth` form.
  - Inputs accept a number of beats or a string such as `"5.1.1"` (bar 5) or `"2 bars"`.
  - Seconds appear only where they are the natural unit (render length, measurements).
- **Notes:** `{pitch, start, length, velocity}`.
  - Pitch is a MIDI number, or a name with C3 = 60 ("D#4").
  - Start and length are in beats from the clip's start.
  - Velocity is 1–127.
- **Parameter values** are plain values in the parameter's unit (dB, Hz, ms, %), or text the parameter understands ("12 dB", "LP 24", "C3"). Results give the value, its text, the range and the default. Values out of range are clamped, and the result says so.
- **Batches over loops.** A tool takes many items where it makes sense (set several parameters, add many notes), so one musical idea is one undo step.
- **Annotations:** every tool declares `readOnlyHint`, `destructiveHint` and `idempotentHint` honestly.
- **No vendor names** in tools, results or prompts (ADR 0008).

## 4. Resources (read)

| URI | Content |
|---|---|
| `segno://project` | Name, file, unsaved state, tempo, time signature, length, loop, and every track with its clips (no notes) and its chain (module names and ids) |
| `segno://track/{id}` | One track: name, colour, instrument, clips, chain |
| `segno://clip/{id}` | One clip: kind, position, length, offset, source file; for MIDI, every note |
| `segno://module/{id}` | One module in a chain: type, on/off, the delay it adds (`latencyMs`: how late it plays its track, so an assistant can say what a linear-phase EQ costs live), every parameter (id, name, value, text, unit, range, default, automatable), its sections (id, title, collapsed) and its non-parameter state (e.g. the Sampler's sample) |
| `segno://modules` | The catalogue: every module type that can be added (ours and scanned plugins), with kind and short description |
| `segno://module-type/{type}` | One type's reference: what it does, each parameter's meaning (the SPEC's user-facing text), and its sections |
| `segno://transport` | Playing, position, loop, metronome |

The project, track, clip and module resources send change notifications when the snapshot changes or a module's parameters change. The transport doesn't (it changes constantly); assistants read it when they need it.

## 5. Tools

**Read** (read-only; resources by another route, for clients that prefer tools):

| Tool | Arguments | Result |
|---|---|---|
| `get_project` | – | As `segno://project` |
| `get_clip` | `clip` | As `segno://clip/{id}`, notes included |
| `get_module` | `module` | As `segno://module/{id}` |
| `list_module_types` | `kind?` (instrument, effect) | As `segno://modules` |

**Transport:**

| Tool | Arguments | Undo |
|---|---|---|
| `play` / `stop` | – | No (transport) |
| `set_position` | `at` | No |
| `set_loop` | `start`, `end`, `on?` | No |
| `set_metronome` | `on` | No |
| `set_tempo` | `bpm` | Yes |
| `set_time_signature` | `numerator`, `denominator` | Yes |
| `set_key` | `root` (a name or 0–11) and `scale?` (an ID or name; major by default), or `none: true` (M3 P3, [key-and-scale.md](key-and-scale.md) §14) | Yes |
| `list_scales` | – | No (reads) |
| `detect_key` | `clips?` (every MIDI clip by default): up to three major or minor keys that fit, with how well | No (reads; it only suggests) |

**Tracks and clips:**

| Tool | Arguments | Result |
|---|---|---|
| `add_track` | `name?` | The track id |
| `create_midi_clip` | `track`, `start`, `length?` (default a bar), `notes?` | The clip id. No instrument is added (ADR 0033): on a track without one the clip is silent |
| `insert_audio_file` | `track`, `start`, `path` | The clip id |
| `insert_midi_file` | `track`, `start`, `path` | The clip id (no instrument is added) |
| `move_clips` | `clips`, `by` (beats), `to_track?` | New positions (placing cuts what it covers, ADR 0019) |
| `trim_clip` | `clip`, `start?`, `end?` | The new extent. A looped clip's edges go anywhere (M3 C10) |
| `duplicate_clips` | `clips`, `times?` (default 1) | The new clip ids, placed after the originals |
| `split_clips` | `clips`, `at` | The new right-hand clip ids |
| `delete_clips` | `clips` | – (undoable, so no confirmation) |
| `mute_clips` | `clips`, `muted?` (true) | The clips; muted ones keep their place and play nothing (C7) |
| `loop_clips` | `clips`, `loop?` (true), `start?` (in the content: beats or "2.1.1"), `length?` (beats, "1 bar"; audio also "1.5 s") | The clips: each loops what it shows, or the loop given; with `loop: false` the repeats go. Refuses, changing nothing, a clip with takes or one stretched to the tempo (M3 C10, [clip-loops.md](clip-loops.md) §11). Clip results carry `loop` (start and length in beats; audio also in seconds), null while a clip doesn't loop; in a looped clip, notes count from its loop's start, and notes written past its end lengthen the loop (`loopLengthened`) |
| `set_fades` | `clips`, `in?`, `out?` (ms or text: "10 ms", "0.5 s", "1/16"), `inBend?`, `outBend?` | The clips' fades ([fades.md](fades.md) §7) |
| `crossfade` | `clips`, `length?` (10 ms), `shape?`, `placement?` ("before", "centred", "after"), `remove?` | Each join between them: its crossfade (with its placement), or why it couldn't be made or placed |
| `set_edge_fades` | `on` | The project's edge fades on or off |
| `bounce_clips` | `clips`, `newTrack?` (false) | Renders them to audio ([bounce.md](bounce.md) §11): in place, through each clip's instrument, keeping its source as a take; or to a new track through the whole chain, muting the source. Waits for the render (with progress); one undo step when it lands. The bounced clips, the new tracks, and any audible tails a clip that wasn't bounced cut |

**Notes** (MIDI clips; every tool is one undo step; notes past the clip's end lengthen it as in the piano roll, B15):

| Tool | Arguments |
|---|---|
| `add_notes` | `clip`, `notes` (each may be `muted`) |
| `set_notes` | `clip`, `notes`: replaces them all |
| `remove_notes` | `clip`, `where`: all, or a filter (pitch range, time range) |
| `edit_notes` | `clip`, `where`, `transpose?`, `shift?` (beats), `velocity?` (set, or ± with a sign), `quantise?` (grid in beats) with `strength?` and `swing?` (0–1); and the clip view's Notes menu (M3 P4, [piano-roll.md](piano-roll.md) §14): `transpose_steps?` (steps of the song key's scale), `fit_to_key?`, `stretch?` (a factor from the first note; negative reverses), `legato?`, `fixed_length?`, `join?`, `split_at?`, `reverse?`, `mirror?`, `invert?` (up, down), `voicing?` (smooth, close, open, drop2, drop3, spread), `mute?`, `add_bass?`; applied in that order |
| `get_chords` | `clip`: the chords its notes make, as the chord lane names them: start, length, name, step (a Roman numeral in the song's key, or null), pitches (M3 P4) |
| `set_chord` | `clip`, `chord` (a name, "Dm7", "C/E", or with a key a numeral, "ii7", "bVII", "V7/V"), `start`, `length?` (a bar), `kind?`, `voicing?`, `inversion?`, `velocity?`: writes it there, or rewrites the chord there keeping its timing and velocities, voice-led |
| `make_chords` | `clip`, `where`, `kind?` (triad, seventh, ninth, sus2, sus4, add9), `voicing?`, `inversion?`: each note the root of the key's chord on it |
| `suggest_chords` | `clip`, `at`, `kind?`: the key's chords, the usual next ones after the chord before (with why) and borrowed ones, as the Chord popover offers (reads) |

`get_clip`'s notes say `"muted": true` for a muted note (only then).

**Modules** (ours, and third-party plugins as far as their parameters go):

| Tool | Arguments | Result |
|---|---|---|
| `add_module` | `track`, `type`, `position?` | The module id, with its parameters |
| `remove_module` | `module` | – |
| `move_module` | `module`, `position` | – |
| `set_module_on` | `module`, `on` | – |
| `set_parameters` | `module`, `values`: `{param id: value or text}` | Each value as set (with its text), and any that were clamped or unknown |
| `load_sample` | `module` (a Sampler), `path` (a sample or an SFZ file) | The sample's length (a multisample's longest) and root key, the zones, how many samples are missing and what in the file isn't played; with what the file sets (root key; an SFZ's mode, loop, velocity to volume, amp envelope), one undo step ([devices/sampler/SPEC.md](../../devices/sampler/SPEC.md), "Stage 2") |
| `get_zones` | `module` (a Sampler) | What it plays: the file, and each zone's sample, keys and velocities (low, high), root, tuning, gain, round robin and random share; missing ones marked. Read-only |
| `list_wavetables` | `category?` | The Wavetable Synth's tables: the factory ones (id, name, category, frames) and the files in the Segno folder's Wavetables (path, name). Read-only |
| `set_wavetable` | `module` (a Wavetable Synth), `oscillator` (1–3), `table` (a factory id) or `path` (a WAV or `.wt` file) | The table as set: its id (`user` for a file, carried in the project), name and frames; one undo step ([devices/wavetable/SPEC.md](../../devices/wavetable/SPEC.md), "Tables and their files") |
| `make_wavetable` | `module` (a Wavetable), `oscillator` (1–3), `clip` (an audio clip, with `from?`, `to?` on the timeline) or `path` (a sound file), `mode` (`pitched`, `spectral`, `slices`), `frames?` (8–256, default 64) | The table made from the audio as Clip > Make wavetable makes it, its recipe kept: its name, frames and the pitch found; one undo step ([table-editor.md](table-editor.md) §5) |
| `get_wavetable` | `module`, `oscillator`, `data?` | The table it plays (id, name, frames) and its recipe: the one the table editor made, or (`edited` false) one of the table, to start from. The frames of audio, image and file layers are left out (their count instead) unless `data` asks for them. Read-only |
| `edit_wavetable` | `module`, `oscillator`, `recipe` | The recipe replaced and baked, checked whole first; a layer that leaves out its frames keeps those of the layer at its place now; one undo step |
| `export_wavetable` | `module`, `oscillator`, `path?`, `format?` (`wav`, `wt`) | The table written as WAV (`clm `) or `.wt`, by default into Segno/Wavetables named for it; the file's path. Not an undo step |
| `set_mix` | `track` (a track, return or the master), `volume?` (dB, "-inf"), `pan?` (−1 … 1, "25L"), `mute?`, `solo?`, `exclusive?` | The channel's mix; one undo step (solo alone: none) ([mixer.md](mixer.md) §10) |
| `add_return_track` | `name?` | The return's id and name |
| `set_send` | `track`, `return`, `level?` (dB), `preFader?` | The send as set |
| `measure_mix` | `seconds?` (0.5) | Every channel's peak over the window (dBFS), and whether anything clipped |
| `add_automation_lane` | `after?`, `name?`, `parameters?` (each with `from?`, `to?`) | A lane of its own ("a:12"), at the end of the tracks unless `after` says where, assigned to those parameters (C4, [automation.md](automation.md) §11); named `name`, else the next number, "Automation 3" ([tracks-and-clips.md](tracks-and-clips.md) §18) |
| `set_automation_lane` / `remove_automation_lane` | `lane`, `name` / `lane` | Renames it (never empty: refused) / deletes it ("Delete an automation lane"; the released name stays) |
| `move_automation_lane` | `lane`, `before?` (a track or a lane) | Moves the lane right above that row, or to the end of the tracks and lanes; nothing else moves ([ADR 0042](../decisions/0042-every-row-moves-on-its-own.md)) |
| `assign_automation` / `unassign_automation` | `lane` and a parameter (`module` + `parameter`, or `track` + `volume`/`pan`/`send` + `return`), `from?`, `to?` / a parameter | What a lane drives, each through its range; `movedFrom` when the parameter was on another lane |
| `get_automation` | `lane` or a parameter, `from?`, `to?` | Points (position, lane value, value and text in the parameter's units, bend), assignments with ranges, whether it plays |
| `set_automation` | `lane` or a parameter, `points` (position, value or text, `bend?`), `from?`, `to?` | Replaces the points (or those in the range); by parameter, through its range, making a lane if it has none |
| `clear_automation` | `lane` or a parameter, `from?`, `to?` | Removes points (by parameter: refused on a shared lane) |
| `set_automation_on` | `lane`, a parameter and `on?`, or `all` | Turns assignments back on (not an undo step) |
| `set_module_sidechain` | `module`, `source` (a track, or null for none) | The source; `get_module` reports `sidechain: {source, possibleSources}` for modules with a sidechain input ([ADR 0025](../decisions/0025-sidechain-routing.md)) |
| `set_module_view` | `module`, `collapsed?` (section ids), `folded?`, `page?` (a page id, for a module with pages: [ADR 0051](../decisions/0051-pages-for-very-wide-modules.md)) | – (view state: never an undo step); `get_module` lists the pages with their sections, and the one showing |
| `get_grid` | – | Each editor's grid ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md)): `arrangement` and `piano_roll`, each with `mode` (adaptive, fixed), `width` (narrowest … widest), `value` ("8 bars" … "1/32"), `triplets`, `snap`, and the `step` it snaps to now ("1/16", "1/8T") |
| `set_grid` | `editor` (arrangement, piano_roll), `mode?`, `width?`, `value?`, `triplets?`, `snap?` | That editor's grid as set, with its step (view state: never an undo step) |
| `list_presets` | `module` or `type` | A module's presets (Default settings, factory, yours: id, name, source, tags, description, author, favourite), and for a module the one it came from, whether it changed since, and whether A or B shows ([presets.md](presets.md) §14) |
| `load_preset` | `module`, `preset` (id or name), `source?` (factory, yours) | Loads it into the setting shown; one undo step |
| `save_preset` | `module`, `name`, `tags?`, `description?`, `author?`, `replace?` | Saves one of yours (refused for a name in use unless `replace`); assistants never delete presets |
| `compare` | `module`, `action` (switch, copy) | A/B: which shows now |
| `scan_plugins` | – | The number found; long, so it runs as a task with progress |
| `search` | `query`, `kinds?`, `limit?` (20) | What the browser finds ([browser.md](browser.md) §6): modules, presets, plugins, and the samples, multisamples, wavetables, MIDI files, projects and folders in its places, best first, each with an id the other tools take (a module's `type`, a preset's id, a file's `path`). Read-only |
| `browse` | `folder?` | The browser's places, or what's in one of their folders (folders, and files with their length and tempo when known). Read-only |
| `preview` | `item` (an id or a path), `stop?` | Plays it for the person as the browser would: a sample (in time with the song when it fits), a MIDI file on the live MIDI track, an instrument preset's phrase. Allowed in read-only mode |
| `collect_files` | – | File > Collect files and save: outside files copied into the project's Samples, then saved; refused before a first save. Assistants never delete files |
| `get_settings` | – | Every setting, by section ([settings.md](settings.md) §11): audio (driver, devices, rate, buffer, its latency, the rates and buffers offered), MIDI inputs, recording, library and plugin folders, a new project's export settings, the shortcuts changed, the interface's size, the assistants' switches. Read-only |
| `set_settings` | `recording?`, `export?`, `display?`, `midi?`, `audio?` (each an object of the settings it changes) | Changes them at once, for every project (not an undo step); everything is checked first, so a refused call changes nothing. `assistants` and `keys` are refused: an assistant never changes its own permissions or your shortcuts |

For third-party plugins, `set_parameters` uses the plugin's own parameter names and normalised values (0–1), on a best-effort basis.

**Render and measure** (read-only; nothing in the project changes):

| Tool | Arguments | Result |
|---|---|---|
| `render` | `start`, `end`, `what?` (the mix, or a track with whatever feeds its sidechains), `audio?` (false) | Measurements of the rendered audio: peak (dBFS) and true peak (dBTP), RMS, loudness (LUFS: integrated, the loudest 3 s and 400 ms), crest factor, clipped samples, the level in seven bands (sub to air, the channels' average), and the stereo correlation. With `audio: true`, the audio itself too (16-bit WAV, 30 s at most; §9) |
| `measure_module` | `module`, `seconds?` (0.5; 0.1 to 5) | What one of our modules' meters show over that window, by name, each in its unit: levels (dBFS, dBTP), gain reduction (dB), a gate opening, loudness (LUFS), loudness ranges and differences (LU), stereo correlation (−1 to 1), voices. Says when nothing was playing. Modules declare their meters (`DeviceInfo::meters`); the measurement reads its own copy of each peak, so the panel's meters lose nothing. Spectra (the EQ's analyzer) aren't meters: `render` measures bands |

**Project:**

| Tool | Arguments | Notes |
|---|---|---|
| `undo` / `redo` | `steps?` | Says what was undone |
| `save_project` | `path?` | Confirms before overwriting another file |
| `new_project` / `open_project` | `path` | Confirm before discarding unsaved changes |

**Prompts:** a few named starting points, optional for clients:
- `start-a-track` (tempo, a drum pattern and a bassline as starting material);
- `check-the-mix` (render, measure and suggest);
- `explain-module` (what a module in the chain does and what to try).

## 6. Coverage

Every DAW feature ships with its tools (the coverage rule, ADR 0021). Today:

| Feature | Tools |
|---|---|
| Transport, tempo, signature, loop, metronome | Transport tools |
| Tracks: add, rename, recolour, move, duplicate, delete (C11) | `add_track`, `set_track`, `move_track`, `duplicate_track`, `delete_tracks`; lanes move with `move_automation_lane` (ADR 0042) |
| Audio and MIDI clips: insert, move, trim, duplicate, split, delete; rename and recolour, copy to a place (C11) | Track and clip tools; `set_clips`, `copy_clips` |
| Piano roll | Note tools; its chords (M3 P4, ADR 0058): `get_chords`, `set_chord`, `make_chords`, `suggest_chords`, and the Notes menu's edits in `edit_notes` |
| The editing grid of each editor (ADR 0047) | `get_grid`, `set_grid` |
| Module chains: add, remove, reorder, bypass, parameters, Sampler sample, collapse and fold | Module tools |
| Presets and A/B (M3 A1, ADR 0044) | `list_presets`, `load_preset`, `save_preset`, `compare`; `add_module` takes a `preset` |
| Plugin scanning | `scan_plugins` |
| Settings (M3 C1, ADR 0045) | `get_settings`, `set_settings` (never the assistants' own or the shortcuts) |
| The browser (M3 C2, ADR 0046) | `search`, `browse`, `preview`, `collect_files`; loading through `add_module` (with `preset`), `insert_audio_file`, `insert_midi_file`, `load_sample` (a sample or a multisample) |
| The song's sections (M3 C3, ADR 0049) | `list_sections`, `add_section`, `set_section`, `move_section`, `copy_section` (before or after a section, or `at` a point), `insert_section`, `delete_section` (section ids are `s:`); `get_project` lists the sections |
| The song's key (M3 P3, ADR 0057) | `set_key`, `list_scales`, `detect_key`; `get_project` has the key (root, scale, name, notes; ASCII # and b) or null |
| Projects, undo | Project tools |
| Live MIDI (computer keyboard, inputs) | Not a tool: a live performance input, not an edit. A `preview_note` tool may come later |

Each feature brings its tools with it: managing tracks and clips (C11) brought `set_track`, `delete_tracks`, `move_track`, `duplicate_track`, `set_clips` and `copy_clips` (clips report their colour; a clip's colour is its own, and `set_clips` with a `null` colour gives each clip its track's, once: ADR 0043); recording (C5), fades (C6) and bounce (C7) brought theirs. Automation (C4) has its lane tools, and `get_project` lists the automation lanes with their assignments.

## 7. Architecture

```
app/src/mcp/
  protocol/   JSON-RPC 2.0, revisions, discovery, the tool/resource/prompt registry with JSON schemas,
              result building. JUCE-free and unit-tested like app/src/model.
  tools/      one file per area (transport, clips, notes, modules, render, project): thin, over
              engine::Session and the module host binding.
  transport/  Streamable HTTP (on cpp-httplib, MIT, pinned in cmake/Dependencies.cmake), the stdio
              relay, the token and Origin checks.
  McpServer   owns the above; runs every tool on the message thread.
```

- **Threads:** HTTP requests arrive on the server's thread. Each tool call is posted to the message thread, where the Session lives, and the reply waits for it. Renders run on a worker, with progress on the response stream.
- **Session support to add in C9:**
  - named undo steps (`Session::beginEdit(name)`);
  - an offline render of a range (`Session::Impl::renderCopy`), shared by bounce (C7) and later export (C8);
  - snapshot change notifications for resources (`onSnapshotChanged` exists);
  - parameter reads and writes through `DeviceHandle` (exist).
- **Boundaries:** nothing below `app/` includes the MCP code; the plugins never open connections. A CTest boundary check enforces it (`CLAUDE.md`).

As built:

| Part | Where | Notes |
|---|---|---|
| Protocol | `app/src/mcp/protocol` (`Dispatcher`, `Schema`, `Encoding`, `Registry`) | Both eras on one endpoint; legacy sessions with their notification stream; MRTR state kept server-side for 10 minutes, used once; argument validation against each tool's schema (the subset in `Schema.h`). |
| Conventions | `app/src/mcp/Conventions` | Typed ids, positions and lengths, note names, velocities. |
| HTTP | `app/src/mcp/transport/HttpServer` | cpp-httplib; a fresh server per port tried, never `SO_REUSEPORT`, so a second DAW moves to the next port. JSON responses, or SSE for subscriptions, progress and legacy confirmations. |
| stdio | `app/src/mcp/transport/StdioRelay`, `app/src/Main.cpp` | `--mcp-stdio` relays to the endpoint in the endpoint file, or runs headless when none answers. The protocol gets the real standard output; everything else printed goes to standard error. |
| Tools | `app/src/mcp/tools/*` | Each body runs on the message thread (`MainThread`), with the latest snapshot published first; edits run inside `Session::EditGroup`. |
| Server | `app/src/mcp/McpServer` | Settings (`assistants.json`), the token (`mcp-token`) and the endpoint file (`mcp-endpoint.json`: URL, port, token) in the user data folder, the latter two readable by the user only. Resource notifications: after any change, each subscribed resource is read again (at most ten times a second) and announced when its content differs. |
| Popover | `app/src/ui/AssistantsPopover` | See daw-shell.md, "Transport". |

## 8. Testing

- **Protocol** (fast): every request type for both revisions; bad JSON and bad arguments; errors; discovery; schema validation of every tool's declared input and output.
- **Tools:** an in-process client drives every tool against a real Session. It checks the effect, one named undo step, read-only refusals, and confirmations.
- **HTTP:** a loopback test with the token and with a wrong `Origin`; the stdio relay against a headless app.
- **Real assistants:** a session with Claude Code that builds a short loop (a drum pattern, a bassline, a filter sweep's settings), checked by ear. Product owner: setup from the popover in both clients.

## 9. Decisions

Decided with the product owner on 2026-10-09 (open questions B18–B21).

| # | Question | Decision |
|---|---|---|
| Q1 | **Which files may an assistant read?** (`insert_audio_file`, `load_sample`, `open_project` take paths) | Any file the user's account can read, as the DAW itself can; the popover can narrow it to chosen folders later |
| Q2 | **Audio in render results** (`audio: true`): assistants that accept audio could listen | Allowed, capped at 30 s of 48 kHz stereo (about 11 MB as WAV); longer renders give measurements only |
| Q3 | **Default port** | 7377, then the next free one |
| Q4 | **Should the stdio relay start the app when it isn't running?** | Yes, headless, so batch jobs work; a running app is preferred when there is one |

## 10. Built, and later

Built in M2 step C9 as designed above, except:

| Item | Status |
|---|---|
| `measure_module` | Built with the dynamics devices (B1–B3): devices name their meters (`MeterSpec`), and peak slots have a second reader for measurements. |
| True peak and LUFS in `render` | Done with the dynamics core (A1). |
| The tasks extension | Later. Long calls (render, plugin scans) stream progress notifications on their response instead. |
| Module type reference | Gives each module's purpose (from its SPEC), its parameters (what each does, range, default, unit, choices) and its sections. Parameter descriptions came with B22. |
| Read-only mode | Transport tools (play, stop, position, loop, metronome) count as changes and are refused too. |

Tests: `fd_mcp_tests` (protocol, conventions, HTTP and the relay, `[fast]`), `McpToolTests` (every tool against a real Session: one named undo step per edit, read-only mode, confirmations, render, resources and their notifications, HTTP end to end), the popover in `AppSmokeTests`. The stdio relay and headless mode were run by hand; the product owner's checks are in `manual-tests/M2.md`.
