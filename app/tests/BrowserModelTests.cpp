// The browser's model (docs/design/browser.md, ADR 0046): search and its speed, a module's
// folder of presets, the index file, tempos in names.
#include "model/Browser.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace fd::model;

namespace
{
BrowserItem item(std::string id, BrowserKind kind, std::string name, std::vector<std::string> tags = {},
                 std::string caption = {}, std::string words = {})
{
    return {std::move(id),    kind, std::move(name), std::move(caption), std::move(tags),
            std::move(words), false};
}

std::vector<std::string> names(const BrowserSearch& s, const BrowserGroup& g)
{
    std::vector<std::string> out;
    for (const auto i : g.items)
        out.push_back(s.items()[i].name);
    return out;
}
} // namespace

TEST_CASE("browser: search matches every word, names first, grouped by kind", "[fast]")
{
    BrowserSearch s;
    s.setItems({
        item("module:fd:compressor", BrowserKind::Module, "Compressor"),
        item("preset:a", BrowserKind::Preset, "Glue", {"Bus", "Subtle"}, "Compressor",
             "Holds a mix together"),
        item("preset:b", BrowserKind::Preset, "Vocal leveller", {"Vocals"}, "Compressor"),
        item("preset:c", BrowserKind::Preset, "Sub bass", {"Bass", "Clean"}, "Subtractive Synth"),
        item("file:/s/Kick 808.wav", BrowserKind::Sample, "Kick 808.wav", {}, "Drums"),
        item("file:/s/Bass loop 124 bpm.wav", BrowserKind::Sample, "Bass loop 124 bpm.wav", {}, "Loops"),
        item("file:/s/Drums", BrowserKind::Folder, "Drums", {}, "Samples"),
    });

    // Nothing typed: nothing found (the tree shows).
    CHECK(s.search("").empty());

    // "comp": the module first (its name starts so), then the presets that are its.
    auto r = s.search("comp");
    REQUIRE(r.size() == 2);
    CHECK(r[0].kind == BrowserKind::Module);
    CHECK(r[1].kind == BrowserKind::Preset);
    CHECK(names(s, r[1]) == std::vector<std::string>{"Glue", "Vocal leveller"});

    // "bass": a name starting so ranks above one where it's a later word; the best group first.
    r = s.search("bass");
    REQUIRE(r.size() == 2);
    CHECK(r[0].kind == BrowserKind::Sample);
    CHECK(names(s, r[0]) == std::vector<std::string>{"Bass loop 124 bpm.wav"});
    CHECK(names(s, r[1]) == std::vector<std::string>{"Sub bass"});

    // Every word must match; tags, captions and words count.
    CHECK(s.search("glue subtle").size() == 1);
    CHECK(s.search("glue vocals").empty());
    CHECK(names(s, s.search("mix together").front()) == std::vector<std::string>{"Glue"});
    r = s.search("drums");
    REQUIRE(r.size() == 2);
    CHECK(r[0].kind == BrowserKind::Folder); // the folder's name, before a sample in it
    CHECK(r[1].kind == BrowserKind::Sample);

    // Tags narrow, alone or with words.
    CHECK(names(s, s.search("", {"Vocals"}).front()) == std::vector<std::string>{"Vocal leveller"});
    CHECK(s.search("compressor", {"bus"}).front().items.size() == 1);
    CHECK(s.tagsOf(s.search("compressor")) == std::vector<std::string>{"Bus", "Subtle", "Vocals"});
}

TEST_CASE("browser: favourites rank higher; search over 10 000 files answers within 50 ms", "[fast]")
{
    std::vector<BrowserItem> items;
    for (int i = 0; i < 10000; ++i)
        items.push_back(item("file:" + std::to_string(i), BrowserKind::Sample,
                             (i % 3 == 0   ? "Kick "
                              : i % 3 == 1 ? "Snare "
                                           : "Hat ") +
                                 std::to_string(i) + ".wav",
                             {}, "Drums/Kit " + std::to_string(i / 100)));
    items[5000].favourite = true;
    items[5000].name = "Kick 9999 loud.wav";
    items[4999].name = "Kick 9999 soft.wav";
    BrowserSearch s;
    s.setItems(std::move(items));

    const auto r = s.search("kick 9999");
    REQUIRE_FALSE(r.empty());
    CHECK(s.items()[r.front().items.front()].name == "Kick 9999 loud.wav"); // the favourite

    for (const auto* q : {"k", "kick", "snare 12", "kit 42", "hat 9", "zzz"})
    {
        const auto start = std::chrono::steady_clock::now();
        (void)s.search(q);
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        INFO(q << ": " << ms << " ms");
        CHECK(ms < 50.0);
    }
}

TEST_CASE("browser: a module's folder holds Yours, then the factory presets", "[fast]")
{
    const std::vector<PresetEntry> presets{
        {"factory/Sub bass", "Sub bass", "", {"Bass", "Clean"}, false},
        {"factory/Warm pad", "Warm pad", "", {"Pad"}, false},
        {"factory/Acid line", "Acid line", "", {"Bass", "Lead"}, false},
        {"factory/Noise", "Noise", "", {}, false},
        {"user//My bass", "My bass", "", {"Bass"}, true},
        {"user/Live/Set 2", "Set 2", "Live", {}, true},
        {"user/Live/Set 1", "Set 1", "Live", {}, true},
    };
    // An instrument: Yours first, then folders by the first tag, A to Z, then what has no tag.
    const auto synth = modulePresetTree(presets, true);
    REQUIRE(synth.folders.size() == 3);
    CHECK(synth.folders[0].name == "Yours");
    CHECK(synth.folders[0].presets == std::vector<size_t>{4});
    REQUIRE(synth.folders[0].folders.size() == 1);
    CHECK(synth.folders[0].folders[0].name == "Live");
    CHECK(synth.folders[0].folders[0].presets == std::vector<size_t>{6, 5}); // Set 1, Set 2
    CHECK(synth.folders[1].name == "Bass");
    CHECK(synth.folders[1].presets == std::vector<size_t>{2, 0}); // Acid line, Sub bass
    CHECK(synth.folders[2].name == "Pad");
    CHECK(synth.presets == std::vector<size_t>{3});

    // An effect: Yours, then every factory preset A to Z.
    const auto effect = modulePresetTree(presets, false);
    REQUIRE(effect.folders.size() == 1);
    CHECK(effect.presets == std::vector<size_t>{2, 3, 0, 1});

    // Nothing of yours: no Yours folder.
    CHECK(modulePresetTree({presets[0]}, true).folders.front().name == "Bass");
}

TEST_CASE("browser: the index file round-trips and drops what it can't read; tempos in names", "[fast]")
{
    BrowserIndexFile index;
    index.places["/home/me/Samples"] = {
        {"/home/me/Samples/Kick.wav", 1000, 42, BrowserKind::Sample, 0.5, 44100.0, 1, 0.0},
        {"/home/me/Samples/Loop 124 bpm.flac", 9000, 43, BrowserKind::Sample, 7.74, 48000.0, 2, 124.0},
        {"/home/me/Samples/Groove.mid", 300, 44, BrowserKind::Midi, 0.0, 0.0, 0, 0.0},
    };
    CHECK(browserIndexFromJson(toJson(index)) == index);
    CHECK(browserIndexFromJson("not json").places.empty());
    const auto partial = browserIndexFromJson(
        R"({"places":{"/p":[{"path":"/p/a.wav","size":1},{"path":"/p/notes.txt"},{"size":3},"x"]},"later":true})");
    REQUIRE(partial.places.at("/p").size() == 1);
    CHECK(partial.places.at("/p")[0].kind == BrowserKind::Sample);

    CHECK(fileKind("/a/B.WAV") == BrowserKind::Sample);
    CHECK(fileKind("/a/b.mid") == BrowserKind::Midi);
    CHECK(fileKind("/a/Song.segno") == BrowserKind::Project);
    CHECK_FALSE(fileKind("/a/readme.txt"));
    CHECK_FALSE(fileKind("/a.b/noext"));

    CHECK(tempoFromName("Drums 124 bpm") == 124.0);
    CHECK(tempoFromName("bass_98BPM_Cmin") == 98.0);
    CHECK(tempoFromName("Loop_90") == 90.0);
    CHECK(tempoFromName("Groove 128") == 128.0);
    CHECK(tempoFromName("Kick 808") == 0.0);  // not a likely tempo
    CHECK(tempoFromName("TR909 Hat") == 0.0); // part of a word
    CHECK(tempoFromName("Pad 2") == 0.0);
}
