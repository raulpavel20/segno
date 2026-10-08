#pragma once

namespace fd::ui
{
/// Vertical peak meter for one or more channels. Polls a level source on every display
/// refresh (VBlankAttachment) and decays smoothly. Scale: -60 dB .. +6 dB.
class Meter : public juce::Component
{
public:
    explicit Meter(int numChannels = 2);

    /// Returns the linear peak for a channel since the last call (e.g. AnalysisBus::takePeak).
    void setLevelSource(std::function<float(int channel)> source) { source_ = std::move(source); }

    /// Sets levels directly (tests, snapshots). Linear peak values.
    void setLevels(std::initializer_list<float> linear);

    void paint(juce::Graphics& g) override;

private:
    void update();

    std::function<float(int)> source_;
    std::vector<float> levelsDb_;
    double lastUpdateMs_ = 0.0;
    juce::VBlankAttachment vblank_{this, [this] { update(); }};
};
} // namespace fd::ui
