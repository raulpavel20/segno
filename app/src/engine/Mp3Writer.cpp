#include "engine/Mp3Writer.h"

#include <cmath>
#include <lame.h>
#include <memory>
#include <vector>

namespace fd::engine
{
namespace
{
/// The rate the MP3 is written at: MP3's own rates, the rendered one where it is one.
int mp3RateFor(double rate)
{
    const auto r = static_cast<int>(std::lround(rate));
    if (r == 32000 || r == 44100 || r == 48000)
        return r;
    return r % 11025 == 0 ? 44100 : 48000;
}

/// UTF-16 with a byte-order mark and a terminator, as LAME's ID3v2 calls take it.
std::vector<unsigned short> utf16(const juce::String& text)
{
    std::vector<unsigned short> out{0xFEFF};
    const auto* p = text.toUTF16().getAddress();
    while (*p != 0)
        out.push_back(static_cast<unsigned short>(*p++));
    out.push_back(0);
    return out;
}
} // namespace

bool writeMp3(const juce::File& file, const juce::AudioBuffer<float>& audio, int frames, double sampleRate,
              int kbps, const juce::String& title, std::string& error)
{
    const int channels = std::min(2, audio.getNumChannels());
    std::unique_ptr<lame_global_flags, decltype(&lame_close)> lame(lame_init(), &lame_close);
    if (lame == nullptr || channels < 1)
    {
        error = "The MP3 encoder couldn't start.";
        return false;
    }
    auto* gf = lame.get();
    lame_set_in_samplerate(gf, static_cast<int>(std::lround(sampleRate)));
    lame_set_out_samplerate(gf, mp3RateFor(sampleRate));
    lame_set_num_channels(gf, channels);
    lame_set_mode(gf, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_VBR(gf, vbr_off);
    lame_set_brate(gf, kbps);
    lame_set_quality(gf, 2); // LAME's "high quality" (-q 2), as its own presets for 320 use
    // The tags, written by hand so the info frame's place is known.
    lame_set_write_id3tag_automatic(gf, 0);
    id3tag_init(gf);
    id3tag_v2_only(gf);
    if (title.isNotEmpty())
        id3tag_set_textinfo_utf16(gf, "TIT2", utf16(title).data());
    id3tag_set_textinfo_utf16(gf, "TSSE", utf16("Segno").data());
    if (lame_init_params(gf) < 0)
    {
        error = "The MP3 encoder doesn't take these settings.";
        return false;
    }

    juce::FileOutputStream out(file);
    if (out.failedToOpen())
    {
        error = "Can't write to " + file.getFullPathName().toStdString() + ".";
        return false;
    }
    out.truncate();
    std::vector<unsigned char> buffer(1 << 16);
    const auto tagSize = lame_get_id3v2_tag(gf, buffer.data(), buffer.size());
    if (tagSize > buffer.size() || !out.write(buffer.data(), tagSize))
    {
        error = "The MP3's tags couldn't be written.";
        return false;
    }

    constexpr int chunk = 4096;
    buffer.resize(static_cast<size_t>(1.25 * chunk + 7200));
    for (int pos = 0; pos < frames; pos += chunk)
    {
        const int n = std::min(chunk, frames - pos);
        const float* left = audio.getReadPointer(0, pos);
        const float* right = channels > 1 ? audio.getReadPointer(1, pos) : left;
        const int bytes =
            lame_encode_buffer_ieee_float(gf, left, right, n, buffer.data(), static_cast<int>(buffer.size()));
        if (bytes < 0 || !out.write(buffer.data(), static_cast<size_t>(bytes)))
        {
            error = "The MP3 couldn't be written (is the disk full?).";
            return false;
        }
    }
    const int last = lame_encode_flush(gf, buffer.data(), static_cast<int>(buffer.size()));
    if (last < 0 || !out.write(buffer.data(), static_cast<size_t>(last)))
    {
        error = "The MP3 couldn't be finished.";
        return false;
    }
    // LAME's info frame over the empty one it left after the tags.
    const auto info = lame_get_lametag_frame(gf, buffer.data(), buffer.size());
    if (info > 0 && info <= buffer.size())
    {
        out.flush();
        if (!out.setPosition(static_cast<juce::int64>(tagSize)) || !out.write(buffer.data(), info))
        {
            error = "The MP3's length couldn't be written.";
            return false;
        }
    }
    out.flush();
    if (out.getStatus().failed())
    {
        error = "The MP3 couldn't be written (is the disk full?).";
        return false;
    }
    return true;
}
} // namespace fd::engine
