#include "Harmony.h"

#include <fd/music/Scales.h>
#include <vector>

namespace fd::devices::harmony
{
std::span<const std::string_view> scaleIds()
{
    // Frozen with the parameter (docs/ids.md): the catalogue's 36 at approval, in its order then.
    // A new scale is appended, never inserted, so a saved index keeps its scale.
    static constexpr std::array<std::string_view, 36> ids{"major",
                                                          "minor",
                                                          "harmonic-minor",
                                                          "melodic-minor",
                                                          "major-pentatonic",
                                                          "minor-pentatonic",
                                                          "blues",
                                                          "major-blues",
                                                          "dorian",
                                                          "phrygian",
                                                          "lydian",
                                                          "mixolydian",
                                                          "locrian",
                                                          "harmonic-major",
                                                          "dorian-sharp-4",
                                                          "phrygian-dominant",
                                                          "lydian-augmented",
                                                          "lydian-dominant",
                                                          "super-locrian",
                                                          "hungarian-minor",
                                                          "double-harmonic",
                                                          "spanish-8-tone",
                                                          "whole-tone",
                                                          "diminished-half-whole",
                                                          "diminished-whole-half",
                                                          "messiaen-3",
                                                          "messiaen-4",
                                                          "messiaen-5",
                                                          "messiaen-6",
                                                          "messiaen-7",
                                                          "hirajoshi",
                                                          "in-sen",
                                                          "iwato",
                                                          "kumoi",
                                                          "pelog-selisir",
                                                          "pelog-tembung"};
    return ids;
}

std::span<const std::string_view> scaleChoices()
{
    static const std::vector<std::string_view> names = []
    {
        std::vector<std::string_view> out;
        for (const auto id : scaleIds())
        {
            const auto* s = music::findScale(id);
            out.push_back(s != nullptr ? s->name : id);
        }
        return out;
    }();
    return names;
}

std::array<ParamSpec, Count> params()
{
    constexpr Smoothing none{SmoothingKind::None, 0.0f};
    return {{
        {.id = "follow",
         .name = "Follow",
         .kind = ParamKind::Choice,
         .max = 3.0f,
         .choices = followChoices,
         .smoothing = none,
         .description = "Whose chords it follows: the song's, the listened track's, chords played below the "
                        "split, or its own."},
        {.id = "key",
         .name = "Key",
         .kind = ParamKind::Choice,
         .max = 1.0f,
         .choices = keyChoices,
         .smoothing = none,
         .description = "The song's key, or a key of its own (used too when the song has none)."},
        {.id = "root",
         .name = "Root",
         .kind = ParamKind::Choice,
         .max = 11.0f,
         .choices = rootChoices,
         .smoothing = none,
         .description = "Its own key's and its own chord's root."},
        {.id = "scale",
         .name = "Scale",
         .kind = ParamKind::Choice,
         .max = static_cast<float>(scaleChoices().size() - 1),
         .choices = scaleChoices(),
         .smoothing = none,
         .description = "Its own key's scale."},
        {.id = "chord",
         .name = "Own chord",
         .shortName = "Chord",
         .kind = ParamKind::Choice,
         .max = 7.0f,
         .choices = chordChoices,
         .smoothing = none,
         .description = "Its own chord, on the root."},
        {.id = "split",
         .name = "Split",
         .kind = ParamKind::Discrete,
         .max = 127.0f,
         .def = 48.0f,
         .unit = Unit::Note,
         .smoothing = none,
         .description = "With Follow on Played, notes below this note set the chord and aren't played."},
    }};
}
} // namespace fd::devices::harmony
