#include "model/TrackLayout.h"

#include <algorithm>
#include <numeric>

namespace fd::model
{
void TrackLayout::setTrackCount(int count)
{
    heights_.resize(static_cast<size_t>(std::max(0, count)), defaultHeight);
    clampScroll();
}

void TrackLayout::setHeight(int track, double height)
{
    if (track >= 0 && track < trackCount())
        heights_[static_cast<size_t>(track)] = std::clamp(height, minHeight, maxHeight);
    clampScroll();
}

void TrackLayout::setViewportHeight(double h) noexcept
{
    viewport_ = std::max(0.0, h);
    clampScroll();
}

double TrackLayout::contentHeight() const noexcept
{
    return std::accumulate(heights_.begin(), heights_.end(), 0.0);
}

void TrackLayout::scrollBy(double dy) noexcept
{
    setScroll(scroll_ + dy);
}

void TrackLayout::setScroll(double y) noexcept
{
    scroll_ = y;
    clampScroll();
}

void TrackLayout::clampScroll() noexcept
{
    // Allow scrolling until the last track sits at the top half of the view.
    const double maxScroll = std::max(0.0, contentHeight() - viewport_ * 0.5);
    scroll_ = std::clamp(scroll_, 0.0, maxScroll);
}

double TrackLayout::top(int track) const noexcept
{
    double y = -scroll_;
    for (int i = 0; i < track && i < trackCount(); ++i)
        y += heights_[static_cast<size_t>(i)];
    return y;
}

std::optional<TrackLayout::Hit> TrackLayout::hit(double y) const noexcept
{
    double t = -scroll_;
    if (y < t)
        return std::nullopt;
    for (int i = 0; i < trackCount(); ++i)
    {
        const double h = heights_[static_cast<size_t>(i)];
        if (y < t + h)
            return Hit{i, y - t};
        t += h;
    }
    return std::nullopt;
}
} // namespace fd::model
