#include "ui/WaveformTiles.h"

#include <cmath>

namespace fd::app
{
namespace
{
/// The whole pixels inside `area`: a waveform never spills past its clip's fractional edges onto
/// its outline (the clip's body fills the partial pixels).
juce::Rectangle<int> wholePixelsIn(juce::Rectangle<float> area)
{
    const int x0 = static_cast<int>(std::ceil(area.getX())), x1 = static_cast<int>(std::floor(area.getRight()));
    const int y0 = static_cast<int>(std::ceil(area.getY())), y1 = static_cast<int>(std::floor(area.getBottom()));
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

/// Fills the outline of `columns` waveform columns starting at x0, each `columnWidth` wide.
/// `frameAt(c)` is the first source frame of column c (and of c + 1 for its end). The outline
/// goes along the top edge and back along the bottom; points where an edge doesn't change are
/// skipped.
template <typename FrameAt>
void fillColumns(juce::Graphics& g, const model::Peaks& peaks, int columns, float x0, float columnWidth, float mid,
                 float half, FrameAt&& frameAt, const std::function<float(int column)>& gainAt = {})
{
    if (columns <= 0)
        return;
    std::vector<std::pair<float, float>> col(static_cast<size_t>(columns));
    std::int64_t from = frameAt(0);
    for (int c = 0; c < columns; ++c)
    {
        const std::int64_t to = std::max(from + 1, frameAt(c + 1));
        auto [lo, hi] = peaks.range(from, to);
        if (gainAt)
        {
            const float gain = gainAt(c);
            lo *= gain;
            hi *= gain;
        }
        from = to;
        const float top = mid - std::clamp(hi, -1.0f, 1.0f) * half;
        col[static_cast<size_t>(c)] = {top, std::max(mid - std::clamp(lo, -1.0f, 1.0f) * half, top + 1.0f)};
    }

    juce::Path p;
    p.preallocateSpace(columns * 6 + 8);
    auto edge = [&](bool top, int i) { return top ? col[static_cast<size_t>(i)].first : col[static_cast<size_t>(i)].second; };
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool top = pass == 0;
        for (int k = 0; k < columns; ++k)
        {
            const int i = top ? k : columns - 1 - k;
            const bool flat = k > 0 && k < columns - 1 && std::abs(edge(top, i) - edge(top, i - 1)) < 0.25f &&
                              std::abs(edge(top, i) - edge(top, i + 1)) < 0.25f;
            if (flat)
                continue;
            const float x = x0 + static_cast<float>(i) * columnWidth;
            const float y = edge(top, i);
            if (k == 0 && top)
                p.startNewSubPath(x, y);
            else
                p.lineTo(top ? x : x + columnWidth, y);
            p.lineTo(top ? x + columnWidth : x, y);
        }
    }
    p.closeSubPath();
    g.fillPath(p);
}
} // namespace

void WaveformTiles::beginPaint() noexcept
{
    ++paintCount_;
    rendersLeft_ = rendersPerFrame;
    incomplete_ = false;
}

const WaveformTiles::Tile* WaveformTiles::find(const Key& key)
{
    const auto it = tiles_.find(key);
    if (it == tiles_.end())
        return nullptr;
    it->second.lastUsed = paintCount_;
    return &it->second;
}

const WaveformTiles::Tile* WaveformTiles::render(const Key& key, float deviceScale)
{
    if (rendersLeft_ <= 0)
        return nullptr;
    --rendersLeft_;
    const int width = juce::roundToInt(static_cast<float>(tileWidth) * deviceScale);
    Tile tile;
    tile.image = juce::Image(juce::Image::ARGB, width, key.height, true, juce::SoftwareImageType());
    {
        juce::Graphics g(tile.image);
        g.setColour(juce::Colour(key.colour));
        const auto& peaks = *key.peaks;
        const double secondsPerColumn = 1.0 / (key.pixelsPerSecond * deviceScale);
        const double start = static_cast<double>(key.index) * tileWidth / key.pixelsPerSecond;
        const auto h = static_cast<float>(key.height);
        fillColumns(g, peaks, width, 0.0f, 1.0f, h * 0.5f, h * 0.5f - deviceScale, [&](int c) {
            return static_cast<std::int64_t>(std::floor((start + c * secondsPerColumn) * peaks.sampleRate));
        });
    }
    tile.lastUsed = paintCount_;
    bytes_ += sizeOf(tile.image);
    auto [it, inserted] = tiles_.insert_or_assign(key, std::move(tile));
    (void)inserted;
    evict();
    return &it->second;
}

std::size_t WaveformTiles::sizeOf(const juce::Image& image) noexcept
{
    return static_cast<std::size_t>(image.getWidth()) * static_cast<std::size_t>(image.getHeight()) * 4u;
}

void WaveformTiles::evict()
{
    while (bytes_ > budgetBytes && !tiles_.empty())
    {
        auto oldest = tiles_.begin();
        for (auto it = tiles_.begin(); it != tiles_.end(); ++it)
            if (it->second.lastUsed < oldest->second.lastUsed)
                oldest = it;
        if (oldest->second.lastUsed == paintCount_)
            break; // never evict what this paint uses
        bytes_ -= sizeOf(oldest->second.image);
        tiles_.erase(oldest);
    }
}

void WaveformTiles::drawDirect(juce::Graphics& g, const model::Peaks& peaks, const model::ClipInfo& clip,
                               juce::Rectangle<float> body, juce::Rectangle<int> visible, const model::Timeline& timeline,
                               int lanesX, juce::Colour colour, const std::function<float(double seconds)>& gain)
{
    const auto area = body.getIntersection(visible.toFloat());
    if (body.getHeight() < 4.0f || area.isEmpty())
        return;
    const int x0 = static_cast<int>(std::floor(area.getX()));
    const int x1 = static_cast<int>(std::ceil(area.getRight()));
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(wholePixelsIn(area));
    g.setColour(colour);
    fillColumns(g, peaks, x1 - x0, static_cast<float>(x0), 1.0f, body.getCentreY(), body.getHeight() * 0.5f - 1.0f,
                [&](int c) {
                    const double file = timeline.timeForX(x0 + c - lanesX) - clip.start + clip.offset;
                    return static_cast<std::int64_t>(std::floor(file * peaks.sampleRate));
                },
                gain ? std::function<float(int)>([&](int c) { return gain(timeline.timeForX(static_cast<float>(x0 + c - lanesX) + 0.5f)); })
                     : std::function<float(int)>());
}

void WaveformTiles::draw(juce::Graphics& g, const model::Peaks& peaks, const model::ClipInfo& clip,
                         juce::Rectangle<float> body, juce::Rectangle<int> visible, const model::Timeline& timeline,
                         int lanesX, juce::Colour colour, bool exact)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int height = juce::roundToInt(body.getHeight() * scale);
    const auto area = body.getIntersection(visible.toFloat());
    if (height < 4 || area.isEmpty())
        return;

    const double pps = timeline.pixelsPerSecond();
    auto fileAt = [&](float x) { return timeline.timeForX(x - static_cast<float>(lanesX)) - clip.start + clip.offset; };
    auto xAt = [&](double fileSeconds) {
        return static_cast<float>(lanesX + timeline.xForTime(fileSeconds - clip.offset + clip.start));
    };
    const double fileA = fileAt(area.getX()), fileB = fileAt(area.getRight());
    const int scaleKey = juce::roundToInt(scale * 100.0f);
    const auto colourKey = colour.getARGB();
    const double levelPps = std::exp2(std::ceil(std::log2(pps) - 1e-9)); // >= pps: drawn scaled down <= 2x

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(wholePixelsIn(area));
    g.setColour(colour);

    auto drawTile = [&](const Tile& tile, double renderPps, long index) {
        const float x = std::round(xAt(static_cast<double>(index) * tileWidth / renderPps) * scale) / scale;
        const auto w = static_cast<float>(tileWidth * pps / renderPps);
        g.drawImage(tile.image, juce::Rectangle<float>(x, body.getY(), w, body.getHeight()),
                    juce::RectanglePlacement::stretchToFit, false);
    };
    auto drawDirect = [&](double from, double to) {
        const int x0 = static_cast<int>(std::floor(xAt(from)));
        const int x1 = static_cast<int>(std::ceil(xAt(to)));
        fillColumns(g, peaks, x1 - x0, static_cast<float>(x0), 1.0f, body.getCentreY(), body.getHeight() * 0.5f - 1.0f,
                    [&](int c) {
                        return static_cast<std::int64_t>(std::floor(fileAt(static_cast<float>(x0 + c)) * peaks.sampleRate));
                    });
    };

    const double primaryPps = exact ? pps : levelPps;
    const double tileSeconds = tileWidth / primaryPps;
    for (auto i = static_cast<long>(std::floor(fileA / tileSeconds)); i <= static_cast<long>(std::floor(fileB / tileSeconds)); ++i)
    {
        const Key key{&peaks, primaryPps, i, height, scaleKey, colourKey};
        const Tile* tile = find(key);
        if (tile == nullptr)
            tile = render(key, scale);
        if (tile != nullptr)
        {
            drawTile(*tile, primaryPps, i);
            continue;
        }

        // Out of render budget: stand in for this tile's span with cached level tiles, else draw
        // it directly, and paint again next frame.
        incomplete_ = true;
        const double s0 = std::max(fileA, static_cast<double>(i) * tileSeconds);
        const double s1 = std::min(fileB, static_cast<double>(i + 1) * tileSeconds);
        juce::Graphics::ScopedSaveState span(g);
        g.reduceClipRegion(juce::Rectangle<float>(xAt(s0), body.getY(), xAt(s1) - xAt(s0) + 1.0f, body.getHeight())
                               .getSmallestIntegerContainer());
        bool covered = exact;
        if (exact)
        {
            const double levelSeconds = tileWidth / levelPps;
            for (auto j = static_cast<long>(std::floor(s0 / levelSeconds)); j <= static_cast<long>(std::floor(s1 / levelSeconds)); ++j)
                if (const Tile* level = find({&peaks, levelPps, j, height, scaleKey, colourKey}))
                    drawTile(*level, levelPps, j);
                else
                    covered = false;
        }
        if (!covered)
            drawDirect(s0, s1);
    }
}
} // namespace fd::app
