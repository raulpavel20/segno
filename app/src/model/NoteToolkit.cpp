#include "model/NoteToolkit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace fd::model
{
namespace
{
// ---- describing the tools ----------------------------------------------------------------------

ToolSetting number(std::string_view id, std::string_view label, double min, double max, double initial,
                   std::string_view unit = {}, double step = 1.0)
{
    return {.id = id, .label = label, .min = min, .max = max, .step = step, .initial = initial, .unit = unit};
}

ToolSetting choice(std::string_view id, std::string_view label, std::vector<std::string_view> choices,
                   std::vector<std::string_view> words, int initial = 0, bool menu = false)
{
    return {.id = id,
            .label = label,
            .choices = std::move(choices),
            .words = std::move(words),
            .max = 0.0,
            .initial = static_cast<double>(initial),
            .menu = menu};
}

ToolSetting shownFor(ToolSetting s, std::string_view when, int is)
{
    s.when = when;
    s.whenIs = is;
    return s;
}

ToolSetting seed()
{
    return number("seed", "Seed", 1, 9999, 1);
}

ToolSetting existing()
{
    return choice("existing", "Existing", {"Keep", "Replace"}, {"keep", "replace"});
}

ToolSetting rate(std::string_view id, std::string_view label, std::vector<std::string_view> rates,
                 int initial)
{
    auto words = rates; // as written: "1/16"
    return choice(id, label, std::move(rates), std::move(words), initial);
}

std::vector<ToolSpec> makeTools()
{
    using K = ToolKind;
    return {
        {"quantise",
         "Quantise",
         K::Transform,
         "Moves notes to the grid, part of the way or all, with swing",
         {number("strength", "Strength", 0, 100, 100, "%"), number("swing", "Swing", 0, 100, 0, "%"),
          choice("moves", "Moves", {"Starts", "Ends", "Both"}, {"starts", "ends", "both"})}},
        {"humanize",
         "Humanize",
         K::Transform,
         "Small random shifts of timing, velocity and length",
         {number("timing", "Timing", 0, 60, 15, "ms"), number("velocity", "Velocity", 0, 40, 10),
          number("length", "Length", 0, 50, 10, "%"), seed()}},
        {"strum",
         "Strum",
         K::Transform,
         "Spreads each chord's notes in time, like a strum",
         {choice("direction", "Direction", {"Up", "Down", "Alternate"}, {"up", "down", "alternate"}),
          number("time", "Time", 0, 250, 40, "ms"),
          choice("curve", "Curve", {"Even", "Faster", "Slower"}, {"even", "faster", "slower"}),
          number("velocity", "Velocity", -20, 20, 0, "per note")}},
        {"arpeggiate",
         "Arpeggiate",
         K::Transform,
         "Each chord becomes its notes one after another, over its length",
         {choice("order", "Order", {"Up", "Down", "Up-down", "Played", "Random"},
                 {"up", "down", "up-down", "played", "random"}),
          rate("rate", "Rate", {"1/4", "1/8", "1/16", "1/32"}, 2), number("gate", "Gate", 10, 100, 80, "%"),
          number("octaves", "Octaves", 1, 4, 1), seed()}},
        {"chop",
         "Chop",
         K::Transform,
         "Splits each note into equal repeats",
         {number("pieces", "Pieces", 2, 16, 4),
          choice("velocity", "Velocity", {"Even", "Falling", "Rising"}, {"even", "falling", "rising"}),
          number("change", "Change", 0, 100, 50, "%")}},
        {"ornament",
         "Ornament",
         K::Transform,
         "Flams and grace notes before each note",
         {choice("kind", "Kind", {"Flam", "From above", "From below"}, {"flam", "above", "below"}),
          choice("notes", "Notes", {"1", "2"}, {"1", "2"}), number("before", "Before", 5, 150, 30, "ms"),
          number("loudness", "Loudness", 10, 100, 60, "%")}},
        {"velocity",
         "Velocity",
         K::Transform,
         "Shapes velocities: a ramp, a wave, a range, at random",
         {choice("shape", "Shape", {"Ramp", "Wave", "Range", "Random"}, {"ramp", "wave", "range", "random"}),
          shownFor(number("from", "From", 1, 127, 40), "shape", 0),
          shownFor(number("to", "To", 1, 127, 110), "shape", 0),
          shownFor(number("cycles", "Cycles", 1, 16, 2), "shape", 1),
          shownFor(number("depth", "Depth", 0, 64, 30), "shape", 1),
          shownFor(number("range", "Range", -100, 100, -50, "%"), "shape", 2),
          shownFor(number("amount", "Amount", 0, 64, 12), "shape", 3), shownFor(seed(), "shape", 3)}},
        {"span",
         "Span",
         K::Transform,
         "Each note lasts a share of the time to the next (100 % legato)",
         {number("length", "Length", 5, 100, 100, "%")}},
        {"randomise",
         "Randomise pitch",
         K::Transform,
         "Pitches moved at random within a range, by steps of the key with one",
         {number("range", "Range", 1, 24, 5), choice("inKey", "In key", {"Yes", "No"}, {"yes", "no"}),
          seed()}},
        {"rhythm",
         "Rhythm",
         K::Generate,
         "A Euclidean pattern, on a pitch or on the chords there",
         {number("steps", "Steps", 1, 32, 16), number("pulses", "Pulses", 1, 32, 5),
          number("rotate", "Rotate", 0, 31, 0), rate("step", "Step", {"1/4", "1/8", "1/16", "1/32"}, 2),
          choice("on", "On", {"A pitch", "The chords"}, {"pitch", "chords"}),
          shownFor(number("pitch", "Pitch", 0, 127, 60, "pitch"), "on", 0),
          number("velocity", "Velocity", 1, 127, 100), existing()}},
        {"melody",
         "Melody",
         K::Generate,
         "Notes of the key, mostly by steps; chord notes on strong beats",
         {number("low", "Lowest", 0, 127, 60, "pitch"), number("high", "Highest", 0, 127, 72, "pitch"),
          number("density", "Density", 10, 100, 60, "%"), rate("step", "Step", {"1/8", "1/16"}, 0), seed(),
          existing()}},
        {"chords",
         "Chords",
         K::Generate,
         "A progression of the key's chords, voiced as the Chord popover does",
         {choice("progression", "Progression",
                 {"1–5–6–4", "6–4–1–5", "1–4–5–1", "2–5–1", "1–6–3–7", "Suggested"},
                 {"1-5-6-4", "6-4-1-5", "1-4-5-1", "2-5-1", "1-6-3-7", "suggested"}, 0, true),
          choice("kind", "Kind", {"Triad", "7th"}, {"triad", "seventh"}),
          choice("each", "Each", {"1 bar", "2 beats", "1 beat"}, {"bar", "2 beats", "beat"}),
          choice("voicing", "Voicing", {"Smooth", "Close", "Open", "Spread"},
                 {"smooth", "close", "open", "spread"}),
          shownFor(seed(), "progression", 5), number("velocity", "Velocity", 1, 127, 90), existing()}},
        {"bassline",
         "Bassline",
         K::Generate,
         "A bassline following the chord lane",
         {choice("pattern", "Pattern", {"Roots", "Roots and fifths", "Walking"},
                 {"roots", "fifths", "walking"}, 0, true),
          rate("step", "Step", {"1/4", "1/8"}, 0), number("around", "Around", 24, 60, 36, "pitch"),
          number("velocity", "Velocity", 1, 127, 100), existing()}},
    };
}

// ---- running them --------------------------------------------------------------------------------

constexpr double kTogether = 1.0 / 64.0; // notes starting this close are one chord

/// Seeded dice: the same seed, the same throws.
struct Dice
{
    std::uint64_t state;

    explicit Dice(double seed)
        : state(static_cast<std::uint64_t>(std::llround(seed)) * 0x9E3779B97F4A7C15ULL + 1U)
    {
    }

    double next() noexcept
    {
        state += 0x9E3779B97F4A7C15ULL;
        auto z = state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        return static_cast<double>(z >> 11U) * 0x1.0p-53;
    }
    /// −1 … 1.
    double signedNext() noexcept { return next() * 2.0 - 1.0; }
    int below(int n) noexcept { return std::min(n - 1, static_cast<int>(next() * n)); }
};

double rateBeats(std::string_view word)
{
    if (word == "1/4")
        return 1.0;
    if (word == "1/8")
        return 0.5;
    if (word == "1/32")
        return 0.125;
    return 0.25;
}

float clampVelocity(double v)
{
    return midiVelocity(static_cast<float>(std::clamp(v, 1.0 / 127.0, 1.0)));
}

Note sane(Note n)
{
    n.pitch = std::clamp(n.pitch, 0, 127);
    n.start = std::max(0.0, n.start);
    n.length = std::max(minNoteLength, n.length);
    n.velocity = clampVelocity(n.velocity);
    return n;
}

struct Targets
{
    std::vector<std::size_t> indices; // in time order
    bool all = false;
};

Targets targetsOf(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    Targets t;
    for (auto i : selection)
        if (i < notes.size())
            t.indices.push_back(i);
    if (t.indices.empty())
    {
        t.all = true;
        for (std::size_t i = 0; i < notes.size(); ++i)
            t.indices.push_back(i);
    }
    std::stable_sort(t.indices.begin(), t.indices.end(),
                     [&](auto a, auto b) { return noteOrder(notes[a], notes[b]); });
    return t;
}

/// The targets replaced by `changed`, which are selected unless the tool acted on every note.
NoteEdit replace(const std::vector<Note>& notes, const Targets& targets, const std::vector<Note>& changed)
{
    std::vector<Note> out;
    std::set<std::size_t> placed;
    const std::set<std::size_t> gone(targets.indices.begin(), targets.indices.end());
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!gone.contains(i))
            out.push_back(notes[i]);
    for (const auto& n : changed)
    {
        placed.insert(out.size());
        out.push_back(sane(n));
    }
    auto edit = resolveOverlaps(out, placed);
    if (targets.all)
        edit.selection.clear();
    return edit;
}

/// The targets in groups of notes starting together (chords; a lone note is a group of one).
std::vector<std::vector<std::size_t>> groupsOf(const std::vector<Note>& notes, const Targets& t)
{
    std::vector<std::vector<std::size_t>> groups;
    for (auto i : t.indices)
        if (!groups.empty() && notes[i].start - notes[groups.back().front()].start < kTogether)
            groups.back().push_back(i);
        else
            groups.push_back({i});
    return groups;
}

double get(const ToolValues& v, const char* id)
{
    const auto it = v.find(id);
    return it == v.end() ? 0.0 : it->second;
}

int pick(const ToolValues& v, const char* id)
{
    return static_cast<int>(std::lround(get(v, id)));
}

const music::Key& keyOrC(const ToolContext& c)
{
    static const music::Key cMajor{0, "major"};
    return c.key != nullptr ? *c.key : cMajor;
}

double msToBeats(double ms, const ToolContext& c)
{
    return ms / 1000.0 / std::max(1e-6, c.secondsPerBeat);
}

// ---- transforms ---------------------------------------------------------------------------------

std::vector<Note> humanize(std::vector<Note> notes, const ToolValues& v, const ToolContext& c)
{
    Dice dice(get(v, "seed"));
    const double timing = msToBeats(get(v, "timing"), c);
    for (auto& n : notes)
    {
        n.start = std::max(0.0, n.start + dice.signedNext() * timing);
        n.velocity = clampVelocity(n.velocity + dice.signedNext() * get(v, "velocity") / 127.0);
        n.length *= 1.0 + dice.signedNext() * get(v, "length") / 100.0;
    }
    return notes;
}

std::vector<Note> strum(const std::vector<Note>& notes, const std::vector<std::vector<std::size_t>>& groups,
                        const ToolValues& v, const ToolContext& c)
{
    std::vector<Note> out;
    const double time = msToBeats(get(v, "time"), c);
    const int direction = pick(v, "direction"), curve = pick(v, "curve");
    for (std::size_t g = 0; g < groups.size(); ++g)
    {
        auto group = groups[g];
        std::sort(group.begin(), group.end(),
                  [&](auto a, auto b) { return notes[a].pitch < notes[b].pitch; });
        const bool down = direction == 1 || (direction == 2 && g % 2 == 1);
        if (down)
            std::reverse(group.begin(), group.end());
        const auto n = group.size();
        const double span = time * static_cast<double>(n - 1);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = n > 1 ? static_cast<double>(i) / static_cast<double>(n - 1) : 0.0;
            // Faster: the gaps shrink as it goes; slower: they grow.
            const double shaped = curve == 1 ? 1.0 - (1.0 - x) * (1.0 - x) : curve == 2 ? x * x : x;
            auto note = notes[group[i]];
            const double end = note.start + note.length;
            note.start += span * shaped;
            note.length = std::max(minNoteLength, end - note.start); // it ends where it did
            note.velocity =
                clampVelocity(note.velocity + get(v, "velocity") * static_cast<double>(i) / 127.0);
            out.push_back(note);
        }
    }
    return out;
}

std::vector<Note> arpeggiate(const std::vector<Note>& notes,
                             const std::vector<std::vector<std::size_t>>& groups, const ToolValues& v)
{
    std::vector<Note> out;
    Dice dice(get(v, "seed"));
    const auto& spec = *findNoteTool("arpeggiate");
    const double rate = rateBeats(spec.setting("rate")->words[static_cast<std::size_t>(pick(v, "rate"))]);
    const double gate = get(v, "gate") / 100.0;
    const int octaves = std::max(1, pick(v, "octaves")), order = pick(v, "order");
    for (const auto& group : groups)
    {
        // The notes to play, with each source note's velocity, an octave up for each octave asked.
        std::vector<Note> played;
        for (auto i : group)
            played.push_back(notes[i]);
        if (order != 3) // "Played": as they are in the clip
            std::sort(played.begin(), played.end(),
                      [](const auto& a, const auto& b) { return a.pitch < b.pitch; });
        std::vector<Note> sequence;
        for (int o = 0; o < octaves; ++o)
            for (auto n : played)
            {
                n.pitch += 12 * o;
                sequence.push_back(n);
            }
        if (order == 1)
            std::reverse(sequence.begin(), sequence.end());
        else if (order == 2 && sequence.size() > 2) // up then down, the ends once
            for (auto k = sequence.size() - 2; k >= 1; --k)
            {
                const auto again = sequence[k];
                sequence.push_back(again);
            }
        double start = notes[group.front()].start, end = start;
        for (auto i : group)
            end = std::max(end, notes[i].start + notes[i].length);
        std::size_t k = 0;
        for (double t = start; t < end - 1e-9; t += rate, ++k)
        {
            auto n = order == 4
                         ? sequence[static_cast<std::size_t>(dice.below(static_cast<int>(sequence.size())))]
                         : sequence[k % sequence.size()];
            n.start = t;
            n.length = std::min(rate * gate, end - t);
            out.push_back(n);
        }
    }
    return out;
}

std::vector<Note> chop(const std::vector<Note>& notes, const ToolValues& v)
{
    std::vector<Note> out;
    const int pieces = std::max(2, pick(v, "pieces")), shape = pick(v, "velocity");
    const double change = get(v, "change") / 100.0;
    for (const auto& note : notes)
        for (int k = 0; k < pieces; ++k)
        {
            auto n = note;
            n.length = note.length / pieces;
            n.start = note.start + n.length * k;
            const double x = static_cast<double>(k) / (pieces - 1);
            const double scale = shape == 1 ? 1.0 - change * x : shape == 2 ? 1.0 - change * (1.0 - x) : 1.0;
            n.velocity = clampVelocity(note.velocity * scale);
            out.push_back(n);
        }
    return out;
}

std::vector<Note> ornament(const std::vector<Note>& notes, const ToolValues& v, const ToolContext& c)
{
    std::vector<Note> out;
    const int kind = pick(v, "kind"), count = pick(v, "notes") + 1;
    const double before = msToBeats(get(v, "before"), c), loud = get(v, "loudness") / 100.0;
    for (const auto& note : notes)
    {
        out.push_back(note);
        for (int g = 1; g <= count; ++g)
        {
            const double at = note.start - before * g;
            if (at < 0.0)
                continue;
            // A grace note a step of the key away (a semitone without one); the farther first.
            const int away = kind == 1 ? g : kind == 2 ? -g : 0;
            const int pitch = away == 0          ? note.pitch
                              : c.key != nullptr ? music::moveInKey(*c.key, note.pitch, away)
                                                 : note.pitch + away;
            auto grace = note;
            grace.pitch = pitch;
            grace.start = at;
            grace.length = before;
            grace.velocity = clampVelocity(note.velocity * loud);
            grace.chance = 1.0f;
            out.push_back(grace);
        }
    }
    return out;
}

std::vector<Note> velocityShape(std::vector<Note> notes, const ToolValues& v)
{
    if (notes.empty())
        return notes;
    const int shape = pick(v, "shape");
    double t0 = notes.front().start, t1 = t0, mean = 0.0;
    for (const auto& n : notes)
    {
        t0 = std::min(t0, n.start);
        t1 = std::max(t1, n.start);
        mean += n.velocity;
    }
    mean /= static_cast<double>(notes.size());
    Dice dice(get(v, "seed"));
    for (auto& n : notes)
    {
        const double x = t1 - t0 > 1e-9 ? (n.start - t0) / (t1 - t0) : 0.0;
        double out = n.velocity;
        if (shape == 0)
            out = (get(v, "from") + (get(v, "to") - get(v, "from")) * x) / 127.0;
        else if (shape == 1)
            out = n.velocity +
                  get(v, "depth") / 127.0 * std::sin(2.0 * std::numbers::pi * get(v, "cycles") * x);
        else if (shape == 2)
            out = mean + (n.velocity - mean) * (1.0 + get(v, "range") / 100.0);
        else
            out = n.velocity + dice.signedNext() * get(v, "amount") / 127.0;
        n.velocity = clampVelocity(out);
    }
    return notes;
}

std::vector<Note> spanNotes(std::vector<Note> notes, const ToolValues& v)
{
    const double share = get(v, "length") / 100.0;
    for (auto& n : notes)
    {
        // The next start after this one (a chord's notes share it); the last keeps its length.
        double next = -1.0;
        for (const auto& m : notes)
            if (m.start > n.start + kTogether && (next < 0.0 || m.start < next))
                next = m.start;
        if (next > 0.0)
            n.length = std::max(minNoteLength, (next - n.start) * share);
    }
    return notes;
}

std::vector<Note> randomise(std::vector<Note> notes, const ToolValues& v, const ToolContext& c)
{
    Dice dice(get(v, "seed"));
    const int range = pick(v, "range");
    const bool inKey = c.key != nullptr && pick(v, "inKey") == 0;
    for (auto& n : notes)
    {
        const int by = dice.below(2 * range + 1) - range;
        n.pitch = inKey ? music::moveInKey(*c.key, music::nearestInKey(*c.key, n.pitch), by) : n.pitch + by;
        n.pitch = std::clamp(n.pitch, 0, 127);
    }
    return notes;
}

// ---- generators ---------------------------------------------------------------------------------

/// The new notes into the span: Replace takes the notes starting there out first; Keep leaves out
/// any new note on a pitch a kept note is sounding. The new notes are selected.
NoteEdit place(const std::vector<Note>& notes, std::vector<Note> made, const ToolValues& v,
               const ToolContext& c, const std::set<std::size_t>& drop = {})
{
    const bool keep = pick(v, "existing") == 0;
    const auto [from, to] = c.span;
    std::vector<Note> out, kept;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!drop.contains(i) && (keep || notes[i].start < from - 1e-9 || notes[i].start >= to - 1e-9))
            out.push_back(notes[i]);
    kept = out;
    std::set<std::size_t> placed;
    for (auto n : made)
    {
        n = sane(n);
        n.length = std::min(n.length, std::max(minNoteLength, to - n.start)); // within the span
        const bool clash = keep && std::any_of(kept.begin(), kept.end(),
                                               [&](const Note& k)
                                               {
                                                   return k.pitch == n.pitch &&
                                                          k.start < n.start + n.length - 1e-9 &&
                                                          k.start + k.length > n.start + 1e-9;
                                               });
        if (clash)
            continue;
        placed.insert(out.size());
        out.push_back(n);
    }
    return resolveOverlaps(out, placed);
}

/// The chord sounding at a content beat (the clip's own notes), if any.
const ChordSpan* chordAt(const std::vector<ChordSpan>& spans, double beat)
{
    for (const auto& s : spans)
        if (beat >= s.start - 1e-9 && beat < s.end - 1e-9)
            return &s;
    return nullptr;
}

/// The `degree`th note of the key's scale (1: the root), as a pitch class.
int degreeClass(const music::Key& key, int degree)
{
    std::vector<int> classes;
    const auto set = music::notes(key);
    for (int k = 0; k < 12; ++k)
        if ((set >> ((key.root + k) % 12) & 1U) != 0)
            classes.push_back((key.root + k) % 12);
    if (classes.empty())
        return key.root;
    return classes[static_cast<std::size_t>((degree - 1) % static_cast<int>(classes.size()))];
}

/// A pitch class placed nearest `around`.
int nearestPitch(int pitchClass, int around)
{
    int best = pitchClass;
    for (int p = pitchClass; p <= 127; p += 12)
        if (std::abs(p - around) < std::abs(best - around))
            best = p;
    return best;
}

/// On the chords, the chords' notes starting in the span go: the pattern plays them instead.
std::vector<Note> rhythm(const std::vector<Note>& notes, const ToolValues& v, const ToolContext& c,
                         std::set<std::size_t>& drop)
{
    std::vector<Note> out;
    const int steps = std::max(1, pick(v, "steps"));
    const int pulses = std::clamp(pick(v, "pulses"), 0, steps), rotate = pick(v, "rotate");
    const auto& spec = *findNoteTool("rhythm");
    const double step = rateBeats(spec.setting("step")->words[static_cast<std::size_t>(pick(v, "step"))]);
    const bool onChords = pick(v, "on") == 1;
    const auto spans = chordSpans(notes);
    const auto velocity = static_cast<float>(get(v, "velocity") / 127.0);
    if (onChords)
        for (const auto& s : spans)
            for (auto n : s.notes)
                if (notes[n].start >= c.span.first - 1e-9 && notes[n].start < c.span.second - 1e-9)
                    drop.insert(n);
    int k = 0;
    for (double t = c.span.first; t < c.span.second - 1e-9; t += step, ++k)
    {
        const int i = ((k + rotate) % steps + steps) % steps;
        if ((i * pulses) % steps >= pulses) // Euclid: the pulses spread as evenly as they go
            continue;
        if (!onChords)
            out.push_back({pick(v, "pitch"), t, step, velocity});
        else if (const auto* chord = chordAt(spans, t))
            for (auto n : chord->notes)
                out.push_back({notes[n].pitch, t, step, velocity});
    }
    return out;
}

std::vector<Note> melody(const std::vector<Note>& notes, const ToolValues& v, const ToolContext& c)
{
    std::vector<Note> out;
    const auto& key = keyOrC(c);
    const int low = std::min(pick(v, "low"), pick(v, "high")),
              high = std::max(pick(v, "low"), pick(v, "high"));
    const auto& spec = *findNoteTool("melody");
    const double step = rateBeats(spec.setting("step")->words[static_cast<std::size_t>(pick(v, "step"))]);
    const double density = get(v, "density") / 100.0;
    const auto spans = chordSpans(notes);
    Dice dice(get(v, "seed"));
    int pitch = std::clamp(music::nearestInKey(key, (low + high) / 2), low, high);
    for (double t = c.span.first; t < c.span.second - 1e-9; t += step)
    {
        if (dice.next() >= density)
            continue;
        // Mostly by steps of the key; turned back at the range's ends.
        static constexpr int moves[] = {-2, -1, -1, 0, 1, 1, 2};
        int next = music::moveInKey(key, pitch, moves[dice.below(7)]);
        if (next < low || next > high)
            next = music::moveInKey(key, pitch, next < low ? 1 : -1);
        pitch = std::clamp(next, low, high);
        // On a strong beat over a chord: its nearest note.
        const double songBeat = c.origin + t;
        const bool strong = std::abs(songBeat - std::round(songBeat)) < 1e-6;
        if (const auto* chord = strong ? chordAt(spans, t) : nullptr)
        {
            const auto classes = music::pitchClasses(chord->chord);
            int best = pitch, distance = 128;
            for (int p = low; p <= high; ++p)
                if ((classes >> (p % 12) & 1U) != 0 && std::abs(p - pitch) < distance)
                {
                    best = p;
                    distance = std::abs(p - pitch);
                }
            pitch = best;
        }
        const auto velocity = static_cast<float>((strong ? 100.0 : 84.0) / 127.0);
        out.push_back({pitch, t, step * 0.9, velocity});
    }
    // Each lasts until the next, as a line does (at most a beat).
    for (std::size_t i = 0; i + 1 < out.size(); ++i)
        out[i].length = std::min(1.0, out[i + 1].start - out[i].start) * 0.95;
    return out;
}

std::vector<Note> chords(const std::vector<Note>& notes, const ToolValues& v, const ToolContext& c)
{
    std::vector<Note> out;
    const auto& key = keyOrC(c);
    static const std::vector<std::vector<int>> progressions{
        {1, 5, 6, 4}, {6, 4, 1, 5}, {1, 4, 5, 1}, {2, 5, 1}, {1, 6, 3, 7}};
    const int which = pick(v, "progression");
    const auto shape = pick(v, "kind") == 1 ? music::ChordShape::Seventh : music::ChordShape::Triad;
    const double each =
        std::array{c.beatsPerBar, 2.0, 1.0}[static_cast<std::size_t>(std::clamp(pick(v, "each"), 0, 2))];
    const auto voicing =
        std::array{music::Voicing::Smooth, music::Voicing::Close, music::Voicing::Open,
                   music::Voicing::Spread}[static_cast<std::size_t>(std::clamp(pick(v, "voicing"), 0, 3))];
    const auto velocity = static_cast<float>(get(v, "velocity") / 127.0);
    Dice dice(get(v, "seed"));
    std::vector<int> before = pitchesBefore(notes, c.span.first);
    std::optional<music::Chord> last;
    int k = 0;
    for (double t = c.span.first; t < c.span.second - 1e-9; t += each, ++k)
    {
        music::Chord chord;
        if (which < static_cast<int>(progressions.size()))
        {
            const auto& degrees = progressions[static_cast<std::size_t>(which)];
            chord = music::keyChord(
                &key, degreeClass(key, degrees[static_cast<std::size_t>(k) % degrees.size()]), shape);
        }
        else if (!last) // Suggested: from the key's home chord, each next one of the usual moves
            chord = music::keyChord(&key, key.root, shape);
        else
        {
            const auto next = music::nextChords(key, *last, shape);
            chord =
                next.empty()
                    ? music::keyChord(&key, key.root, shape)
                    : next[static_cast<std::size_t>(dice.below(std::min(3, static_cast<int>(next.size()))))]
                          .chord;
        }
        last = chord;
        const auto pitches = music::voiceChord(chord, {.voicing = voicing, .before = before, .centre = 62.0});
        for (int p : pitches)
            out.push_back({p, t, std::min(each, c.span.second - t), velocity});
        if (!pitches.empty())
            before = pitches;
    }
    return out;
}

std::vector<Note> bassline(const std::vector<Note>& notes, const ToolValues& v, const ToolContext& c)
{
    std::vector<Note> out;
    const auto spans = chordSpans(notes);
    const auto& spec = *findNoteTool("bassline");
    const double step = rateBeats(spec.setting("step")->words[static_cast<std::size_t>(pick(v, "step"))]);
    const int around = pick(v, "around"), pattern = pick(v, "pattern");
    const auto velocity = static_cast<float>(get(v, "velocity") / 127.0);
    const auto& key = keyOrC(c);
    for (std::size_t s = 0; s < spans.size(); ++s)
    {
        const auto& span = spans[s];
        const double from = std::max(span.start, c.span.first), to = std::min(span.end, c.span.second);
        if (to <= from)
            continue;
        const int root = nearestPitch(span.chord.root, around); // its root, however the chord is voiced
        const auto classes = music::pitchClasses(span.chord);
        const int fifth = (classes >> ((span.chord.root + 7) % 12) & 1U) != 0 ? root + 7 : root;
        // Walking: the root, a chord note, the fifth, then a note leading to the next chord's root.
        int third = root;
        for (int k : {3, 4})
            if ((classes >> ((span.chord.root + k) % 12) & 1U) != 0)
                third = root + k;
        const int nextRoot = s + 1 < spans.size() ? nearestPitch(spans[s + 1].chord.root, around) : root;
        const int leading =
            nextRoot > root ? music::moveInKey(key, nextRoot, -1) : music::moveInKey(key, nextRoot, 1);
        int k = 0;
        for (double t = from; t < to - 1e-9; t += step, ++k)
        {
            int pitch = root;
            if (pattern == 1)
                pitch = k % 2 == 0 ? root : fifth;
            else if (pattern == 2)
            {
                const bool lastStep = t + step >= to - 1e-9;
                pitch =
                    lastStep && s + 1 < spans.size() ? leading : std::array{root, third, fifth, third}[k % 4];
            }
            out.push_back({std::clamp(pitch, 0, 127), t, std::min(step, to - t) * 0.9, velocity});
        }
    }
    return out;
}
} // namespace

const ToolSetting* ToolSpec::setting(std::string_view which) const noexcept
{
    for (const auto& s : settings)
        if (s.id == which)
            return &s;
    return nullptr;
}

const std::vector<ToolSpec>& noteTools()
{
    static const auto tools = makeTools();
    return tools;
}

const ToolSpec* findNoteTool(std::string_view id) noexcept
{
    for (const auto& t : noteTools())
        if (t.id == id)
            return &t;
    return nullptr;
}

ToolValues toolValues(const ToolSpec& tool, const ToolValues& given)
{
    ToolValues out;
    for (const auto& s : tool.settings)
    {
        const auto it = given.find(std::string(s.id));
        double v = it == given.end() ? s.initial : it->second;
        if (s.isChoice())
            v = std::clamp(std::round(v), 0.0, static_cast<double>(s.choices.size() - 1));
        else
        {
            v = std::clamp(v, s.min, s.max);
            if (s.step > 0.0)
                v = s.min + std::round((v - s.min) / s.step) * s.step;
        }
        out[std::string(s.id)] = v;
    }
    return out;
}

bool settingShown(const ToolSpec& tool, const ToolSetting& setting, const ToolValues& values)
{
    if (setting.when.empty())
        return true;
    const auto it = values.find(std::string(setting.when));
    const auto* other = tool.setting(setting.when);
    const double v = it != values.end() ? it->second : other != nullptr ? other->initial : 0.0;
    return static_cast<int>(std::lround(v)) == setting.whenIs;
}

std::string settingText(const ToolSetting& setting, double value, const music::Key* key)
{
    if (setting.isChoice())
        return std::string(setting.choices[static_cast<std::size_t>(
            std::clamp<long>(std::lround(value), 0, static_cast<long>(setting.choices.size() - 1)))]);
    const auto whole = static_cast<long>(std::lround(value));
    if (setting.unit == "pitch")
        return music::noteName(key, static_cast<int>(whole));
    std::string text =
        whole < 0 ? "\xE2\x88\x92" + std::to_string(-whole) : std::to_string(whole); // a real minus
    // A setting finer than a whole shows its step's decimals ("92.00 BPM", "0.250 s").
    if (setting.step > 0.0 && setting.step < 1.0)
    {
        const int digits = static_cast<int>(std::ceil(-std::log10(setting.step) - 1e-9));
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.*f", digits, std::abs(value));
        text = (value < -0.5 * setting.step ? std::string("\xE2\x88\x92") : std::string()) + buffer;
    }
    if (setting.unit == "%")
        return text + " %";
    if (!setting.unit.empty())
        return text + " " + std::string(setting.unit);
    return text;
}

std::string toolUnavailable(const ToolSpec& tool, const std::vector<Note>& notes)
{
    if (tool.kind == ToolKind::Transform && notes.empty())
        return "The clip has no notes yet";
    if (tool.id == "bassline" && chordSpans(notes).empty())
        return "The bassline follows the chord lane: write chords first";
    return {};
}

ToolRun runNoteTool(const ToolSpec& tool, const std::vector<Note>& notes,
                    const std::set<std::size_t>& selection, const ToolValues& given,
                    const ToolContext& context)
{
    if (auto why = toolUnavailable(tool, notes); !why.empty())
        return {{notes, {}}, std::move(why)};
    const auto v = toolValues(tool, given);
    if (tool.kind == ToolKind::Generate)
    {
        std::vector<Note> made;
        std::set<std::size_t> drop;
        if (tool.id == "rhythm")
            made = rhythm(notes, v, context, drop);
        else if (tool.id == "melody")
            made = melody(notes, v, context);
        else if (tool.id == "chords")
            made = chords(notes, v, context);
        else
            made = bassline(notes, v, context);
        return {place(notes, std::move(made), v, context, drop), {}};
    }
    if (tool.id == "quantise") // the grid's own function (§9.5)
    {
        const int moves = pick(v, "moves");
        const QuantiseSettings settings{.strength = get(v, "strength") / 100.0,
                                        .swing = get(v, "swing") / 100.0,
                                        .starts = moves != 1,
                                        .ends = moves != 0};
        return {
            quantiseNotes(notes, selection,
                          {.step = context.gridStep, .origin = context.origin, .bar = context.beatsPerBar},
                          settings),
            {}};
    }
    const auto t = targetsOf(notes, selection);
    std::vector<Note> picked;
    for (auto i : t.indices)
        picked.push_back(notes[i]);
    std::vector<Note> changed;
    if (tool.id == "humanize")
        changed = humanize(picked, v, context);
    else if (tool.id == "strum")
        changed = strum(notes, groupsOf(notes, t), v, context);
    else if (tool.id == "arpeggiate")
        changed = arpeggiate(notes, groupsOf(notes, t), v);
    else if (tool.id == "chop")
        changed = chop(picked, v);
    else if (tool.id == "ornament")
        changed = ornament(picked, v, context);
    else if (tool.id == "velocity")
        changed = velocityShape(picked, v);
    else if (tool.id == "span")
        changed = spanNotes(picked, v);
    else
        changed = randomise(picked, v, context);
    return {replace(notes, t, changed), {}};
}
} // namespace fd::model
