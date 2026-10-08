#pragma once

#include <fd_ui/fd_ui.h>
#include <juce_opengl/juce_opengl.h>

#include <deque>
#include <mutex>

namespace fd::spike
{
/// Frame statistics over the last few hundred frames.
class FrameStats
{
public:
    void addFrame(double paintMs, double intervalMs);
    void clear();
    [[nodiscard]] double paintAvg() const;
    [[nodiscard]] double paintP95() const;
    [[nodiscard]] double fps() const;
    [[nodiscard]] int frames() const
    {
        const std::scoped_lock lock(mutex_);
        return static_cast<int>(paint_.size());
    }

private:
    // OpenGL painting happens on the GL thread; the UI reads stats on the message thread.
    mutable std::mutex mutex_;
    std::deque<double> paint_, interval_;
};

/// The rendering spike (ADR 0012, M0 step 10): two stress scenes drawn in the real theme,
/// switchable between JUCE's standard renderer and its OpenGL renderer.
class SpikeComponent final : public juce::Component
{
public:
    enum class Scene
    {
        DeviceChain,
        Arrangement
    };

    SpikeComponent();
    ~SpikeComponent() override;

    void setScene(Scene scene);
    void setOpenGL(bool enabled);
    [[nodiscard]] bool isOpenGL() const { return gl_.isAttached(); }
    [[nodiscard]] const FrameStats& stats() const { return stats_; }
    [[nodiscard]] juce::String describe() const;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    /// Called by the scene views when they paint.
    void framePainted(double paintMs);

private:
    void onVBlank(double timestampSec);

    juce::OpenGLContext gl_;
    std::unique_ptr<juce::Component> scene_;
    Scene sceneType_ = Scene::DeviceChain;
    ui::Toggle deviceButton_{"Device chain"}, arrangementButton_{"Arrangement"};
    ui::Toggle softwareButton_{"Software"}, openGLButton_{"OpenGL"};
    FrameStats stats_;
    double lastPaintStartMs_ = 0.0;
    double startTime_ = 0.0;
    juce::VBlankAttachment vblank_{this, [this](double t) { onVBlank(t); }};
};
} // namespace fd::spike
