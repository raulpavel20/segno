#pragma once

#include "fd/audiofile/AudioFile.h"

#include <optional>
#include <string>

/// Segno's own drum voices (docs/design/drum-kit.md §27.3, ADR 0068): rendered by the Library build
/// from a recipe's parameters where no CC0 recording exists, then trimmed and levelled as any piece.
/// Deterministic: the same parameters (and seed) give the same samples on every machine. Rendered
/// at 48 kHz, mono. JUCE-free.
///
/// A voice is a JSON object, its "voice" one of:
///   - "kick", "tom": a sine falling from "sweep" Hz to "pitch" Hz over "sweepTime" s, fading over
///     "decay" s, with a "click" (0..1) of noise at its start, "noise" (0..1) under it, "drive"
///     (0..1) saturating it;
///   - "snare": two tones ("tone", "tone2" Hz, fading over "toneDecay" s) and band-passed noise
///     (around "color" Hz, fading over "noiseDecay" s), "noise" (0..1) their balance;
///   - "clap": "bursts" bursts of band-passed noise (around "color" Hz) "spacing" s apart, then a
///     tail fading over "decay" s;
///   - "metal": square waves at "freqs" Hz (the six of a classic machine's hats by default), high-
///     passed at "hp" Hz and fading over "decay" s: hats, cymbals; a "bp" Hz band-pass instead for a
///     cowbell;
///   - "rim": a short tone at "pitch" Hz with a click, fading over "decay" s;
///   - "chip": an 8-bit voice: "wave" pulse, triangle or noise ("noiseMode" long or short), from
///     "start" Hz to "end" Hz over "sweepTime" s, fading over "decay" s in "steps" levels, held at
///     "rate" Hz;
///   - "sub": a tuned 808 bass, a sine at "pitch" Hz gliding from "glide" semitones above, fading
///     over "decay" s, "drive" (0..1) saturating it.
/// Every voice takes "seconds" (its length), "level" (0..1, its loudness: a softer layer) and "seed"
/// (its noise).
namespace fd::library_build
{
/// Renders a voice from its JSON text; nullopt (and `error`) for one it doesn't know.
[[nodiscard]] std::optional<audiofile::DecodedAudio> renderVoice(const std::string& json,
                                                                 std::string* error = nullptr);

/// Bumped when a voice sounds different, so every rendered sample is made again.
inline constexpr const char* drumSynthVersion = "segno-drums 1";
} // namespace fd::library_build
