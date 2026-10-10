#pragma once

#include <optional>
#include <vector>

namespace fd::model
{
/// Vertical layout of the arrangement: track heights, scroll, hit testing.
class TrackLayout
{
public:
    static constexpr double defaultHeight = 64.0;
    static constexpr double minHeight = 24.0;
    static constexpr double maxHeight = 480.0;

    /// Keeps existing heights; new tracks get the default.
    void setTrackCount(int count);
    [[nodiscard]] int trackCount() const noexcept { return static_cast<int>(heights_.size()); }

    /// Sets a row's own height (clamped); its extra height stays.
    void setHeight(int track, double height);
    /// A row's whole height: its own plus its extra.
    [[nodiscard]] double height(int track) const noexcept
    {
        return heights_[static_cast<size_t>(track)] + extras_[static_cast<size_t>(track)];
    }
    /// A row's own height (the header and its clips), without the extra.
    [[nodiscard]] double baseHeight(int track) const noexcept { return heights_[static_cast<size_t>(track)]; }
    /// Extra height below a row's own (a track's open takes, ADR 0032); not clamped.
    void setExtra(int track, double extra);
    [[nodiscard]] double extra(int track) const noexcept { return extras_[static_cast<size_t>(track)]; }

    void setViewportHeight(double h) noexcept;
    [[nodiscard]] double viewportHeight() const noexcept { return viewport_; }
    [[nodiscard]] double contentHeight() const noexcept;

    [[nodiscard]] double scroll() const noexcept { return scroll_; }
    void scrollBy(double dy) noexcept;
    void setScroll(double y) noexcept;

    /// Top of `track` in view coordinates (after scrolling).
    [[nodiscard]] double top(int track) const noexcept;

    struct Hit
    {
        int track = 0;
        double offset = 0.0; // y within the track
    };
    /// The track at view y; nullopt above the first or below the last track.
    [[nodiscard]] std::optional<Hit> hit(double y) const noexcept;

private:
    void clampScroll() noexcept;

    std::vector<double> heights_, extras_;
    double viewport_ = 400.0;
    double scroll_ = 0.0;
};
} // namespace fd::model
