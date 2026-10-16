#include "PadInlet.h"

#include <algorithm>

namespace fd::engine
{
namespace
{
class PadInletType final : public te::PluginManager::BuiltInType
{
public:
    PadInletType() : BuiltInType(PadInlet::xmlTypeName) {}
    te::Plugin::Ptr create(te::PluginCreationInfo info) override { return new PadInlet(info); }
};

bool hasBit(const std::array<std::uint64_t, 2>& bits, int note) noexcept
{
    return note >= 0 && note < 128 && ((bits[static_cast<size_t>(note >> 6)] >> (note & 63)) & 1u) != 0;
}
} // namespace

const char* PadInlet::xmlTypeName = "fdPadInlet";
const juce::Identifier PadInlet::noteId{"fdPadNote"};
const juce::Identifier PadInlet::playsId{"fdPadPlays"};
const juce::Identifier PadInlet::chokeId{"fdPadChoke"};

void PadInlet::registerType(te::Engine& e)
{
    e.getPluginManager().registerBuiltInType(std::make_unique<PadInletType>());
}

PadInlet::PadInlet(te::PluginCreationInfo info) : te::Plugin(info)
{
    auto* um = getUndoManager();
    note_.referTo(state, noteId, um, 36);
    plays_.referTo(state, playsId, um, 60);
    choke_.referTo(state, chokeId, um, 0);
    kept_.reserve(capacity);
    publish();
}

PadInlet::~PadInlet()
{
    notifyListenersOfDeletion();
}

void PadInlet::initialise(const te::PluginInitialisationInfo&)
{
    publish();
    if (auto rack = getOwnerRackType())
        updateChokes(*rack);
}

void PadInlet::publish() noexcept
{
    liveNote_.store(std::clamp(note_.get(), 0, 127), std::memory_order_relaxed);
    livePlays_.store(std::clamp(plays_.get(), 0, 127), std::memory_order_relaxed);
}

void PadInlet::setNote(int note, juce::UndoManager* um)
{
    note_.setValue(std::clamp(note, 0, 127), um);
}

void PadInlet::setPlays(int note, juce::UndoManager* um)
{
    plays_.setValue(std::clamp(note, 0, 127), um);
}

void PadInlet::setChoke(int group, juce::UndoManager* um)
{
    choke_.setValue(std::clamp(group, 0, 16), um);
}

void PadInlet::restorePluginStateFromValueTree(const juce::ValueTree& v)
{
    te::copyPropertiesToCachedValues(v, note_, plays_, choke_);
    publish();
}

void PadInlet::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == state && (id == noteId || id == playsId || id == chokeId))
    {
        note_.forceUpdateOfCachedValue();
        plays_.forceUpdateOfCachedValue();
        choke_.forceUpdateOfCachedValue();
        publish();
        if (id != playsId)
            if (auto rack = getOwnerRackType())
                updateChokes(*rack);
        return;
    }
    te::Plugin::valueTreePropertyChanged(tree, id);
}

void PadInlet::updateChokes(te::RackType& rack)
{
    std::vector<PadInlet*> inlets;
    for (auto* p : rack.getPlugins())
        if (auto* inlet = dynamic_cast<PadInlet*>(p))
            inlets.push_back(inlet);
    for (auto* inlet : inlets)
    {
        std::array<std::uint64_t, 2> notes{};
        if (const int group = inlet->choke(); group > 0)
            for (const auto* other : inlets)
                if (other != inlet && other->choke() == group && other->note() != inlet->note())
                {
                    const int n = std::clamp(other->note(), 0, 127);
                    notes[static_cast<size_t>(n >> 6)] |= std::uint64_t{1} << (n & 63);
                }
        inlet->setChokedBy(notes);
    }
}

void PadInlet::setChokedBy(const std::array<std::uint64_t, 2>& notes) noexcept
{
    for (size_t i = 0; i < notes.size(); ++i)
        chokedBy_[i].store(notes[i], std::memory_order_relaxed);
}

bool PadInlet::chokedBy(int note) const noexcept
{
    return hasBit(
        {chokedBy_[0].load(std::memory_order_relaxed), chokedBy_[1].load(std::memory_order_relaxed)}, note);
}

void PadInlet::applyToBuffer(const te::PluginRenderContext& fc)
{
    // The pad's sound starts from silence: its instrument adds to nothing.
    if (fc.destBuffer != nullptr)
        fc.destBuffer->clear(fc.bufferStartSample, fc.bufferNumSamples);

    auto* midi = fc.bufferForMidiMessages;
    if (midi == nullptr || midi->isEmpty() || midi->size() > capacity)
        return; // more than it can filter without allocating: an overload, passed as it came

    const int note = liveNote_.load(std::memory_order_relaxed);
    const int plays = livePlays_.load(std::memory_order_relaxed);
    const std::array<std::uint64_t, 2> chokes{chokedBy_[0].load(std::memory_order_relaxed),
                                              chokedBy_[1].load(std::memory_order_relaxed)};
    // What goes on, in order, then copied back: an array only ever gets shorter here, so neither
    // allocates (removing in place could shrink its storage, which allocates).
    kept_.clear();
    for (const auto& m : *midi)
    {
        if (m.isNoteOnOrOff() || m.isAftertouch())
        {
            const int key = m.getNoteNumber();
            if (key == note)
            {
                kept_.add(m);
                kept_[kept_.size() - 1].setNoteNumber(plays);
            }
            else if (m.isNoteOn() && hasBit(chokes, key))
                kept_.add({juce::MidiMessage::allSoundOff(m.getChannel()).withTimeStamp(m.getTimeStamp()),
                           m.mpeSourceID});
            continue; // another pad's note
        }
        // Bend, pressure, controllers: every pad's. (A SysEx's copy allocates, as it would anywhere.)
        kept_.add(m);
    }
    const bool allNotesOff = midi->isAllNotesOff;
    midi->clear();
    for (const auto& m : kept_)
        midi->add(m);
    midi->isAllNotesOff = allNotesOff;
}
} // namespace fd::engine
