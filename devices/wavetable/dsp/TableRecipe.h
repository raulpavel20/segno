#pragma once

#include "fd/dsp/Wavetable.h"

#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace fd::devices::wavetable
{
/// A table's recipe (docs/design/table-editor.md §3): layers that make its frames, changes that
/// shape them. The baked table is what plays and what's saved beside the recipe; the recipe is
/// what the editor edits. Not real-time.
struct Recipe
{
    static constexpr int maxLayers = 4, maxFrames = dsp::Wavetable::maxFrames,
                         frameSize = dsp::Wavetable::frameSize;
    static constexpr int numHarmonicBars = 64; // a Harmonics source's bars

    /// How a layer fills the frames between its keyframes, or between its source's frames.
    enum class Fill : std::uint8_t
    {
        Crossfade, // the samples, mixed
        Spectral   // each harmonic's level and phase, moved from one to the next
    };

    enum class SourceKind : std::uint8_t
    {
        Draw,      // a drawn wave on each keyframe
        Harmonics, // harmonic bars on each keyframe
        Formula,   // one line in x and t
        Audio,     // frames taken from audio (pitched, spectral or slices), kept as frames
        Image,     // an image's rows, as waves or as harmonics, kept as rows
        Table      // a factory table, or frames of one of yours
    };

    struct Keyframe
    {
        int frame = 0;
        // Draw: the wave's points (x 0–1, y -1–1, the bend to the next).
        std::vector<std::array<float, 3>> points;
        // Harmonics: harmonics 1 ... numHarmonicBars, their levels (0–1) and phases (0–1 of a turn).
        std::vector<float> levels, phases;
    };

    struct Layer
    {
        SourceKind kind = SourceKind::Draw;
        bool on = true;
        float level = 1.0f; // 0–1
        Fill fill = Fill::Crossfade;
        std::vector<Keyframe> keyframes; // Draw, Harmonics: sorted by frame
        std::string formula;             // Formula
        std::string tableId;             // Table: a factory table's ID (else its own frames)
        // Audio, Image and a Table of your own: frames of frameSize samples, as they came in.
        std::vector<float> frames;
        bool imageAsHarmonics = false; // Image: each row a frame's harmonics (else its wave)
        std::string note;              // where it came from ("Pitched, 64 frames from Vox.wav")

        [[nodiscard]] int sourceFrames() const noexcept
        {
            return static_cast<int>(frames.size()) / frameSize;
        }
    };

    enum class ChangeKind : std::uint8_t
    {
        Normalise,  // each frame to a peak of 1
        RemoveDc,   // each frame's average taken out
        PhaseAlign, // each frame turned so its fundamental starts at zero
        Smooth,     // a gentle low pass on the harmonics
        Fold,       // louder, folded back at the edges
        Filter,     // a low, high or band pass on the harmonics
        Reverse,    // played backwards
        Mirror,     // the second half reflecting the first
        Warp        // one of the oscillator's spectral warps
    };

    struct Change
    {
        ChangeKind kind = ChangeKind::Normalise;
        bool on = true;
        dsp::Warp warp = dsp::Warp::FormantShift; // Warp: which
        int filterType = 0;                       // Filter: 0 low, 1 high, 2 band pass
        /// Its amount (0–1) at keyframes: (frame, amount), sorted; one entry holds for the whole
        /// table, none means 1.
        std::vector<std::pair<int, float>> amounts;

        [[nodiscard]] float amountAt(int frame) const noexcept;
    };

    std::string name;
    int frames = 64;
    std::vector<Layer> layers;
    std::vector<Change> changes;
};

/// The recipe as it's saved (`oscN.recipe`), and back. Reading checks everything and refuses what
/// it can't use (`error` says what).
[[nodiscard]] nlohmann::json toJson(const Recipe& recipe);
[[nodiscard]] std::optional<Recipe> recipeFromJson(const nlohmann::json& json, std::string* error = nullptr);

/// Bakes a recipe into its table: the same frames on any machine, every time.
[[nodiscard]] dsp::Wavetable bake(const Recipe& recipe);

/// A recipe to start editing a table from: a factory table by its ID, or a table's own frames.
[[nodiscard]] Recipe recipeFromTable(const dsp::Wavetable& table, const std::string& factoryId);

/// The text of a Formula source that fails to parse (empty when it's fine).
[[nodiscard]] std::string formulaError(const std::string& text);
} // namespace fd::devices::wavetable
