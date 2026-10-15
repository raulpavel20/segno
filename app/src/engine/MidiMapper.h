#pragma once

#include "model/MidiMap.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::engine
{
namespace te = tracktion;

/// MIDI learn's input (docs/design/midi-learn.md §10): a listener on each physical MIDI input
/// takes, on the MIDI thread, the messages mapped to controls, and every message while a control
/// waits to learn. What it takes reaches no track (ML8, the midi-input-consumers patch) and is
/// queued, lock-free, for the message thread to drain each frame. Which messages are mapped is
/// published to the MIDI thread as flags, set on the message thread.
class MidiMapper
{
public:
    explicit MidiMapper(te::Engine& engine);
    ~MidiMapper();
    MidiMapper(const MidiMapper&) = delete;
    MidiMapper& operator=(const MidiMapper&) = delete;

    /// Follows the physical MIDI inputs (when they change).
    void syncDevices();
    /// The sources mapped now (message thread): their messages are taken from here on.
    void setMapped(const std::vector<model::MidiSource>& sources);
    /// While true, every input's next channel messages are taken, for learning (message thread).
    void setLearning(bool learning);
    /// Which messages of one input are mapped: controllers and notes per channel, pitch bend.
    /// Read on the MIDI thread, set on the message thread.
    struct Flags
    {
        std::array<std::atomic<bool>, 16 * 128> cc{}, note{};
        std::array<std::atomic<bool>, 16> bend{};
        void clear();
        [[nodiscard]] bool has(const juce::MidiMessage& m) const;
    };
    /// What the mapper takes from the input named `device`, for another listener to leave alone
    /// (Capture MIDI): its flags, kept alive while held, and whether it's learning (message
    /// thread). The mapper outlives what holds them.
    struct Taken
    {
        std::shared_ptr<const Flags> flags;
        const std::atomic<bool>* learning = nullptr;
        [[nodiscard]] bool takes(const juce::MidiMessage& m) const; // any thread
    };
    [[nodiscard]] Taken takenFrom(const juce::String& device);

    /// A message taken: the input's name and its bytes.
    struct Message
    {
        std::string device;
        unsigned char bytes[3]{};
        int size = 0;
    };
    /// What was taken since the last drain, oldest first (message thread).
    [[nodiscard]] std::vector<Message> drain();
    /// A message as if it came from the input named `device` and was taken (message thread):
    /// assistants' tests, and inputs other than physical ones.
    void inject(const std::string& device, const juce::MidiMessage& m);

private:
    /// An input's flags, made the first time it's asked for (by name: an input unplugged and
    /// plugged in again keeps its own).
    std::shared_ptr<Flags> flagsOf(const juce::String& device);
    struct Raw
    {
        unsigned char bytes[3]{};
        int size = 0;
    };
    struct Queue
    {
        juce::AbstractFifo fifo{1024};
        std::array<Raw, 1024> buffer;
        void push(const Raw& raw);
    };
    struct Device;

    te::Engine& engine_;
    std::vector<std::unique_ptr<Device>> devices_;
    std::map<juce::String, std::shared_ptr<Flags>> flags_;
    std::vector<model::MidiSource> sources_; // what setMapped set
    std::atomic<bool> learning_{false};
    std::vector<Message> injected_;
};
} // namespace fd::engine
