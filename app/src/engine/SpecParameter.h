#pragma once

#include <fd/device/Param.h>
#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

/// A parameter of ours in tracktion, from its spec: a module's (DevicePlugin) or a modulator's.
/// Normalised (0..1, the control's travel), stepped kinds discrete, so automation holds their
/// values as steps and modulation snaps a choice or a switch to one.
class SpecParameter final : public te::AutomatableParameter
{
public:
    SpecParameter(const ParamSpec& spec, te::AutomatableEditItem& owner)
        : te::AutomatableParameter(juce::String(std::string(spec.id)), juce::String(std::string(spec.name)),
                                   owner, juce::NormalisableRange<float>(0.0f, 1.0f)),
          spec_(spec)
    {
        valueToStringFunction = [&spec](float n)
        { return juce::String(spec.toText(spec.fromNormalized(n))); };
        stringToValueFunction = [&spec](const juce::String& s)
        {
            const auto v = spec.fromText(s.toStdString());
            return spec.toNormalized(v ? *v : spec.def);
        };
    }

    bool isDiscrete() const override { return spec_.isStepped(); }
    int getNumberOfStates() const override { return spec_.numSteps(); }
    float getValueForState(int state) const override
    {
        const int last = spec_.numSteps() - 1;
        return last > 0 ? juce::jlimit(0.0f, 1.0f, static_cast<float>(state) / static_cast<float>(last))
                        : 0.0f;
    }
    int getStateForValue(float value) const override
    {
        return juce::roundToInt(juce::jlimit(0.0f, 1.0f, value) *
                                static_cast<float>(std::max(0, spec_.numSteps() - 1)));
    }
    float snapToState(float value) const override
    {
        return spec_.isStepped() ? spec_.toNormalized(spec_.fromNormalized(value)) : value;
    }
    std::optional<float> getDefaultValue() const override { return spec_.toNormalized(spec_.def); }

private:
    const ParamSpec& spec_;
};
} // namespace fd::engine
