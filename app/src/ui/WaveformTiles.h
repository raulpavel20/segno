#pragma once

#include "model/Peaks.h"
#include "model/Snapshot.h"
#include "model/Timeline.h"

#include <juce_graphics/juce_graphics.h>

#include <map>

namespace fd::app
{
/// Cached waveform drawing (ADR 0015 "heavy views cache their drawing"; docs/design/daw-shell.md).
///
/// Waveforms are rendered into tiles: images of `tileWidth` logical px of one file's time at a
/// given zoom and colour, at device resolution. The GPU scales them when drawing, so a frame
/// costs a few textured quads per clip instead of rasterising every waveform. (Tiles are
/// pre-coloured: drawing an alpha mask "with the current brush" is rasterised on the CPU.)
/// - While zooming, tiles come from power-of-two zoom levels and are drawn scaled down by at
///   most 2x, so they stay valid across many frames.
/// - When the zoom settles, tiles are re-rendered at the exact zoom (crisp), a few per frame;
///   until then the level tiles stand in.
/// Tiles are shared by every clip of the same file; the least recently used are evicted
/// beyond `budgetBytes`. Paint-time use only (the message thread is locked).
class WaveformTiles
{
public:
    static constexpr int tileWidth = 512;
    static constexpr std::size_t budgetBytes = 192u * 1024u * 1024u;
    static constexpr int rendersPerFrame = 12;

    /// Call at the start of each paint: resets the per-frame render budget.
    void beginPaint() noexcept;

    /// Draws `clip`'s waveform into `body` (logical coordinates), within `visible`.
    /// `exact`: render at the exact zoom when tiles are missing (zoom has settled).
    void draw(juce::Graphics& g, const model::Peaks& peaks, const model::ClipInfo& clip, juce::Rectangle<float> body,
              juce::Rectangle<int> visible, const model::Timeline& timeline, int lanesX, juce::Colour colour, bool exact);

    /// Draws without tiles (the software renderer, where scaling images costs more than drawing).
    static void drawDirect(juce::Graphics& g, const model::Peaks& peaks, const model::ClipInfo& clip,
                           juce::Rectangle<float> body, juce::Rectangle<int> visible, const model::Timeline& timeline,
                           int lanesX, juce::Colour colour);

    /// True if the last paint had to fall back (missing tiles): paint again soon.
    [[nodiscard]] bool wantsRepaint() const noexcept { return incomplete_; }
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t tileCount() const noexcept { return tiles_.size(); }

private:
    struct Key
    {
        const model::Peaks* peaks = nullptr;
        double pixelsPerSecond = 0.0; // render zoom
        long index = 0;               // tile number along the file's time
        int height = 0;               // device px
        int scale = 0;                // device px per logical px, x100
        juce::uint32 colour = 0;

        auto operator<=>(const Key&) const = default;
    };
    struct Tile
    {
        juce::Image image;
        std::uint64_t lastUsed = 0;
    };

    const Tile* find(const Key& key);
    const Tile* render(const Key& key, float deviceScale);
    [[nodiscard]] static std::size_t sizeOf(const juce::Image& image) noexcept;
    void evict();

    std::map<Key, Tile> tiles_;
    std::uint64_t paintCount_ = 0;
    std::size_t bytes_ = 0;
    int rendersLeft_ = rendersPerFrame;
    bool incomplete_ = false;
};
} // namespace fd::app
