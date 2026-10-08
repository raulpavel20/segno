#include "engine/SessionImpl.h"

namespace fd::engine
{
namespace
{
const juce::Identifier kColourId{"fdColour"}; // index into the clip palette

/// Number of colours in ui::Theme::clipColours; snapshots carry indices modulo this.
constexpr int kPaletteSize = 16;

model::TrackInfo::Kind kindOf(te::AudioTrack& t)
{
    for (auto* p : t.pluginList)
        if (p->isSynth())
            return model::TrackInfo::Kind::Instrument;
    return model::TrackInfo::Kind::Audio;
}
} // namespace

// ---- Impl ---------------------------------------------------------------------------------

Session::Impl::Impl(Session& o, te::Engine& e) : owner(o), engine(e)
{
    edit = te::Edit::createSingleTrackEdit(engine);
    edit->ensureNumberOfAudioTracks(4);
    int i = 0;
    for (auto* t : te::getAudioTracks(*edit))
    {
        t->setName(juce::String(i + 1) + " Audio");
        t->state.setProperty(kColourId, i * 3 % kPaletteSize, nullptr);
        ++i;
    }
    edit->getUndoManager().clearUndoHistory();
    rebuildSnapshot();
    edit->state.addListener(this);
}

Session::Impl::~Impl()
{
    cancelPendingUpdate();
    edit->state.removeListener(this);
    edit->getTransport().stop(false, true);
}

void Session::Impl::changed(juce::ValueTree& tree)
{
    // Transport state (position, play state) changes constantly and isn't part of a snapshot.
    for (auto t = tree; t.isValid(); t = t.getParent())
        if (t.hasType(te::IDs::TRANSPORT))
            return;
    triggerAsyncUpdate();
}

void Session::Impl::rebuildSnapshot()
{
    auto s = std::make_shared<model::Snapshot>();
    s->version = ++version;
    if (auto* tempo = edit->tempoSequence.getTempo(0))
        s->tempo.bpm = tempo->getBpm();
    if (auto* sig = edit->tempoSequence.getTimeSig(0))
    {
        s->tempo.numerator = sig->numerator;
        s->tempo.denominator = sig->denominator;
    }

    int index = 0;
    for (auto* t : te::getAudioTracks(*edit))
    {
        model::TrackInfo info;
        info.id = t->itemID.getRawID();
        info.kind = kindOf(*t);
        info.name = t->getName().toStdString();
        info.colour = static_cast<int>(t->state.getProperty(kColourId, index * 3 % kPaletteSize));
        for (auto* c : t->getClips())
        {
            model::ClipInfo clip;
            clip.id = c->itemID.getRawID();
            clip.kind = dynamic_cast<te::MidiClip*>(c) != nullptr ? model::ClipInfo::Kind::Midi : model::ClipInfo::Kind::Audio;
            clip.name = c->getName().toStdString();
            const auto pos = c->getPosition();
            clip.start = pos.getStart().inSeconds();
            clip.length = pos.getLength().inSeconds();
            clip.offset = pos.getOffset().inSeconds();
            clip.colour = static_cast<int>(c->state.getProperty(kColourId, info.colour));
            if (auto* audio = dynamic_cast<te::AudioClipBase*>(c))
            {
                clip.sourcePath = audio->getOriginalFile().getFullPathName().toStdString();
                clip.sourceLength = audio->getSourceLength().inSeconds();
            }
            info.clips.push_back(std::move(clip));
        }
        std::sort(info.clips.begin(), info.clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        s->tracks.push_back(std::move(info));
        ++index;
    }
    snapshot = std::move(s);
    if (owner.onSnapshotChanged)
        owner.onSnapshotChanged();
}

te::AudioTrack* Session::Impl::track(std::uint64_t id) const
{
    for (auto* t : te::getAudioTracks(*edit))
        if (t->itemID.getRawID() == id)
            return t;
    return nullptr;
}

te::Clip* Session::Impl::clip(std::uint64_t id) const
{
    return te::findClipForID(*edit, te::EditItemID::fromRawID(id));
}

// ---- Session ------------------------------------------------------------------------------

Session::Session(te::Engine& engine) : impl_(std::make_unique<Impl>(*this, engine)) {}
Session::~Session() = default;

std::shared_ptr<const model::Snapshot> Session::snapshot() const
{
    return impl_->snapshot;
}

void Session::updateSnapshotNow()
{
    impl_->rebuildSnapshot();
}

void Session::play()
{
    impl_->edit->getTransport().play(false);
}

void Session::stop()
{
    impl_->edit->getTransport().stop(false, false);
}

bool Session::isPlaying() const
{
    return impl_->edit->getTransport().isPlaying();
}

double Session::position() const
{
    return impl_->edit->getTransport().getPosition().inSeconds();
}

void Session::setPosition(double seconds)
{
    impl_->edit->getTransport().setPosition(te::TimePosition::fromSeconds(std::max(0.0, seconds)));
}

bool Session::isLooping() const
{
    return impl_->edit->getTransport().looping;
}

void Session::setLooping(bool on)
{
    impl_->edit->getTransport().looping = on;
}

std::pair<double, double> Session::loopRange() const
{
    const auto r = impl_->edit->getTransport().getLoopRange();
    return {r.getStart().inSeconds(), r.getEnd().inSeconds()};
}

void Session::setLoopRange(double start, double end)
{
    if (end < start)
        std::swap(start, end);
    impl_->edit->getTransport().setLoopRange(
        {te::TimePosition::fromSeconds(std::max(0.0, start)), te::TimePosition::fromSeconds(std::max(0.0, end))});
}

bool Session::isMetronomeOn() const
{
    return impl_->edit->clickTrackEnabled;
}

void Session::setMetronome(bool on)
{
    impl_->edit->clickTrackEnabled = on;
}

void Session::setTempo(double bpm)
{
    if (auto* tempo = impl_->edit->tempoSequence.getTempo(0))
    {
        impl_->beginEdit();
        tempo->setBpm(std::clamp(bpm, 20.0, 999.0));
    }
}

void Session::setTimeSignature(int numerator, int denominator)
{
    if (auto* sig = impl_->edit->tempoSequence.getTimeSig(0))
    {
        impl_->beginEdit();
        sig->setStringTimeSig(juce::String(std::clamp(numerator, 1, 32)) + "/" + juce::String(denominator));
    }
}

std::uint64_t Session::addAudioTrack()
{
    impl_->beginEdit();
    auto& edit = *impl_->edit;
    const int count = te::getAudioTracks(edit).size();
    auto track = edit.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(edit), nullptr);
    if (track == nullptr)
        return 0;
    track->setName(juce::String(count + 1) + " Audio");
    track->state.setProperty(kColourId, count * 3 % kPaletteSize, &edit.getUndoManager());
    return track->itemID.getRawID();
}

std::uint64_t Session::insertAudioFile(std::uint64_t trackId, const juce::File& file, double seconds)
{
    auto* track = impl_->track(trackId);
    const te::AudioFile audio(impl_->engine, file);
    if (track == nullptr || !audio.isValid())
        return 0;
    impl_->beginEdit();
    const auto start = te::TimePosition::fromSeconds(std::max(0.0, seconds));
    auto clip = track->insertWaveClip(file.getFileNameWithoutExtension(), file,
                                      {{start, te::TimeDuration::fromSeconds(audio.getLength())}, {}}, false);
    return clip != nullptr ? clip->itemID.getRawID() : 0;
}

void Session::applyClipEdits(const std::vector<model::ClipEdit>& edits)
{
    if (edits.empty())
        return;
    impl_->beginEdit();
    for (const auto& e : edits)
    {
        auto* c = impl_->clip(e.clip);
        auto* target = impl_->track(e.track);
        if (c == nullptr || target == nullptr)
            continue;
        if (c->getClipTrack() != target)
            c->moveTo(*target);
        c->setPosition({{te::TimePosition::fromSeconds(e.start), te::TimePosition::fromSeconds(e.start + e.length)},
                        te::TimeDuration::fromSeconds(e.offset)});
    }
}

std::vector<std::uint64_t> Session::duplicateClips(const std::vector<model::ClipEdit>& placements)
{
    std::vector<std::uint64_t> created;
    if (placements.empty())
        return created;
    impl_->beginEdit();
    for (const auto& p : placements)
    {
        auto* source = impl_->clip(p.clip);
        auto* target = impl_->track(p.track);
        if (source == nullptr || target == nullptr)
            continue;
        if (auto copy = te::duplicateClip(*source))
        {
            if (copy->getClipTrack() != target)
                copy->moveTo(*target);
            copy->setPosition({{te::TimePosition::fromSeconds(p.start), te::TimePosition::fromSeconds(p.start + p.length)},
                               te::TimeDuration::fromSeconds(p.offset)});
            created.push_back(copy->itemID.getRawID());
        }
    }
    return created;
}

void Session::deleteClips(const std::vector<std::uint64_t>& ids)
{
    if (ids.empty())
        return;
    impl_->beginEdit();
    for (auto id : ids)
        if (auto* c = impl_->clip(id))
            c->removeFromParent();
}

std::vector<std::uint64_t> Session::splitClips(const std::vector<std::uint64_t>& ids, double seconds)
{
    std::vector<std::uint64_t> created;
    const auto at = te::TimePosition::fromSeconds(seconds);
    bool begun = false;
    for (auto id : ids)
    {
        auto* c = impl_->clip(id);
        if (c == nullptr || !c->getEditTimeRange().containsInclusive(at) || c->getPosition().getStart() == at ||
            c->getPosition().getEnd() == at)
            continue;
        if (!begun)
        {
            impl_->beginEdit();
            begun = true;
        }
        if (auto* track = c->getClipTrack())
            if (auto* right = track->splitClip(*c, at))
                created.push_back(right->itemID.getRawID());
    }
    return created;
}

void Session::undo()
{
    impl_->edit->getUndoManager().undo();
}

void Session::redo()
{
    impl_->edit->getUndoManager().redo();
}

bool Session::canUndo() const
{
    return impl_->edit->getUndoManager().canUndo();
}

bool Session::canRedo() const
{
    return impl_->edit->getUndoManager().canRedo();
}

juce::AudioDeviceManager& Session::deviceManager()
{
    return impl_->engine.getDeviceManager().deviceManager;
}

double Session::cpuUsage() const
{
    return impl_->engine.getDeviceManager().getCpuUsage();
}
} // namespace fd::engine
