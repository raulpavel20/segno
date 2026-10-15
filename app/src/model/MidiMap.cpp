#include "model/MidiMap.h"

#include <algorithm>
#include <cmath>
#include <fd/music/Key.h>

namespace fd::model
{
bool isRelative(MidiMode mode) noexcept
{
    return mode == MidiMode::RelativeTwos || mode == MidiMode::RelativeOffset ||
           mode == MidiMode::RelativeSigned;
}

bool isButton(MidiMode mode) noexcept
{
    return mode == MidiMode::Toggle || mode == MidiMode::Momentary;
}

std::string modeName(MidiMode mode)
{
    switch (mode)
    {
    case MidiMode::Absolute:
        return "Absolute";
    case MidiMode::Toggle:
        return "Toggle";
    case MidiMode::Momentary:
        return "Momentary";
    case MidiMode::RelativeTwos:
        return "Relative (two's)";
    case MidiMode::RelativeOffset:
        return "Relative (offset)";
    case MidiMode::RelativeSigned:
        return "Relative (signed)";
    }
    return {};
}

std::vector<MidiMode> modesFor(MidiSource::Kind kind)
{
    switch (kind)
    {
    case MidiSource::Kind::Note:
        return {MidiMode::Toggle, MidiMode::Momentary};
    case MidiSource::Kind::Cc:
        return {MidiMode::Absolute,     MidiMode::Toggle,         MidiMode::Momentary,
                MidiMode::RelativeTwos, MidiMode::RelativeOffset, MidiMode::RelativeSigned};
    case MidiSource::Kind::Cc14:
    case MidiSource::Kind::PitchBend:
        return {MidiMode::Absolute};
    }
    return {};
}

namespace
{
constexpr std::pair<MidiSource::Kind, const char*> kKinds[] = {{MidiSource::Kind::Cc, "cc"},
                                                               {MidiSource::Kind::Cc14, "cc14"},
                                                               {MidiSource::Kind::Note, "note"},
                                                               {MidiSource::Kind::PitchBend, "pitchBend"}};
constexpr std::pair<MidiMode, const char*> kModes[] = {{MidiMode::Absolute, "absolute"},
                                                       {MidiMode::Toggle, "toggle"},
                                                       {MidiMode::Momentary, "momentary"},
                                                       {MidiMode::RelativeTwos, "relativeTwos"},
                                                       {MidiMode::RelativeOffset, "relativeOffset"},
                                                       {MidiMode::RelativeSigned, "relativeSigned"}};
constexpr std::pair<MidiTakeover, const char*> kTakeovers[] = {
    {MidiTakeover::PickUp, "pickUp"}, {MidiTakeover::Jump, "jump"}, {MidiTakeover::Scale, "scale"}};

template <typename T, size_t N> std::string keyOf(const std::pair<T, const char*> (&table)[N], T value)
{
    for (const auto& [v, k] : table)
        if (v == value)
            return k;
    return {};
}
template <typename T, size_t N>
std::optional<T> valueOf(const std::pair<T, const char*> (&table)[N], const std::string& key)
{
    for (const auto& [v, k] : table)
        if (key == k)
            return v;
    return std::nullopt;
}
} // namespace

std::string kindKey(MidiSource::Kind kind)
{
    return keyOf(kKinds, kind);
}
std::optional<MidiSource::Kind> kindFromKey(const std::string& key)
{
    return valueOf(kKinds, key);
}
std::string modeKey(MidiMode mode)
{
    return keyOf(kModes, mode);
}
std::optional<MidiMode> modeFromKey(const std::string& key)
{
    return valueOf(kModes, key);
}
std::string takeoverKey(MidiTakeover takeover)
{
    return keyOf(kTakeovers, takeover);
}
std::optional<MidiTakeover> takeoverFromKey(const std::string& key)
{
    return valueOf(kTakeovers, key);
}

nlohmann::json toJson(const MidiMapping& m)
{
    return {{"device", m.source.device},
            {"kind", kindKey(m.source.kind)},
            {"channel", m.source.channel},
            {"number", m.source.number},
            {"target", {{"channel", m.target.channel}, {"key", m.target.key}}},
            {"min", m.min},
            {"max", m.max},
            {"mode", modeKey(m.mode)}};
}

std::optional<MidiMapping> midiMappingFromJson(const nlohmann::json& j)
{
    if (!j.is_object() || !j.contains("target") || !j["target"].is_object())
        return std::nullopt;
    MidiMapping m;
    const auto kind = kindFromKey(j.value("kind", std::string()));
    const auto mode = modeFromKey(j.value("mode", std::string()));
    if (!kind || !mode)
        return std::nullopt;
    m.source.kind = *kind;
    m.source.device = j.value("device", std::string());
    m.source.channel = std::clamp(j.value("channel", 1), 1, 16);
    m.source.number = std::clamp(j.value("number", 0), 0, 127);
    m.target.channel = j["target"].value("channel", std::uint64_t{0});
    m.target.key = j["target"].value("key", std::string());
    m.min = std::clamp(j.value("min", 0.0f), 0.0f, 1.0f);
    m.max = std::clamp(j.value("max", 1.0f), 0.0f, 1.0f);
    m.mode = *mode;
    const auto modes = modesFor(m.source.kind);
    if (m.target.key.empty() || std::find(modes.begin(), modes.end(), m.mode) == modes.end())
        return std::nullopt;
    return m;
}

std::string sourceLabel(const MidiSource& source)
{
    switch (source.kind)
    {
    case MidiSource::Kind::Cc:
    case MidiSource::Kind::Cc14:
        return "CC " + std::to_string(source.number);
    case MidiSource::Kind::Note:
        return music::noteName(nullptr, source.number);
    case MidiSource::Kind::PitchBend:
        return "PB";
    }
    return {};
}

std::string sourceText(const MidiSource& source)
{
    auto text = source.kind == MidiSource::Kind::PitchBend ? std::string("Pitch bend") : sourceLabel(source);
    if (source.kind == MidiSource::Kind::Cc14)
        text += " (14-bit)";
    if (source.channel != 1)
        text += " \xc2\xb7 Ch " + std::to_string(source.channel);
    return text;
}

MidiMode learnedMode(const MidiSource& source, int firstValue)
{
    if (source.kind == MidiSource::Kind::Note)
        return MidiMode::Toggle;
    if (source.kind == MidiSource::Kind::Cc && firstValue == 127)
        return MidiMode::Toggle;
    return MidiMode::Absolute;
}

int relativeSteps(MidiMode mode, int value) noexcept
{
    switch (mode)
    {
    case MidiMode::RelativeTwos:
        return value < 64 ? value : value - 128;
    case MidiMode::RelativeOffset:
        return value - 64;
    case MidiMode::RelativeSigned:
        return (value & 64) != 0 ? -(value & 63) : (value & 63);
    default:
        return 0;
    }
}

std::optional<MidiMode> guessRelative(const std::vector<int>& values)
{
    // Small steps only: an encoder turned fast sends a few at a time, never dozens.
    constexpr int kMostSteps = 15;
    bool repeats = false;
    for (size_t i = 1; i < values.size(); ++i)
        repeats = repeats || values[i] == values[i - 1];
    if (!repeats)
        return std::nullopt;
    const auto all = [&values](auto&& fits) { return std::all_of(values.begin(), values.end(), fits); };
    if (all([](int v) { return (v >= 1 && v <= kMostSteps) || (v >= 128 - kMostSteps && v <= 127); }))
        return MidiMode::RelativeTwos;
    if (all([](int v) { return v != 64 && std::abs(v - 64) <= kMostSteps; }))
        return MidiMode::RelativeOffset;
    if (all([](int v) { return (v >= 1 && v <= kMostSteps) || (v >= 65 && v <= 64 + kMostSteps); }))
        return MidiMode::RelativeSigned;
    return std::nullopt;
}

std::string takeoverName(MidiTakeover takeover)
{
    switch (takeover)
    {
    case MidiTakeover::PickUp:
        return "Pick up";
    case MidiTakeover::Jump:
        return "Jump";
    case MidiTakeover::Scale:
        return "Scale";
    }
    return {};
}

std::optional<float> absoluteMove(const MidiMapping& m, float hardware, float current, MidiTakeover takeover,
                                  MidiFollow& follow)
{
    hardware = std::clamp(hardware, 0.0f, 1.0f);
    const float span = m.max - m.min;
    const auto at = [&](float u) { return m.min + span * u; };
    // Moved by something else since it set it last: catch up again.
    constexpr float kNear = 1.0f / 127.0f;
    if (follow.set && std::abs(*follow.set - current) > kNear * 0.5f)
        follow.following = false;
    const auto previous = follow.hardware;
    follow.hardware = hardware;
    const auto set = [&](float travel)
    {
        follow.set = std::clamp(travel, 0.0f, 1.0f);
        return follow.set;
    };
    if (takeover == MidiTakeover::Jump || follow.following)
    {
        follow.following = true;
        return set(at(hardware));
    }
    // Where the control is along the mapping (0 at Min, 1 at Max), and the hardware's place there.
    const float u = std::abs(span) < 1e-6f ? 0.0f : std::clamp((current - m.min) / span, 0.0f, 1.0f);
    if (takeover == MidiTakeover::PickUp)
    {
        // At it, or passed it since the last message: caught.
        if (std::abs(hardware - u) <= kNear || (previous && (*previous - u) * (hardware - u) <= 0.0f))
        {
            follow.following = true;
            return set(at(hardware));
        }
        follow.set.reset();
        return std::nullopt;
    }
    // Scale: the travel left on the hardware's side spread over the control's.
    if (!previous)
    {
        follow.set.reset();
        return std::nullopt; // the first message only says where the hardware is
    }
    float next = u;
    if (hardware > *previous)
        next = u + (hardware - *previous) * (1.0f - u) / std::max(1e-6f, 1.0f - *previous);
    else if (hardware < *previous)
        next = u - (*previous - hardware) * u / std::max(1e-6f, *previous);
    next = std::clamp(next, 0.0f, 1.0f);
    if (std::abs(next - hardware) <= kNear)
        follow.following = true;
    return set(at(next));
}

float relativeMove(const MidiMapping& m, int steps, float step, float current)
{
    const float lo = std::min(m.min, m.max), hi = std::max(m.min, m.max);
    const float direction = m.max >= m.min ? 1.0f : -1.0f;
    return std::clamp(current + direction * static_cast<float>(steps) * step, lo, hi);
}

std::optional<float> buttonMove(const MidiMapping& m, bool down, int choices, float current)
{
    if (m.mode == MidiMode::Momentary)
        return down ? m.max : m.min;
    if (!down)
        return std::nullopt;
    if (choices > 2)
    {
        // The next choice, back to the first after the last.
        const int at = static_cast<int>(std::lround(current * static_cast<float>(choices - 1)));
        return static_cast<float>((at + 1) % choices) / static_cast<float>(choices - 1);
    }
    // A switch or a knob: to whichever end it's further from.
    return std::abs(current - m.max) < std::abs(current - m.min) ? m.min : m.max;
}
int fullScale(MidiSource::Kind kind) noexcept
{
    return kind == MidiSource::Kind::Cc14 || kind == MidiSource::Kind::PitchBend ? 16383 : 127;
}

int feedbackValue(const MidiMapping& m, float travel)
{
    const float span = m.max - m.min;
    const float u = std::abs(span) < 1e-6f ? 0.0f : std::clamp((travel - m.min) / span, 0.0f, 1.0f);
    if (isButton(m.mode) || m.source.kind == MidiSource::Kind::Note)
        return u >= 0.5f ? 127 : 0;
    return static_cast<int>(std::lround(u * static_cast<float>(fullScale(m.source.kind))));
}
} // namespace fd::model
