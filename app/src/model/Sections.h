#pragma once

#include "model/Snapshot.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fd::model
{
// The song's sections (M3 step C3, ADR 0049, docs/design/arranger.md): what a section is, where
// one can land, and each command's plan, worked out here without the engine.

/// The usual names, suggested when renaming a section (§7).
inline constexpr std::array<std::string_view, 9> kUsualSectionNames{
    "Intro", "Verse", "Pre-chorus", "Chorus", "Bridge", "Breakdown", "Build-up", "Drop", "Outro"};

/// Whether [start, end) fits among `sections` (overlapping none of them but `ignore`), and is
/// at least `shortest` long.
[[nodiscard]] bool sectionFits(const std::vector<SectionInfo>& sections, double start, double end,
                               std::uint64_t ignore = 0, double shortest = 1e-3);

/// The section at `seconds` (its start included, its end not), if any.
[[nodiscard]] const SectionInfo* sectionAt(const std::vector<SectionInfo>& sections, double seconds);

/// A new section's name: the first letter A … Z no section has, then "Section 27" and on.
[[nodiscard]] std::string newSectionName(const std::vector<SectionInfo>& sections);

/// A copy's name: its source's with the next free number ("Chorus" → "Chorus 2", "Chorus 2" →
/// "Chorus 3" when "Chorus 3" is free).
[[nodiscard]] std::string copyName(const std::vector<SectionInfo>& sections, const std::string& name);

/// A new section's colour: the palette colour after the last section's (by start); 0 for the
/// first.
[[nodiscard]] int newSectionColour(const std::vector<SectionInfo>& sections, int paletteSize);

/// Where a section can land (§9): every section's start and end, in order, each once.
[[nodiscard]] std::vector<double> sectionBoundaries(const std::vector<SectionInfo>& sections);

/// The boundary nearest `seconds` (none without sections).
[[nodiscard]] std::optional<double> nearestBoundary(const std::vector<SectionInfo>& sections, double seconds);

/// Where Previous section (Alt+←) and Next section (Alt+→) take the playhead from `seconds`: the
/// start of the section before or after the one it's in (none past either end).
[[nodiscard]] std::optional<double> previousSectionStart(const std::vector<SectionInfo>& sections,
                                                         double seconds);
[[nodiscard]] std::optional<double> nextSectionStart(const std::vector<SectionInfo>& sections,
                                                     double seconds);

/// Where a run of sections (`span`) dragged to start at `start` lands (arranger.md §9): the cut it
/// makes (a plan's `to`) and where the run then starts. A move later cuts at its new end, one
/// earlier at its new start, a copy at its start; a cut inside another section goes to that
/// section's nearer edge, so a run lands between sections, never in one. None where a move
/// wouldn't move, or a copy would land inside itself.
struct Landing
{
    double to = 0.0, start = 0.0;
    friend bool operator==(const Landing&, const Landing&) = default;
};
[[nodiscard]] std::optional<Landing> landRun(const std::vector<SectionInfo>& sections,
                                             std::pair<double, double> span, double start, bool copy);

/// The span of some sections (by id): the first one's start to the last one's end.
[[nodiscard]] std::optional<std::pair<double, double>> spanOf(const std::vector<SectionInfo>& sections,
                                                              const std::vector<std::uint64_t>& ids);

// ---- plans --------------------------------------------------------------------------------

/// The rest of the song, to its end.
inline constexpr double kSongEnd = std::numeric_limits<double>::infinity();

/// A stretch of the song as it was, in the song as it will be: `from` to `to` (kSongEnd for the
/// rest), or an empty stretch `emptyLength` long put in at `from`. A `copy` holds copies of what's
/// in it; everything else comes once, as itself. An empty stretch `pasted` holds a piece of a song
/// copied earlier (SongPiece).
struct Stretch
{
    double from = 0.0, to = 0.0;
    double emptyLength = 0.0;
    bool copy = false;
    bool pasted = false;

    [[nodiscard]] bool empty() const noexcept { return emptyLength > 0.0; }
    [[nodiscard]] double length() const noexcept { return empty() ? emptyLength : to - from; }
    friend bool operator==(const Stretch&, const Stretch&) = default;
};
/// A command's plan: the song as it will be, as the stretches of the song as it was, in order
/// (a stretch can come twice, a copy, or not at all, deleted).
using Plan = std::vector<Stretch>;

/// Moving the music from `start` to `end` to `to` (a boundary outside it): A B C D, B after C,
/// becomes A C B D. Empty if it wouldn't move.
[[nodiscard]] Plan planMove(double start, double end, double to);
/// Copying it to `to`: A B C D, B after C, becomes A B C B D.
[[nodiscard]] Plan planCopy(double start, double end, double to);
/// Deleting it, the gap closing: A B C D, B gone, becomes A C D.
[[nodiscard]] Plan planDelete(double start, double end);
/// An empty stretch `length` long at `at`: A B C, after B, becomes A B ▢ C.
[[nodiscard]] Plan planInsert(double at, double length);
/// Room for a piece `length` long at `at`, the music after it moving later (planInsert's, the
/// stretch `pasted`).
[[nodiscard]] Plan planPaste(double at, double length);
/// Where sections pasted at `at` (the playhead) go: there, or, inside a section, at its nearer
/// edge, as a dragged one does (landRun).
[[nodiscard]] double pasteLanding(const std::vector<SectionInfo>& sections, double at);

/// The times a plan cuts the song at: where clips crossing them are split first, so what's in
/// each stretch moves exactly (§9).
[[nodiscard]] std::vector<double> cutPoints(const Plan& plan);

/// Where what was at `t` lands: as itself (none if it's gone), and as each copy.
struct PlanLandings
{
    std::optional<double> itself;
    std::vector<double> copies;
};
[[nodiscard]] PlanLandings landingsOf(const Plan& plan, double t);
/// Where an edge at `t` lands: a start edge with the stretch it's in, an end edge with the one just
/// before it, so time taken out after a start or before an end moves it, and time put in between
/// them lengthens what they bound; an edge in time that's gone lands where its gap closed.
[[nodiscard]] double planEdge(const Plan& plan, double t, bool isEnd);

/// What a plan acts on.
struct SongContent
{
    struct Clip
    {
        std::uint64_t id = 0, track = 0;
        double start = 0.0, end = 0.0;
    };
    struct Lane
    {
        std::uint64_t id = 0;
        AutomationPoints points;
    };
    std::vector<Clip> clips;
    std::vector<Lane> lanes;
    std::vector<SectionInfo> sections;
};

/// A piece of a song, copied (the section clipboard, arranger.md §9): the sections in it and each
/// lane's points, their times from its start, each lane's values pinned at its edges. Its clips
/// are the engine's to keep.
struct SongPiece
{
    double length = 0.0;
    std::vector<SectionInfo> sections;
    std::vector<SongContent::Lane> lanes;
};
/// The piece of `content` from `start` to `end`.
[[nodiscard]] SongPiece pieceOf(const SongContent& content, double start, double end);

/// What a plan does to it.
struct PlanResult
{
    struct Place
    {
        std::uint64_t clip = 0; // the clip (for copies: the clip copied)
        double start = 0.0;
    };
    std::vector<Place> moves;             // clips whose start changes
    std::vector<Place> copies;            // copies made of clips
    std::vector<std::uint64_t> deletes;   // clips that go
    std::vector<SongContent::Lane> lanes; // lanes whose points change: their new points
    std::vector<SectionInfo> sections;    // the sections after, by start (copies and new ones: id 0)
};

/// Carries `plan` out on `content` (its clips already split at the plan's cut points: a clip
/// belongs to the stretch its start is in).
/// - **Clips:** each one moves to where its stretch lands; a stretch that comes twice copies it,
///   one that's gone deletes it.
/// - **Automation:** each lane's points move with their stretch, and a point is added at every
///   edge where a stretch joins one it didn't follow, at the curve's value there, so the curve
///   keeps its values everywhere (a bent segment cut at an edge keeps its ends' values). An empty
///   stretch holds the value where it was put in.
/// - **Sections** move like clips; copies get `copyName`, and an empty stretch a new section
///   (`newSection`'s name and colour) if given.
/// - **A pasted stretch** holds `piece`'s points (a lane it has none for holds its value, as an
///   empty stretch does) and its sections, new, keeping their names unless the song has them
///   already (then `copyName`).
[[nodiscard]] PlanResult applyPlan(const SongContent& content, const Plan& plan,
                                   const std::optional<SectionInfo>& newSection = std::nullopt,
                                   const SongPiece* piece = nullptr);
} // namespace fd::model
