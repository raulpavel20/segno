#include "model/Bounce.h"

#include "model/Takes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace fd::model
{
namespace
{
constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kEpsilon = 1e-9;

float levelOf(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

/// The block holding `seconds` (from the run's start), or nullopt outside the render.
std::optional<size_t> blockAt(const BlockLevels& levels, double fromRunStart)
{
    if (levels.blockSeconds <= 0.0 || fromRunStart < 0.0)
        return std::nullopt;
    const auto i = static_cast<size_t>(std::floor(fromRunStart / levels.blockSeconds));
    if (i >= levels.peaks.size())
        return std::nullopt;
    return i;
}

/// The end of the last block above `threshold` in [from, to) (seconds from the run's start),
/// clamped to `to`; nullopt if all of it is below.
std::optional<double> lastSounding(const BlockLevels& levels, double from, double to, float threshold)
{
    if (levels.blockSeconds <= 0.0 || !(to > from))
        return std::nullopt;
    const auto first = static_cast<size_t>(std::max(0.0, std::floor(from / levels.blockSeconds)));
    const auto last = std::min(levels.peaks.size(), static_cast<size_t>(std::ceil(to / levels.blockSeconds)));
    for (size_t i = last; i-- > first;)
        if (levels.peaks[i] > threshold)
            return std::min(to, static_cast<double>(i + 1) * levels.blockSeconds);
    return std::nullopt;
}
} // namespace

std::string refusalText(BounceRefusal refusal)
{
    switch (refusal)
    {
    case BounceRefusal::None:
        return {};
    case BounceRefusal::NoInstrument:
        return "Add an instrument to bounce this clip.";
    case BounceRefusal::ModulesBeforeInstrument:
        return "Modules before the instrument would change the bounce: move them after it, or bounce to a new track.";
    case BounceRefusal::Muted:
        return "A muted clip plays nothing to bounce.";
    }
    return {};
}

BounceRefusal bounceRefusal(const TrackInfo& track, const ClipInfo& clip, BounceMode mode)
{
    if (clip.muted)
        return BounceRefusal::Muted;
    if (clip.kind != ClipInfo::Kind::Midi)
        return BounceRefusal::None;
    const auto instrument = std::find_if(track.devices.begin(), track.devices.end(), [](const auto& d) { return d.instrument; });
    if (instrument == track.devices.end())
        return BounceRefusal::NoInstrument;
    // ADR 0019: modules before the instrument process only the audio clips; a bounce in place
    // enters at the start of the chain and would pass through them. MIDI modules don't count.
    if (mode == BounceMode::InPlace &&
        std::any_of(track.devices.begin(), instrument, [](const auto& d) { return !d.midiEffect; }))
        return BounceRefusal::ModulesBeforeInstrument;
    return BounceRefusal::None;
}

double tailAllowance(ClipInfo::Kind kind, BounceMode mode) noexcept
{
    return kind == ClipInfo::Kind::Audio && mode == BounceMode::InPlace ? 0.0 : kBounceTailSeconds;
}

std::vector<BounceRun> bounceRuns(const Snapshot& snapshot, const std::set<std::uint64_t>& clips, BounceMode mode)
{
    std::vector<BounceRun> out;
    for (const auto& track : snapshot.tracks)
    {
        const auto groups = clipGroups(track);
        // The pieces of this track, in time order, and where each one's next clip starts.
        std::vector<BouncePiece> pieces;
        std::vector<ClipInfo::Kind> kinds;
        for (size_t g = 0; g < groups.size(); ++g)
        {
            const auto& group = groups[g];
            const bool selected =
                std::any_of(group.parts.begin(), group.parts.end(), [&](const auto* c) { return clips.contains(c->id); });
            if (!selected)
                continue;
            if (std::any_of(group.parts.begin(), group.parts.end(),
                            [&](const auto* c) { return bounceRefusal(track, *c, mode) != BounceRefusal::None; }))
                continue;
            BouncePiece piece;
            for (const auto* c : group.parts)
                piece.parts.push_back(c->id);
            piece.name = group.parts.front()->name;
            piece.colour = group.parts.front()->colour;
            piece.start = group.start();
            piece.end = group.end();
            piece.limit = g + 1 < groups.size() ? groups[g + 1].start() : kInfinity;
            if (g + 1 < groups.size())
                piece.limitName = groups[g + 1].parts.front()->name;
            const bool midi =
                std::any_of(group.parts.begin(), group.parts.end(), [](const auto* c) { return c->kind == ClipInfo::Kind::Midi; });
            pieces.push_back(std::move(piece));
            kinds.push_back(midi ? ClipInfo::Kind::Midi : ClipInfo::Kind::Audio);
        }
        // Runs: one kind, each piece within the tail allowance of the one before.
        for (size_t i = 0; i < pieces.size(); ++i)
        {
            const double allowance = tailAllowance(kinds[i], mode);
            if (out.empty() || out.back().track != track.id || out.back().kind != kinds[i] ||
                pieces[i].start > out.back().pieces.back().end + allowance + kEpsilon)
                out.push_back({.track = track.id, .kind = kinds[i], .start = pieces[i].start});
            out.back().pieces.push_back(pieces[i]);
            out.back().renderEnd = pieces[i].end + allowance;
        }
    }
    // Each piece's limit within its run: on its own track (in place), the next clip; on a new
    // track, only the next piece of the run.
    for (auto& run : out)
        for (size_t i = 0; i < run.pieces.size(); ++i)
        {
            auto& piece = run.pieces[i];
            const bool next = i + 1 < run.pieces.size();
            if (mode == BounceMode::NewTrack)
            {
                piece.limit = next ? run.pieces[i + 1].start : kInfinity;
                piece.limitName = next ? run.pieces[i + 1].name : std::string();
                piece.limitIsBounced = next;
            }
            else
                piece.limitIsBounced = next && std::abs(piece.limit - run.pieces[i + 1].start) < kEpsilon;
        }
    return out;
}

BounceEnd bounceEnd(const BounceRun& run, const BouncePiece& piece, const BlockLevels& levels, BounceMode mode)
{
    const double allowance = tailAllowance(run.kind, mode);
    const double reach = std::min(piece.end + allowance, run.renderEnd);
    const double windowEnd = std::min(reach, piece.limit);
    BounceEnd out{.end = piece.end};
    const float silence = levelOf(kBounceSilenceDb);
    if (const auto sounding = lastSounding(levels, piece.end - run.start, windowEnd - run.start, silence))
        out.end = std::max(piece.end, run.start + *sounding);
    // Still sounding where a clip that isn't bounced starts: the tail is cut there.
    if (!piece.limitIsBounced && piece.limit < reach)
        if (const auto block = blockAt(levels, piece.limit - run.start); block && levels.peaks[*block] > silence)
        {
            out.end = piece.limit;
            out.cut = true;
            out.audible = levels.peaks[*block] > levelOf(kBounceAudibleDb);
        }
    return out;
}

double keptLength(const BounceRun& run, const BlockLevels& levels, BounceMode mode)
{
    if (run.pieces.empty())
        return 0.0;
    const double last = run.pieces.back().end;
    const double reach = std::min(last + tailAllowance(run.kind, mode), run.renderEnd);
    const auto sounding = lastSounding(levels, last - run.start, reach - run.start, levelOf(kBounceSilenceDb));
    return std::max(last - run.start, sounding.value_or(0.0));
}

std::string bounceTakeName(const std::vector<TakeInfo>& takes)
{
    int highest = 0;
    for (const auto& t : takes)
    {
        if (t.name == "Bounce")
            highest = std::max(highest, 1);
        else if (t.name.rfind("Bounce ", 0) == 0)
            highest = std::max(highest, std::atoi(t.name.c_str() + 7));
    }
    return highest == 0 ? std::string("Bounce") : "Bounce " + std::to_string(highest + 1);
}

std::string bounceFileName(const std::string& track, const std::string& clip)
{
    return track + " - " + clip + " - Bounce";
}

std::string bounceTrackName(const std::string& track)
{
    return track + " bounce";
}
} // namespace fd::model
