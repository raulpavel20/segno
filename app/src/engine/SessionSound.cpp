// An audio clip's sound and the audio editor's ranges (M3 step P6, ADR 0063,
// docs/design/audio-editor.md §5, §6): gain, the gain line, reverse, polarity, channels, Normalise,
// and what acts on a range of a clip.

#include "engine/ClipFades.h"
#include "engine/SessionImpl.h"

#include <array>
#include <sstream>

namespace fd::engine
{
namespace
{
/// A range's edges this close to the clip's own count as on them.
constexpr double kEdge = 0.001;

const char* rangeName(Session::RangeAction action)
{
    switch (action)
    {
    case Session::RangeAction::Split:
        return "Split";
    case Session::RangeAction::Delete:
        return "Delete";
    case Session::RangeAction::Crop:
        return "Crop";
    case Session::RangeAction::Silence:
        return "Silence";
    case Session::RangeAction::Gain:
        return "Gain";
    case Session::RangeAction::Fade:
        return "Fade";
    case Session::RangeAction::Loop:
        return "Loop";
    }
    return "Edit";
}

/// A reversed copy of `original` in `folder`, 32-bit float WAV, written once: a copy already there
/// that matches (its format, and its start is the original's end reversed) is used again. Invalid
/// if the original can't be read or the copy written.
juce::File reversedCopy(te::Engine& engine, const juce::File& original, const juce::File& folder)
{
    auto& formats = engine.getAudioFileFormatManager();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.readFormatManager.createReaderFor(original));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};
    const auto frames = reader->lengthInSamples;
    const auto channels = static_cast<int>(reader->numChannels);
    const auto check = static_cast<int>(std::min<juce::int64>(frames, 4096));
    juce::AudioBuffer<float> tail(channels, check);
    reader->read(&tail, 0, check, frames - check, true, true);
    tail.reverse(0, check);
    const auto matches = [&](const juce::File& f)
    {
        std::unique_ptr<juce::AudioFormatReader> r(formats.readFormatManager.createReaderFor(f));
        if (r == nullptr || r->lengthInSamples != frames || static_cast<int>(r->numChannels) != channels ||
            r->sampleRate != reader->sampleRate)
            return false;
        juce::AudioBuffer<float> head(channels, check);
        r->read(&head, 0, check, 0, true, true);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < check; ++i)
                if (std::abs(head.getSample(c, i) - tail.getSample(c, i)) > 1e-6f)
                    return false;
        return true;
    };
    (void)folder.createDirectory();
    const auto base = juce::File::createLegalFileName(original.getFileNameWithoutExtension() + " reversed");
    for (int n = 1;; ++n)
    {
        const auto f = folder.getChildFile(base + (n > 1 ? " " + juce::String(n) : juce::String()) + ".wav");
        if (f.existsAsFile())
        {
            if (matches(f))
                return f;
            continue;
        }
        std::unique_ptr<juce::OutputStream> stream(f.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer;
        if (stream != nullptr)
            writer = formats.getWavFormat()->createWriterFor(
                stream, juce::AudioFormatWriterOptions{}
                            .withSampleRate(reader->sampleRate)
                            .withNumChannels(channels)
                            .withBitsPerSample(32)
                            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer == nullptr)
        {
            f.deleteFile();
            return {};
        }
        constexpr int block = 65536;
        juce::AudioBuffer<float> buffer(channels, block);
        for (juce::int64 end = frames; end > 0; end -= block)
        {
            const auto count = static_cast<int>(std::min<juce::int64>(block, end));
            reader->read(&buffer, 0, count, end - count, true, true);
            buffer.reverse(0, count);
            if (!writer->writeFromAudioSampleBuffer(buffer, 0, count))
            {
                writer.reset();
                f.deleteFile();
                return {};
            }
        }
        return f;
    }
}

/// The clip's offset and loop for the same audio played from the other end of its file: what it
/// plays first is what it played last (audio-editor.md §6.3).
void mirrorPlacement(te::AudioClipBase& clip, const model::ClipInfo& info, juce::UndoManager* um)
{
    const double length = info.sourceLength;
    if (info.loop.on())
    {
        const model::ClipLoop mirrored{length - info.loop.end(), info.loop.length};
        const double phase = model::phaseOf(info.offset, info.loop);
        const double endPhase = std::fmod(phase + info.length, info.loop.length);
        writeLoop(clip, mirrored, um);
        clip.setOffset(
            te::TimeDuration::fromSeconds(std::fmod(info.loop.length - endPhase, info.loop.length)));
        return;
    }
    clip.setOffset(te::TimeDuration::fromSeconds(std::max(0.0, length - (info.offset + info.length))));
}

/// What Normalise measured: the file, the stretches of it the clip plays, and how it plays them.
std::string levelKey(const std::string& file, const std::vector<model::LineSpan>& spans,
                     const model::ClipInfo& clip)
{
    std::ostringstream key;
    key << file << '|' << static_cast<int>(clip.sound.channels);
    for (const auto& s : spans)
        key << '|' << s.from << '-' << s.to;
    for (const auto& p : clip.sound.line)
        key << '|' << p.time << ',' << p.value << ',' << p.curve;
    return key.str();
}
} // namespace

bool Session::setSound(const std::vector<std::uint64_t>& clips, const SoundChange& change,
                       const std::string& undoName)
{
    std::vector<te::AudioClipBase*> audio;
    for (auto id : clips)
        if (auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(id)))
            audio.push_back(c);
    if (audio.empty())
        return false;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(juce::String(undoName));
    const auto clampGain = [](float db)
    { return std::clamp(db, model::minusInfinityDb, model::kMaxClipGainDb); };
    // Reversed, a clip plays a reversed copy of its file, and keeps where its forward one is; its
    // offset and loop mirror so it plays the same audio. A clip with takes plays its stack: left alone.
    const auto reverse = [this](te::AudioClipBase& c, bool on, juce::UndoManager* undo)
    {
        const auto info = clipInfoOf(c);
        if (info.stack != 0)
            return;
        auto& s = c.state;
        if (on)
        {
            const auto copy = reversedCopy(impl_->engine, c.getOriginalFile(),
                                           impl_->projectFolder().getChildFile("Reversed"));
            if (copy == juce::File())
                return;
            s.setProperty(soundIds::forward, s[te::IDs::source], undo);
            s.setProperty(te::IDs::source, copy.getFullPathName(),
                          undo); // made relative when saved (ADR 0030)
        }
        else
        {
            s.setProperty(te::IDs::source, s[soundIds::forward], undo);
            s.removeProperty(soundIds::forward, undo);
        }
        mirrorPlacement(c, info, undo);
    };
    for (auto* c : audio)
    {
        auto& s = c->state;
        if (change.gainDb)
            s.setProperty(soundIds::gain, clampGain(*change.gainDb), um);
        if (change.gainBy)
        {
            const float now = soundOf(*c).gainDb;
            // From −∞, a step up starts at −60 dB, where a step is heard.
            s.setProperty(soundIds::gain,
                          clampGain(now <= model::minusInfinityDb && *change.gainBy > 0.0f
                                        ? -60.0f + *change.gainBy
                                        : now + *change.gainBy),
                          um);
        }
        if (change.inverted)
            s.setProperty(soundIds::inverted, *change.inverted, um);
        if (change.channels)
            s.setProperty(soundIds::channels, static_cast<int>(*change.channels), um);
        if (change.reversed && s.hasProperty(soundIds::forward) != *change.reversed)
            reverse(*c, *change.reversed, um);
    }
    return true;
}

bool Session::setGainLine(std::uint64_t clipId, const model::AutomationPoints& line,
                          const std::string& undoName)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr)
        return false;
    auto sorted = line;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.time < b.time; });
    impl_->beginEdit(juce::String(undoName));
    writeGainLine(c->state, sorted, &impl_->edit->getUndoManager());
    return true;
}

std::string Session::rangeRefusal(std::uint64_t clipId, double from, double to, RangeAction action) const
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr)
        return "Not an audio clip.";
    const auto info = clipInfoOf(*c);
    if (!(to - from > kEdge) || from < info.start - kEdge || to > info.end() + kEdge)
        return "The range isn't inside the clip.";
    const bool atStart = from <= info.start + kEdge, atEnd = to >= info.end() - kEdge;
    switch (action)
    {
    case RangeAction::Split:
        if (atStart && atEnd)
            return "The range is the whole clip: there's nothing to split.";
        break;
    case RangeAction::Delete:
    case RangeAction::Crop:
        if (info.stack != 0)
            return "A clip with takes keeps all of itself: choose a take, or split it first.";
        if (atStart && atEnd)
            return action == RangeAction::Delete ? "The range is the whole clip: delete the clip instead."
                                                 : "The range is the whole clip already.";
        break;
    case RangeAction::Fade:
        if (!atStart && !atEnd)
            return "A fade starts at the clip's start or ends at its end: select a range from one of them.";
        break;
    case RangeAction::Loop:
        if (auto track = dynamic_cast<te::AudioTrack*>(c->getClipTrack()); track == nullptr)
            return "Not on a track.";
        if (info.stack != 0)
            return "A clip with takes can't loop.";
        break;
    case RangeAction::Silence:
    case RangeAction::Gain:
        break;
    }
    return {};
}

Session::RangeResult Session::editRange(std::uint64_t clipId, double from, double to, RangeAction action,
                                        double db)
{
    if (auto why = rangeRefusal(clipId, from, to, action); !why.empty())
        return {{}, std::move(why)};
    auto* c = impl_->clip(clipId);
    const auto info = clipInfoOf(*c);
    from = std::max(from, info.start);
    to = std::min(to, info.end());
    const bool atStart = from <= info.start + kEdge, atEnd = to >= info.end() - kEdge;
    EditGroup group(*this, rangeName(action));
    RangeResult result;

    // The range made a clip of its own: [left,] middle[, right].
    const auto split = [&]
    {
        std::uint64_t middle = clipId, left = 0, right = 0;
        if (!atEnd)
            if (const auto r = splitClips({clipId}, to); !r.empty())
                right = r.front();
        if (!atStart)
            if (const auto m = splitClips({clipId}, from); !m.empty())
            {
                left = clipId;
                middle = m.front();
            }
        return std::array<std::uint64_t, 3>{left, middle, right};
    };

    switch (action)
    {
    case RangeAction::Split:
    {
        const auto [left, middle, right] = split();
        result.clips.push_back(middle);
        for (auto id : {left, right})
            if (id != 0)
                result.clips.push_back(id);
        break;
    }
    case RangeAction::Delete:
    {
        const auto [left, middle, right] = split();
        deleteClips({middle});
        for (auto id : {left, right})
            if (id != 0)
                result.clips.push_back(id);
        break;
    }
    case RangeAction::Crop:
    {
        const auto [left, middle, right] = split();
        std::vector<std::uint64_t> outer;
        for (auto id : {left, right})
            if (id != 0)
                outer.push_back(id);
        deleteClips(outer);
        result.clips.push_back(middle);
        break;
    }
    case RangeAction::Silence:
    case RangeAction::Gain:
    {
        auto line = info.sound.line;
        for (const auto& span : model::fileSpans(info, from, to))
            line = action == RangeAction::Silence ? model::lineSilenced(line, span)
                                                  : model::lineWithGain(line, span, db);
        setGainLine(clipId, line, rangeName(action));
        result.clips.push_back(clipId);
        break;
    }
    case RangeAction::Fade:
        if (atStart)
            setFades({clipId}, model::Fade{to - from, 0.0f}, std::nullopt);
        else
            setFades({clipId}, std::nullopt, model::Fade{to - from, 0.0f});
        result.clips.push_back(clipId);
        break;
    case RangeAction::Loop:
        if (auto why = setClipLoop(clipId, {model::contentAt(info, from), to - from}, "Loop"); !why.empty())
            result.why = std::move(why);
        else
            result.clips.push_back(clipId);
        break;
    }
    return result;
}

std::vector<std::pair<std::uint64_t, std::optional<float>>>
Session::normaliseGains(const std::vector<std::uint64_t>& clips, const NormaliseSettings& settings) const
{
    std::vector<std::uint64_t> ids;
    std::vector<model::ClipLevels> levels;
    std::vector<float> gains;
    for (auto id : clips)
    {
        auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(id));
        if (c == nullptr)
            continue;
        const auto info = clipInfoOf(*c);
        // What it plays of the file as recorded, through its warp and its loop: a reversed clip's
        // original, which its line follows.
        const auto file = forwardFileOf(*c).getFullPathName().toStdString();
        const auto spans = model::fileSpans(info, info.start, info.end());
        const auto key = levelKey(file, spans, info);
        auto found = impl_->levelCache.find(key);
        if (found == impl_->levelCache.end())
        {
            const auto measured = model::measureClip(file, spans, info.sound);
            found = impl_->levelCache.emplace(key, measured.value_or(model::ClipLevels{})).first;
        }
        ids.push_back(id);
        levels.push_back(found->second);
        gains.push_back(info.sound.gainDb);
    }
    const auto result =
        model::normaliseGains(levels, gains, settings.to, settings.targetDb, settings.together);
    std::vector<std::pair<std::uint64_t, std::optional<float>>> out;
    for (size_t i = 0; i < ids.size(); ++i)
        out.emplace_back(ids[i], result[i]);
    return out;
}

std::vector<std::uint64_t> Session::normalise(const std::vector<std::uint64_t>& clips,
                                              const NormaliseSettings& settings)
{
    const auto gains = normaliseGains(clips, settings);
    std::vector<std::uint64_t> changed;
    EditGroup group(*this, "Normalise");
    for (const auto& [id, gain] : gains)
        if (gain && setSound({id}, {.gainDb = *gain}, "Normalise"))
            changed.push_back(id);
    return changed;
}
} // namespace fd::engine
