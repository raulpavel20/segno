#include "fd/device/VoiceManager.h"

namespace fd
{
void VoiceManager::setMode(Mode m) noexcept
{
    if (m != mode_)
    {
        mode_ = m;
        stackSize_ = 0;
    }
}

void VoiceManager::setVoiceCount(int n, Listener& l) noexcept
{
    const int before = voiceCount_;
    setVoiceCount(n);
    for (int v = voiceCount_; v < before; ++v)
        if (auto& voice = voices_[static_cast<size_t>(v)]; voice.active && (voice.gate || voice.sustained))
        {
            voice.gate = voice.sustained = false;
            l.voiceRelease(v);
        }
}

void VoiceManager::setMode(Mode m, Listener& l) noexcept
{
    if (m != mode_ && m != Mode::Poly)
        for (int v = 1; v < maxVoices; ++v)
            if (auto& voice = voices_[static_cast<size_t>(v)]; voice.active && (voice.gate || voice.sustained))
            {
                voice.gate = voice.sustained = false;
                l.voiceRelease(v);
            }
    setMode(m);
}

void VoiceManager::reset() noexcept
{
    voices_ = {};
    pedal_ = false;
    stackSize_ = 0;
    clock_ = 0;
}

int VoiceManager::activeVoices() const noexcept
{
    int n = 0;
    for (int v = 0; v < voiceCount_; ++v)
        n += voices_[static_cast<size_t>(v)].active ? 1 : 0;
    return n;
}

void VoiceManager::voiceFinished(int v) noexcept
{
    auto& voice = voices_[static_cast<size_t>(v)];
    if (!voice.gate)
        voice = {};
}

void VoiceManager::startVoice(int v, int key, float velocity, int channel, std::int32_t noteId, Listener& l) noexcept
{
    auto& voice = voices_[static_cast<size_t>(v)];
    const bool retrigger = voice.active;
    voice = {key, channel, noteId, true, true, false, ++clock_};
    l.voiceStart(v, key, velocity, channel, noteId, retrigger);
}

void VoiceManager::releaseVoice(int v, Listener& l) noexcept
{
    auto& voice = voices_[static_cast<size_t>(v)];
    voice.gate = false;
    if (pedal_)
    {
        voice.sustained = true;
        return;
    }
    voice.sustained = false;
    l.voiceRelease(v);
}

void VoiceManager::handle(const Event& e, Listener& l) noexcept
{
    switch (e.type)
    {
    case EventType::NoteOn:
        if (e.value <= 0.0f)
            noteOff(e, l); // velocity 0 = note off
        else
            noteOn(e, l);
        break;
    case EventType::NoteOff:
        noteOff(e, l);
        break;
    case EventType::Controller:
        if (e.key == 64)
        {
            const bool down = e.value >= 0.5f;
            if (pedal_ && !down)
                for (int v = 0; v < voiceCount_; ++v)
                    if (auto& voice = voices_[static_cast<size_t>(v)]; voice.active && voice.sustained)
                    {
                        voice.sustained = false;
                        l.voiceRelease(v);
                    }
            pedal_ = down;
        }
        break;
    case EventType::AllNotesOff:
        pedal_ = false;
        stackSize_ = 0;
        for (int v = 0; v < maxVoices; ++v)
            if (auto& voice = voices_[static_cast<size_t>(v)]; voice.active && (voice.gate || voice.sustained))
            {
                voice.gate = voice.sustained = false;
                l.voiceRelease(v);
            }
        break;
    case EventType::AllSoundOff:
        pedal_ = false;
        stackSize_ = 0;
        for (int v = 0; v < maxVoices; ++v)
            if (auto& voice = voices_[static_cast<size_t>(v)]; voice.active)
            {
                voice.gate = voice.sustained = false;
                l.voiceStop(v);
            }
        break;
    default:
        break;
    }
}

void VoiceManager::noteOn(const Event& e, Listener& l) noexcept
{
    if (mode_ != Mode::Poly)
    {
        // Remove any older entry of this key, then push it on top.
        int w = 0;
        for (int i = 0; i < stackSize_; ++i)
            if (stack_[static_cast<size_t>(i)] != e.key)
            {
                stack_[static_cast<size_t>(w)] = stack_[static_cast<size_t>(i)];
                stackVelocity_[static_cast<size_t>(w)] = stackVelocity_[static_cast<size_t>(i)];
                ++w;
            }
        stackSize_ = w < 128 ? w : 127;
        stack_[static_cast<size_t>(stackSize_)] = e.key;
        stackVelocity_[static_cast<size_t>(stackSize_)] = e.value;
        ++stackSize_;

        auto& voice = voices_[0];
        if (mode_ == Mode::Legato && voice.active && voice.gate)
        {
            voice.key = e.key;
            l.voiceLegato(0, e.key, e.value);
        }
        else
        {
            startVoice(0, e.key, e.value, e.channel, e.noteId, l);
        }
        return;
    }

    // Poly: same key held -> retrigger that voice.
    for (int v = 0; v < voiceCount_; ++v)
        if (const auto& voice = voices_[static_cast<size_t>(v)];
            voice.active && voice.key == e.key && voice.channel == e.channel)
        {
            startVoice(v, e.key, e.value, e.channel, e.noteId, l);
            return;
        }
    // A free voice.
    for (int v = 0; v < voiceCount_; ++v)
        if (!voices_[static_cast<size_t>(v)].active)
        {
            startVoice(v, e.key, e.value, e.channel, e.noteId, l);
            return;
        }
    // Steal: oldest released voice first, then the oldest playing one.
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (int v = 0; v < voiceCount_; ++v)
        {
            const auto& voice = voices_[static_cast<size_t>(v)];
            const bool released = !voice.gate && !voice.sustained;
            if ((pass == 0) != released)
                continue;
            if (best < 0 || voice.age < voices_[static_cast<size_t>(best)].age)
                best = v;
        }
    startVoice(best, e.key, e.value, e.channel, e.noteId, l);
}

void VoiceManager::noteOff(const Event& e, Listener& l) noexcept
{
    if (mode_ != Mode::Poly)
    {
        int w = 0;
        for (int i = 0; i < stackSize_; ++i)
            if (stack_[static_cast<size_t>(i)] != e.key)
            {
                stack_[static_cast<size_t>(w)] = stack_[static_cast<size_t>(i)];
                stackVelocity_[static_cast<size_t>(w)] = stackVelocity_[static_cast<size_t>(i)];
                ++w;
            }
        stackSize_ = w;
        auto& voice = voices_[0];
        if (!voice.active || voice.key != e.key)
            return;
        if (stackSize_ > 0)
        {
            // Return to the previous held key (last-note priority).
            const int key = stack_[static_cast<size_t>(stackSize_ - 1)];
            const float vel = stackVelocity_[static_cast<size_t>(stackSize_ - 1)];
            if (mode_ == Mode::Legato)
            {
                voice.key = key;
                l.voiceLegato(0, key, vel);
            }
            else
            {
                startVoice(0, key, vel, voice.channel, -1, l);
            }
            return;
        }
        releaseVoice(0, l);
        return;
    }

    for (int v = 0; v < voiceCount_; ++v)
        if (const auto& voice = voices_[static_cast<size_t>(v)];
            voice.active && voice.gate && voice.key == e.key && voice.channel == e.channel)
            releaseVoice(v, l);
}
} // namespace fd
