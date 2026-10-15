#include "engine/MonitoringLatency.h"

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"

namespace fd::engine
{
bool reducesLatency(const tracktion::Edit& edit)
{
    return edit.state.getProperty(reducedLatencyId, false);
}

bool isMonitored(const tracktion::AudioTrack& track)
{
    auto* context = track.edit.getCurrentPlaybackContext();
    if (context == nullptr)
        return false;
    for (auto* in : context->getAllInputs())
        if (!in->getInputDevice().isTrackDevice() && in->getTargets().contains(track.itemID) &&
            in->isLivePlayEnabled(track))
            return true;
    return false;
}

bool isMonitoredWithReducedLatency(const tracktion::AudioTrack& track)
{
    return reducesLatency(track.edit) && isMonitored(track);
}

bool delaysTooMuch(tracktion::Plugin& module)
{
    if (auto* rack = Session::Impl::rackTypeOf(&module))
    {
        const auto modules = Session::Impl::rackModules(*rack);
        return std::any_of(modules.begin(), modules.end(),
                           [](auto* m) { return m->isEnabled() && delaysTooMuch(*m); });
    }
    double ms = 1000.0 * module.getLatencySeconds();
    if (auto* native = dynamic_cast<DevicePlugin*>(&module))
    {
        // Ours, at the rate it runs at (or the audio device's, before it has run).
        const double rate = native->sampleRate() > 0.0 ? native->sampleRate()
                                                       : module.engine.getDeviceManager().getSampleRate();
        ms = rate > 0.0 ? 1000.0 * native->device().latencySamples() / rate : 0.0;
    }
    return ms >= kBypassFromMs - 1e-6;
}

bool shouldBypassWhileMonitoring(tracktion::Plugin& module)
{
    return module.isEnabled() && !Session::Impl::playsNotes(module) && delaysTooMuch(module);
}
} // namespace fd::engine
