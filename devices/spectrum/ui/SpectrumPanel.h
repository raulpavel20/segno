#pragma once

#include <fd/dsp/Spectrum.h>
#include <fd_device_ui/fd_device_ui.h>

namespace fd::devices
{
const ui::PanelFactory& spectrumPanelFactory();

/// The Spectrum's display, as the spectrogram holds it (SPEC.md, stage 2; tests).
class SpectrogramView
{
public:
    virtual ~SpectrogramView() = default;
    [[nodiscard]] virtual bool spectrogramShown() const = 0;
    [[nodiscard]] virtual const dsp::SpectrogramHistory& history() const noexcept = 0;
    /// What hovering it at (x, y) reads: "2.60 kHz · E6   1.2 s ago   −24 dB".
    [[nodiscard]] virtual juce::String spectrogramReadout(float x, float y) const = 0;
};
} // namespace fd::devices
