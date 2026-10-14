#include "model/PianoRollOptions.h"

#include <array>
#include <nlohmann/json.hpp>
#include <string_view>

namespace fd::model
{
namespace
{
// How each value is written in view state: stable words, not the names people read.
constexpr std::array<std::string_view, 3> drawWords{"note", "chord", "chordUnder"};
constexpr std::array<std::string_view, 3> labelWords{"both", "names", "steps"};
constexpr std::array<std::string_view, 4> laneWords{"velocity", "chance", "release", "controller"};
constexpr std::array<std::string_view, 6> shapeWordList{"triad", "seventh", "ninth", "sus2", "sus4", "add9"};
constexpr std::array<std::string_view, 6> voicingWordList{"smooth", "close", "open",
                                                          "drop2",  "drop3", "spread"};
constexpr std::array<std::string_view, 5> inversionWordList{"auto", "root", "first", "second", "third"};

template <typename E, std::size_t N>
E fromWord(const nlohmann::json& json, const char* key, const std::array<std::string_view, N>& words,
           E fallback)
{
    if (const auto it = json.find(key); it != json.end() && it->is_string())
        for (std::size_t i = 0; i < N; ++i)
            if (words[i] == it->get<std::string>())
                return static_cast<E>(i);
    return fallback;
}

template <typename E, std::size_t N> std::string toWord(E value, const std::array<std::string_view, N>& words)
{
    return std::string(words[static_cast<std::size_t>(value)]);
}

template <typename E, std::size_t N>
std::optional<E> wordTo(std::string_view word, const std::array<std::string_view, N>& words) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        if (words[i] == word)
            return static_cast<E>(i);
    return std::nullopt;
}
} // namespace

std::span<const std::string_view> chordShapeWords() noexcept
{
    return shapeWordList;
}

std::span<const std::string_view> voicingWords() noexcept
{
    return voicingWordList;
}

std::span<const std::string_view> inversionWords() noexcept
{
    return inversionWordList;
}

std::optional<music::ChordShape> chordShapeFromWord(std::string_view word) noexcept
{
    return wordTo<music::ChordShape>(word, shapeWordList);
}

std::optional<music::Voicing> voicingFromWord(std::string_view word) noexcept
{
    return wordTo<music::Voicing>(word, voicingWordList);
}

std::optional<music::Inversion> inversionFromWord(std::string_view word) noexcept
{
    return wordTo<music::Inversion>(word, inversionWordList);
}

nlohmann::json pianoRollOptionsToJson(const PianoRollOptions& o)
{
    return {{"wholeRoll", o.wholeRoll},
            {"others", o.others},
            {"othersHidden", o.othersHidden},
            {"lane", toWord(o.lane.kind, laneWords)},
            {"laneController", o.lane.controller},
            {"draw", toWord(o.draw, drawWords)},
            {"chordLabels", toWord(o.chordLabels, labelWords)},
            {"chordKind", toWord(o.chord.shape, shapeWordList)},
            {"voicing", toWord(o.chord.voicing, voicingWordList)},
            {"inversion", toWord(o.chord.inversion, inversionWordList)},
            {"quantiseStrength", o.quantise.strength},
            {"quantiseSwing", o.quantise.swing},
            {"quantiseStarts", o.quantise.starts},
            {"quantiseEnds", o.quantise.ends}};
}

PianoRollOptions pianoRollOptionsFromJson(const nlohmann::json& json)
{
    PianoRollOptions o;
    if (!json.is_object())
        return o;
    const auto flag = [&](const char* key, bool fallback)
    {
        const auto it = json.find(key);
        return it != json.end() && it->is_boolean() ? it->get<bool>() : fallback;
    };
    const auto number = [&](const char* key, double fallback, double lo, double hi)
    {
        const auto it = json.find(key);
        return it != json.end() && it->is_number() ? std::clamp(it->get<double>(), lo, hi) : fallback;
    };
    if (const auto it = json.find("wholeRoll"); it != json.end() && it->is_array())
        for (const auto& id : *it)
            if (id.is_number_unsigned())
                o.wholeRoll.insert(id.get<std::uint64_t>());
    o.others = flag("others", o.others);
    if (const auto it = json.find("othersHidden"); it != json.end() && it->is_array())
        for (const auto& id : *it)
            if (id.is_number_unsigned())
                o.othersHidden.insert(id.get<std::uint64_t>());
    o.lane.kind = fromWord(json, "lane", laneWords, o.lane.kind);
    if (const auto it = json.find("laneController");
        it != json.end() && it->is_number_integer() && isController(it->get<int>()))
        o.lane.controller = it->get<int>();
    o.draw = fromWord(json, "draw", drawWords, o.draw);
    o.chordLabels = fromWord(json, "chordLabels", labelWords, o.chordLabels);
    o.chord.shape = fromWord(json, "chordKind", shapeWordList, o.chord.shape);
    o.chord.voicing = fromWord(json, "voicing", voicingWordList, o.chord.voicing);
    o.chord.inversion = fromWord(json, "inversion", inversionWordList, o.chord.inversion);
    o.quantise.strength = number("quantiseStrength", o.quantise.strength, 0.0, 1.0);
    o.quantise.swing = number("quantiseSwing", o.quantise.swing, 0.0, 1.0);
    o.quantise.starts = flag("quantiseStarts", o.quantise.starts);
    o.quantise.ends = flag("quantiseEnds", o.quantise.ends);
    if (!o.quantise.starts && !o.quantise.ends)
        o.quantise.starts = true;
    return o;
}

std::optional<NoteLevel> BottomLane::level() const noexcept
{
    switch (kind)
    {
    case Kind::Velocity:
        return NoteLevel::Velocity;
    case Kind::Chance:
        return NoteLevel::Chance;
    case Kind::Release:
        return NoteLevel::Release;
    case Kind::Controller:
    default:
        return std::nullopt;
    }
}

std::string bottomLaneName(const BottomLane& lane)
{
    switch (lane.kind)
    {
    case BottomLane::Kind::Chance:
        return "Chance";
    case BottomLane::Kind::Release:
        return "Release velocity";
    case BottomLane::Kind::Controller:
        return controllerName(lane.controller);
    case BottomLane::Kind::Velocity:
    default:
        return "Velocity";
    }
}

std::string bottomLaneLabel(const BottomLane& lane)
{
    switch (lane.kind)
    {
    case BottomLane::Kind::Chance:
        return "CHANCE";
    case BottomLane::Kind::Release:
        return "RELEASE";
    case BottomLane::Kind::Velocity:
        return "VELOCITY";
    case BottomLane::Kind::Controller:
    default:
        break;
    }
    switch (lane.controller)
    {
    case controllers::pitchBend:
        return "BEND";
    case controllers::aftertouch:
        return "TOUCH";
    case controllers::modWheel:
        return "MOD";
    case controllers::breath:
        return "BREATH";
    case 4:
        return "FOOT";
    case 7:
        return "VOLUME";
    case 10:
        return "PAN";
    case controllers::expression:
        return "EXPR";
    case controllers::sustain:
        return "SUSTAIN";
    case 65:
        return "PORTA";
    case 66:
        return "SOST";
    case 67:
        return "SOFT";
    default:
        return "CC " + std::to_string(lane.controller);
    }
}

const char* drawModeName(DrawMode mode) noexcept
{
    switch (mode)
    {
    case DrawMode::Chord:
        return "Draw chord";
    case DrawMode::ChordUnder:
        return "Draw chord under";
    default:
        return "Draw note";
    }
}

const char* chordLabelsName(ChordLabels labels) noexcept
{
    switch (labels)
    {
    case ChordLabels::Names:
        return "Names";
    case ChordLabels::Steps:
        return "Steps";
    default:
        return "Names and steps";
    }
}

const char* chordShapeName(music::ChordShape shape) noexcept
{
    switch (shape)
    {
    case music::ChordShape::Seventh:
        return "7th";
    case music::ChordShape::Ninth:
        return "9th";
    case music::ChordShape::Sus2:
        return "sus2";
    case music::ChordShape::Sus4:
        return "sus4";
    case music::ChordShape::Add9:
        return "add9";
    default:
        return "Triad";
    }
}

const char* voicingName(music::Voicing voicing) noexcept
{
    switch (voicing)
    {
    case music::Voicing::Close:
        return "Close";
    case music::Voicing::Open:
        return "Open";
    case music::Voicing::Drop2:
        return "Drop 2";
    case music::Voicing::Drop3:
        return "Drop 3";
    case music::Voicing::Spread:
        return "Spread";
    default:
        return "Smooth";
    }
}

const char* inversionName(music::Inversion inversion) noexcept
{
    switch (inversion)
    {
    case music::Inversion::Root:
        return "Root";
    case music::Inversion::First:
        return "1st";
    case music::Inversion::Second:
        return "2nd";
    case music::Inversion::Third:
        return "3rd";
    default:
        return "Auto";
    }
}
} // namespace fd::model
