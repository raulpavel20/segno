#include "engine/MidiTap.h"

#include "engine/Session.h"

#include <algorithm>

namespace fd::engine
{
namespace
{
constexpr double kKeep = 120.0; // seconds of what was played

/// The input clock: the stream time tracktion gives every MIDI input, between audio blocks too.
double clockOf(const te::MidiInputDevice& device)
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001 + device.getAdjustSecs();
}
} // namespace

struct MidiTap::Device final : te::PhysicalMidiInputDevice::Listener
{
    std::shared_ptr<te::MidiInputDevice> device; // kept alive while listened to
    te::PhysicalMidiInputDevice* physical = nullptr;
    Queue queue;

    void handleIncomingMidiMessage(const juce::MidiMessage& m) override // MIDI thread
    {
        if (!keep(m))
            return;
        Raw raw{.time = clockOf(*device), .size = std::min(3, m.getRawDataSize())};
        std::copy_n(m.getRawData(), raw.size, raw.bytes);
        queue.push(raw);
    }
};

void MidiTap::Queue::push(const Raw& raw)
{
    const auto scope = fifo.write(1);
    if (scope.blockSize1 > 0)
        buffer[static_cast<size_t>(scope.startIndex1)] = raw;
    // Full: dropped (never blocks the MIDI thread).
}

bool MidiTap::keep(const juce::MidiMessage& m)
{
    // Channel messages only: notes, pedal, controllers, bends, pressure, program changes.
    return m.getRawDataSize() >= 1 && m.getRawDataSize() <= 3 && m.getRawData()[0] >= 0x80 && m.getRawData()[0] < 0xf0;
}

MidiTap::MidiTap(te::Engine& engine) : engine_(engine) {}

MidiTap::~MidiTap()
{
    for (auto& d : devices_)
        d->physical->removeListener(d.get());
}

void MidiTap::syncDevices()
{
    const auto inputs = engine_.getDeviceManager().getMidiInDevices();
    // Inputs gone: stop listening (the device stays alive until then).
    std::erase_if(devices_, [&](const std::unique_ptr<Device>& d) {
        const bool present = std::any_of(inputs.begin(), inputs.end(), [&](const auto& in) { return in == d->device; });
        if (!present)
            d->physical->removeListener(d.get());
        return !present;
    });
    for (const auto& in : inputs)
        if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*>(in.get()))
            if (std::none_of(devices_.begin(), devices_.end(), [&](const auto& d) { return d->device == in; }))
            {
                auto d = std::make_unique<Device>();
                d->device = in;
                d->physical = physical;
                physical->addListener(d.get());
                devices_.push_back(std::move(d));
            }
}

void MidiTap::pushKeyboard(const juce::MidiMessage& message)
{
    if (!keep(message))
        return;
    Raw raw{.time = now(), .size = std::min(3, message.getRawDataSize())};
    std::copy_n(message.getRawData(), raw.size, raw.bytes);
    keyboard_.push(raw);
}

double MidiTap::now() const
{
    for (const auto& in : engine_.getDeviceManager().getMidiInDevices())
        if (in != nullptr)
            return clockOf(*in);
    return engine_.getDeviceManager().getCurrentStreamTime();
}

std::string MidiTap::sourceName(std::uint16_t source) const
{
    if (source == 0)
        return "keyboard";
    const auto i = static_cast<size_t>(source - 1);
    return i < devices_.size() ? devices_[i]->device->getName().toStdString() : std::string();
}

void MidiTap::drain(const std::function<std::optional<double>(double)>& songAt)
{
    std::vector<model::MidiEvent> fresh;
    auto take = [&](Queue& q, std::uint16_t source) {
        const auto scope = q.fifo.read(q.fifo.getNumReady());
        auto add = [&](int start, int count) {
            for (int i = 0; i < count; ++i)
            {
                const auto& raw = q.buffer[static_cast<size_t>(start + i)];
                model::MidiEvent e{.time = raw.time, .song = songAt(raw.time), .size = raw.size, .source = source};
                std::copy_n(raw.bytes, 3, e.bytes);
                fresh.push_back(e);
            }
        };
        add(scope.startIndex1, scope.blockSize1);
        add(scope.startIndex2, scope.blockSize2);
    };
    take(keyboard_, 0);
    for (size_t i = 0; i < devices_.size(); ++i)
        take(devices_[i]->queue, static_cast<std::uint16_t>(i + 1));
    if (!fresh.empty())
    {
        std::stable_sort(fresh.begin(), fresh.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        events_.insert(events_.end(), fresh.begin(), fresh.end());
        if (!events_.empty() && events_.size() > fresh.size() && fresh.front().time < events_[events_.size() - fresh.size() - 1].time)
            std::stable_sort(events_.begin(), events_.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        ++version_;
    }
    // Two minutes are kept.
    const double cutoff = now() - kKeep;
    const auto old = std::find_if(events_.begin(), events_.end(), [&](const auto& e) { return e.time >= cutoff; });
    if (old != events_.begin())
    {
        events_.erase(events_.begin(), old);
        ++version_;
    }
}
} // namespace fd::engine
