#pragma once

#include "engine/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <string>

namespace fd::app::browser
{
// Putting what the browser holds into the song (docs/design/browser.md §10, ADR 0046): what each
// drop makes, the same from the arrangement, the mixer, the module chain and a double-click.

/// What a drag from the browser carries: this, then the item's id.
inline constexpr std::string_view dragPrefix = "segno-browser:";
/// The item a drag carries, if it's the browser's.
[[nodiscard]] std::optional<std::string> itemOf(const juce::var& dragDescription);

/// An item, taken apart: a module or plugin (its key: "fd:eq", "ext:…"), a preset (its module's
/// key and its own), or a file.
struct Ref
{
    model::BrowserKind kind = model::BrowserKind::Module;
    std::string key;    // module, plugin, preset: the device's key
    std::string preset; // a preset's key ("factory/Glue")
    juce::File file;    // sample, MIDI file, project, folder
};
[[nodiscard]] std::optional<Ref> parse(const std::string& itemId);

/// What a drop did.
struct Result
{
    bool done = false;
    juce::String toast;       // said after it (an instrument replaced)
    std::uint64_t track = 0;  // a track it made
    std::uint64_t module = 0; // a module it added or loaded
    juce::String refused;     // why it couldn't, said instead (an instrument into a group's chain)
};

/// Loads a preset (`ref`, of its kind) into one of our modules or modulators, in the step under way
/// (or its own, "Load preset …"). False if it can't.
bool loadPreset(engine::Session& session, std::uint64_t module, const Ref& ref);

/// Whether `item` can go on a channel (a track; modules also on returns and the master), and
/// whether it lands on `module` itself (a preset of its kind, a sample on a Sampler).
[[nodiscard]] bool fitsChannel(const std::string& item, bool holdsClips);
[[nodiscard]] bool landsOn(const std::string& item, const model::DeviceSlot& module);

/// On a channel (its header, lanes or strip): a module at the end of its chain (an instrument in
/// place of its own), a preset with its module, a sample or MIDI file as a clip at `seconds`. On a
/// group (groups.md §11), what makes notes or clips goes on a new track at the group's end.
Result onChannel(engine::Session& session, const std::string& item, std::uint64_t channel, double seconds);
/// In a channel's chain: where dropped (`index`), or into `onModule` when it lands on it. A group's
/// chain takes no instrument (refused, saying why).
Result inChain(engine::Session& session, const std::string& item, std::uint64_t channel, int index,
               std::optional<std::uint64_t> onModule);
/// In a rack's chain (racks.md §5): a module (with its preset), or a sample or table in a module
/// of its own, at the place; an instrument here doesn't replace the track's.
Result inRack(engine::Session& session, const std::string& item, const engine::Session::ChainPlace& place);
/// Whether `item` can go on a Drum Kit's pad (drum-kit.md §6): a sample or multisample, a module,
/// preset or plugin (not a rack, a kit or a modulator), a wavetable.
[[nodiscard]] bool fitsPad(const std::string& item);
/// On a Drum Kit's pad (§6): a sample into the pad's Sampler or a Sampler of its own; an instrument
/// (with its preset) in place of the pad's; an effect after its modules; a table on a Wavetable of
/// its own. One step.
Result onPad(engine::Session& session, const std::string& item, std::uint64_t kit, int note);
/// Several samples in a channel's chain (drum-kit.md §11): a Drum Kit with them from C1 up, in
/// place of the channel's instrument. One step.
Result kitWith(engine::Session& session, const std::vector<juce::File>& samples, std::uint64_t channel);
/// Below the tracks: a new track with it; in `group`, at its end.
Result newTrack(engine::Session& session, const std::string& item, double seconds, std::uint64_t group = 0);
/// Double-click or Enter (§10): into the selected module if it lands on it, else onto the
/// selected track at `seconds` (the playhead), else a new track.
Result onSelection(engine::Session& session, const std::string& item, std::optional<std::uint64_t> track,
                   std::optional<std::uint64_t> module, double seconds);
} // namespace fd::app::browser
