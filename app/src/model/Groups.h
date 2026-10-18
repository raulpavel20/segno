#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace fd::model
{
// Groups (ADR 0072, docs/design/groups.md): what a group holds, read from the snapshot, where each
// group comes before the tracks and groups in it (TrackInfo::group names the group a channel is in).

/// The group `channel` is in; null when it's in none.
[[nodiscard]] const TrackInfo* parentGroup(const Snapshot& snapshot, const TrackInfo& channel);

/// How many groups `channel` is in (0: none).
[[nodiscard]] int groupDepth(const Snapshot& snapshot, const TrackInfo& channel);

/// Whether `channel` is in `group`, at any depth.
[[nodiscard]] bool isInGroup(const Snapshot& snapshot, const TrackInfo& channel, std::uint64_t group);

/// The groups `channel` is in, outermost first.
[[nodiscard]] std::vector<const TrackInfo*> groupsAbove(const Snapshot& snapshot, const TrackInfo& channel);

/// What's in `group`, in order: the tracks and groups directly in it, or (`nested`) everything
/// inside it at any depth.
[[nodiscard]] std::vector<const TrackInfo*> membersOf(const Snapshot& snapshot, std::uint64_t group,
                                                      bool nested);

/// How many tracks a group holds at any depth: its header's "4 tracks" (groups.md §6.2).
[[nodiscard]] int tracksIn(const Snapshot& snapshot, std::uint64_t group);
/// "4 tracks", "1 track".
[[nodiscard]] std::string tracksText(int count);

/// Whether a folded group hides `channel` (one of the groups it's in is folded, §6.4).
[[nodiscard]] bool hiddenByFolding(const Snapshot& snapshot, const TrackInfo& channel,
                                   const std::set<std::uint64_t>& folded);

/// `channels` without those inside another of them, in their order: a group chosen with what it
/// holds acts for it (moved, deleted, duplicated whole, §8).
[[nodiscard]] std::vector<std::uint64_t> outermost(const Snapshot& snapshot,
                                                   const std::vector<std::uint64_t>& channels);

/// A view's folded groups with `group` folded, or unfolded when it is (§6.4); with `all` (Alt),
/// every group the same way.
[[nodiscard]] std::set<std::uint64_t>
toggledFold(const Snapshot& snapshot, const std::set<std::uint64_t>& folded, std::uint64_t group, bool all);

/// The tracks that hold clips, as indices into Snapshot::tracks in order, groups left out: the
/// rows clips and stretches move and paste across (§6.5, §11), as the engine counts them.
[[nodiscard]] std::vector<int> clipTracks(const Snapshot& snapshot);
/// A track's place among clipTracks; -1 for a group or no track.
[[nodiscard]] int clipTrackRank(const Snapshot& snapshot, int track);

/// Whether something in `group` is soloed while the group itself isn't: its solo shows half lit
/// (§7.3). And whether `channel` is in a muted group: its mute shows half lit.
[[nodiscard]] bool holdsSolo(const Snapshot& snapshot, const TrackInfo& group);
[[nodiscard]] bool inMutedGroup(const Snapshot& snapshot, const TrackInfo& channel);
} // namespace fd::model
