#include "engine/WarpPlayer.h"

#include "engine/ClipFades.h"
#include "engine/SongKey.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fd/dsp/Resampling.h>
#include <limits>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_graph/tracktion_graph.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wextra-semi"
#if __clang__
#pragma GCC diagnostic ignored "-Wimplicit-float-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#endif
#endif
#include <tracktion_engine/3rd_party/signalsmith-stretch/signalsmith-stretch.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace fd::engine
{
namespace te = tracktion;

namespace
{
using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

/// Voices playing at once: the piece playing, the next fading in, and the next ones primed.
constexpr int kVoices = 4;
/// Stretchers for Tones: one per voice that stretches at once.
constexpr int kStretchers = 3;
/// Output frames rendered at a time: every buffer is sized for this, never for the device's block.
constexpr int kChunk = 256;
/// The fastest a piece reads its file, in file seconds a second (Beats transposed up).
constexpr double kMaxSpeed = 16.0;
/// Stretchers are seeded, so a render is the same each time.
constexpr long kSeed = 0x5345474e;

/// A piece of the file as the player plays it: from its pinned moment (`file`, seconds) at its
/// beat, read at `filePerBeat` seconds of the file a beat (Tones, Repitch), or from its moment at
/// its own speed until `fileEnd` (Beats).
struct Piece
{
    double beat = 0.0, file = 0.0, fileEnd = 0.0, filePerBeat = 0.5;
};

/// What a clip's player plays, read from the clip when the graph is built.
struct Play
{
    std::vector<Piece> pieces; // in order, the file's start first
    model::WarpMode mode = model::WarpMode::Tones;
    model::WarpTails tails = model::WarpTails::Fade;
    double semitones = 0.0;
    bool formants = false;
    double startBeat = 0.0;   // the edit's beat where the clip starts
    double offsetBeats = 0.0; // the content's beat there (its phase in the loop, while it loops)
    double loopStart = 0.0, loopLength = 0.0; // content beats; length 0: no loop
    double playedFrom = 0.0, playedTo = 0.0;  // edit beats: the clip's range with any extension
    double beatsPerSecond = 2.0;              // the song's where the clip starts, when it was built
};

/// One occurrence of a piece: piece `piece` in lap `lap`, heard over [from, to) of the clip's
/// unwrapped content (u), where content beat c = u − shift. At a piece's own start it fades in
/// over the fade before `from`, and out over the fade before `to` where the next piece starts; at a
/// loop's seam it starts and stops dead (the clip's seam fades are the fade node's).
struct Occurrence
{
    long lap = 0;
    int piece = 0;
    double from = 0.0, to = 0.0, shift = 0.0;
    bool fadesIn = true, fadesOut = true;
};

class WarpPlayerNode final : public te::graph::Node, public te::TracktionEngineNode
{
public:
    WarpPlayerNode(te::AudioFile file, Play play, int channels, te::ChannelConfiguration sourceChannels,
                   te::LiveClipLevel level, te::ProcessState& state, te::EditItemID id, bool offline)
        : TracktionEngineNode(state), file_(std::move(file)), play_(std::move(play)), channels_(channels),
          sourceChannels_(std::move(sourceChannels)), level_(std::move(level)), id_(id), offline_(offline)
    {
        fileRate_ = file_.getSampleRate();
        fileFrames_ = file_.getLengthInSamples();
    }

    te::graph::NodeProperties getNodeProperties() override
    {
        te::graph::NodeProperties props;
        props.hasAudio = true;
        props.hasMidi = false;
        props.numberOfChannels = channels_;
        props.nodeID = static_cast<size_t>(id_.getRawID());
        constexpr size_t magicHash = size_t(0x5345474e57415250); // "SEGNWARP"
        if (props.nodeID != 0)
            te::hash_combine(props.nodeID, magicHash);
        return props;
    }

    std::vector<te::graph::Node*> getDirectInputNodes() override { return {}; }

    void prepareToPlay(const te::graph::PlaybackInitialisationInfo& info) override
    {
        (void)dsp::SincKernels::instance(); // built here, never on the audio thread
        outRate_ = info.sampleRate;
        auto& cache = file_.engine->getAudioFileManager().cache;
        for (auto& v : voices_)
        {
            v = Voice{};
            v.reader = cache.createReader(file_);
        }
        const double ratio = fileRate_ > 0.0 && outRate_ > 0.0 ? fileRate_ / outRate_ : 1.0;
        const int padding = dsp::SincKernels::padding;
        const int fileCapacity =
            static_cast<int>(std::ceil(kChunk * kMaxSpeed * std::max(1.0, ratio))) + 2 * padding + 8;
        fileBuffer_.setSize(channels_, fileCapacity);
        gains_.assign(kChunk, 0.0f);
        tails_.assign(kChunk, 0.0f);
        // Stretchers only for what stretches: none at the clip's own tempo and pitch (a recording,
        // a loop placed at the song's tempo), one for each piece that does, up to the most a clip
        // plays at once.
        int stretched = 0;
        if (play_.mode == model::WarpMode::Tones)
            for (const auto& piece : play_.pieces)
                stretched += std::abs(piece.filePerBeat * play_.beatsPerSecond - 1.0) > 1e-9 ||
                                     std::abs(play_.semitones) > 1e-9
                                 ? 1
                                 : 0;
        stretchers_.clear();
        if (stretched > 0)
        {
            for (int i = 0; i < std::min(kStretchers, stretched + 1); ++i)
            {
                auto s = std::make_unique<Stretch>(kSeed + i);
                s->presetDefault(channels_, static_cast<float>(outRate_), !offline_);
                stretchers_.push_back(std::move(s));
            }
            const auto& s = *stretchers_.front();
            inLatency_ = s.inputLatency();
            outLatency_ = s.outputLatency();
            primeFrames_ = s.seekLength() + kChunk;
            // An outputSeek takes up to its latency at ×4, and a chunk takes up to four chunks.
            const int inputCapacity =
                std::max(s.seekLength(), inLatency_ + 4 * outLatency_) + 4 * kChunk + 16;
            inputBuffer_.setSize(channels_, inputCapacity);
            outputBuffer_.setSize(channels_, kChunk);
            // The source at the output's rate, for a stretcher fed at other rates.
            sourceBuffer_.setSize(channels_,
                                  static_cast<int>(std::ceil(inputCapacity * std::max(1.0, ratio))) +
                                      2 * padding + 8);
        }
        stretcherBusy_.fill(false);
        jumped_ = true;
    }

    bool isReadyToProcess() override
    {
        if (!offline_ || file_.isNull())
            return true;
        for (auto& v : voices_)
            if (v.reader == nullptr)
            {
                v.reader = file_.engine->getAudioFileManager().cache.createReader(file_);
                if (v.reader == nullptr)
                    return false;
            }
        return fileRate_ > 0.0;
    }

    void process(ProcessContext& pc) override
    {
        auto dest = pc.buffers.audio;
        dest.clear();
        const int frames = static_cast<int>(dest.getNumFrames());
        const auto beats = getEditBeatRange();
        const auto times = getEditTimeRange();
        const double b0 = beats.getStart().inBeats(), b1 = beats.getEnd().inBeats();
        const double t0 = times.getStart().inSeconds(), t1 = times.getEnd().inSeconds();
        auto& head = getPlayHeadState();
        if (!head.isContiguousWithPreviousBlock() || head.isFirstBlockOfLoop())
            jumped_ = true;
        if (frames == 0 || !(b1 > b0) || !(t1 > t0) || fileRate_ <= 0.0 || b1 <= play_.playedFrom ||
            b0 >= play_.playedTo)
        {
            retireAll();
            return;
        }
        if (jumped_)
        {
            retireAll();
            jumped_ = false;
        }
        const double perFrame = (b1 - b0) / frames;
        bps_ = (b1 - b0) / (t1 - t0);
        int first = 0, last = frames;
        // The first frame at or after each end (a frame's worth of rounding in beats is no frame).
        const auto frameAt = [&](double beat)
        { return std::clamp(static_cast<int>(std::ceil((beat - b0) / perFrame - 1e-6)), 0, frames); };
        if (b0 < play_.playedFrom)
            first = frameAt(play_.playedFrom);
        if (b1 > play_.playedTo)
            last = frameAt(play_.playedTo);
        for (int c = first; c < last; c += kChunk)
            renderChunk(dest, c, std::min(kChunk, last - c), toU(b0 + perFrame * c), perFrame);
        applyLevel(dest, frames);
    }

private:
    struct Voice
    {
        bool active = false, started = false;
        Occurrence at;
        double jumpFadeFrom = -std::numeric_limits<double>::infinity();
        bool stretching = false;
        int stretcher = -1;
        bool immediate = false; // its stretcher starts when it's heard, not primed ahead
        std::int64_t fed = 0;   // the stretcher's input so far, in source frames at the output's rate
        te::AudioFileCache::Reader::Ptr reader;
    };

    te::AudioFile file_;
    Play play_;
    int channels_ = 2;
    te::ChannelConfiguration sourceChannels_;
    te::LiveClipLevel level_;
    std::array<float, 2> lastGains_{-1.0f, -1.0f}; // the level's gains the last block ended at
    te::EditItemID id_;
    bool offline_ = false;
    double fileRate_ = 0.0, outRate_ = 44100.0, bps_ = 2.0;
    std::int64_t fileFrames_ = 0;
    std::array<Voice, kVoices> voices_;
    std::vector<std::unique_ptr<Stretch>> stretchers_;
    std::array<bool, kStretchers> stretcherBusy_{};
    int inLatency_ = 0, outLatency_ = 0, primeFrames_ = 0;
    juce::AudioBuffer<float> fileBuffer_, inputBuffer_, outputBuffer_, sourceBuffer_;
    std::vector<float> gains_, tails_;
    bool jumped_ = true;

    /// The clip's level, pan and mute, as the engine's own player applies them (a take auditioned
    /// mutes the clip it stands in for): ramped across a block where they change.
    void applyLevel(choc::buffer::ChannelArrayView<float>& dest, int frames)
    {
        std::array<float, 2> gains{};
        if (channels_ == 2)
            level_.getLeftAndRightGains(gains[0], gains[1]);
        else
            gains[0] = gains[1] = level_.getGainIncludingMute();
        if (getPlayHead().isUserDragging())
            for (auto& g : gains)
                g *= 0.4f;
        for (int c = 0; c < channels_; ++c)
        {
            const auto side = static_cast<size_t>(std::min(c, 1));
            const float from = lastGains_[side] < 0.0f ? gains[side] : lastGains_[side], to = gains[side];
            auto* out = dest.getIterator(static_cast<choc::buffer::ChannelCount>(c)).sample;
            if (from == to)
            {
                if (to != 1.0f)
                    for (int i = 0; i < frames; ++i)
                        out[i] *= to;
                continue;
            }
            const float step = (to - from) / static_cast<float>(std::max(1, frames));
            for (int i = 0; i < frames; ++i)
                out[i] *= from + step * static_cast<float>(i + 1);
        }
        lastGains_ = gains;
    }

    // ---- where things are ---------------------------------------------------------------------

    bool loops() const noexcept { return play_.loopLength > 0.0; }

    /// The clip's unwrapped content at an edit beat.
    double toU(double editBeat) const noexcept { return play_.offsetBeats + (editBeat - play_.startBeat); }

    int pieceAt(double beat) const noexcept
    {
        const auto& p = play_.pieces;
        auto it =
            std::upper_bound(p.begin(), p.end(), beat, [](double b, const Piece& x) { return b < x.beat; });
        return std::max(0, static_cast<int>(it - p.begin()) - 1);
    }

    /// Seconds of the file a voice reads at u (Tones and Repitch: the piece's map; Beats: from the
    /// piece's moment at its speed).
    double fileSecondsAt(const Voice& v, double u) const noexcept
    {
        const auto& piece = play_.pieces[static_cast<size_t>(v.at.piece)];
        const double beatsIn = (u - v.at.shift) - piece.beat;
        if (play_.mode == model::WarpMode::Beats)
            return piece.file + beatsIn / bps_ * beatsSpeed();
        return piece.file + beatsIn * piece.filePerBeat;
    }

    double beatsSpeed() const noexcept
    {
        return std::clamp(std::pow(2.0, play_.semitones / 12.0), 1.0 / kMaxSpeed, kMaxSpeed);
    }

    /// The occurrences heard in [ua, ub) of the content.
    template <typename F> void forEachOccurrence(double ua, double ub, F f) const
    {
        const auto& p = play_.pieces;
        const int count = static_cast<int>(p.size());
        const double inf = std::numeric_limits<double>::infinity();
        if (count == 0)
            return;
        if (!loops())
        {
            for (int k = pieceAt(ua), end = pieceAt(ub); k <= end; ++k)
                f(Occurrence{0, k, k == 0 ? -inf : p[static_cast<size_t>(k)].beat,
                             k + 1 < count ? p[static_cast<size_t>(k + 1)].beat : inf, 0.0, true, true});
            return;
        }
        const double ls = play_.loopStart, len = play_.loopLength, le = ls + len;
        const int kFirst = pieceAt(ls), kLast = pieceAt(std::nextafter(le, -inf));
        for (auto lap = static_cast<long>(std::floor(ua / len)),
                  lapEnd = static_cast<long>(std::floor(ub / len));
             lap <= lapEnd; ++lap)
        {
            const double shift = static_cast<double>(lap) * len - ls;
            const double ca = std::clamp(ua - shift, ls, le),
                         cb = std::clamp(ub - shift, ls, std::nextafter(le, -inf));
            for (int k = std::max(kFirst, pieceAt(ca)), end = std::min(kLast, pieceAt(cb)); k <= end; ++k)
            {
                const double from = k == kFirst ? ls : p[static_cast<size_t>(k)].beat;
                const double to = k == kLast ? le : p[static_cast<size_t>(k + 1)].beat;
                f(Occurrence{lap, k, from + shift, to + shift, shift, k != kFirst, k != kLast});
            }
        }
    }

    // ---- voices ---------------------------------------------------------------------------------

    void retire(Voice& v)
    {
        if (v.stretcher >= 0)
            stretcherBusy_[static_cast<size_t>(v.stretcher)] = false;
        auto reader = std::move(v.reader);
        v = Voice{};
        v.reader = std::move(reader);
    }

    void retireAll()
    {
        for (auto& v : voices_)
            if (v.active)
                retire(v);
    }

    /// Whether a piece is stretched rather than read: Tones, away from its own speed or pitch.
    bool stretches(const Piece& piece) const noexcept
    {
        if (stretchers_.empty())
            return false;
        return std::abs(piece.filePerBeat * bps_ - 1.0) > 1e-9 || std::abs(play_.semitones) > 1e-9;
    }

    void renderChunk(choc::buffer::ChannelArrayView<float>& dest, int at, int frames, double ua, double du)
    {
        const double ub = ua + du * frames;
        const double fade = model::kPieceFadeSeconds * bps_;
        const double prime = play_.mode == model::WarpMode::Tones ? primeFrames_ * du : 0.0;
        const double uFrom = toU(play_.playedFrom);
        // Voices whose piece is over go; new ones start for what's heard soon.
        for (auto& v : voices_)
            if (v.active && v.at.to <= ua)
                retire(v);
        forEachOccurrence(ua, ub + fade + prime,
                          [&](const Occurrence& o)
                          {
                              if (o.to <= ua)
                                  return;
                              for (auto& v : voices_)
                                  if (v.active && v.at.lap == o.lap && v.at.piece == o.piece)
                                      return;
                              const bool soon = o.from - (o.fadesIn ? fade : 0.0) <= ub;
                              const auto& piece = play_.pieces[static_cast<size_t>(o.piece)];
                              const bool stretched = stretches(piece);
                              if (!soon && !stretched)
                                  return;
                              for (auto& v : voices_)
                                  if (!v.active)
                                  {
                                      start(v, o, stretched, ua, du, fade, uFrom);
                                      return;
                                  }
                          });
        for (auto& v : voices_)
            if (v.active)
                render(v, dest, at, frames, ua, du, fade);
    }

    void start(Voice& v, const Occurrence& o, bool stretched, double ua, double du, double fade, double uFrom)
    {
        v.active = true;
        v.started = false;
        v.at = o;
        v.stretching = stretched;
        v.stretcher = -1;
        // Begun where it's already heard (a jump, a new graph, no voice free in time): it fades in,
        // unless it's where the clip starts (its edge fades see to that).
        const bool heard = ua > o.from - (o.fadesIn ? fade : 0.0);
        v.jumpFadeFrom = heard && ua > uFrom + 1.5 * du ? ua : -std::numeric_limits<double>::infinity();
        if (!stretched)
            return;
        for (int i = 0; i < static_cast<int>(stretchers_.size()); ++i)
            if (!stretcherBusy_[static_cast<size_t>(i)])
            {
                stretcherBusy_[static_cast<size_t>(i)] = true;
                v.stretcher = i;
                break;
            }
        if (v.stretcher < 0)
        {
            v.stretching = false; // none free: read, unstretched, rather than not at all
            return;
        }
        auto& s = *stretchers_[static_cast<size_t>(v.stretcher)];
        s.reset();
        s.setTransposeSemitones(static_cast<float>(play_.semitones));
        s.setFormantFactor(1.0f, play_.formants);
        // Primed ahead while there's time to, so its first heard frame is warmed up; otherwise it
        // starts exactly where it's heard, at the cost of a burst of work.
        v.immediate = (o.from - (o.fadesIn ? fade : 0.0) - ua) < primeFrames_ * du;
    }

    /// The source in frames at the output's rate (the stretcher's input): frames [from, from + n)
    /// into inputBuffer_ at `offset`.
    void sourceFrames(Voice& v, std::int64_t from, int n, int offset)
    {
        const double ratio = fileRate_ / outRate_;
        if (std::abs(ratio - 1.0) < 1e-12)
        {
            readFile(v, from, n, inputBuffer_, offset);
            return;
        }
        const int padding = dsp::SincKernels::padding;
        const double x0 = static_cast<double>(from) * ratio;
        const auto w0 = static_cast<std::int64_t>(std::floor(x0)) - padding;
        const int wn = static_cast<int>(std::ceil(n * ratio)) + 2 * padding + 2;
        readFile(v, w0, wn, sourceBuffer_, 0);
        const auto& table = dsp::SincKernels::instance().table(std::max(1.0, ratio));
        for (int c = 0; c < channels_; ++c)
        {
            const float* src = sourceBuffer_.getReadPointer(c) + padding;
            float* out = inputBuffer_.getWritePointer(c) + offset;
            for (int i = 0; i < n; ++i)
                out[i] =
                    dsp::SincKernels::read(table, src, x0 + i * ratio - static_cast<double>(w0 + padding));
        }
    }

    /// File frames [from, from + n) into `buffer` at `offset`: silence outside the file.
    void readFile(Voice& v, std::int64_t from, int n, juce::AudioBuffer<float>& buffer, int offset)
    {
        n = std::min(n, buffer.getNumSamples() - offset);
        if (n <= 0)
            return;
        buffer.clear(offset, n);
        const auto a = std::max<std::int64_t>(from, 0), b = std::min<std::int64_t>(from + n, fileFrames_);
        if (b <= a || v.reader == nullptr)
            return;
        v.reader->setReadPosition(a);
        const auto dests = te::ChannelConfiguration::discreteChannels(channels_);
        if (!v.reader->readSamples(static_cast<int>(b - a), buffer, dests,
                                   offset + static_cast<int>(a - from), sourceChannels_, offline_ ? 5000 : 3))
            buffer.clear(offset, n);
    }

    /// Adds n frames read at file frames x0 + step·i, each times gains_[g0 + i], into dest from
    /// `at`, in runs that fit the buffer.
    void addRead(Voice& v, choc::buffer::ChannelArrayView<float>& dest, int at, int n, double x0, double step,
                 int g0 = 0)
    {
        const int padding = dsp::SincKernels::padding;
        const int span = fileBuffer_.getNumSamples() - 2 * padding - 4;
        const int most = std::max(1, static_cast<int>(span / std::max(1.0, std::abs(step))));
        for (int i = 0; i < n; i += most)
            addRun(v, dest, at + i, std::min(most, n - i), x0 + step * i, step, g0 + i);
    }

    void addRun(Voice& v, choc::buffer::ChannelArrayView<float>& dest, int at, int n, double x0, double step,
                int g0)
    {
        const int padding = dsp::SincKernels::padding;
        const float* gains = gains_.data() + g0;
        const auto add = [&](int c, int i, float value)
        {
            dest.getSample(static_cast<choc::buffer::ChannelCount>(c),
                           static_cast<choc::buffer::FrameCount>(at + i)) += value;
        };
        // Reading the file as it is, frame for frame: exact.
        if (std::abs(step - 1.0) < 1e-9 && std::abs(x0 - std::round(x0)) < 1e-6)
        {
            readFile(v, static_cast<std::int64_t>(std::llround(x0)), n, fileBuffer_, 0);
            for (int c = 0; c < channels_; ++c)
            {
                const float* src = fileBuffer_.getReadPointer(c);
                for (int i = 0; i < n; ++i)
                    add(c, i, src[i] * gains[i]);
            }
            return;
        }
        const double xn = x0 + step * (n - 1);
        const auto w0 = static_cast<std::int64_t>(std::floor(std::min(x0, xn))) - padding;
        const int wn =
            static_cast<int>(std::floor(std::max(x0, xn)) - std::floor(std::min(x0, xn))) + 2 * padding + 2;
        readFile(v, w0, wn, fileBuffer_, 0);
        const auto& table = dsp::SincKernels::instance().table(std::max(1.0, std::abs(step)));
        for (int c = 0; c < channels_; ++c)
        {
            const float* src = fileBuffer_.getReadPointer(c) + padding;
            for (int i = 0; i < n; ++i)
                if (gains[i] != 0.0f)
                    add(c, i,
                        gains[i] * dsp::SincKernels::read(table, src,
                                                          x0 + step * i - static_cast<double>(w0 + padding)));
        }
    }

    void render(Voice& v, choc::buffer::ChannelArrayView<float>& dest, int at, int n, double ua, double du,
                double fade)
    {
        bool any = false;
        for (int i = 0; i < n; ++i)
        {
            const double u = ua + du * i;
            const double in = v.at.fadesIn ? std::clamp((u - (v.at.from - fade)) / fade, 0.0, 1.0)
                                           : (u >= v.at.from ? 1.0 : 0.0);
            const double out =
                v.at.fadesOut ? std::clamp((v.at.to - u) / fade, 0.0, 1.0) : (u < v.at.to ? 1.0 : 0.0);
            float g = static_cast<float>(in * out * std::clamp((u - v.jumpFadeFrom) / fade, 0.0, 1.0));
            gains_[static_cast<size_t>(i)] = g;
            any = any || g > 0.0f;
        }
        if (v.stretching)
        {
            renderStretched(v, dest, at, n, ua, du, any);
            return;
        }
        if (!any)
            return;
        v.started = true;
        const double x0 = fileSecondsAt(v, ua) * fileRate_;
        if (play_.mode != model::WarpMode::Beats)
        {
            const auto& piece = play_.pieces[static_cast<size_t>(v.at.piece)];
            addRead(v, dest, at, n, x0, piece.filePerBeat * du * fileRate_);
            return;
        }
        renderBeats(v, dest, at, n, x0);
    }

    /// Beats: the piece from its moment at its speed; past its end, its tail faded or looped.
    void renderBeats(Voice& v, choc::buffer::ChannelArrayView<float>& dest, int at, int n, double x0)
    {
        const auto& piece = play_.pieces[static_cast<size_t>(v.at.piece)];
        const double step = beatsSpeed() * fileRate_ / outRate_;
        const double end = piece.fileEnd * fileRate_;
        const double fadeFrames = model::kPieceFadeSeconds * beatsSpeed() * fileRate_;
        if (play_.tails == model::WarpTails::Fade || !(end > piece.file * fileRate_ + 4.0 * fadeFrames))
        {
            for (int i = 0; i < n; ++i)
                gains_[static_cast<size_t>(i)] *=
                    static_cast<float>(std::clamp((end - (x0 + step * i)) / fadeFrames, 0.0, 1.0));
            addRead(v, dest, at, n, x0, step);
            return;
        }
        // Loop: the piece's second half again and again, crossfaded at each turn.
        const double loopFrom = piece.file * fileRate_ + 0.5 * (end - piece.file * fileRate_);
        const double period = end - loopFrom;
        std::copy(gains_.begin(), gains_.begin() + n, tails_.begin());
        int i = 0;
        while (i < n)
        {
            double x = x0 + step * i;
            if (x >= end)
                x = loopFrom + std::fmod(x - loopFrom, period);
            // A run up to the next turn.
            const int run = std::min(n - i, std::max(1, static_cast<int>(std::ceil((end - x) / step))));
            const double fadeStart = end - fadeFrames;
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int j = 0; j < run; ++j)
                {
                    const double xj = x + step * j;
                    const double a = std::clamp((xj - fadeStart) / fadeFrames, 0.0, 1.0);
                    gains_[static_cast<size_t>(j)] =
                        tails_[static_cast<size_t>(i + j)] * static_cast<float>(pass == 0 ? 1.0 - a : a);
                }
                if (pass == 1 && x + step * run <= fadeStart)
                    break;
                addRead(v, dest, at + i, run, pass == 0 ? x : x - period, step);
            }
            i += run;
        }
    }

    /// Tones, stretched: the voice's stretcher fed its source up to where its output is heard.
    void renderStretched(Voice& v, choc::buffer::ChannelArrayView<float>& dest, int at, int n, double ua,
                         double du, bool heard)
    {
        auto& s = *stretchers_[static_cast<size_t>(v.stretcher)];
        const auto& piece = play_.pieces[static_cast<size_t>(v.at.piece)];
        const double rate = piece.filePerBeat * du * outRate_; // source frames an output frame
        const auto sigma = [&](double u) { return fileSecondsAt(v, u) * outRate_; };
        const double lead = inLatency_ + rate * outLatency_;
        if (!v.started)
        {
            if (v.immediate && !heard)
                return; // it starts when it's heard
            v.started = true;
            if (v.immediate)
            {
                const auto from = static_cast<std::int64_t>(std::llround(sigma(ua)));
                const int length =
                    std::min(static_cast<int>(std::llround(lead)), inputBuffer_.getNumSamples());
                sourceFrames(v, from, length, 0);
                s.outputSeek(inputBuffer_.getArrayOfReadPointers(), length);
                v.fed = from + length;
            }
            else
            {
                const auto to = static_cast<std::int64_t>(std::llround(sigma(ua) + lead));
                const int length = s.seekLength();
                sourceFrames(v, to - length, length, 0);
                s.seek(inputBuffer_.getArrayOfReadPointers(), length, rate);
                v.fed = to;
            }
        }
        const auto target = static_cast<std::int64_t>(std::llround(sigma(ua + du * n) + lead));
        const int count =
            static_cast<int>(std::clamp<std::int64_t>(target - v.fed, 0, inputBuffer_.getNumSamples()));
        sourceFrames(v, v.fed, count, 0);
        v.fed += count;
        s.process(inputBuffer_.getArrayOfReadPointers(), count, outputBuffer_.getArrayOfWritePointers(), n);
        if (!heard)
            return;
        for (int c = 0; c < channels_; ++c)
        {
            const float* out = outputBuffer_.getReadPointer(c);
            for (int i = 0; i < n; ++i)
                dest.getSample(static_cast<choc::buffer::ChannelCount>(c),
                               static_cast<choc::buffer::FrameCount>(at + i)) +=
                    out[i] * gains_[static_cast<size_t>(i)];
        }
    }
};
} // namespace

model::AudioWarp warpOf(const te::AudioClipBase& clip)
{
    double fallback = 120.0;
    if (!clip.state.hasProperty(warpIds::tempo))
    {
        auto& c = const_cast<te::AudioClipBase&>(clip);
        if (const double bpm = c.getLoopInfo().getBpm(c.getWaveInfo()); bpm > 0.0)
            fallback = bpm;
    }
    return warpOf(clip.state, fallback);
}

model::AudioWarp warpOf(const juce::ValueTree& s, double fallbackTempo)
{
    model::AudioWarp w;
    w.on = s.getProperty(te::IDs::autoTempo, false);
    const double tempo = s.getProperty(warpIds::tempo, fallbackTempo);
    w.tempo = tempo > 0.0 ? std::clamp(tempo, model::kMinClipTempo, model::kMaxClipTempo) : 120.0;
    w.mode =
        static_cast<model::WarpMode>(std::clamp(static_cast<int>(s.getProperty(warpIds::mode, 1)), 0, 2));
    w.tails =
        static_cast<model::WarpTails>(std::clamp(static_cast<int>(s.getProperty(warpIds::tails, 0)), 0, 1));
    w.formants = s.getProperty(warpIds::formants, false);
    w.sensitivity = std::clamp(static_cast<double>(s.getProperty(warpIds::sensitivity, 0.5)), 0.0, 1.0);
    const auto markers = s.getChildWithName(warpIds::markers);
    for (const auto& m : markers)
        if (m.hasType(warpIds::marker))
            w.markers.push_back(
                {static_cast<double>(m[warpIds::file]), static_cast<double>(m[warpIds::beat])});
    std::sort(w.markers.begin(), w.markers.end(),
              [](const model::WarpMarker& a, const model::WarpMarker& b) { return a.file < b.file; });
    return w;
}

model::AudioPitch pitchOf(const juce::ValueTree& clip)
{
    return {
        std::clamp(static_cast<int>(clip.getProperty(warpIds::transpose, 0)), -model::kMaxTranspose,
                   model::kMaxTranspose),
        std::clamp(static_cast<int>(clip.getProperty(warpIds::fine, 0)), -model::kMaxFine, model::kMaxFine)};
}

std::optional<std::optional<music::Key>> storedKeyOf(const juce::ValueTree& clip)
{
    if (!clip.hasProperty(keyIds::key))
        return std::nullopt;
    const auto text = clip[keyIds::key].toString();
    const int space = text.indexOfChar(' ');
    if (space <= 0)
        return std::optional<music::Key>{};
    music::Key key{text.substring(0, space).getIntValue(), text.substring(space + 1).toStdString()};
    key.root = ((key.root % 12) + 12) % 12;
    if (music::scaleOf(key) == nullptr)
        return std::optional<music::Key>{};
    return std::optional(key);
}

void writeStoredKey(juce::ValueTree& clip, const std::optional<std::optional<music::Key>>& key, bool found,
                    juce::UndoManager* um)
{
    if (!key)
    {
        clip.removeProperty(keyIds::key, um);
        clip.removeProperty(keyIds::found, um);
        return;
    }
    clip.setProperty(keyIds::key,
                     *key ? juce::String((*key)->root) + " " + juce::String((*key)->scale) : juce::String(),
                     um);
    if (found)
        clip.setProperty(keyIds::found, true, um);
    else
        clip.removeProperty(keyIds::found, um);
}

bool followsKey(const juce::ValueTree& clip)
{
    return clip.getProperty(keyIds::follow, false);
}

int followStepsOf(const te::AudioClipBase& clip)
{
    if (!followsKey(clip.state))
        return 0;
    auto& c = const_cast<te::AudioClipBase&>(clip);
    if (c.getAutoTempo() && warpOf(c).mode == model::WarpMode::Repitch)
        return 0;
    const auto stored = storedKeyOf(clip.state);
    const auto song = songKeyOf(clip.edit);
    if (!stored || !*stored || !song)
        return 0;
    return music::stepsToKey(**stored, *song);
}

double playedSemitones(const te::AudioClipBase& clip)
{
    return pitchOf(clip.state).semitones() + followStepsOf(clip);
}

void syncPitch(te::AudioClipBase& clip)
{
    const auto semitones = static_cast<float>(playedSemitones(clip));
    if (clip.getPitchChange() == semitones)
        return;
    // Without Warp the engine's own reader transposes it, in real time.
    clip.setUsesProxy(false);
    clip.setPitchChange(semitones);
}

model::ContentMap contentMapOf(const te::AudioClipBase& clip)
{
    model::ContentMap m;
    auto& c = const_cast<te::AudioClipBase&>(clip);
    m.warped = c.getAutoTempo();
    m.reversed = c.state.hasProperty(soundIds::forward);
    m.fileLength = c.getSourceLength().inSeconds();
    m.beatsPerSecond = c.edit.tempoSequence.getBeatsPerSecondAt(c.getPosition().getStart());
    if (m.warped)
        m.played = m.reversed ? model::reversedWarp(warpOf(c), m.fileLength) : warpOf(c);
    return m;
}

void writeWarpLoopInfo(te::AudioClipBase& c, const model::AudioWarp& warp)
{
    const double length = c.getSourceLength().inSeconds();
    if (!(length > 0.0))
        return;
    auto info = c.getLoopInfo();
    const double beats = model::beatAtFile(warp, length);
    info.setNumBeats(beats);
    info.setBpm(beats * 60.0 / length, c.getWaveInfo());
    if (info.getNumerator() <= 0)
    {
        info.setNumerator(4);
        info.setDenominator(4);
    }
    c.setLoopInfo(info);
}

void writeWarpMarkers(juce::ValueTree& clip, const std::vector<model::WarpMarker>& markers,
                      juce::UndoManager* um)
{
    auto existing = clip.getChildWithName(warpIds::markers);
    if (existing.isValid())
        clip.removeChild(existing, um);
    if (markers.empty())
        return;
    juce::ValueTree tree(warpIds::markers);
    for (const auto& m : markers)
        tree.appendChild(juce::ValueTree(warpIds::marker, {{warpIds::file, m.file}, {warpIds::beat, m.beat}}),
                         nullptr);
    clip.appendChild(tree, um);
}

bool affectsWarp(const juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree.hasType(warpIds::markers) || tree.hasType(warpIds::marker))
        return true;
    return id == warpIds::tempo || id == warpIds::mode || id == warpIds::tails || id == warpIds::formants ||
           id == warpIds::sensitivity || id == warpIds::transpose || id == warpIds::fine ||
           id == warpIds::transientsAdded || id == warpIds::transientsRemoved || id == keyIds::key ||
           id == keyIds::follow;
}

std::vector<double> timesOf(const juce::var& text)
{
    std::vector<double> out;
    for (const auto& token : juce::StringArray::fromTokens(text.toString(), " ", ""))
        if (token.isNotEmpty())
            out.push_back(token.getDoubleValue());
    std::sort(out.begin(), out.end());
    return out;
}

juce::String textOf(const std::vector<double>& times)
{
    juce::StringArray tokens;
    for (double t : times)
        tokens.add(juce::String(t, 6));
    return tokens.joinIntoString(" ");
}

std::unique_ptr<te::graph::Node> makeWarpPlayer(te::AudioClipBase& clip, te::TimeRange played,
                                                te::ProcessState& state, bool offline,
                                                const std::vector<double>& transients)
{
    if (!clip.getAutoTempo())
        return {};
    const auto file = clip.getPlaybackFile();
    if (!file.isValid())
        return {};
    const double length = file.getLength();
    auto warp = warpOf(clip);
    auto found = transients;
    // A reversed clip plays a reversed copy of its file: its map and transients seen from the end.
    if (clip.state.hasProperty(soundIds::forward))
    {
        warp = model::reversedWarp(warp, length);
        for (auto& t : found)
            t = length - t;
        std::reverse(found.begin(), found.end());
    }
    Play play;
    const auto starts = model::pieceStarts(warp, found, length);
    // Beats plays each piece at its speed: where that's the map's own (at its own tempo, not
    // transposed), one piece runs on into the next exactly, and needs no cut.
    const double bps = clip.edit.tempoSequence.getBeatsPerSecondAt(clip.getPosition().getStart());
    const double beatsSpeed =
        std::pow(2.0, (warp.mode == model::WarpMode::Repitch ? 0.0 : playedSemitones(clip)) / 12.0);
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Piece p;
        p.beat = starts[i].beat;
        p.file = starts[i].file;
        if (i + 1 < starts.size())
        {
            p.fileEnd = starts[i + 1].file;
            p.filePerBeat = (starts[i + 1].file - starts[i].file) / (starts[i + 1].beat - starts[i].beat);
        }
        else
        {
            p.fileEnd = length;
            p.filePerBeat = 60.0 / warp.tempo;
        }
        // A marker that doesn't change the speed starts no piece: the file reads on through it,
        // exactly. In Beats, a piece whose own speed is the one before's reaches it exactly too.
        if (!play.pieces.empty())
        {
            auto& before = play.pieces.back();
            const bool joins = warp.mode == model::WarpMode::Beats
                                   ? std::abs(before.filePerBeat * bps - beatsSpeed) <= 1e-9 * beatsSpeed
                                   : std::abs(before.filePerBeat - p.filePerBeat) <= 1e-12 * p.filePerBeat;
            if (joins)
            {
                before.fileEnd = p.fileEnd;
                if (warp.mode != model::WarpMode::Beats)
                    continue;
                before.filePerBeat = p.filePerBeat; // its run on is at this one's speed
                continue;
            }
        }
        play.pieces.push_back(p);
    }
    // Beats plays each piece from its moment at its own speed; past the last marker, as Tones
    // pieces do, at the clip's tempo. Repitch has no transposition: the speed sets the pitch.
    play.mode = warp.mode;
    play.tails = warp.tails;
    play.formants = warp.formants;
    play.semitones = warp.mode == model::WarpMode::Repitch ? 0.0 : playedSemitones(clip);
    play.startBeat = clip.getStartBeat().inBeats();
    play.offsetBeats = clip.getOffsetInBeats().inBeats();
    if (clip.isLooping())
    {
        play.loopStart = clip.getLoopStartBeats().inBeats();
        play.loopLength = clip.getLoopLengthBeats().inBeats();
    }
    const auto& tempo = clip.edit.tempoSequence;
    play.beatsPerSecond = bps;
    play.playedFrom = tempo.toBeats(played.getStart()).inBeats();
    play.playedTo = tempo.toBeats(played.getEnd()).inBeats();
    const auto channels = clip.getActiveChannelConfiguration();
    return std::make_unique<WarpPlayerNode>(file, std::move(play), std::max(1, channels.getNumChannels()),
                                            channels, clip.getLiveClipLevel(), state, clip.itemID, offline);
}
} // namespace fd::engine
