// Chords in MIDI clips (M3 P4, ADR 0058, docs/design/piano-roll.md §14): the chords the notes make,
// as the clip view's chord lane names them; writing and making chords, voiced and voice-led as the
// clip view does; and what its Chord popover suggests. Each write is one undo step.

#include "mcp/tools/Tools.h"

#include "model/NoteTools.h"
#include "model/PianoRollOptions.h"

#include <fd/music/Chords.h>

namespace fd::mcp
{
using namespace tools;

namespace
{
using music::Accidentals;

model::ClipInfo midiClipArg(ToolEnv& env, const Json& value)
{
    auto c = clip(env, value);
    if (c.kind != model::ClipInfo::Kind::Midi)
        throw Problem{formatId(IdKind::Clip, c.id) + " is an audio clip; chords go in MIDI clips (create_midi_clip makes one)."};
    return c;
}

/// The song's key where the clip is.
std::optional<music::Key> keyAt(ToolEnv& env, const model::ClipInfo& c)
{
    return env.session.snapshot()->keys.keyAt(c.contentStartBeat);
}

std::string nameOf(const music::Chord& chord, const std::optional<music::Key>& key)
{
    return music::chordName(chord, key ? &*key : nullptr, Accidentals::Ascii);
}

Json stepOf(const music::Chord& chord, const std::optional<music::Key>& key)
{
    return key ? Json(music::romanNumeral(chord, *key, Accidentals::Ascii)) : Json(nullptr);
}

Json chordJson(const music::Chord& chord, const std::optional<music::Key>& key)
{
    return {{"name", nameOf(chord, key)}, {"step", stepOf(chord, key)}};
}

Json wordsEnum(std::span<const std::string_view> words, const std::string& description)
{
    Json list = Json::array();
    for (auto w : words)
        list.push_back(std::string(w));
    return {{"type", "string"}, {"enum", std::move(list)}, {"description", description}};
}

/// The kind, voicing and inversion asked for (the clip view's Chord popover's defaults otherwise).
model::ChordStyle styleArg(const Json& args)
{
    model::ChordStyle style;
    const auto word = [&](const char* name) { return args.contains(name) ? args[name].get<std::string>() : std::string(); };
    if (args.contains("kind"))
    {
        const auto shape = model::chordShapeFromWord(word("kind"));
        if (!shape)
            throw Problem{"kind: \"" + word("kind") + "\" isn't one of triad, seventh, ninth, sus2, sus4, add9."};
        style.shape = *shape;
    }
    if (args.contains("voicing"))
    {
        const auto voicing = model::voicingFromWord(word("voicing"));
        if (!voicing)
            throw Problem{"voicing: \"" + word("voicing") + "\" isn't one of smooth, close, open, drop2, drop3, spread."};
        style.voicing = *voicing;
    }
    if (args.contains("inversion"))
    {
        const auto inversion = model::inversionFromWord(word("inversion"));
        if (!inversion)
            throw Problem{"inversion: \"" + word("inversion") + "\" isn't one of auto, root, first, second, third."};
        style.inversion = *inversion;
    }
    return style;
}

Json styleSchema(Json properties)
{
    properties["kind"] = wordsEnum(model::chordShapeWords(),
                                   "What the key's chords stack: triad (default), seventh, ninth, sus2, sus4, add9");
    properties["voicing"] = wordsEnum(model::voicingWords(),
                                      "smooth (default: moves least from the chord before), close, open, drop2, "
                                      "drop3, spread");
    properties["inversion"] =
        wordsEnum(model::inversionWords(), "Which tone is in the bass: auto (default), root, first, second, third");
    return properties;
}

double beatArg(ToolEnv& env, const Json& value, const char* name)
{
    if (value.is_number() && value.get<double>() >= 0.0)
        return value.get<double>();
    try
    {
        return lengthArg(env, value);
    }
    catch (const Problem& p)
    {
        throw Problem{std::string(name) + ": " + p.message};
    }
}

Json writtenResult(ToolEnv& env, const model::ClipInfo& before, const model::ClipInfo& after, std::size_t notes)
{
    return {{"clip", clipJson(env, after, false)},
            {"notes", notes},
            {"lengthened", after.end() > before.end() + 1e-9},
            {"loopLengthened", after.loop.length > before.loop.length + 1e-9}};
}

Json writtenSchema()
{
    return result({{"clip", clipResultSchema()},
                   {"notes", integer("How many notes the chords have now")},
                   {"chord", string("The chord written, as the chord lane names it")},
                   {"lengthened", boolean("The clip grew to fit notes past its end")},
                   {"loopLengthened", boolean("A looped clip's loop grew to fit notes past its end")}},
                  {"clip", "notes"});
}
} // namespace

void addChordTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    reads.idempotent = true;
    ToolAnnotations writes;

    registry.tools.push_back(make(
        env,
        {"get_chords", "Read chords",
         "The chords a MIDI clip's notes make, as its chord lane names them: wherever three or more pitch "
         "classes sound together (muted notes don't). Each with its span in beats from the clip's start, its "
         "name (C/E for an inversion), its step in the song's key as a Roman numeral (bVII for a borrowed "
         "chord; null without a key) and its pitches.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")}}, {"clip"}),
         result({{"key", keyResultSchema()},
                 {"chords", {{"type", "array"}, {"description", "In time order: start, length, name, step, pitches"}}}},
                {"key", "chords"}),
         reads},
        [&env](const Json& args, CallContext&) {
            const auto c = midiClipArg(env, args["clip"]);
            const auto key = keyAt(env, c);
            const double offset = noteOffsetBeats(env, c);
            Json chords = Json::array();
            for (const auto& span : model::chordSpans(c.notes))
            {
                std::set<int> pitches;
                for (auto i : span.notes)
                    pitches.insert(c.notes[i].pitch);
                auto j = chordJson(span.chord, key);
                j["start"] = std::round((span.start - offset) * 1e6) / 1e6;
                j["length"] = std::round((span.end - span.start) * 1e6) / 1e6;
                j["pitches"] = std::vector<int>(pitches.begin(), pitches.end());
                chords.push_back(std::move(j));
            }
            return ToolResult::ok({{"key", keyJson(key)}, {"chords", std::move(chords)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_chord", "Write a chord",
         "Writes a chord over a span of a MIDI clip, or changes the chord there: its notes are rewritten, keeping "
         "their timing and velocities, voice-led from the chord before (moving as little as it can) unless a "
         "voicing is given. Give the chord as a name (\"Dm7\", \"C/E\", \"Bbmaj7\") or, with a song key, a Roman "
         "numeral (\"ii7\", \"bVII\", \"V7/V\").",
         object(styleSchema({{"clip", id(IdKind::Clip, "The MIDI clip")},
                             {"chord", string("A chord name or a Roman numeral")},
                             {"start", {{"type", {"number", "string"}}, {"description", "Beats from the clip's start"}}},
                             {"length", length("How long it lasts (default: a bar)")},
                             {"velocity", integer("New notes' velocity, 1-127 (default 100)", 1, 127)}}),
                {"clip", "chord", "start"}),
         writtenSchema(), writes},
        [&env](const Json& args, CallContext&) {
            const auto c = midiClipArg(env, args["clip"]);
            const auto key = keyAt(env, c);
            const auto text = args["chord"].get<std::string>();
            const auto chord = music::parseChord(text, key ? &*key : nullptr);
            if (!chord)
                throw Problem{"chord: \"" + text + "\" isn't a chord name such as \"Dm7\", \"C/E\" or \"F#m7b5\"" +
                              (key ? std::string(", or a Roman numeral such as \"ii7\" or \"bVII\".")
                                   : std::string("; Roman numerals need a song key (set_key)."))};
            const double offset = noteOffsetBeats(env, c);
            const double start = beatArg(env, args["start"], "start") + offset;
            const double length = args.contains("length") ? lengthArg(env, args["length"]) : tempo(env).beatsPerBar();
            // The notes of the chords starting in the span are the ones it replaces.
            std::set<std::size_t> replacing;
            for (const auto& span : model::chordSpans(c.notes))
                if (span.start >= start - 1e-9 && span.start < start + length - 1e-9)
                    replacing.insert(span.notes.begin(), span.notes.end());
            const auto edit = model::writeChord(c.notes, start, start + length, *chord, replacing, styleArg(args),
                                                velocityFromMidi(args.value("velocity", 100.0)));
            env.session.setNotes(c.id, edit.notes);
            env.session.flushSnapshot();
            auto out = writtenResult(env, c, clip(env, args["clip"]), edit.selection.size());
            out["chord"] = nameOf(*chord, key);
            return ToolResult::ok(std::move(out));
        }));

    registry.tools.push_back(make(
        env,
        {"make_chords", "Make chords",
         "Turns notes into chords: each matching note becomes the root of the song key's chord on it (as in C "
         "major without a key), keeping its timing and velocity. With the default voicing, each chord moves as "
         "little as it can from the one before, so a line of single notes becomes a progression.",
         object(styleSchema({{"clip", id(IdKind::Clip, "The MIDI clip")},
                             {"where",
                              {{"anyOf", {{{"enum", {"all"}}}, {{"type", "object"}}}},
                               {"description", "\"all\", or a filter as edit_notes takes"}}}}),
                {"clip", "where"}),
         writtenSchema(), writes},
        [&env](const Json& args, CallContext&) {
            const auto c = midiClipArg(env, args["clip"]);
            const auto key = keyAt(env, c);
            const auto chosen = selectNotes(env, c, args["where"]);
            if (chosen.empty())
                return ToolResult::ok(writtenResult(env, c, c, 0), "No notes match.");
            const auto edit = model::makeChords(c.notes, chosen, key ? &*key : nullptr, styleArg(args));
            env.session.setNotes(c.id, edit.notes);
            env.session.flushSnapshot();
            return ToolResult::ok(writtenResult(env, c, clip(env, args["clip"]), edit.selection.size()));
        }));

    registry.tools.push_back(make(
        env,
        {"suggest_chords", "Suggest chords",
         "What the clip view's Chord popover offers at a place in a MIDI clip: the song key's chords on each step "
         "of its scale, the usual chords after the chord before that place (each with why), and chords borrowed "
         "from the parallel key. Without a song key, as in C major, and nothing borrowed.",
         object(styleSchema({{"clip", id(IdKind::Clip, "The MIDI clip")},
                             {"at", {{"type", {"number", "string"}}, {"description", "Beats from the clip's start"}}}}),
                {"clip", "at"}),
         result({{"key", keyResultSchema()},
                 {"before", {{"type", {"object", "null"}}, {"description", "The chord before the place, if any"}}},
                 {"inKey", {{"type", "array"}, {"description", "The key's chords, from its root up"}}},
                 {"next", {{"type", "array"}, {"description", "The usual next chords, most usual first, with why"}}},
                 {"borrowed", {{"type", "array"}, {"description", "Chords from outside the key, with why"}}}},
                {"key", "inKey", "next", "borrowed"}),
         reads},
        [&env](const Json& args, CallContext&) {
            const auto c = midiClipArg(env, args["clip"]);
            const auto songKey = keyAt(env, c);
            const music::Key key = songKey.value_or(music::Key{0, "major"});
            const auto shape = styleArg(args).shape;
            const double at = beatArg(env, args["at"], "at") + noteOffsetBeats(env, c);
            Json inKey = Json::array(), next = Json::array(), borrowed = Json::array();
            for (const auto& chord : music::keyChords(key, shape))
                inKey.push_back(chordJson(chord, key));
            const auto before = model::chordBefore(c.notes, at);
            if (before)
                for (const auto& s : music::nextChords(key, *before, shape))
                {
                    auto j = chordJson(s.chord, key);
                    j["why"] = std::string(s.why);
                    next.push_back(std::move(j));
                }
            if (songKey)
                for (const auto& s : music::borrowedChords(key, shape))
                {
                    auto j = chordJson(s.chord, key);
                    j["why"] = std::string(s.why);
                    borrowed.push_back(std::move(j));
                }
            return ToolResult::ok({{"key", keyJson(songKey)},
                                   {"before", before ? chordJson(*before, key) : Json(nullptr)},
                                   {"inKey", std::move(inKey)},
                                   {"next", std::move(next)},
                                   {"borrowed", std::move(borrowed)}});
        }));
}
} // namespace fd::mcp
