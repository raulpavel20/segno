// Presets in the DAW (M3 A1, ADR 0044, docs/design/presets.md): a module's preset host, the
// library, and your default for new modules.

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"

#include <fd/device/Registry.h>

namespace fd::engine
{
namespace
{
const juce::Identifier kPresetId{"fdPreset"}; // the module's PresetMemo, beside its state

/// What a preset host does in the DAW whatever it serves: the library, a load as one undo step
/// (what loads while the list is open joining it), the trash and the folder.
class BrowsingPresetHost : public PresetHost
{
public:
    explicit BrowsingPresetHost(Session& session) : session_(session) {}
    ~BrowsingPresetHost() override { endBrowsing(true); }

    PresetLibrary& library() override { return session_.presetLibrary(); }

    void apply(const DeviceState& state, const PresetMemo& memo, const std::string& undoName) final
    {
        auto& impl = session_.impl();
        if (!browsing_ || !grouped_)
        {
            impl.beginEdit(juce::String(undoName));
            if (browsing_)
            {
                ++impl.groupDepth; // what loads while the list is open joins this step
                grouped_ = true;
            }
        }
        else
            impl.edit->getUndoManager().setCurrentTransactionName(juce::String(undoName));
        applyState(state, memo);
    }

    void beginBrowsing() override
    {
        browsing_ = true;
        grouped_ = false;
    }

    void endBrowsing(bool keep) override
    {
        if (std::exchange(grouped_, false))
        {
            --session_.impl().groupDepth;
            if (!keep)
                session_.undo(); // back to what was there before the list opened
        }
        browsing_ = false;
    }

    bool trash(const std::filesystem::path& file) override
    {
        return juce::File(juce::String::fromUTF8(file.string().c_str())).moveToTrash();
    }

    void reveal(const std::filesystem::path& folder) override
    {
        const juce::File f(juce::String::fromUTF8(folder.string().c_str()));
        if (!f.isDirectory())
            f.createDirectory();
        f.startAsProcess(); // the file manager, at that folder
    }

protected:
    /// Sets the whole state and memo, in the step apply() opened.
    virtual void applyState(const DeviceState& state, const PresetMemo& memo) = 0;
    Session& session_;

private:
    bool browsing_ = false, grouped_ = false;
};

/// One of our modules' presets: its state through its automatable parameters, its memo in its
/// state tree, each change through the Edit's undo manager.
class SessionPresetHost final : public BrowsingPresetHost
{
public:
    SessionPresetHost(Session& session, DevicePlugin& plugin)
        : BrowsingPresetHost(session), plugin_(&plugin), native_(plugin)
    {
    }

    DeviceState state() const override
    {
        const auto& info = native_.device().info();
        DeviceState s;
        s.deviceId = std::string(info.id);
        s.stateVersion = info.stateVersion;
        // The stored values: what the project keeps, whatever a lane plays over them.
        for (size_t i = 0; i < info.params.size(); ++i)
            if (!info.params[i].hasFlag(ParamFlags::Transient)) // how you listen isn't kept
                s.params[std::string(info.params[i].id)] =
                    info.params[i].fromNormalized(native_.storedValue(i));
        native_.device().writeExtraState(s.extra);
        return s;
    }

    DeviceState migrated(DeviceState s) const override
    {
        if (s.stateVersion < native_.device().info().stateVersion)
            native_.device().migrateState(s);
        return s;
    }

    PresetMemo memo() const override { return Session::Impl::presetMemo(native_); }

    void setMemo(const PresetMemo& memo) override
    {
        native_.state.setProperty(kPresetId, juce::String(memo.toJson().dump()), native_.getUndoManager());
    }

private:
    void applyState(const DeviceState& state, const PresetMemo& memo) override
    {
        session_.impl().applyModuleState(native_, state, memo);
    }

    te::Plugin::Ptr plugin_; // keeps the module alive while its host exists
    DevicePlugin& native_;
};

/// A modulator's presets (modulators.md §14): its settings and a Steps pattern.
class ModulatorPresetHost final : public BrowsingPresetHost
{
public:
    ModulatorPresetHost(Session& session, Modulator& m) : BrowsingPresetHost(session), keepAlive_(&m), m_(m)
    {
    }

    DeviceState state() const override { return Session::Impl::modulatorState(m_); }

    PresetMemo memo() const override
    {
        const auto text = m_.state.getProperty(kPresetId).toString();
        return text.isEmpty()
                   ? PresetMemo{}
                   : PresetMemo::fromJson(nlohmann::json::parse(text.toStdString(), nullptr, false));
    }

    void setMemo(const PresetMemo& memo) override
    {
        m_.state.setProperty(kPresetId, juce::String(memo.toJson().dump()),
                             &session_.impl().edit->getUndoManager());
    }

private:
    void applyState(const DeviceState& state, const PresetMemo& memo) override
    {
        session_.impl().applyModulatorState(m_, state, memo);
    }

    te::Modifier::Ptr keepAlive_;
    Modulator& m_;
};
} // namespace

PresetMemo Session::Impl::presetMemo(const DevicePlugin& plugin)
{
    const auto text = plugin.state.getProperty(kPresetId).toString();
    if (text.isEmpty())
        return {};
    return PresetMemo::fromJson(nlohmann::json::parse(text.toStdString(), nullptr, false));
}

void Session::Impl::applyModuleState(DevicePlugin& plugin, const DeviceState& state, const PresetMemo& memo)
{
    const auto& info = plugin.device().info();
    // Parameters through their stored values: in this undo step, heard at once. No gesture, so
    // nothing records automation; a lane that plays keeps playing over them (presets.md §11).
    for (size_t i = 0; i < info.params.size(); ++i)
    {
        const auto& spec = info.params[i];
        const auto it = state.params.find(std::string(spec.id));
        const float normalised =
            spec.toNormalized(spec.sanitize(it != state.params.end() ? it->second : spec.def));
        if (std::abs(plugin.storedValue(i) - normalised) > 1e-7f)
            plugin.setStoredValue(i, normalised);
    }
    // The extra state by patch: the preset's, with the session's left as they are.
    nlohmann::json extra = nlohmann::json::object();
    plugin.device().writeExtraState(extra);
    if (const auto patch = extraPatch(info, extra, state.extra); !patch.empty())
        plugin.applyExtraState(patch);
    const auto json = memo.empty() ? juce::String() : juce::String(memo.toJson().dump());
    if (plugin.state.getProperty(kPresetId).toString() != json)
    {
        if (json.isEmpty())
            plugin.state.removeProperty(kPresetId, plugin.getUndoManager());
        else
            plugin.state.setProperty(kPresetId, json, plugin.getUndoManager());
    }
}

DeviceState Session::Impl::modulatorState(const Modulator& m)
{
    const auto& info = m.info();
    DeviceState s;
    s.deviceId = std::string(info.id);
    s.stateVersion = info.stateVersion;
    for (size_t i = 0; i < info.params.size(); ++i)
        if (!info.params[i].hasFlag(ParamFlags::Transient))
            s.params[std::string(info.params[i].id)] = m.plain(i);
    if (m.kind() == model::ModulatorKind::Steps && m.patternSet())
        s.extra = {{"steps", m.stepValues()}, {"chances", m.stepChances()}};
    return s;
}

void Session::Impl::applyModulatorState(Modulator& m, const DeviceState& state, const PresetMemo& memo)
{
    const auto& info = m.info();
    auto* um = &edit->getUndoManager();
    for (size_t i = 0; i < info.params.size(); ++i)
    {
        const auto& spec = info.params[i];
        if (spec.hasFlag(ParamFlags::Transient))
            continue;
        const auto it = state.params.find(std::string(spec.id));
        const float plain = spec.sanitize(it != state.params.end() ? it->second : spec.def);
        if (std::abs(m.plain(i) - plain) > 1e-6f * std::max(1.0f, std::abs(plain)))
            m.parameter(i).setParameter(spec.toNormalized(plain), juce::sendNotification);
    }
    // A Steps pattern: the preset's, or the default one.
    if (m.kind() == model::ModulatorKind::Steps)
    {
        const auto floats = [&](const char* key)
        {
            std::vector<float> out;
            if (state.extra.is_object() && state.extra.contains(key) && state.extra[key].is_array())
                for (const auto& v : state.extra[key])
                    if (v.is_number())
                        out.push_back(v.get<float>());
            return out;
        };
        const auto values = floats("steps");
        if (!values.empty() || m.patternSet())
            m.setPattern(values, floats("chances"), um);
    }
    const auto json = memo.empty() ? juce::String() : juce::String(memo.toJson().dump());
    if (m.state.getProperty(kPresetId).toString() != json)
    {
        if (json.isEmpty())
            m.state.removeProperty(kPresetId, um);
        else
            m.state.setProperty(kPresetId, json, um);
    }
}

PresetLibrary& Session::presetLibrary()
{
    // Our modules' factory presets, and the modulators' (modulators.md §14).
    static const std::vector<FactoryPresetFile> factory = []
    {
        std::vector<FactoryPresetFile> all(factoryPresetFiles().begin(), factoryPresetFiles().end());
        const auto modulators = model::modulatorFactoryPresets();
        all.insert(all.end(), modulators.begin(), modulators.end());
        return all;
    }();
    if (impl_->presets == nullptr)
        impl_->presets = std::make_unique<PresetLibrary>(
            factory, std::filesystem::path(impl_->presetsFolder.getFullPathName().toStdString()));
    return *impl_->presets;
}

void Session::setPresetsFolder(const juce::File& folder)
{
    impl_->presetsFolder = folder;
    impl_->presets.reset();
}

std::unique_ptr<PresetHost> Session::presetHost(std::uint64_t deviceId)
{
    if (auto* native = dynamic_cast<DevicePlugin*>(impl_->plugin(deviceId)))
        return std::make_unique<SessionPresetHost>(*this, *native);
    if (auto* m = impl_->modulator(deviceId))
        return std::make_unique<ModulatorPresetHost>(*this, *m);
    return nullptr;
}
} // namespace fd::engine
