#include "model/SongKey.h"

#include <algorithm>

namespace fd::model
{
std::vector<music::DetectNote> notesForKeyDetection(const Snapshot& snapshot, const std::vector<std::uint64_t>& clips)
{
    std::vector<music::DetectNote> out;
    for (const auto& track : snapshot.tracks)
        for (const auto& clip : track.clips)
        {
            if (clip.kind != ClipInfo::Kind::Midi || clip.muted)
                continue;
            if (!clips.empty() && std::find(clips.begin(), clips.end(), clip.id) == clips.end())
                continue;
            // What it plays, in beats from its notes' zero: its loop, or what lies between its edges.
            const double from = snapshot.tempo.beatsForSeconds(clip.loop.on() ? clip.loop.start : clip.offset);
            const double to =
                snapshot.tempo.beatsForSeconds(clip.loop.on() ? clip.loop.end() : clip.offset + clip.length);
            for (const auto& n : clip.notes)
                if (n.start >= from - 1e-9 && n.start < to)
                    out.push_back({n.pitch, std::min(n.length, to - n.start)});
        }
    return out;
}

std::string keyStepName(const std::optional<music::Key>& key)
{
    return key ? "Set key to " + music::keyName(*key) : "Remove the key";
}
} // namespace fd::model
