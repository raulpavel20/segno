# Groups: tracks summed into one channel

A **group** holds tracks and sums them. Put the drums in a group and they share one fader, one chain of modules (a bus compressor, a saturator), one mute and solo, and the group's own sends. A group folds to a single row in the arrangement, or a single strip in the mixer, with its tracks drawn in miniature in its lane. Groups nest: a Drums group can sit inside a Rhythm group.

**Status:** approved by the product owner (2026-10-17), every recommendation of G1–G23 but one, with [ADR 0072](../decisions/0072-a-group-is-a-channel-that-sums-tracks.md) accepted. The exception is editing on a group's row (G16, §6.5): a drag there selecting the members' clips was a stand-in for a time selection, which Segno lacks. The product owner chose the real thing: **selecting a stretch of time** is designed and built first, everywhere it makes sense ([time-selection.md](time-selection.md)), and a group's row uses it. Groups are built after it.

What the product owner asked (2026-10-17):
- "We need to implement a new feature. For a buss/group. I think calling it a group is fine."
- "It should act more or less like Ableton's groups, but let me know if your market research leads you to other suggestions."

## 1. What's there now

- **Three kinds of channel** ([ADR 0026](../decisions/0026-mixing-in-the-arrangement.md), [mixer.md](mixer.md)):
  - tracks: clips, a chain, sends;
  - return tracks: a chain, no clips, no sends;
  - the master.

  Every track and return goes to the master. There's no output routing, so nothing can be sent anywhere else.
- **Groups were planned for later.** ADR 0026 says "Group tracks, return-to-return sends and sidechains from returns wait for routing work with racks (M4)". [mixer.md](mixer.md) §2 says the same. [tracks-and-clips.md](tracks-and-clips.md) §16 lists "groups and folder tracks" and "selecting several tracks" as later work.
- **The selection holds one channel** ([ADR 0027](../decisions/0027-selection-and-context-panels.md)). Making a group from several tracks needs several selected. [mixer.md](mixer.md) §8 and [tracks-and-clips.md](tracks-and-clips.md) §16 already say what several selected tracks do, but none of it is built.
- **Automation lanes are rows of their own** among the tracks, each stored as "after a track" ([ADR 0029](../decisions/0029-independent-automation-lanes.md)). Every row moves on its own ([ADR 0042](../decisions/0042-every-row-moves-on-its-own.md)).
- **Ctrl+G is taken:** Module > Group into rack. Ctrl+Shift+G is Ungroup rack ([racks.md](racks.md) §9). Live uses the same pair for tracks and devices, acting on whichever is selected.
- **Solo is solo in place.** Soloing silences every track that isn't soloed, but never the returns or the master. A track that isn't heard sends nothing to the returns (`engine::makeEngineBehaviour`).
- **Export** writes the mix, or stems of the selected tracks or of all of them. With all tracks, the stems played together through the master make the mix ([export.md](export.md) §2).
- **Bounce to new track** prints a track's chain onto a new track ([bounce.md](bounce.md)).
- **The song row** draws every track as a thin line where it has clips ([arranger.md](arranger.md) §6).
- **The engine has what groups need** (§15). tracktion's `FolderTrack` becomes a *submix* once it holds any plugin other than a VCA. A submix:
  - sums the tracks inside it, at any depth, then runs its own chain;
  - holds no clips;
  - passes mute down, so a member of a muted folder is muted;
  - passes solo both ways: a soloed folder's members are heard, and a soloed member keeps its folder heard;
  - can render a member alone, without the folder's chain.

  One thing is missing: a submix can't be a sidechain source, because tracktion makes the sidechain's send only for audio tracks. That needs a small patch.

## 2. How others do it (October 2026)

| | What it is | Made with | Nesting | The folded row | Clips on the group | Bounce, freeze | VCA |
|---|---|---|---|---|---|---|---|
| **Live 12.4** | One kind, the Group Track, always summing | Ctrl+G, Ctrl+Shift+G; scattered tracks pulled together | Since Live 10, no limit stated | An overview of the members' clips. Since 12.2 a nested group is one lane, and the overview zooms with the row's height. Time selections, moving clips and bounces work on it | None: "cannot contain clips" | Bounce Group in Place, Bounce Group to New Track (12.3); no freeze | None |
| **Bitwig 6.1** | One kind, the Group Track, always summing | Ctrl+G, Ctrl+Shift+G | Unlimited | **Meta clips**: editing them edits the children. A menu "enters" a group level in the Arranger and Mixer | Audio clips replace the group's output; note clips feed its chain (6.0) | Bounce in Place from the group's lane; no freeze | None |
| **Logic 12.4** | Track Stacks: a **Folder Stack** (its main track a VCA, routing unchanged) or a **Summing Stack** (a bus) | Shift-Cmd-D asks which; Shift-Cmd-H folder, Shift-Cmd-G summing; scattered tracks pulled together | Either kind in either | An overview of every subtrack; a collapsed stack shows all their automation | MIDI regions on a summing stack's main track play its instruments | No direct bounce or freeze (users report) | VCA groups, apart |
| **Cubase 15** | A folder that doesn't sum, which can be given a **Group Channel** (15.0.20); group channels and VCAs apart | Move Selected Tracks to New Folder (with Group Channel) | Folders nest; groups feed groups | **Folder parts**: editing one edits everything inside | No | Render Selection with the complete signal path | Yes |
| **Studio Pro 8.1** | A folder track with a **Bus** selector; Ctrl+G makes an edit group | Pack Folder | Folders nest; the bus takes only direct children | One event: sized, moved, cut, copied, duplicated | No | A bus rendered to one track, and back | Yes |
| **REAPER 7.82** | Any track can be a folder parent, which is a bus | The folder icon, a drag, Move tracks to folder | Unlimited | Faint peaks of the children; four collapse sizes | The folder is a track: it holds items | Folders render as stems | VCA lead/follow |
| **Pro Tools 2026.4** | A **Basic** folder (tidy) or a **Routing** folder (an aux) | Move To New Folder | Nine levels | The Overview; edits there apply to every member | No | Routing folders freeze and commit | VCA master tracks |

- **FL Studio** groups playlist tracks only to tidy them. The mixer's routing does the summing, and groups don't nest. **Ardour** has route groups (linked controls) and subgroup busses, but no folders. **Waveform** has a folder (a VCA, no audio through it) and a submix (summing). Adding a plug-in turns a folder into a submix, which is the engine rule Segno inherits (§15).

**Most agree on:**
- **A parent row** with its members under it, folded with a triangle. Moving the parent moves the members.
- **Members end up together.** Grouping scattered tracks gathers them (Live, Logic, Pro Tools).
- **Nesting.** Only Pro Tools states a limit (nine).
- **A folded row that shows the members' clips, and edits them.** Live's time selection, Bitwig's meta clips, Cubase's folder parts, Studio Pro's one event and Pro Tools' Overview all act on everything inside. Logic is the unclear one.
- **Group mute and solo act on the members, and soloing a member keeps its group heard:** Live, Bitwig, Logic, REAPER, Pro Tools' routing folders.
- **Members' sends go straight to the returns,** so the group's fader doesn't turn their reverb down. This is the most cited reason mixing DAWs add VCAs.
- **Groups have modules and sends, can be sidechain sources, and export as stems.**

**Where they differ:**
- **One kind or two.** Live, Bitwig and REAPER always sum. Logic, Pro Tools, Cubase, Studio Pro and Waveform have a second kind that doesn't, and it's often a VCA rather than a plain folder. The difference needs explaining: Production Expert has tutorials on it, and a Cubase user asked why both exist when "they both are containers". The trend runs one way: Pro Tools (2020), Cubase (15.0.20) and Studio One added summing to their folders. No summing-first DAW added a folder that only tidies.
- **VCAs:** every mixing-heritage DAW has them. Live, Bitwig and FL don't.
- **Returns inside a group:** Bitwig, Logic, Pro Tools, REAPER and Cubase allow them. Live and Studio Pro don't.
- **Ungrouping:** Logic keeps a main track that does something, Pro Tools asks, Cubase sends members to the main mix. Live doesn't say what happens to the group's devices.
- **Folding in the mixer:** Cubase folds the mixer with the arrangement, and users complain that closing a folder hides its channels. Studio Pro makes the link optional, and Logic's mixer has "Follow Track Stacks".
- **Colours:** Live has a command, "Assign Track Color to Grouped Tracks and Clips". Cubase can colour a folder's tracks automatically, and Bitwig tints them. Pro Tools never passes a folder's colour on.

Sources:
- **Live:** [mixing: group tracks](https://www.ableton.com/en/live-manual/12/mixing/#group-tracks), [arrangement](https://www.ableton.com/en/live-manual/12/arrangement-view/), [bouncing groups](https://www.ableton.com/en/live-manual/12/bounce-to-audio/#bouncing-group-tracks), [shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/#commands-for-tracks), [routing](https://www.ableton.com/en/live-manual/12/routing-and-i-o/#internal-routings), [freeze](https://www.ableton.com/en/live-manual/12/computer-audio-resources-and-strategies/#track-freeze), [export](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/#selection-options), [Live 12 release notes](https://www.ableton.com/en/release-notes/live-12/), [Live 10 release notes](https://www.ableton.com/en/release-notes/live-10/).
- **Bitwig:** [tracks](https://www.bitwig.com/userguide/latest/intro_to_tracks), [6.0 release notes](https://downloads.bitwig.com/stable/6.0/Release-Notes-6.0.html), [5.0 release notes](https://downloads.bitwig.com/stable/5.0/Release-Notes-5.0.html), [export](https://www.bitwig.com/userguide/latest/exporting_audio).
- **Logic:** [track stacks](https://support.apple.com/guide/logicpro/track-stacks-overview-lgcp9bc4b63d/mac), [creating them](https://support.apple.com/guide/logicpro/create-and-edit-track-stacks-lgcp5bafa811/mac).
- **Cubase:** [folders with a group channel](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/track_handling/track_handling_folder_tracks_group_channel_enabling_t.html), [folder parts](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/parts_and_events/parts_and_events_folder_parts_c.html), [users on closing folders](https://forums.steinberg.net/t/closing-a-non-group-track-folder-hides-channels-from-mixer/1027949).
- **Studio Pro:** [folder tracks](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/Folder_Tracks.htm).
- **REAPER:** [user guide](https://www.reaper.fm/userguide/ReaperUserGuide781c.pdf), [what's new](https://www.reaper.fm/whatsnew.txt).
- **Pro Tools:** [reference guide 2026.4](https://resources.avid.com/SupportFiles/PT/Pro_Tools_Reference_Guide_2026.4.pdf).
- **Waveform:** [folder tracks](https://tracktion.github.io/waveform_manual/latest/folder-tracks/), [submix tracks](https://tracktion.github.io/waveform_manual/latest/submix-tracks/).
- **Comparisons:** [Production Expert: folders, submixes or VCAs](https://www.production-expert.com/production-expert-1/pro-tools-folders-submixes-or-vcas-which-should-you-use), [Sound On Sound: VCA Team](https://www.soundonsound.com/techniques/vca-team).

**What we take from Live,** as the product owner asked:
- **One kind of group, always summing.** This is Live's, Bitwig's and REAPER's model, and where the two-kind DAWs have been heading.
- **Ctrl+G and Ctrl+Shift+G,** shared with racks as Live shares them with devices.
- **Scattered tracks gathered** when they're grouped.
- **Delete takes everything inside** the group.
- **Nesting.**
- **No clips on a group, and no returns in one.**
- **An overview of the members' clips** in the group's lane, with a nested group as one line (12.2) and a size that follows the row's height.
- **Editing through the folded row** with a time selection, as Live's, built first for the whole arrangement ([time-selection.md](time-selection.md), §6.5).
- **Mute and solo** as Live has them.
- **A group as a sidechain source.**
- **The fold key, U,** as Alt+U, because a plain letter plays a note here.
- **Live's one-off colour command** (§4).

**Beyond Live:**
- **Each view folds on its own** (§6.4). Linking them is Cubase's most cited annoyance.
- **Stems that still add up to the mix,** with groups whole (§11).
- **The sidechain picker leaves out groups that would make a loop** (§7.5).

**Left out:**
- **Bitwig's clips on a group.** They make a group two things at once: a sum and a track.
- **Bitwig's "enter a group" navigation.** Folding is enough below a few levels.
- **VCAs** (§16).

## 3. Principles

1. **One thing, called a group.** A group always sums its tracks. A group with no modules, at 0 dB and centre, changes nothing in the sound (§7). That makes it the folder that only tidies, too. There's no second kind to choose between (§2).
2. **Like Live where Live is right,** as the product owner asked. Beyond it where another DAW does better, and the doc says so each time.
3. **A group is a channel like the others.** It has a chain, a mix, sends, a meter, automation, modulators and a sidechain out, through the same components and Session commands as a track. The only new things are what holding tracks needs: folding, nesting and the members' miniature.
4. **Folding is only a view.** It changes what you see, never what you hear, and never the selection. It's saved with the project and is never an undo step.
5. **In place first, everything by mouse** ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)). A group is made, filled, emptied and folded where the tracks are, and every command is in the menu bar. Assistants get the same ([ADR 0021](../decisions/0021-mcp-server.md)).
6. **Drawn before it's built.** As with racks and the Drum Kit, the UI is drawn in every state and approved before anything is built (§17, step 0).

## 4. What a group is

- **A channel that holds tracks and sums them.** The members' sound goes into the group, through its modules, its volume and its pan, and on to the group it's in, or to the master.
- **It holds tracks and other groups.** Groups nest to any depth (§6.3). Return tracks and the master are never in a group (§7.4).
- **It holds no clips.** Its lane shows its members in miniature instead (§6.2).
- **Its chain** takes what a return's takes: audio effects, racks of effects, modulators and plugins. It takes no instruments, because no notes reach a group. Dropping an instrument on a group makes a track in it instead (§11).
- **Its mix:** volume, pan, mute, solo, a send to each return, and a meter. It has no arm: it records nothing.
- **Never empty.** A group is made with its tracks (§5.1). When its last member is moved out or deleted, the group goes too, in the same undo step, and the toast says so.
- **Its name:** "1 Group", "2 Group"…, one past the highest number in use, as tracks are numbered (`model::nextTrackName`, [tracks-and-clips.md](tracks-and-clips.md) §18). Live names them the same way. You rename a group as you rename a track: double-click, F2 or the inspector.
- **Its colour** is its own ([ADR 0043](../decisions/0043-a-clips-colour-is-its-own.md)). A new group takes its topmost member's colour once, so a folded Drums group still looks like the drums. Members keep their own colours.
  - **Give tracks the group's colour** recolours its members and their clips once, as Live's "Assign Track Color to Grouped Tracks and Clips" does. It's one undo step, and nothing stays tied: an action, not a link.
  - It's on the group's right-click, and in Track > Colour ▸ while a group is selected.
- **Only in the DAW.** The plugins stay one module each.

## 5. Making and unmaking

### 5.1 Group tracks (Ctrl+G)

- **Select the tracks** (§8): one or several, groups among them too. Then use Ctrl+G, Track > Group tracks, or Group tracks on a selected header's right-click.
- **Where the group goes:** at the place of the topmost selected row, inside whatever group that row was in. The selected rows move into it in their order.
  - Rows that weren't next to each other are gathered, as Live and Logic do.
  - Every other row stays where it was ([ADR 0042](../decisions/0042-every-row-moves-on-its-own.md)).
- **Lanes:** a lane between two of the selected tracks, with nothing unselected between them, ends up inside the group with them. A lane anywhere else stays where it is.
- **After:**
  - the group is selected and unfolded;
  - no name field opens by itself, as for a new track: rename it with F2 or a double-click;
  - it's one undo step, "Group tracks".
- **A single track** can be grouped on its own. That's a way to start a group and drag others in.

### 5.2 Ungroup tracks (Ctrl+Shift+G)

- **With a group selected:** Ctrl+Shift+G, Track > Ungroup tracks, or the group's right-click.
- **The members take the group's place,** inside its parent, in their order.
- **The group goes,** taking with it its modules, its mix and sends, and the lanes that drive only it. Deleting a track takes the same ([tracks-and-clips.md](tracks-and-clips.md) §5).
- **The sound changes:** whatever the group's modules did is gone.
  - The toast says so: "Ungrouped Drums: its 2 modules are gone", with Undo.
  - Live doesn't document what it does with the group's devices. Logic keeps a main track that does something. A toast with Undo is clearer than either, and it asks nothing.
- One undo step, "Ungroup tracks".

### 5.3 Moving tracks in and out

- **Dragging a header** ([tracks-and-clips.md](tracks-and-clips.md) §6) moves tracks, lanes and groups in and out of groups:
  - dropped between two members, it joins their group;
  - dropped on a group's header, it goes into that group, at the end;
  - dropped at the end of a group (after its last row), it could land inside the group or after it. **The pointer's left–right position chooses**, as in outliners. The drop line is drawn at the depth it lands at, indented as the header would be.
- **A dragged group** moves with everything in it. It can't be dropped inside itself.
- **Several selected tracks** move together, in their order (§8).
- **Remove from group** (the Track menu, or a member's right-click): the selected tracks leave their group and land right after it, in the group above it if there is one. One undo step, "Remove from group".
- **The mixer view** does the same with strips (§9).

### 5.4 Deleting and duplicating

- **Delete a group** (Delete with its header used last, Track > Delete track, or its right-click), and it goes **with everything in it**, as in Live.
  - The toast says "Deleted Drums and its 6 tracks", with Undo.
  - Pro Tools asks whether to keep the members. Segno never asks: Undo is one key away ([tracks-and-clips.md](tracks-and-clips.md) §3).
  - What goes with each track is as now ([tracks-and-clips.md](tracks-and-clips.md) §5).
- **Duplicate (Ctrl+D) a group,** and the copy goes right after it, named "Drums 2". Its members are copied as tracks are copied ([tracks-and-clips.md](tracks-and-clips.md) §7). A member that's duplicated stays in its group, right after itself.

## 6. In the arrangement

### 6.1 The rows

```
┌ ▾ Drums      M S ▮│▁▁▁▁ ▁▁▁▁ ▁▁▁▁ ▁▁▁▁ ▁▁▁▁ ▁▁▁▁ ▁▁▁▁ ▁▁▁▁│ ← the group: its members in miniature
│   4 tracks       ▮│▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔  ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔ │
│ ┃▌Kick     M S R ▮│ ▆  ▆  ▆  ▆  ▆  ▆  ▆  ▆  ▆  ▆  ▆  ▆  ▆ │ ← members: the group's colour bar runs down
│ ┃▌Snare    M S R ▮│    ▆     ▆     ▆     ▆     ▆     ▆    │   their left edge, their own colour beside it
│ ┃▌Hats     M S R ▮│ ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂  ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ │
│ ┃▌Snare verb      │ ───────╱‾‾‾‾‾‾‾‾╲──────────────────── │ ← a lane among them is in the group too
├ ▸ Bass       M S ▮│▁▁▁▁▁▁▁▁▁▁  ▁▁▁▁▁▁▁▁▁▁▁▁  ▁▁▁▁▁▁▁▁▁▁▁▁▁│ ← a folded group: one row, its members in
│   2 tracks       ▮│▔▔▔▔▔▔▔▔▔▔  ▔▔▔▔▔▔▔▔▔▔▔▔  ▔▔▔▔▔▔▔▔▔▔▔▔▔│   miniature
└ ▌Lead      M S R ▮│  ████████████    █████████████        │ ← a track in no group
```

- **The group's row comes first,** with its members under it, in order.
- **Members are marked by the group's colour.** The group's colour bar (the header kit's 3 px bar) runs down the left edge of every row in it, and each member's own colour bar sits beside it.
  - Each nested group adds another bar, so depth reads as a stack of colour bars.
  - It's built from the part every header already has, not a new look.
  - The step from one level to the next is one new metric, `shell.groupIndent`, drawn for approval in step 0.
- **A group's rows** are its members and the lanes among them (§5.1). They fold with it.

### 6.2 The group's header and lane

- **The header** uses the track header kit ([ui-foundations.md](ui-foundations.md)):
  - a **fold triangle** before the name, ▾ open and ▸ folded. A click folds or unfolds the group; **Alt+click folds or unfolds every group** in that view;
  - the name; mute and solo (no arm); the meter; and, when the row is tall enough, the volume and pan bars, as on a track;
  - the subtitle slot, where a track names its instrument, says how many tracks the group holds: "4 tracks".

  The fold triangle is a new icon pair. It isn't the chevrons that fold modules sideways or open a take's menu: one look per thing ([ui-foundations.md](ui-foundations.md)).
- **The lane** shows the members in miniature, drawn as the song row draws every track ([arranger.md](arranger.md) §6). Both use one shared component.
  - Each member is one thin line where it has clips, in the member's colour; muted clips are dimmer.
  - **A nested group is one line,** wherever any of its tracks play, as in Live since 12.2.
  - With more lines than pixels, neighbours share a line. The lines grow with the row's height, as Live's overview zooms.
  - The miniature shows whether the group is folded or not, because unfolded members may be scrolled out of view.
- **Height:** a track's (64 px), resized as any row. Folding keeps the group's own height.

### 6.3 Nesting

- **Any depth.** tracktion nests submixes without a limit, and of the DAWs in §2 only Pro Tools sets one (nine).
- **The header column stays 180 px wide.** At depth 4 the names still have room: four steps of `shell.groupIndent`. Deeper than that, the indent stops growing and the colour bars still show the depth.
- A test checks that nothing touches at depth 4 at the narrowest header.

### 6.4 Folding

- **How:** the triangle; Alt+click for every group; **Alt+U** or Track > Fold group (a toggle, checked when folded) for the selected group; View > Fold all groups and Unfold all groups.
  - Live folds with U. A plain letter plays a note here, so it's Alt+U, beside Alt+M, Alt+S and Alt+R, the other track keys.
- **A folded group hides its rows.** In the arrangement those are the tracks and lanes; in the mixer view, the strips.
- **Each view folds on its own:** the arrangement and the mixer view each keep a fold state for every group, as each keeps its own scroll position. So the drums can be folded while you arrange and still open while you mix.
  - Cubase links the two, and its users complain that closing a folder hides its channels. Studio Pro makes the link optional, and Logic's mixer has "Follow Track Stacks".
  - Each state is kept once, in the view's state, behind one setter. Both are saved with the project and are never an undo step.
- **Folding never changes the selection.** What's selected inside a folded group shows in its miniature in the selection colour: a selected member's line, or selected clips. Commands act on it as before.
- **New groups start unfolded** in both views.

### 6.5 Working on a group's lane

- **A click** in the lane selects the group, as a click in a track's empty lane selects the track.
- **A time selection on a group's row covers the group and every row in it,** folded or not ([time-selection.md](time-selection.md)). Every command on a time selection then acts on all of them, as Live's does (§2): select bars 9 to 17 on a folded Drums group and Ctrl+D duplicates those bars on every drum track.
  - The selection shows in the miniature as it shows on any row.
  - *(Approved 2026-10-17 in place of a stand-in: a drag that selected the members' whole clips. With long clips it acted on more than the stretch dragged across.)*
- **A clip is never dropped on a group's lane:** the drag shows it can't land there. Files and instruments dropped there make a new track in the group (§11).

## 7. Signal flow and the mix

### 7.1 The sum

- **Members → the group → its modules → its volume and pan → its parent, or the master.** The order of the members makes no difference to the sound.
- **A neutral group changes nothing.** With no modules, at 0 dB and centre, a mix with the group nulls against the same mix without it (< −120 dBFS), as a neutral mix nulls against its sources ([mixer.md](mixer.md) §3).
- **Delay compensation** works across groups. A module with lookahead on a group delays that group alone, and everything else waits, as it does for tracks ([ADR 0016](../decisions/0016-constant-device-latency.md)).

### 7.2 Sends

- **A member's sends go straight to the returns,** from the member, post- or pre-fader as set on it. They don't pass through the group's modules or fader. Every DAW in §2 works this way.
- So a group's fader turns down the dry drums, not their reverb. A send on the group itself sends both. Mixing DAWs add VCAs mainly for this (§16).
- **A group has sends of its own,** one per return, post-fader by default, like a track's.

### 7.3 Mute and solo

- **Muting a group** silences it and everything in it, including the members' sends. That's today's rule: a track that isn't heard sends nothing to the returns.
  - The members' own mute buttons keep their state.
  - Their rows look the way a solo-silenced track's do.
- **Soloing a group** solos it with every member.
- **Soloing a member** keeps its group, and every group above it, heard. Everything else is silenced, the group's other members included.
- **A group holding a soloed track** shows its solo half-lit, as Live does.
- **Solo stays as it is everywhere else** ([mixer.md](mixer.md) §3):
  - it's solo in place;
  - it never silences the returns or the master;
  - it's never an undo step;
  - a click is exclusive, and Ctrl+click adds.

### 7.4 What can't be grouped

- **Return tracks** stay among the returns, and **the master** stays the master. Live and Studio Pro don't group returns either (§2).
- A return in a group could let the group's sound reach the return and come back into the group: a loop.

### 7.5 Sidechains and the Follower

- **A group is a sidechain source** in every picker. It's heard after its modules and fader and before its mute, as a track is ([ADR 0025](../decisions/0025-sidechain-routing.md)). Duck the music group under the kick.
- **The loop rule holds:** a picker leaves out any channel whose sound depends on this one. A module on a member can't listen to its own group, or any group above it, because those are made from it.
- **The Follower** listens to a group as it listens to a track ([modulators.md](modulators.md) §12.4).
- **The engine:** a small tracktion patch makes the sidechain's send for a submix, as tracktion already does for an audio track (§15).

### 7.6 Automation and modulators

- A group's volume, pan, sends and modules are automated as a track's are, with lanes anywhere ([ADR 0029](../decisions/0029-independent-automation-lanes.md)).
- **Modulators** go on a group's chain, as on any channel's, and move the parameters on it ([ADR 0061](../decisions/0061-modulators-move-any-parameter-on-their-track.md)).
- Reaching into the members' modules comes later (§16). Bitwig 5 lets a group's modulators move its children's mixer controls.

## 8. Selecting several tracks

Grouping needs several tracks selected. [mixer.md](mixer.md) §8 and [tracks-and-clips.md](tracks-and-clips.md) §16 left that for later. It comes first (§17, step 1).

- **In the headers** (and in the mixer view's strips):
  - a click selects one track, as now;
  - **Ctrl+click** adds a track or removes it;
  - **Shift+click** selects the run of tracks and groups from the one clicked last. Lanes are skipped, because they aren't channels.
  - The control zones (mute, solo, the bars) keep their clicks: Ctrl+click on solo still adds a solo.
- **The selection** gains the set of channels. Its `channel` stays the one clicked last, so the detail panel shows that channel's modules ([ADR 0027](../decisions/0027-selection-and-context-panels.md)).
- **The inspector** shows TRACKS (Selected: 4), then MIX.
  - A drag in MIX moves every selected channel together, in one step ([mixer.md](mixer.md) §8): volume by the same number of dB, pan by the same amount, mute to the same state.
  - SENDS shows when every selected channel has the same returns.
- **What acts on several tracks:**
  - Group tracks;
  - Delete; Duplicate (each copy after its original); Colour;
  - Alt+M, Alt+S, Arm;
  - dragging: they move together, in order;
  - Remove from group;
  - Export's selected tracks.
- **Clips and tracks aren't selected together.** Selecting a clip leaves one channel selected, its track, as now.
- **A group and its members** can be selected together. Delete takes the group, which already holds them. Group tracks puts the group, with the other selected tracks, into a new group.

## 9. In the mixer view

```
┌ MIXER ─────────────────────────────────────────────────────────── ● Modules  ● Sends ┐
│         │▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔│         │         ┆           │      │        │ ← the group's colour over its members
│ ▾ Drums │ ▌Kick  │ ▌Snare  │ ▌Hats   │ ▸ Bass  │ ▌Lead   ┆ ▌A Return │      │▌Master │
│ Comp    │ EQ     │ EQ      │         │ Sat     │ Synth   ┆ Reverb    │      │Limiter │
│ ...     │ ...    │ ...     │ ...     │ ...     │ ...     ┆ ...       │      │ ...    │
```

- **Strips follow the arrangement's order:** the group's strip, then its members'. A bar of the group's colour runs across the top of the members' strips, as it runs down their headers in the arrangement.
- **The group's strip** is a track's without arm, with the fold triangle beside its name. Folded, its members' strips are hidden. The mixer keeps its own fold state (§6.4).
- **Dragging a strip** into, out of and along groups follows §5.3's rules, sideways.
- **The strip layout** gains the group bar (`model::ChannelStripLayout`), so every fader still starts at the same height.

## 10. The inspector

- **A group:** GROUP (Name, Colour, Tracks: 4, Modules), then MIX and SENDS.
- **A member:** TRACK gains a **Group** row naming its group. A click on the name selects the group.

## 11. Elsewhere in Segno

- **Browser drops** ([browser.md](browser.md) §10):
  - an audio effect, a rack of effects or a modulator dropped on a group's header or chain goes on the group's chain. Studio Pro does the same with its folders;
  - an instrument, a sample or a MIDI file dropped on a group's row makes a new track at the group's end with it (a clip goes where it's dropped), as a drop below the tracks makes a new track there;
  - an instrument dropped on a group's chain is refused. The toast says a group has no notes.
- **Add track** (Ctrl+T) with a group or a member selected adds the new track at the end of that group, as Live does from Push. With anything else selected, it adds the track at the end of the tracks, as now.
- **Song sections** ([ADR 0049](../decisions/0049-the-master-row-is-the-song-row.md)) move everything in their time on every track and lane, members included. A group has no clips of its own to move.
- **The song row's overview** keeps every track as a line, including members of folded groups.
- **Pasting clips** from several tracks keeps their rows from the selected track down ([tracks-and-clips.md](tracks-and-clips.md) §9). It skips groups, which hold no clips, and includes the members of folded groups.
- **Recording:** a group has no arm. To arm several members at once, select them and use Arm (§8).
- **Live MIDI** follows the selected track with an instrument, as now. Selecting a group leaves it where it was.
- **Export** ([export.md](export.md) §2):
  - **the mix,** as now;
  - **selected tracks:** a selected group gives one stem, its whole sound through its modules and fader. A selected member gives its own sound, without its group's modules;
  - **all tracks** gains a choice:
    - **Groups whole,** the default: each group is one stem, and so is each track in no group and each return. Played together through the master, they make the mix, as all tracks do now;
    - **Every track:** each track alone, without its groups' modules, and each group as well. That's what Live's "All Individual Tracks" gives, and those stems don't add up to the mix.
- **Bounce to new track** on a group (its header's right-click, or Clip > Bounce to new track with the group selected) prints the group's whole sound, before its fader, onto a new track after it.
  - The new track copies the group's mix, and the group is muted ([bounce.md](bounce.md) §5).
  - Live 12.3 has the same: Bounce Group to New Track.
- **Duplicating and deleting tracks** inside a group keeps the result inside it (§5.4).

## 12. Menu bar and keys ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md))

**Track menu,** after Add return track:

| Item | Key | ID |
|---|---|---|
| Group tracks | Ctrl+G (shared, below) | `track.group` |
| Ungroup tracks | Ctrl+Shift+G (shared) | `track.ungroup` |
| Remove from group | | `track.removeFromGroup` |
| Fold group | Alt+U (a toggle, checked when folded) | `track.fold` |

**Track > Colour ▸** ends with **Give tracks the group's colour** while a group is selected (`track.colourMembers`).

**View menu,** after Show returns: **Fold all groups** and **Unfold all groups** (`view.foldGroups`, `view.unfoldGroups`), in the view shown.

- **Ctrl+G and Ctrl+Shift+G are shared with racks,** as Live shares them, and act on what was used last:
  - the headers or strips used last: Group tracks and Ungroup tracks;
  - the chain used last: Group into rack and Ungroup rack.

  Both menus show the key beside both items. The key goes to whichever item acts on what was used last (`Action::keysFrom`, as Quantise is shared by the Notes and Audio menus).
- **Right-click menus:**
  - a track's header: Group tracks, and Remove from group for a member, after Duplicate and Delete;
  - a group's header: Rename…, Fold group, Group tracks, Ungroup tracks, Remove from group (when it's nested), Duplicate track, Delete track, Colour ▸, Give tracks the group's colour, Bounce to new track.
- **Undo steps:** "Group tracks", "Ungroup tracks", "Move track", "Remove from group", "Delete track", "Duplicate track", "Track colour". Folding is never one.

## 13. Assistants (MCP, [ADR 0021](../decisions/0021-mcp-server.md))

| Tool | What |
|---|---|
| `group_tracks` | `tracks` (ids, groups too), `name?`, `colour?` → the group's id. As §5.1, one undo step |
| `ungroup_tracks` | `group` → the members' ids. As §5.2 |
| `move_track` | Gains `group`: a group's id, or `null` for no group. With `before`, the track lands right above that row, in its group; without it, at the end of `group`. Released arguments keep their meaning |
| `set_track` | Takes groups (name, colour), and gains `colourMembers` (groups only, §4) |
| `delete_tracks`, `duplicate_track` | Take groups, with everything in them (§5.4) |
| `set_mix`, `set_send`, `add_module`, `get_module`, `set_module_sidechain`, `measure_mix` | Take groups as channels; a group is a sidechain source |
| `export_audio` | Gains `stems: "groups" \| "tracks"` for all tracks (§11) |
| `bounce_group` | `group` → the new track's id (§11) |

`get_project` lists groups beside tracks. Each channel has its `kind` ("track", "group", "return" or "master") and its `group` (the id of the group it's in, or `null`). A group lists its `members`. Folding is the views' state, so assistants neither read nor set it. Each tool comes with a test that drives it through MCP.

## 14. Model and views

- **`model::TrackInfo`** gains `Role::Group` and `group`, the parent's id (or 0). `Snapshot::tracks` stays in the arrangement's order, each group before its members.
- **`model::ArrangementRows`** gains group rows and leaves out the rows of folded groups. Each row knows its depth.
- **Pure plans, under `[fast]` tests:**
  - grouping: where the group goes, what moves into it, and which lanes go with it (§5.1);
  - ungrouping, Remove from group, and a group emptied (§4);
  - a drop's place and depth from the pointer (§5.3), refusing a group inside itself;
  - what Delete and Duplicate take (§5.4);
  - the loop rule for sidechains with groups (§7.5);
  - stems: which channels make which files (§11).
- **`model::Selection`** gains the set of channels (§8).
- **The views' state** gains a set of folded groups for each view (§6.4).
- **`model::TrackHeaderLayout`** gains the depth (its colour bars and indent) and the fold triangle's slot. **`model::ChannelStripLayout`** gains the group bar.
- **One miniature** draws both the song row's overview and a group's lane.

## 15. Engine (tracktion)

- **A group is a `te::FolderTrack`** with the channel plugins every Segno channel has (volume and pan, the meter). Holding them makes it a submix by tracktion's rule from the moment it's made.
- **Grouping** inserts the folder at the topmost row's place and moves the tracks into it (`Edit::moveTrack` with the folder as parent), in one undo transaction.
- **What keeps working:** `te::getAudioTracks` already walks into folders. So the code that visits every track (clips, takes, automation, modulators, recording) sees members as before.
- **What changes:** code that assumes the tracks are one flat list learns about parents. That's the snapshot's order, `move_track`, `insertTrack`, the rows, the sidechain pickers and export.
- **Lanes** are stored after a track. A lane right after a group's last row has to say whether it's in the group or after it. The spike settles how that's stored; the first thing to try is a lane after the group itself, as a row in the list.
- **Sidechain from a group:** `createNodeForSubmixTrack` gets the send that `createNodeForAudioTrack` makes for a sidechain source. That's a small patch, `cmake/patches/tracktion_engine-submix-sidechain.patch`.
- **Mute** reaches members through tracktion's "muted by destination". Segno's engine behaviour already silences the sends of a track that isn't heard.
- **Stems:** tracktion renders a member alone without its folder's chain (`shouldRenderTrackInSubmix`), and a folder with its members (`addImplicitSubmixChildTracks`).

### The spike (step 0), kept as tests

1. A neutral group nulls against no group, three deep too.
2. A member's sends reach the returns from the member, its pre-fader sends too. A group's own sends work.
3. Muting a group silences its members' sends. Soloing a member keeps its groups and the returns heard.
4. A group's compressor ducks from a track's sidechain. A group is a sidechain source (with the patch), and the Follower hears a group.
5. With a lookahead module on a group, everything stays in time with the other tracks.
6. Automation and a modulator on a group's modules play.
7. Moving a track into and out of a group while playing: no click beyond the graph rebuild every move already makes.
8. Group, ungroup, move and delete are each one undo step. The project saves and opens the same, groups and fold states included.
9. Stems: a member alone, and a group whole.
10. With 64 tracks in 8 groups, the arrangement and the mixer keep their frame budgets.

## 16. Not now

- **VCA faders.** Logic, Cubase, Studio Pro, Pro Tools, REAPER and Ardour have them, and tracktion has a VCA plugin on folders. A VCA scales its tracks' faders without summing them, so post-fader sends follow it and existing automation is kept. Live, Bitwig and FL do without one, and a group's fader covers the common use. Later, if asked.
- **Edit groups** (Pro Tools, Cubase, Studio Pro), and Live's Link Tracks from a group's header: tracks that edit or comp together without being summed. Selecting a group's clips from its lane (§6.5) covers the common case. Comping across a group's tracks comes with recording's comp groups (M4, [recording.md](recording.md)).
- **Output routing:** a track sent to a group it isn't in, or to a return. Segno has no output routing at all.
- **Returns in groups, and sends between returns.**
- **Modulators on a group moving its members' modules** (Bitwig 5 has it, for mixer controls).
- **Bounce a group in place,** replacing it with one track (Live 12.3), and freeze.
- **A group saved as a file** with its tracks, to reuse in another song.
- **Push 2** ([push-2.md](push-2.md)): groups on its track buttons come with it. Live unfolds groups in Push's mix modes.

## 17. Steps

**First, time selection** ([time-selection.md](time-selection.md)), designed, built and signed off on its own (the product owner, 2026-10-17). Then:

0. **The spike (§15) and the UI drawn,** both before anything is built. The UI is drawn with the real track header kit and mixer strips, in every state:
   - tracks selected, with Ctrl+G about to group them;
   - a group open and folded, with lanes among its members;
   - three levels of nesting at the narrowest header width;
   - a drag landing between members, on a group's header, and at a group's end, at both depths;
   - a muted group, a soloed member, and the group's half-lit solo;
   - a time selection on a folded group's row, and a selected member's line while folded;
   - the mixer view with one group open and one folded;
   - the inspector for a group, a member and several tracks;
   - at standard and compact widths, zoomed.
1. **Selecting several tracks** (§8): the selection, the headers and strips, the inspector's TRACKS and MIX, and every command of §8 on several tracks. MCP doesn't change: its tools already take lists, or one id. Signed off on its own.
2. **Groups:** the engine, the arrangement, the inspector, the menus and keys, browser drops, the mixer view, sidechains, export and bounce, and MCP. Signed off after the product owner's hands-on tasks:
   - group the drums, put a compressor on the group, and fold it;
   - put the drums group and the bass in a Rhythm group;
   - drag a track into a group, out of it, and to a group's end at both depths;
   - duck a pad group under the kick;
   - solo a member, then mute the group;
   - select a bar across a folded group and duplicate it;
   - fold the drums in the arrangement and mix them open in the mixer;
   - export stems with groups whole.

## 18. Tests (done when)

| What | Target |
|---|---|
| The sum | A neutral group nulls against no group (< −120 dBFS), nested three deep too |
| Sends | Members' sends reach the returns from the member, pre and post; a group's own sends are post-fader by default |
| Mute and solo | Each case of §7.3, rendered |
| Sidechains | A group as a source; the loop rule leaves out a member's own groups |
| Latency | A lookahead module on a group keeps every track in time (rendered against an impulse on each track) |
| Model | Every plan of §14 under `[fast]` tests |
| Undo | Every command one step, named as in §12; folding none |
| Save | Groups, members, depth and both views' folding round-trip |
| UI | Header and strip layouts at every depth and width, nothing touching; the layout and UI lints pass; screenshots of every state of step 0, zoomed |
| Performance | 64 tracks in 8 groups within the arrangement's and the mixer's frame budgets |
| MCP | Every tool of §13 driven in a test |

## 19. Questions for review

| # | Question | Recommendation |
|---|---|---|
| G1 | **The name** | Group, in the UI, menus, MCP and docs. Not bus, folder or stack |
| G2 | **One thing or two** (a summing group, and a folder that only tidies) | One: a group always sums. A neutral group is transparent, so it's also the folder (§3, §7.1). This is Live's, Bitwig's and REAPER's model, and where the two-kind DAWs have been heading (§2) |
| G3 | **Like Live?** | Yes, in what it is and does (§2). Beyond it: each view folds on its own, stems that add up, and the sidechain loop rule. Left out: Bitwig's clips on a group, VCAs |
| G4 | **Clips on a group** | None, as in Live: its lane shows its members (§6.2) |
| G5 | **Nesting** | Any depth (§6.3) |
| G6 | **Returns and the master in groups** | Never (§7.4) |
| G7 | **Members' sends** | Straight from the member to the returns, not through the group, as everywhere (§7.2). The group has its own sends |
| G8 | **Muting a group** | Silences everything in it, members' sends too (§7.3) |
| G9 | **Solo** | A soloed group solos its members; a soloed member keeps its groups heard; a group holding a soloed track shows its solo half-lit (§7.3) |
| G10 | **Making a group** | Ctrl+G on the selected tracks, at the topmost one's place, gathering the rest. One track alone is allowed (§5.1) |
| G11 | **Ctrl+G** | Shared with Group into rack, acting on what was used last, as Live does (§12) |
| G12 | **Ungroup** | Keeps the tracks and removes the group with its modules; a toast says so, with Undo (§5.2) |
| G13 | **Deleting a group** | Deletes everything in it, with a toast and Undo, as Live does. No question asked (§5.4) |
| G14 | **An emptied group** | Goes with its last member, in the same undo step (§4) |
| G15 | **Dropping at a group's end** | The pointer's left–right position picks inside or after; the line is drawn at that depth (§5.3) |
| G16 | **The group's lane** | Its members in miniature, drawn as the song row is, with a nested group as one line. ~~A drag selects their clips in that time~~ A time selection on it covers every row in the group, as Live's does; time selection is designed and built first (the product owner, 2026-10-17; §6.2, §6.5) |
| G17 | **Folding** | View state, never undone. **Each view folds on its own** (Cubase's linked folding is a common complaint). Alt+U and Alt+click for all; the selection is never changed (§6.4) |
| G18 | **How members look** | The group's colour bar down their left edge, their own beside it; one bar a level (§6.1) |
| G19 | **A group's colour** | Its own, taken once from its topmost member; Give tracks the group's colour recolours its members and their clips once, as Live's command does (§4) |
| G20 | **Selecting several tracks** | Built first, as step 1, with what [mixer.md](mixer.md) §8 and [tracks-and-clips.md](tracks-and-clips.md) §16 already promise (§8) |
| G21 | **Export and bounce** | For all tracks, stems with Groups whole by default, or Every track; Bounce to new track on a group (§11) |
| G22 | **Add track with a group selected** | Adds the track at the group's end (§11) |
| G23 | **Building it** | The spike and the UI drawn first (step 0), then selecting several tracks (1), then groups (2), each signed off |
