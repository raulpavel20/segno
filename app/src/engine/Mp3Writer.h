#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <string>

namespace fd::engine
{
/// Writes the first `frames` of `audio` (one or two channels, float) to `file` as a constant bit
/// rate MP3 at `kbps` (export.md §17.1), through LAME: an ID3v2 tag with `title` and Segno as the
/// encoder, and LAME's info frame (its length and the encoder's delay, for gapless players).
/// Written at 44.1 or 48 kHz: from 88.2 or 96 kHz LAME converts. False with `error` on failure.
bool writeMp3(const juce::File& file, const juce::AudioBuffer<float>& audio, int frames, double sampleRate,
              int kbps, const juce::String& title, std::string& error);
} // namespace fd::engine
