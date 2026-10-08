#include "Spike.h"

namespace fd::spike
{
namespace
{
double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

/// Animation clock shared by the scenes (seconds since start). Written on the message thread,
/// read while painting (which is the GL thread in OpenGL mode).
std::atomic<double> gAnimationTimeAtomic{0.0};

double animationTime()
{
    return gAnimationTimeAtomic.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Scene 1: a chain of device panels with animated curves, spectra and knobs.
// ---------------------------------------------------------------------------
class MockDevice final : public juce::Component
{
public:
    explicit MockDevice(int seed) : seed_(seed)
    {
        for (int i = 0; i < 6; ++i)
        {
            auto* k = knobs_.add(new ui::Knob());
            k->setName(juce::StringArray{"Freq", "Gain", "Q", "Drive", "Mix", "Out"}[i]);
            k->textFromValue = [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
            addAndMakeVisible(k);
        }
    }

    void animate()
    {
        animateKnobs();
        repaint();
    }

    void animateKnobs()
    {
        for (int i = 0; i < knobs_.size(); ++i)
            knobs_[i]->setValue(0.5f + 0.45f * static_cast<float>(std::sin(animationTime() * (0.7 + 0.13 * i) + seed_)),
                                juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        g.setColour(t.surface2);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusPanel);
        g.setColour(t.textPrimary);
        g.setFont(ui::fonts::font(t.textSm, ui::fonts::Weight::SemiBold));
        g.drawText("EQ " + juce::String(seed_ + 1), getLocalBounds().removeFromTop(t.deviceHeaderHeight).reduced(8, 0),
                   juce::Justification::centredLeft);

        auto display = displayArea().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(display, t.radiusControl);

        // Spectrum: flat bars.
        const int bars = 96;
        const float bw = display.getWidth() / bars;
        g.setColour(t.surface3);
        for (int b = 0; b < bars; ++b)
        {
            const double v = 0.5 + 0.5 * std::sin(animationTime() * 3.1 + b * 0.37 + seed_) * std::cos(b * 0.11);
            const float h = display.getHeight() * static_cast<float>(0.15 + 0.6 * v * std::exp(-b / 70.0));
            g.fillRect(display.getX() + static_cast<float>(b) * bw, display.getBottom() - h, bw - 1.0f, h);
        }

        // Response curve: solid line + flat translucent fill (no gradient, ADR 0014).
        juce::Path curve;
        const int points = 256;
        for (int i = 0; i < points; ++i)
        {
            const double x = static_cast<double>(i) / (points - 1);
            double db = 0.0;
            for (int band = 0; band < 4; ++band)
            {
                const double centre = 0.15 + 0.22 * band + 0.06 * std::sin(animationTime() * (0.5 + 0.2 * band) + seed_);
                const double gain = 9.0 * std::sin(animationTime() * 0.8 + band * 1.7 + seed_);
                db += gain * std::exp(-std::pow((x - centre) / 0.06, 2.0));
            }
            const float px = display.getX() + static_cast<float>(x) * display.getWidth();
            const float py = display.getCentreY() - static_cast<float>(db / 18.0) * display.getHeight() * 0.5f;
            i == 0 ? curve.startNewSubPath(px, py) : curve.lineTo(px, py);
        }
        juce::Path fill(curve);
        fill.lineTo(display.getRight(), display.getCentreY());
        fill.lineTo(display.getX(), display.getCentreY());
        fill.closeSubPath();
        g.setColour(t.value.withAlpha(0.14f));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    }

    void resized() override
    {
        auto knobs = getLocalBounds().reduced(8).withTop(displayArea().getBottom() + 4);
        const int w = knobs.getWidth() / knobs_.size();
        for (auto* k : knobs_)
            k->setBounds(knobs.removeFromLeft(w));
    }

private:
    juce::Rectangle<int> displayArea() const
    {
        return getLocalBounds().reduced(8).withTrimmedTop(ui::Theme::current().deviceHeaderHeight).withHeight(
            juce::jmax(40, getHeight() / 2 - 10));
    }

    int seed_;
    juce::OwnedArray<ui::Knob> knobs_;
};

class DeviceChainScene final : public juce::Component
{
public:
    explicit DeviceChainScene(SpikeComponent& owner) : owner_(owner)
    {
        for (int i = 0; i < 6; ++i)
            addAndMakeVisible(devices_.add(new MockDevice(i)));
    }

    void animate()
    {
        for (auto* d : devices_)
            d->animate();
    }

    void animateKnobsOnly()
    {
        for (auto* d : devices_)
            d->animateKnobs();
    }

    void paint(juce::Graphics& g) override
    {
        start_ = nowMs();
        g.fillAll(ui::Theme::current().surface1);
    }

    void paintOverChildren(juce::Graphics&) override { owner_.framePainted(nowMs() - start_); }

    void resized() override
    {
        // Two rows of three panels, each at least device-panel height.
        auto area = getLocalBounds().reduced(12);
        const int rowH = juce::jmax(ui::Theme::current().devicePanelHeight, (area.getHeight() - 12) / 2);
        for (int row = 0; row < 2; ++row)
        {
            auto r = area.removeFromTop(rowH);
            area.removeFromTop(12);
            const int w = (r.getWidth() - 24) / 3;
            for (int c = 0; c < 3; ++c)
            {
                devices_[row * 3 + c]->setBounds(r.removeFromLeft(w));
                r.removeFromLeft(12);
            }
        }
    }

private:
    SpikeComponent& owner_;
    juce::OwnedArray<MockDevice> devices_;
    double start_ = 0.0;
};

// ---------------------------------------------------------------------------
// Scene 2: 32-track arrangement with waveform clips, continuous zoom and scroll.
// Waveforms use min/max peak mipmaps, as a real thumbnail cache would.
// ---------------------------------------------------------------------------
class PeakSource
{
public:
    explicit PeakSource(int seed)
    {
        // 2^21 samples (~44 s at 48 kHz), level 0 = 64 samples per peak, each next level halves.
        constexpr int base = 1 << 15;
        juce::Random rng(seed);
        std::vector<std::pair<float, float>> level(base);
        for (int i = 0; i < base; ++i)
        {
            const float env = 0.2f + 0.8f * static_cast<float>(std::pow(std::abs(std::sin(i * 0.0021 + seed)), 3.0));
            const float a = env * (0.5f + 0.5f * rng.nextFloat());
            level[static_cast<size_t>(i)] = {-a * (0.7f + 0.3f * rng.nextFloat()), a};
        }
        levels_.push_back(level);
        while (levels_.back().size() > 64)
        {
            const auto& prev = levels_.back();
            std::vector<std::pair<float, float>> next(prev.size() / 2);
            for (size_t i = 0; i < next.size(); ++i)
                next[i] = {std::min(prev[2 * i].first, prev[2 * i + 1].first), std::max(prev[2 * i].second, prev[2 * i + 1].second)};
            levels_.push_back(std::move(next));
        }
    }

    static constexpr double samplesPerBasePeak = 64.0;
    [[nodiscard]] double lengthSamples() const { return static_cast<double>(levels_[0].size()) * samplesPerBasePeak; }

    /// Min/max over [s0, s1) samples, from the coarsest level that still resolves the span.
    [[nodiscard]] std::pair<float, float> range(double s0, double s1) const
    {
        const double span = std::max(1.0, s1 - s0);
        int lvl = juce::jlimit(0, static_cast<int>(levels_.size()) - 1,
                               static_cast<int>(std::log2(std::max(1.0, span / samplesPerBasePeak))));
        const auto& L = levels_[static_cast<size_t>(lvl)];
        const double perPeak = samplesPerBasePeak * std::pow(2.0, lvl);
        const auto i0 = static_cast<size_t>(juce::jlimit(0.0, static_cast<double>(L.size() - 1), s0 / perPeak));
        const auto i1 = static_cast<size_t>(juce::jlimit(static_cast<double>(i0 + 1), static_cast<double>(L.size()), s1 / perPeak));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = i0; i < i1; ++i)
        {
            lo = std::min(lo, L[i].first);
            hi = std::max(hi, L[i].second);
        }
        return {lo, hi};
    }

private:
    std::vector<std::vector<std::pair<float, float>>> levels_;
};

class ArrangementScene final : public juce::Component
{
public:
    explicit ArrangementScene(SpikeComponent& owner) : owner_(owner)
    {
        for (int i = 0; i < 8; ++i)
            sources_.push_back(std::make_unique<PeakSource>(i + 1));
    }

    void animate() { repaint(); }

    void paint(juce::Graphics& g) override
    {
        const double start = nowMs();
        const auto& t = ui::Theme::current();
        constexpr double sr = 48000.0, bpm = 120.0;
        const double secondsPerBar = 4.0 * 60.0 / bpm;

        // Visible range: zoom oscillates between 2 and 96 bars (log scale), scroll drifts.
        const double zoom = std::exp(std::log(2.0) + (std::log(96.0) - std::log(2.0)) * (0.5 + 0.5 * std::sin(animationTime() * 0.6)));
        const double visibleSec = zoom * secondsPerBar;
        const double startSec = 20.0 + 18.0 * std::sin(animationTime() * 0.23);

        g.fillAll(t.surface0);
        constexpr int headerW = 150, rulerH = 22, tracks = 32;
        const auto area = getLocalBounds();
        const float x0 = static_cast<float>(headerW);
        const float width = static_cast<float>(area.getWidth() - headerW);
        const float laneH = static_cast<float>(area.getHeight() - rulerH) / tracks;
        const double secPerPx = visibleSec / width;

        g.setColour(t.surface1);
        g.fillRect(0, 0, area.getWidth(), rulerH);
        g.fillRect(0, rulerH, headerW, area.getHeight() - rulerH);

        // Bar grid.
        const int barStep = zoom > 48 ? 8 : zoom > 16 ? 4 : 1;
        g.setFont(ui::fonts::mono(t.textXs, ui::fonts::Weight::Regular));
        for (int bar = static_cast<int>(startSec / secondsPerBar); bar * secondsPerBar < startSec + visibleSec; ++bar)
        {
            if (bar % barStep != 0)
                continue;
            const float x = x0 + static_cast<float>((bar * secondsPerBar - startSec) / secPerPx);
            g.setColour(t.line);
            g.fillRect(x, static_cast<float>(rulerH), 1.0f, static_cast<float>(area.getHeight() - rulerH));
            g.setColour(t.textTertiary);
            g.drawText(juce::String(bar + 1), juce::Rectangle<float>(x + 3.0f, 0.0f, 40.0f, rulerH), juce::Justification::centredLeft);
        }

        for (int track = 0; track < tracks; ++track)
        {
            const float y = static_cast<float>(rulerH) + static_cast<float>(track) * laneH;
            const auto colour = t.clipColours[track % t.clipColours.size()];
            g.setColour(colour);
            g.fillRect(0.0f, y + 1.0f, 3.0f, laneH - 2.0f);
            g.setColour(t.textPrimary);
            g.setFont(ui::fonts::font(t.textSm, ui::fonts::Weight::Medium));
            g.drawText("Track " + juce::String(track + 1), juce::Rectangle<float>(12.0f, y, headerW - 16.0f, laneH),
                       juce::Justification::centredLeft);

            // Clips: 8-bar clips with 1-bar gaps, offset per track.
            const double clipLen = 8 * secondsPerBar, period = 9 * secondsPerBar;
            const double offset = (track % 4) * secondsPerBar;
            const auto& src = *sources_[static_cast<size_t>(track) % sources_.size()];
            for (double cs = std::floor((startSec - offset) / period) * period + offset; cs < startSec + visibleSec; cs += period)
            {
                const float cx0 = x0 + static_cast<float>((cs - startSec) / secPerPx);
                const float cx1 = x0 + static_cast<float>((cs + clipLen - startSec) / secPerPx);
                auto clip = juce::Rectangle<float>(std::max(cx0, x0), y + 1.0f, std::min(cx1, x0 + width) - std::max(cx0, x0), laneH - 2.0f);
                if (clip.getWidth() <= 1.0f)
                    continue;
                g.setColour(colour.withMultipliedBrightness(0.32f).withMultipliedSaturation(0.8f));
                g.fillRect(clip);
                auto title = clip.removeFromTop(std::min(12.0f, laneH * 0.3f));
                g.setColour(colour);
                g.fillRect(title);

                // Waveform: one min/max column per pixel, batched.
                juce::RectangleList<float> wave;
                const float mid = clip.getCentreY(), half = clip.getHeight() * 0.48f;
                for (float px = clip.getX(); px < clip.getRight(); px += 1.0f)
                {
                    const double s0 = ((px - cx0) * secPerPx) * sr;
                    const double s1 = s0 + secPerPx * sr;
                    const double wrap = std::fmod(s0, src.lengthSamples());
                    const auto [lo, hi] = src.range(wrap, wrap + (s1 - s0));
                    wave.addWithoutMerging({px, mid - hi * half, 1.0f, std::max(1.0f, (hi - lo) * half)});
                }
                g.setColour(colour.brighter(0.5f));
                g.fillRectList(wave);
            }
            g.setColour(t.line);
            g.fillRect(0.0f, y + laneH - 1.0f, static_cast<float>(area.getWidth()), 1.0f);
        }

        // Playhead.
        g.setColour(t.textPrimary);
        g.fillRect(x0 + width * 0.4f, static_cast<float>(rulerH), 1.0f, static_cast<float>(area.getHeight() - rulerH));
        owner_.framePainted(nowMs() - start);
    }

private:
    SpikeComponent& owner_;
    std::vector<std::unique_ptr<PeakSource>> sources_;
};
} // namespace

// ---------------------------------------------------------------------------

void FrameStats::addFrame(double paintMs, double intervalMs)
{
    const std::scoped_lock lock(mutex_);
    paint_.push_back(paintMs);
    if (intervalMs > 0.0)
        interval_.push_back(intervalMs);
    while (paint_.size() > 300)
        paint_.pop_front();
    while (interval_.size() > 300)
        interval_.pop_front();
}

void FrameStats::clear()
{
    const std::scoped_lock lock(mutex_);
    paint_.clear();
    interval_.clear();
}

double FrameStats::paintAvg() const
{
    const std::scoped_lock lock(mutex_);
    return paint_.empty() ? 0.0 : std::accumulate(paint_.begin(), paint_.end(), 0.0) / static_cast<double>(paint_.size());
}

double FrameStats::paintP95() const
{
    const std::scoped_lock lock(mutex_);
    if (paint_.empty())
        return 0.0;
    std::vector<double> v(paint_.begin(), paint_.end());
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(0.95 * static_cast<double>(v.size() - 1))];
}

double FrameStats::fps() const
{
    const std::scoped_lock lock(mutex_);
    if (interval_.empty())
        return 0.0;
    const double avg = std::accumulate(interval_.begin(), interval_.end(), 0.0) / static_cast<double>(interval_.size());
    return avg > 0.0 ? 1000.0 / avg : 0.0;
}

SpikeComponent::SpikeComponent()
{
    for (auto* b : {&deviceButton_, &arrangementButton_})
        b->setRadioGroupId(1);
    for (auto* b : {&softwareButton_, &openGLButton_})
        b->setRadioGroupId(2);
    deviceButton_.onClick = [this] { setScene(Scene::DeviceChain); };
    arrangementButton_.onClick = [this] { setScene(Scene::Arrangement); };
    softwareButton_.onClick = [this] { setOpenGL(false); };
    openGLButton_.onClick = [this] { setOpenGL(true); };
    for (auto* b : {&deviceButton_, &arrangementButton_, &softwareButton_, &openGLButton_})
        addAndMakeVisible(b);

    gl_.setComponentPaintingEnabled(true);
    gl_.setContinuousRepainting(false);
    startTime_ = nowMs();
    setScene(Scene::DeviceChain);
    softwareButton_.setToggleState(true, juce::dontSendNotification);
}

SpikeComponent::~SpikeComponent()
{
    gl_.detach();
}

void SpikeComponent::setScene(Scene scene)
{
    sceneType_ = scene;
    if (scene == Scene::DeviceChain)
        scene_ = std::make_unique<DeviceChainScene>(*this);
    else
        scene_ = std::make_unique<ArrangementScene>(*this);
    addAndMakeVisible(*scene_);
    (scene == Scene::DeviceChain ? deviceButton_ : arrangementButton_).setToggleState(true, juce::dontSendNotification);
    stats_.clear();
    resized();
}

void SpikeComponent::setOpenGL(bool enabled)
{
    gl_.setContinuousRepainting(enabled);
    if (enabled && !gl_.isAttached())
        gl_.attachTo(*this);
    else if (!enabled && gl_.isAttached())
        gl_.detach();
    (enabled ? openGLButton_ : softwareButton_).setToggleState(true, juce::dontSendNotification);
    stats_.clear();
}

juce::String SpikeComponent::describe() const
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
    const double scale = display != nullptr ? display->scale : 1.0;
    return juce::String(sceneType_ == Scene::DeviceChain ? "Device chain" : "Arrangement") + " | " +
           (isOpenGL() ? "OpenGL" : "Software") + " | paint " + juce::String(stats_.paintAvg(), 2) + " ms avg, " +
           juce::String(stats_.paintP95(), 2) + " ms p95 | " + juce::String(stats_.fps(), 1) + " fps | " +
           juce::String(getWidth()) + "x" + juce::String(getHeight()) + " @ " + juce::String(scale, 2) + "x";
}

void SpikeComponent::onVBlank(double)
{
    gAnimationTimeAtomic.store((nowMs() - startTime_) * 0.001, std::memory_order_relaxed);
    // In OpenGL mode the GL thread renders at the display's refresh rate (continuous
    // repainting); invalidating here marks what it must redraw on its next frame.
    if (auto* d = dynamic_cast<DeviceChainScene*>(scene_.get()))
        d->animate();
    else if (auto* a = dynamic_cast<ArrangementScene*>(scene_.get()))
        a->animate();
    repaint(getLocalBounds().removeFromTop(40));
}

void SpikeComponent::framePainted(double paintMs)
{
    const double start = nowMs() - paintMs;
    stats_.addFrame(paintMs, lastPaintStartMs_ > 0.0 ? start - lastPaintStartMs_ : 0.0);
    lastPaintStartMs_ = start;
}

void SpikeComponent::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface1);
}

void SpikeComponent::paintOverChildren(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    auto bar = getLocalBounds().removeFromTop(40).withTrimmedLeft(430).reduced(12, 0);
    g.setColour(t.textSecondary);
    g.setFont(ui::fonts::mono(t.textSm, ui::fonts::Weight::Regular));
    g.drawText(describe(), bar, juce::Justification::centredRight);
}

void SpikeComponent::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(40).reduced(12, 9);
    deviceButton_.setBounds(bar.removeFromLeft(100));
    bar.removeFromLeft(4);
    arrangementButton_.setBounds(bar.removeFromLeft(100));
    bar.removeFromLeft(24);
    softwareButton_.setBounds(bar.removeFromLeft(80));
    bar.removeFromLeft(4);
    openGLButton_.setBounds(bar.removeFromLeft(80));
    if (scene_ != nullptr)
        scene_->setBounds(area);
}
} // namespace fd::spike
