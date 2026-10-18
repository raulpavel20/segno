#pragma once

#include <fd_device_ui/fd_device_ui.h>
#include <nlohmann/json.hpp>
#include <optional>

namespace fd::devices
{
/// A step sequence's lanes as the panel shows them (ui::StepFeed): each lane's steps read from the
/// module (`Module` has step(), playingStep(), maxSteps, neutral() and laneKey()), its length a
/// parameter. Drawing a step writes the lane to the module's extra state, through the host binding.
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
    void setStep(int index, int step, float value) override
    {
        const auto* module = dynamic_cast<const Module*>(ctx_.device);
        if (module == nullptr)
            return;
        auto values = nlohmann::json::array();
        for (int s = 0; s < Module::maxSteps; ++s)
            values.push_back(s == step ? value : module->step(index, s));
        ctx_.params.applyExtraState({{std::string(Module::laneKey(index)), std::move(values)}});
    }
    void setLength(int index, int length) override
    {
        const auto p = firstLength_ + static_cast<std::size_t>(index);
        const bool alone = gesture_ != p; // outside a drag: a gesture of its own
        if (alone)
            ctx_.params.beginGesture(p);
        ctx_.params.setPlain(p, static_cast<float>(length));
        if (alone)
            ctx_.params.endGesture(p);
    }
    void beginDrawing(int index, bool length) override
    {
        if (length)
        {
            gesture_ = firstLength_ + static_cast<std::size_t>(index);
            ctx_.params.beginGesture(*gesture_);
        }
        else
            ctx_.params.beginEdit("Draw steps"); // the steps it draws: one undo step
    }
    void endDrawing() override
    {
        if (gesture_)
            ctx_.params.endGesture(*gesture_);
        gesture_.reset();
    }

private:
    ui::DeviceUiContext& ctx_;
    std::size_t firstLength_;
    std::vector<LaneSpec> lanes_;
    std::optional<std::size_t> gesture_; // the length dragged
};
} // namespace fd::devices
