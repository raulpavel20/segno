#pragma once

#include <fd_device_ui/fd_device_ui.h>

namespace fd::devices
{
/// A step sequence's lanes as the panel shows them (ui::StepFeed): each lane's steps read from the
/// module (`Module` has step(), playingStep(), maxSteps and neutral()), its length a parameter.
/// Drawing a step edits the module's extra state (step 1).
template <typename Module> class LaneFeed final : public ui::StepFeed
{
public:
    struct LaneSpec
    {
        juce::String name;
        float min, max;
        bool pitched = false;
        juce::String unit;
    };

    LaneFeed(ui::DeviceUiContext& ctx, std::size_t firstLength, std::vector<LaneSpec> lanes)
        : ctx_(ctx), firstLength_(firstLength), lanes_(std::move(lanes))
    {
    }

    Lane lane(int index) const override
    {
        const auto& spec = lanes_[static_cast<std::size_t>(index)];
        Lane l;
        l.name = spec.name;
        l.min = spec.min;
        l.max = spec.max;
        l.pitched = spec.pitched;
        l.neutral = Module::neutral(index);
        l.length = static_cast<int>(
            std::lround(ctx_.params.getPlain(firstLength_ + static_cast<std::size_t>(index))));
        const auto* module = dynamic_cast<const Module*>(ctx_.device);
        for (int s = 0; s < ui::StepDisplay::maxSteps; ++s)
            l.values.push_back(module != nullptr ? module->step(index, s) : l.neutral);
        const auto unit = spec.unit;
        l.text = [unit](float v) { return juce::String(std::lround(v)) + unit; };
        return l;
    }
    int playing(int index) const override
    {
        const auto* module = dynamic_cast<const Module*>(ctx_.device);
        return module != nullptr ? module->playingStep(index) : -1;
    }
    void setStep(int, int, float) override {} // extra state through the host binding: step 1
    void setLength(int index, int length) override
    {
        const auto p = firstLength_ + static_cast<std::size_t>(index);
        ctx_.params.beginGesture(p);
        ctx_.params.setPlain(p, static_cast<float>(length));
        ctx_.params.endGesture(p);
    }

private:
    ui::DeviceUiContext& ctx_;
    std::size_t firstLength_;
    std::vector<LaneSpec> lanes_;
};
} // namespace fd::devices
