# 0072. A group is a channel that sums tracks

- **Status:** Accepted (product owner, 2026-10-17), with editing on a group's row done by a time selection, designed and built first ([time-selection.md](../design/time-selection.md))
- **Date:** 2026-10-17
- **Amends:** [0026](0026-mixing-in-the-arrangement.md): a fourth kind of channel, the group; "group tracks wait for routing work with racks (M4)" is answered here. [0027](0027-selection-and-context-panels.md): the selection holds several channels.
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track), [0025](0025-sidechain-routing.md) (sidechains), [0029](0029-independent-automation-lanes.md) and [0042](0042-every-row-moves-on-its-own.md) (lanes and rows), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0043](0043-a-clips-colour-is-its-own.md) (colours are their own), [0049](0049-the-master-row-is-the-song-row.md) (the song row), [0021](0021-mcp-server.md) (assistants)
- **Design:** [design/groups.md](../design/groups.md)

## Context
- Every track goes to the master. There's no way to put a compressor on the drums together, turn a section of the mix up with one fader, or fold twenty tracks out of the way.
- The product owner asked for a bus or group "more or less like Ableton's groups" (2026-10-17).
- DAWs split this into up to three things (design §2): a folder that only tidies, a bus that sums, and a VCA that scales faders. Live, Bitwig and REAPER have one thing that does both of the first two. The two-kind DAWs have been adding summing to their folders (Pro Tools 2020, Cubase 15.0.20); no summing-first DAW has added a folder that only tidies.
- tracktion's folder tracks become submixes when they hold a plugin: they sum their tracks at any depth, pass mute and solo, hold no clips, and render a member without the folder's chain.
- Grouping several tracks needs several selected, which the selection can't hold yet.

## Decision
- **A group is a fourth kind of channel:** it holds tracks and other groups, sums them, and runs them through its own modules, volume and pan to its parent or the master. It has sends, mute, solo, a meter, automation, modulators, and can be a sidechain source. It holds no clips, no instruments, no arm.
- **One thing, always summing.** A neutral group (no modules, 0 dB, centre) changes nothing in the sound, so it is also the folder. No separate folder or VCA.
- **Groups nest to any depth.** Return tracks and the master are never in a group.
- **Members' sends go straight to the returns,** not through the group. Muting a group silences its members and their sends; soloing a member keeps its groups heard.
- **Made from selected tracks** (Ctrl+G, Track > Group tracks), at the topmost one's place; **ungrouped** with Ctrl+Shift+G, which keeps the tracks and removes the group and its modules. Ctrl+G and Ctrl+Shift+G are shared with racks and act on what was used last. **Deleting a group deletes what's in it.** A group is never empty: it goes with its last member.
- **Folding is view state:** each view (the arrangement, the mixer) folds on its own, as each keeps its own scroll; saved, never undone, never changing the selection. Alt+U folds, as Live's U does. A group's lane shows its members in miniature, drawn as the song row draws every track (a nested group as one line), and a time selection on it covers every row in the group, folded or not.
- **Several tracks can be selected** (Ctrl+click and Shift+click on headers and strips), built first, with what mixer.md §8 and tracks-and-clips.md §16 already describe.
- **Stems** export groups whole by default, or every track alone.
- **A group's colour is its own,** taken once from its topmost member; a one-off command gives its members and their clips its colour.
- **Assistants** get `group_tracks`, `ungroup_tracks` and `bounce_group`; `move_track`, `set_track`, `get_project` and the mix and module tools learn groups. Folding is the views' state, which assistants don't see.

## Consequences
- The track list becomes a tree. Code that visits every track keeps working (tracktion's track walks go into folders); code that assumes a flat list (the snapshot's order, rows, moves, pickers, export) learns parents.
- A lane right after a group's last row must say whether it's in the group; the spike settles how it's stored.
- A small tracktion patch: a submix's sidechain send.
- New pure plans under `[fast]` tests: grouping, ungrouping, drops at a depth, what Delete takes, the sidechain loop rule with groups, stems.
- The mixer's strips and the track header kit gain depth (colour bars a level) and a fold triangle, a new icon pair.
- Later, if asked: VCA faders, edit groups, output routing, returns in groups, modulators reaching into members, a group saved as a file.
