#pragma once

#include "fd/device/Events.h"

#include <array>
#include <cstdint>

namespace fd
{
/// Note-to-voice allocation shared by all instruments (real-time safe, no allocation).
///
/// Poly: a free voice, else steal the oldest released voice, else the oldest playing one.
/// Re-playing a held key retriggers its voice. Mono: last-note priority, every new note
/// retriggers. Legato: overlapping notes change pitch without retriggering.
/// The sustain pedal (CC 64) holds released notes; AllNotesOff releases everything.
///
/// The instrument implements Listener; stolen and legato voices are reported so the instrument
/// can keep oscillator phase and envelope level continuous (click-free).
class VoiceManager
{
public:
    static constexpr int maxVoices = 32;

    enum class Mode
    {
        Poly,
        Mono,
        Legato
    };

    struct Listener
    {
        virtual ~Listener() = default;
        /// Start voice `v`. `retrigger` = the voice was sounding (steal or mono retrigger).
        virtual void voiceStart(int v, int key, float velocity, int channel, std::int32_t noteId, bool retrigger) = 0;
        /// Legato: change the voice's pitch target without retriggering.
        virtual void voiceLegato(int v, int key, float velocity) = 0;
        virtual void voiceRelease(int v) = 0;
    };

    struct Voice
    {
        int key = -1;
        int channel = 0;
        std::int32_t noteId = -1;
        bool active = false;    // sounding (until the instrument reports it finished)
        bool gate = false;      // key held
        bool sustained = false; // released while the pedal was down
        std::uint64_t age = 0;
    };

    void setVoiceCount(int n) noexcept { voiceCount_ = n < 1 ? 1 : (n > maxVoices ? maxVoices : n); }
    void setMode(Mode m) noexcept;
    void reset() noexcept;

    void handle(const Event& e, Listener& l) noexcept;
    /// The instrument calls this when a voice has fully faded out.
    void voiceFinished(int v) noexcept;

    [[nodiscard]] const Voice& voice(int v) const noexcept { return voices_[static_cast<size_t>(v)]; }
    [[nodiscard]] int voiceCount() const noexcept { return voiceCount_; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] int activeVoices() const noexcept;

private:
    void noteOn(const Event& e, Listener& l) noexcept;
    void noteOff(const Event& e, Listener& l) noexcept;
    void releaseVoice(int v, Listener& l) noexcept;
    void startVoice(int v, int key, float velocity, int channel, std::int32_t noteId, Listener& l) noexcept;

    std::array<Voice, maxVoices> voices_{};
    int voiceCount_ = 8;
    Mode mode_ = Mode::Poly;
    bool pedal_ = false;
    std::uint64_t clock_ = 0;

    // Mono/legato: held keys, oldest first.
    std::array<int, 128> stack_{};
    std::array<float, 128> stackVelocity_{};
    int stackSize_ = 0;
};
} // namespace fd
