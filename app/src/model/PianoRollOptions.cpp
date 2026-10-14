#include "model/PianoRollOptions.h"

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>

namespace fd::model
{
namespace
{
// How each value is written in view state: stable words, not the names people read.
constexpr std::array<std::string_view, 3> drawWords{"note", "chord", "chordUnder"};
constexpr std::array<std::string_view, 3> foldWords{"all", "key", "notes"};
constexpr std::array<std::string_view, 3> labelWords{"both", "names", "steps"};
constexpr std::array<std::string_view, 6> shapeWordList{"triad", "seventh", "ninth", "sus2", "sus4", "add9"};
constexpr std::array<std::string_view, 6> voicingWordList{"smooth", "close", "open", "drop2", "drop3", "spread"};
constexpr std::array<std::string_view, 5> inversionWordList{"auto", "root", "first", "second", "third"};

template <typename E, std::size_t N>
E fromWord(const nlohmann::json& json, const char* key, const std::array<std::string_view, N>& words, E fallback)
{
    if (const auto it = json.find(key); it != json.end() && it->is_string())
        for (std::size_t i = 0; i < N; ++i)
            if (words[i] == it->get<std::string>())
                return static_cast<E>(i);
    return fallback;
}

template <typename E, std::size_t N>
std::string toWord(E value, const std::array<std::string_view, N>& words)
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
    return {{"key", o.keySnap},
            {"fold", toWord(o.fold, foldWords)},
            {"others", o.others},
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
    o.keySnap = flag("key", o.keySnap);
    o.fold = fromWord(json, "fold", foldWords, o.fold);
    o.others = flag("others", o.others);
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

const char* drawModeName(DrawMode mode) noexcept
{
    switch (mode)
    {
    case DrawMode::Chord: return "Draw chord";
    case DrawMode::ChordUnder: return "Draw chord under";
    default: return "Draw note";
    }
}

const char* foldName(Fold fold) noexcept
{
    switch (fold)
    {
    case Fold::Key: return "Fold to key";
    case Fold::Notes: return "Fold to notes";
    default: return "Fold off";
    }
}

const char* chordLabelsName(ChordLabels labels) noexcept
{
    switch (labels)
    {
    case ChordLabels::Names: return "Names";
    case ChordLabels::Steps: return "Steps";
    default: return "Names and steps";
    }
}

const char* chordShapeName(music::ChordShape shape) noexcept
{
    switch (shape)
    {
    case music::ChordShape::Seventh: return "7th";
    case music::ChordShape::Ninth: return "9th";
    case music::ChordShape::Sus2: return "sus2";
    case music::ChordShape::Sus4: return "sus4";
    case music::ChordShape::Add9: return "add9";
    default: return "Triad";
    }
}

const char* voicingName(music::Voicing voicing) noexcept
{
    switch (voicing)
    {
    case music::Voicing::Close: return "Close";
    case music::Voicing::Open: return "Open";
    case music::Voicing::Drop2: return "Drop 2";
    case music::Voicing::Drop3: return "Drop 3";
    case music::Voicing::Spread: return "Spread";
    default: return "Smooth";
    }
}

const char* inversionName(music::Inversion inversion) noexcept
{
    switch (inversion)
    {
    case music::Inversion::Root: return "Root";
    case music::Inversion::First: return "1st";
    case music::Inversion::Second: return "2nd";
    case music::Inversion::Third: return "3rd";
    default: return "Auto";
    }
}
} // namespace fd::model
