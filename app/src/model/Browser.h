#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fd::model
{
// The browser (docs/design/browser.md, ADR 0046): what it finds and how, the files of your places
// as the index keeps them, and how a module's presets fold into folders. JUCE-free.

enum class BrowserKind
{
    Module,
    Preset,
    Plugin,
    Rack, // a `.segnorack` file: modules of modules (docs/design/racks.md §7)
    Sample,
    Multisample, // an SFZ file: zones for the Sampler (devices/sampler/SPEC.md, S11)
    Wavetable,   // a table for the Wavetable (docs/design/table-editor.md §7)
    Midi,
    Project,
    Folder,
};

/// "MODULES", "PRESETS"… for a group of results; "modules" etc. for counts ("All 240 samples").
[[nodiscard]] std::string groupTitle(BrowserKind kind);
[[nodiscard]] std::string kindPlural(BrowserKind kind);

/// The kind of file the browser lists by its name: samples (WAV, AIFF, FLAC, Ogg, MP3), wavetables
/// (`.wt`, and sound files in a folder named Wavetables, as Segno's own), multisamples (`.sfz`),
/// racks (`.segnorack`), MIDI files and Segno projects; nullopt for anything else.
[[nodiscard]] std::optional<BrowserKind> fileKind(std::string_view path);

/// A tempo written in a file's name: "Drums 124 bpm", "124BPM", "loop_90" (a number from 60 to 200
/// standing alone as the last number). 0 if there's none.
[[nodiscard]] double tempoFromName(std::string_view name);

/// One file of a place, as the index keeps it.
struct IndexedFile
{
    std::string path;          // absolute
    std::int64_t size = 0;     // bytes } a file whose size and time are unchanged isn't read again
    std::int64_t modified = 0; // ms    }
    BrowserKind kind = BrowserKind::Sample;
    double seconds = 0.0; // a sample's length
    double sampleRate = 0.0;
    int channels = 0;
    double tempo = 0.0; // from its loop metadata or its name; 0: unknown
    int frames = 0;     // a wavetable's
    int zones = 0;      // a multisample's

    bool operator==(const IndexedFile&) const = default;
};

/// Every place's files: `browser-index.json` beside settings.json.
struct BrowserIndexFile
{
    std::map<std::string, std::vector<IndexedFile>> places; // a place's folder -> its files

    bool operator==(const BrowserIndexFile&) const = default;
};
[[nodiscard]] std::string toJson(const BrowserIndexFile& index);
/// What's missing or malformed is left out.
[[nodiscard]] BrowserIndexFile browserIndexFromJson(const std::string& json);

/// Something the browser finds.
struct BrowserItem
{
    std::string id; // "module:fd:eq", "preset:fd:compressor|factory/Glue", "plugin:<key>", "file:<path>"
    BrowserKind kind = BrowserKind::Module;
    std::string name;
    std::string caption;           // where it's from: a preset's module, a file's folder, a plugin's maker
    std::vector<std::string> tags; // a preset's
    std::string words;             // also matched: a description, a folder, a maker
    bool favourite = false;
};

/// A kind's results, best first.
struct BrowserGroup
{
    BrowserKind kind = BrowserKind::Module;
    std::vector<size_t> items; // indices into the searched items
};

/// The browser's search (§6): an item matches when every word of the query is in its name, tags,
/// caption or words (case aside) and it carries every tag asked for. Names count most, then the
/// start of a word; favourites rank higher. Results come grouped by kind, the group with the best
/// match first. Prepared once per set of items; searching doesn't allocate per item.
class BrowserSearch
{
public:
    void setItems(std::vector<BrowserItem> items);
    [[nodiscard]] const std::vector<BrowserItem>& items() const noexcept { return items_; }
    /// No query and no tags: no results (the browser shows its tree).
    [[nodiscard]] std::vector<BrowserGroup> search(std::string_view query,
                                                   const std::set<std::string>& tags = {}) const;
    /// The tags the results carry, most used first.
    [[nodiscard]] std::vector<std::string> tagsOf(const std::vector<BrowserGroup>& groups) const;

private:
    struct Prepared
    {
        std::string name, tags, rest; // lower-cased
    };
    std::vector<BrowserItem> items_;
    std::vector<Prepared> prepared_;
};

/// A module's preset, as its folder in the browser lists it (§5).
struct PresetEntry
{
    std::string key; // the preset's key()
    std::string name;
    std::string folder; // yours: the folder under the module's own ("" at its top; "Drums" below)
    std::vector<std::string> tags;
    bool yours = false;
};

/// What a module's folder holds: folders first, then presets (indices into the entries).
struct PresetFolder
{
    std::string name;
    std::vector<PresetFolder> folders;
    std::vector<size_t> presets;
};

/// The module's folder (§5): Yours first (your presets in your subfolders), then the factory
/// presets: an instrument's in folders by their first tag, an effect's as one list, A to Z.
[[nodiscard]] PresetFolder modulePresetTree(const std::vector<PresetEntry>& presets, bool instrument);
} // namespace fd::model
