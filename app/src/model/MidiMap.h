#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
/// A controller's message as a mapping knows it (docs/design/midi-learn.md §3, §8).
struct MidiSource
{
    enum class Kind
    {
        Cc,        // a knob, fader or button sending a controller
        Cc14,      // two controllers 32 apart, together one 14-bit control (`number` the coarse one)
        Note,      // a pad or key: a button
        PitchBend, // a 14-bit fader
    };
    Kind kind = Kind::Cc;
    int channel = 1;    // 1-16
    int number = 0;     // the controller (0-127; 0-31 for Cc14) or the note; 0 for pitch bend
    std::string device; // the input's name, as recording's inputs name it

    friend bool operator==(const MidiSource&, const MidiSource&) = default;
};

/// What a mapping moves (§7): a channel's parameter by its automation key ("volume", "pan",
/// "send/<id>", "m<module>/<parameter>"), or a channel's "mute", "solo" or "arm" (buttons). The
/// transport's buttons have no channel: "transport.play", "transport.stop", "transport.record",
/// "transport.loop", "transport.metronome", "transport.capture" (kept with the app, ML7).
struct MidiTarget
{
    std::uint64_t channel = 0;
    std::string key;

    [[nodiscard]] bool isTransport() const noexcept { return key.rfind("transport.", 0) == 0; }
    /// Mute, solo, arm and the transport: on or off, never automated.
    [[nodiscard]] bool isSwitchOnly() const noexcept
    {
        return isTransport() || key == "mute" || key == "solo" || key == "arm";
    }
    friend bool operator==(const MidiTarget&, const MidiTarget&) = default;
};

/// How the hardware moves its control (§5): a knob or fader sets the value (Absolute); a button
/// flips it (Toggle) or holds it on (Momentary); a relative encoder adds steps, in one of three
/// encodings (as Live guesses them).
enum class MidiMode
{
    Absolute,
    Toggle,
    Momentary,
    RelativeTwos,   // 1-63 up, 127-65 down (two's complement)
    RelativeOffset, // 65-127 up, 63-1 down (binary offset around 64)
    RelativeSigned, // 1-63 up, 65-127 down (the top bit the sign)
};
[[nodiscard]] bool isRelative(MidiMode mode) noexcept;
[[nodiscard]] bool isButton(MidiMode mode) noexcept;
/// As a mapping's row names it: "Absolute", "Toggle", "Momentary", "Relative (two's)",
/// "Relative (offset)", "Relative (signed)".
[[nodiscard]] std::string modeName(MidiMode mode);
/// The modes a source can take: a note is a button; pitch bend and 14-bit controls only set.
[[nodiscard]] std::vector<MidiMode> modesFor(MidiSource::Kind kind);

/// How a knob takes over a value that differs from it (ML4, Settings > Controllers).
enum class MidiTakeover
{
    PickUp, // the control waits until the hardware passes its value, then follows
    Jump,   // the control goes straight to the hardware
    Scale,  // the hardware's travel left is spread over the control's travel left
};

/// One mapping (§3-§6): one source moves one target (ML6), between Min and Max of its control's
/// travel (0..1; a Max below the Min turns it the other way, ML3).
struct MidiMapping
{
    MidiSource source;
    MidiTarget target;
    float min = 0.0f, max = 1.0f;
    MidiMode mode = MidiMode::Absolute;

    friend bool operator==(const MidiMapping&, const MidiMapping&) = default;
};

/// Keys as files and assistants write them: "cc", "cc14", "note", "pitchBend"; "absolute",
/// "toggle", "momentary", "relativeTwos", "relativeOffset", "relativeSigned"; "pickUp", "jump",
/// "scale".
[[nodiscard]] std::string kindKey(MidiSource::Kind kind);
[[nodiscard]] std::optional<MidiSource::Kind> kindFromKey(const std::string& key);
[[nodiscard]] std::string modeKey(MidiMode mode);
[[nodiscard]] std::optional<MidiMode> modeFromKey(const std::string& key);
[[nodiscard]] std::string takeoverKey(MidiTakeover takeover);
[[nodiscard]] std::optional<MidiTakeover> takeoverFromKey(const std::string& key);
/// A mapping as the app's settings keep it: {device, kind, channel, number, target: {channel,
/// key}, min, max, mode}. Nullopt from what isn't one (a mode its source can't take, say).
[[nodiscard]] nlohmann::json toJson(const MidiMapping& mapping);
[[nodiscard]] std::optional<MidiMapping> midiMappingFromJson(const nlohmann::json& json);

/// What a mapped control's corner and tooltip call its source: "CC 21", "C3", "PB"; a channel
/// other than 1 added in the long form ("CC 21 · Ch 2").
[[nodiscard]] std::string sourceLabel(const MidiSource& source);
[[nodiscard]] std::string sourceText(const MidiSource& source);

/// What the first message learned makes (§3): a note, a button (Toggle); pitch bend and a 14-bit
/// pair, Absolute; a controller, Absolute unless it starts at its top (127: a button's press).
[[nodiscard]] MidiMode learnedMode(const MidiSource& source, int firstValue);

/// A relative encoder's encoding, guessed from a controller's first values (§3): only once the
/// same value has come twice in a row (an absolute knob never repeats itself) and every value fits
/// one encoding's small steps. Nullopt: absolute, or not sure yet.
[[nodiscard]] std::optional<MidiMode> guessRelative(const std::vector<int>& values);
/// A relative value's steps (+ up, - down) in an encoding; 0 for Absolute and the buttons.
[[nodiscard]] int relativeSteps(MidiMode mode, int value) noexcept;

[[nodiscard]] std::string takeoverName(MidiTakeover takeover);

/// What a mapping remembers between messages, for takeover and buttons (the engine keeps one per
/// mapping; it starts afresh when the mapping is made or the project opened).
struct MidiFollow
{
    std::optional<float> hardware; // the last position (0..1 of its travel)
    std::optional<float> set;      // the travel the mapping set last
    bool following = false;        // caught up: the control follows the hardware
};

/// An absolute control's next travel for its control at `current` (0..1), the hardware now at
/// `hardware` (0..1 of its own travel). Nullopt: the control stays (picking up). Leaves `follow`
/// ready for the next. A control moved by something else since (the mouse, automation) is picked
/// up again.
[[nodiscard]] std::optional<float> absoluteMove(const MidiMapping& mapping, float hardware, float current,
                                                MidiTakeover takeover, MidiFollow& follow);
/// A relative encoder's next travel: `steps` (relativeSteps) of `step` (the control's small step,
/// as its arrow keys take) added, kept within Min and Max.
[[nodiscard]] float relativeMove(const MidiMapping& mapping, int steps, float step, float current);
/// A button's next travel (ML5) for a press (`down`) or a release, the control having `choices`
/// values (0: continuous; 2: a switch; more: a choice). Toggle: a press flips a switch, flips a
/// knob between Min and Max, or steps a choice on (back to its first after its last); a release
/// does nothing. Momentary: the press sets Max, the release Min. Nullopt: no change.
[[nodiscard]] std::optional<float> buttonMove(const MidiMapping& mapping, bool down, int choices,
                                              float current);

/// What goes back to the hardware for its control at `travel` (ML10: motorised faders, LED rings):
/// where the control is along Min..Max in the source's resolution (0-127; 0-16383 for a 14-bit
/// pair and pitch bend); a button 127 while on (nearer Max), 0 while off.
[[nodiscard]] int feedbackValue(const MidiMapping& mapping, float travel);
/// The largest value a source sends: 127, or 16383 for a 14-bit pair and pitch bend.
[[nodiscard]] int fullScale(MidiSource::Kind kind) noexcept;
} // namespace fd::model
