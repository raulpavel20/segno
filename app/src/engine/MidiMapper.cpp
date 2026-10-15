#include "engine/MidiMapper.h"

#include <algorithm>

namespace fd::engine
{
namespace
{
/// Channel messages: notes, controllers, pitch bend (what can be mapped or learned).
bool mappable(const juce::MidiMessage& m)
{
    return m.isNoteOnOrOff() || m.isController() || m.isPitchWheel();
}
} // namespace

void MidiMapper::Flags::clear()
{
    for (auto& f : cc)
        f.store(false, std::memory_order_relaxed);
    for (auto& f : note)
        f.store(false, std::memory_order_relaxed);
    for (auto& f : bend)
        f.store(false, std::memory_order_relaxed);
}

bool MidiMapper::Flags::has(const juce::MidiMessage& m) const
{
    const int channel = std::clamp(m.getChannel(), 1, 16) - 1;
    if (m.isController())
        return cc[static_cast<size_t>(channel * 128 + m.getControllerNumber())].load(
            std::memory_order_relaxed);
    if (m.isNoteOnOrOff())
        return note[static_cast<size_t>(channel * 128 + m.getNoteNumber())].load(std::memory_order_relaxed);
    if (m.isPitchWheel())
        return bend[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
    return false;
}

void MidiMapper::Queue::push(const Raw& raw)
{
    const auto scope = fifo.write(1);
    if (scope.blockSize1 > 0)
        buffer[static_cast<size_t>(scope.startIndex1)] = raw;
    // Full: dropped (never blocks the MIDI thread).
}

struct MidiMapper::Device final : te::PhysicalMidiInputDevice::Listener
{
    std::shared_ptr<te::MidiInputDevice> device; // kept alive while listened to
    te::PhysicalMidiInputDevice* physical = nullptr;
    juce::String name;
    const std::atomic<bool>* learning = nullptr;
    std::shared_ptr<Flags> flags;
    Queue queue;
    bool taken = false; // the message just handled (MIDI thread only)

    void handleIncomingMidiMessage(const juce::MidiMessage& m) override // MIDI thread
    {
        taken = mappable(m) && (learning->load(std::memory_order_relaxed) || flags->has(m));
        if (!taken)
            return;
        Raw raw{.size = std::min(3, m.getRawDataSize())};
        std::copy_n(m.getRawData(), raw.size, raw.bytes);
        queue.push(raw);
    }
    bool consumesMidiMessage(const juce::MidiMessage&) override { return taken; } // ML8
};

MidiMapper::MidiMapper(te::Engine& engine) : engine_(engine) {}

MidiMapper::~MidiMapper()
{
    for (auto& d : devices_)
        d->physical->removeListener(d.get());
}

void MidiMapper::syncDevices()
{
    const auto inputs = engine_.getDeviceManager().getMidiInDevices();
    std::erase_if(devices_,
                  [&](const std::unique_ptr<Device>& d)
                  {
                      const bool present = std::any_of(inputs.begin(), inputs.end(),
                                                       [&](const auto& in) { return in == d->device; });
                      if (!present)
                          d->physical->removeListener(d.get());
                      return !present;
                  });
    for (const auto& in : inputs)
        if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*>(in.get()))
            if (std::none_of(devices_.begin(), devices_.end(),
                             [&](const auto& d) { return d->device == in; }))
            {
                auto d = std::make_unique<Device>();
                d->device = in;
                d->physical = physical;
                d->name = in->getName();
                d->learning = &learning_;
                d->flags = flagsOf(d->name);
                physical->addListener(d.get());
                devices_.push_back(std::move(d));
            }
}

std::shared_ptr<MidiMapper::Flags> MidiMapper::flagsOf(const juce::String& device)
{
    auto& f = flags_[device];
    if (f == nullptr)
        f = std::make_shared<Flags>();
    return f;
}

MidiMapper::Taken MidiMapper::takenFrom(const juce::String& device)
{
    return {flagsOf(device), &learning_};
}

bool MidiMapper::Taken::takes(const juce::MidiMessage& m) const
{
    return mappable(m) && ((learning != nullptr && learning->load(std::memory_order_relaxed)) ||
                           (flags != nullptr && flags->has(m)));
}

void MidiMapper::setMapped(const std::vector<model::MidiSource>& sources)
{
    sources_ = sources;
    for (const auto& s : sources) // ready for an input not plugged in yet
        (void)flagsOf(juce::String(s.device));
    for (auto& [name, f] : flags_)
    {
        auto& flags = *f;
        flags.clear();
        for (const auto& s : sources)
        {
            if (s.device != name.toStdString())
                continue;
            const auto channel = static_cast<size_t>(std::clamp(s.channel, 1, 16) - 1);
            const auto number = static_cast<size_t>(std::clamp(s.number, 0, 127));
            switch (s.kind)
            {
            case model::MidiSource::Kind::Cc:
                flags.cc[channel * 128 + number] = true;
                break;
            case model::MidiSource::Kind::Cc14:
                flags.cc[channel * 128 + number] = true;
                if (number + 32 < 128)
                    flags.cc[channel * 128 + number + 32] = true; // its fine half
                break;
            case model::MidiSource::Kind::Note:
                flags.note[channel * 128 + number] = true;
                break;
            case model::MidiSource::Kind::PitchBend:
                flags.bend[channel] = true;
                break;
            }
        }
    }
}

void MidiMapper::setLearning(bool learning)
{
    learning_ = learning;
}

std::vector<MidiMapper::Message> MidiMapper::drain()
{
    auto out = std::move(injected_);
    injected_.clear();
    for (auto& d : devices_)
    {
        const auto scope = d->queue.fifo.read(d->queue.fifo.getNumReady());
        const auto take = [&](int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& raw = d->queue.buffer[static_cast<size_t>(i)];
                Message m{.device = d->name.toStdString(), .size = raw.size};
                std::copy_n(raw.bytes, raw.size, m.bytes);
                out.push_back(m);
            }
        };
        take(scope.startIndex1, scope.blockSize1);
        take(scope.startIndex2, scope.blockSize2);
    }
    return out;
}

void MidiMapper::inject(const std::string& device, const juce::MidiMessage& m)
{
    if (!mappable(m))
        return;
    Message message{.device = device, .size = std::min(3, m.getRawDataSize())};
    std::copy_n(m.getRawData(), message.size, message.bytes);
    injected_.push_back(message);
}
} // namespace fd::engine
