#include "model/Grid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-9;

constexpr std::array<std::string_view, GridSettings::numWidths> kWidths{"narrowest", "narrow", "medium",
                                                                        "wide", "widest"};
constexpr std::array<std::string_view, GridSettings::numValues> kValues{
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"};

template <typename E, size_t N>
std::optional<E> fromName(const std::array<std::string_view, N>& names, std::string_view name)
{
    for (size_t i = 0; i < N; ++i)
        if (names[i] == name)
            return static_cast<E>(i);
    return std::nullopt;
}

/// Lines below a bar restart at every bar; whole bars count from the song's start.
bool restarts(double step, double bar) noexcept
{
    return bar > 0.0 && step < bar - kEps;
}
} // namespace

double GridSettings::spacing() const noexcept
{
    return 7.0 * static_cast<double>(1 << static_cast<int>(width));
}

bool GridSettings::canNarrow() const noexcept
{
    return mode == Mode::Adaptive ? width != Width::Narrowest : value != Value::ThirtySecond;
}

bool GridSettings::canWiden() const noexcept
{
    return mode == Mode::Adaptive ? width != Width::Widest : value != Value::Bars8;
}

GridSettings GridSettings::narrower() const noexcept
{
    GridSettings g = *this;
    if (!canNarrow())
        return g;
    if (mode == Mode::Adaptive)
        g.width = static_cast<Width>(static_cast<int>(width) - 1);
    else
        g.value = static_cast<Value>(static_cast<int>(value) + 1);
    return g;
}

GridSettings GridSettings::wider() const noexcept
{
    GridSettings g = *this;
    if (!canWiden())
        return g;
    if (mode == Mode::Adaptive)
        g.width = static_cast<Width>(static_cast<int>(width) + 1);
    else
        g.value = static_cast<Value>(static_cast<int>(value) - 1);
    return g;
}

std::string_view modeName(GridSettings::Mode mode) noexcept
{
    return mode == GridSettings::Mode::Fixed ? "fixed" : "adaptive";
}

std::string_view widthName(GridSettings::Width width) noexcept
{
    return kWidths[static_cast<size_t>(width)];
}

std::string_view valueName(GridSettings::Value value) noexcept
{
    return kValues[static_cast<size_t>(value)];
}

std::optional<GridSettings::Mode> modeFromName(std::string_view name) noexcept
{
    if (name == "adaptive")
        return GridSettings::Mode::Adaptive;
    if (name == "fixed")
        return GridSettings::Mode::Fixed;
    return std::nullopt;
}

std::optional<GridSettings::Width> widthFromName(std::string_view name) noexcept
{
    return fromName<GridSettings::Width>(kWidths, name);
}

std::optional<GridSettings::Value> valueFromName(std::string_view name) noexcept
{
    return fromName<GridSettings::Value>(kValues, name);
}

nlohmann::json gridToJson(const GridSettings& grid, const GridSettings& defaults)
{
    auto j = nlohmann::json::object();
    if (grid.mode != defaults.mode)
        j["mode"] = modeName(grid.mode);
    if (grid.width != defaults.width)
        j["width"] = widthName(grid.width);
    if (grid.value != defaults.value)
        j["value"] = valueName(grid.value);
    if (grid.triplets != defaults.triplets)
        j["triplets"] = grid.triplets;
    if (grid.snap != defaults.snap)
        j["snap"] = grid.snap;
    return j;
}

GridSettings gridFromJson(const nlohmann::json& json, GridSettings defaults)
{
    if (!json.is_object())
        return defaults;
    auto g = defaults;
    const auto text = [&json](const char* key) -> std::string_view
    {
        const auto it = json.find(key);
        return it != json.end() && it->is_string() ? std::string_view(it->get_ref<const std::string&>()) : "";
    };
    const auto flag = [&json](const char* key, bool fallback)
    {
        const auto it = json.find(key);
        return it != json.end() && it->is_boolean() ? it->get<bool>() : fallback;
    };
    g.mode = modeFromName(text("mode")).value_or(g.mode);
    g.width = widthFromName(text("width")).value_or(g.width);
    g.value = valueFromName(text("value")).value_or(g.value);
    g.triplets = flag("triplets", g.triplets);
    g.snap = flag("snap", g.snap);
    return g;
}

nlohmann::json gridsToViewState(const EditorGrids& grids)
{
    return {{"arrangement", gridToJson(grids.arrangement, GridSettings::arrangement())},
            {"pianoRoll", gridToJson(grids.pianoRoll, GridSettings::pianoRoll())}};
}

EditorGrids gridsFromViewState(const nlohmann::json& viewState)
{
    EditorGrids grids;
    if (!viewState.is_object())
        return grids;
    if (const auto it = viewState.find("snap"); it != viewState.end() && it->is_boolean())
        grids.arrangement.snap = grids.pianoRoll.snap = it->get<bool>();
    if (const auto it = viewState.find("grid"); it != viewState.end() && it->is_object())
    {
        const auto entry = [&it](const char* key)
        {
            const auto e = it->find(key);
            return e != it->end() ? *e : nlohmann::json();
        };
        grids.arrangement = gridFromJson(entry("arrangement"), grids.arrangement);
        grids.pianoRoll = gridFromJson(entry("pianoRoll"), grids.pianoRoll);
    }
    return grids;
}

double gridFloor(double beat, double step, double barBeats) noexcept
{
    if (step <= 0.0)
        return beat;
    if (!restarts(step, barBeats))
        return std::floor(beat / step + kEps) * step;
    const double barStart = std::floor(beat / barBeats + kEps) * barBeats;
    return barStart + std::floor((beat - barStart) / step + kEps) * step;
}

double gridNext(double beat, double step, double barBeats) noexcept
{
    if (step <= 0.0)
        return beat;
    const double floor = gridFloor(beat, step, barBeats);
    if (!restarts(step, barBeats))
        return floor + step;
    const double barStart = std::floor(beat / barBeats + kEps) * barBeats;
    return std::min(floor + step, barStart + barBeats);
}

double gridRound(double beat, double step, double barBeats) noexcept
{
    if (step <= 0.0)
        return beat;
    const double floor = gridFloor(beat, step, barBeats), next = gridNext(beat, step, barBeats);
    return beat - floor < next - beat ? floor : next;
}
} // namespace fd::model
