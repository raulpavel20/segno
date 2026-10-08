#pragma once

namespace fd::ui
{
/// Host-agnostic access to a device's parameters for UI controls. Implemented by each host:
/// the plugin adapter (JUCE parameters), the DAW adapter (tracktion AutomatableParameter),
/// and LocalParamBinding (tools). All edits go through here, never into the device directly,
/// so automation recording and undo work (CLAUDE.md, hard rules). Message thread only.
class ParamBinding
{
public:
    virtual ~ParamBinding() = default;

    [[nodiscard]] virtual float getPlain(size_t index) const = 0;
    virtual void setPlain(size_t index, float plain) = 0;
    virtual void beginGesture(size_t index) = 0;
    virtual void endGesture(size_t index) = 0;

    /// Merges `patch` into the device's non-parameter state through the host (ADR 0017), so the
    /// change is saved with the project or preset and is undoable in the DAW.
    virtual void applyExtraState(const nlohmann::json& patch) = 0;
};

/// Merges `patch` into `device`'s extra state directly (hosts call this once they have
/// recorded the change). Not real-time.
void mergeExtraState(Device& device, const nlohmann::json& patch);

/// Binding that writes straight into a ParamStore (tools, tests, the gallery).
class LocalParamBinding final : public ParamBinding
{
public:
    explicit LocalParamBinding(ParamStore& store) : store_(store) {}
    explicit LocalParamBinding(Device& device) : store_(device.params()), device_(&device) {}

    [[nodiscard]] float getPlain(size_t index) const override { return store_.get(index); }
    void setPlain(size_t index, float plain) override { store_.set(index, plain); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json& patch) override
    {
        if (device_ != nullptr)
            mergeExtraState(*device_, patch);
    }

private:
    ParamStore& store_;
    Device* device_ = nullptr;
};

/// Everything a device panel needs from its host.
struct DeviceUiContext
{
    const DeviceInfo& info;
    ParamBinding& params;
    /// Live display data from the audio thread; may be null (e.g. snapshots).
    AnalysisBus* analysis = nullptr;
    /// Read-only access for displays of non-parameter data, e.g. a loaded sample (ADR 0017).
    /// Message thread only; may be null. Never write state through it: use `params`.
    const Device* device = nullptr;
};
} // namespace fd::ui
