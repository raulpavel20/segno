#pragma once

#include "model/Fades.h"
#include "model/Snapshot.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace fd::model
{
// Editing fades and crossfades on clips (M2 step C6, docs/design/fades.md §3, §4): the handles a
// track's clips show, and what dragging them does. JUCE-free; the arrangement draws and hit-tests
// the handles where these place them.

/// A track's audio clips as fades see them, in time order (from the snapshot).
[[nodiscard]] std::vector<FadeClip> fadeClips(const TrackInfo& track);

/// A grip on a fade or a crossfade.
struct FadeHandle
{
    enum class Kind
    {
        FadeIn,      // a fade's end, at the top of the clip's body: drag to set its length
        FadeOut,
        BendIn,      // the dot in the middle of a fade's line: drag up or down to bend it
        BendOut,
        CrossStart, // a crossfade's ends: drag to resize it (where it sits: its menu)
        CrossEnd,
    };
    Kind kind = Kind::FadeIn;
    std::uint64_t clip = 0; // the clip (a crossfade's: the clip before the join)
    double time = 0.0;      // where it sits on the timeline
    float height = 0.0f;    // from the top of the clip's body, as a share of its height
};

/// Every handle of a track's audio clips. A clip shows its fade handles at its corners (where its
/// fades end); its bend dots only where it has a fade; a join with a crossfade shows the
/// crossfade's handles instead of the two clips' fades there. A clip with takes playing several
/// parts fades at its outer edges only (ADR 0032).
[[nodiscard]] std::vector<FadeHandle> fadeHandles(const TrackInfo& track);

/// What a fade gesture changes: one clip's fades, or one join's crossfade.
struct FadeEdit
{
    std::uint64_t clip = 0;
    std::optional<Fade> in, out;
    std::uint64_t crossClip = 0;         // the clip before the join whose crossfade changes (0: none)
    std::optional<Crossfade> crossfade;  // the new crossfade (none, with crossClip set: removed)

    [[nodiscard]] bool empty() const noexcept { return clip == 0 && crossClip == 0; }
};

/// Dragging a fade or crossfade handle (not a bend dot) to `seconds`. A fade handle dragged past
/// its clip's edge into a clip that meets it makes a crossfade there, twice as long as it's
/// dragged past, centred on the join (§3); a crossfade's end keeps the other end where it is.
[[nodiscard]] FadeEdit dragFade(const TrackInfo& track, const FadeHandle& handle, double seconds);

/// Bending a fade (a BendIn or BendOut handle) to `bend`.
[[nodiscard]] FadeEdit bendFade(const TrackInfo& track, const FadeHandle& handle, float bend);
/// The bend after dragging a fade's dot `up` pixels (negative: down), `travel` pixels being a
/// whole bend: up bows the line up, as an automation segment's (automation.md §5).
[[nodiscard]] float bendFor(float base, float up, float travel, bool fadeIn) noexcept;

/// The track's clips with an edit applied, as they'll play (to preview a gesture).
[[nodiscard]] std::vector<FadeClip> withEdit(std::vector<FadeClip> clips, const FadeEdit& edit);
/// The track with an edit applied: its clips' fades, crossfades and playback as they'll be.
[[nodiscard]] TrackInfo withFadeEdit(TrackInfo track, const FadeEdit& edit, bool edgeFades);
} // namespace fd::model
