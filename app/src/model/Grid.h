#pragma once

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string_view>

namespace fd::model
{
/// An editor's grid (docs/design/daw-shell.md, "The grid"; ADR 0047): zoom-adaptive at a width,
/// or a fixed value; triplets; snap. The arrangement and the piano roll each have one. View
/// state: never an undo step.
struct GridSettings
{
    enum class Mode
    {
        Adaptive,
        Fixed
    };
    enum class Width
    {
        Narrowest,
        Narrow,
        Medium,
        Wide,
        Widest
    };
    /// The fixed values, coarsest first.
    enum class Value
    {
        Bars8,
        Bars4,
        Bars2,
        Bar,
        Half,
        Quarter,
        Eighth,
        Sixteenth,
        ThirtySecond
    };
    static constexpr int numWidths = 5, numValues = 9;

    Mode mode = Mode::Adaptive;
    Width width = Width::Narrow;
    Value value = Value::Sixteenth;
    bool triplets = false;
    bool snap = true;

    /// The arrangement's default: Narrow.
    [[nodiscard]] static GridSettings arrangement() noexcept { return {}; }
    /// The piano roll's: Wide, since its step is also the length of a drawn note.
    [[nodiscard]] static GridSettings pianoRoll() noexcept
    {
        GridSettings g;
        g.width = Width::Wide;
        return g;
    }

    /// How far apart adaptive lines must be (px): 7, 14, 28, 56, 112.
    [[nodiscard]] double spacing() const noexcept;

    /// Narrower and wider grid (Ctrl+1, Ctrl+2): the next width, or half or twice the fixed value.
    [[nodiscard]] bool canNarrow() const noexcept;
    [[nodiscard]] bool canWiden() const noexcept;
    [[nodiscard]] GridSettings narrower() const noexcept;
    [[nodiscard]] GridSettings wider() const noexcept;

    friend bool operator==(const GridSettings&, const GridSettings&) = default;
};

/// Names in view state and for assistants: "adaptive", "fixed"; "narrowest" … "widest";
/// "8 bars", "4 bars", "2 bars", "1 bar", "1/2" … "1/32".
[[nodiscard]] std::string_view modeName(GridSettings::Mode mode) noexcept;
[[nodiscard]] std::string_view widthName(GridSettings::Width width) noexcept;
[[nodiscard]] std::string_view valueName(GridSettings::Value value) noexcept;
[[nodiscard]] std::optional<GridSettings::Mode> modeFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<GridSettings::Width> widthFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<GridSettings::Value> valueFromName(std::string_view name) noexcept;

/// `grid` as view state: what differs from `defaults` only (an empty object when nothing does).
[[nodiscard]] nlohmann::json gridToJson(const GridSettings& grid, const GridSettings& defaults);
/// View state back: tolerant, so unknown or bad entries keep `defaults`' values.
[[nodiscard]] GridSettings gridFromJson(const nlohmann::json& json, GridSettings defaults);

/// Both editors' grids, as the UI's view state keeps them.
struct EditorGrids
{
    GridSettings arrangement = GridSettings::arrangement();
    GridSettings pianoRoll = GridSettings::pianoRoll();

    friend bool operator==(const EditorGrids&, const EditorGrids&) = default;
};
/// The view state's "grid" entry: {"arrangement": …, "pianoRoll": …}, defaults left out.
[[nodiscard]] nlohmann::json gridsToViewState(const EditorGrids& grids);
/// The grids from a whole view state; a project saved before the grid (ADR 0047) had one "snap"
/// for both editors.
[[nodiscard]] EditorGrids gridsFromViewState(const nlohmann::json& viewState);

/// Snapping to a grid whose lines below a bar restart at every bar (ADR 0047), in beats from the
/// song's start: the nearest line, the line at or before, and the first line after. A `step` of
/// whole bars counts from the song's start; a step of 0 leaves the beat as it is.
[[nodiscard]] double gridRound(double beat, double step, double barBeats) noexcept;
[[nodiscard]] double gridFloor(double beat, double step, double barBeats) noexcept;
[[nodiscard]] double gridNext(double beat, double step, double barBeats) noexcept;
} // namespace fd::model
