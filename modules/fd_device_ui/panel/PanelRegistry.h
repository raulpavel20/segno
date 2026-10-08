#pragma once

namespace fd::ui
{
struct PanelFactory
{
    std::string_view deviceId;
    std::unique_ptr<DevicePanel> (*create)(DeviceUiContext&);
};

/// Every device panel. Generated at build time (link fd_panel_registry).
std::span<const PanelFactory* const> allPanels();

[[nodiscard]] inline const PanelFactory* findPanel(std::string_view deviceId)
{
    for (const auto* p : allPanels())
        if (p->deviceId == deviceId)
            return p;
    return nullptr;
}
} // namespace fd::ui
