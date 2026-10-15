#pragma once

#include "engine/Session.h"
#include "model/RoundTrip.h"

#include <fd_ui/fd_ui.h>
#include <functional>
#include <optional>

namespace fd::app
{
/// Measuring the round trip (recording.md §22.2, RL6-RL8): the panel Settings > Recording > Audio
/// offset's Measure… opens under its row. An output and an input joined by a cable, Measure (the
/// test signal three times), then the round trip, what the interface reports and the offset
/// recordings need; Use sets Audio offset to it, Cancel leaves it as it was. A failure is named.
class RoundTripPanel final : public juce::Component
{
public:
    explicit RoundTripPanel(engine::Session& session);
    ~RoundTripPanel() override; // a measurement running stops

    /// Its height: the title and hint, the channels, the result and the offset with its buttons.
    [[nodiscard]] static int preferredHeight();
    /// Opened again: the device's channels listed, the last result gone.
    void opened();
    /// Use or Cancel: done with it (the page closes it; Use has set the offset first).
    std::function<void()> onClose;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ---- for tests -------------------------------------------------------------------------------
    [[nodiscard]] juce::ComboBox& outputs() noexcept { return outputs_; }
    [[nodiscard]] juce::ComboBox& inputs() noexcept { return inputs_; }
    [[nodiscard]] ui::Toggle& measureButton() noexcept { return measure_; }
    [[nodiscard]] ui::Toggle& useButton() noexcept { return use_; }
    [[nodiscard]] ui::Toggle& cancelButton() noexcept { return cancel_; }
    /// The result line and the offset line, as shown.
    [[nodiscard]] juce::String resultText() const;
    [[nodiscard]] juce::String offsetLine() const;

private:
    void measure();
    void update();

    engine::Session& session_;
    juce::ComboBox outputs_, inputs_;
    ui::Toggle measure_{"Measure"}, use_{"Use"}, cancel_{"Cancel"};
    std::optional<model::RoundTripResult> result_;
    int trying_ = 0; // the try running (1-3), 0 when none
    // Laid out by resized(): the painted text's places.
    juce::Rectangle<int> title_, hint_, outputCaption_, inputCaption_, resultArea_, offset_;
};
} // namespace fd::app
