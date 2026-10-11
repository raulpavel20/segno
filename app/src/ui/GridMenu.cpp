#include "ui/GridMenu.h"

namespace fd::app
{
namespace
{
/// "narrowest" as a menu says it: "Narrowest".
juce::String menuName(std::string_view name)
{
    auto text = juce::String(std::string(name));
    return text.substring(0, 1).toUpperCase() + text.substring(1);
}
} // namespace

juce::PopupMenu gridMenu(const model::GridSettings& grid,
                         std::function<void(const model::GridSettings&)> apply)
{
    using G = model::GridSettings;
    const bool adaptive = grid.mode == G::Mode::Adaptive;
    juce::PopupMenu widths, values;
    for (int i = 0; i < G::numWidths; ++i)
    {
        const auto width = static_cast<G::Width>(i);
        auto g = grid;
        g.mode = G::Mode::Adaptive;
        g.width = width;
        widths.addItem(menuName(model::widthName(width)), true, adaptive && grid.width == width,
                       [apply, g] { apply(g); });
    }
    for (int i = 0; i < G::numValues; ++i)
    {
        const auto value = static_cast<G::Value>(i);
        auto g = grid;
        g.mode = G::Mode::Fixed;
        g.value = value;
        values.addItem(juce::String(std::string(model::valueName(value))), true,
                       !adaptive && grid.value == value, [apply, g] { apply(g); });
    }
    juce::PopupMenu menu;
    menu.addSubMenu("Adaptive", widths, true, nullptr, adaptive);
    menu.addSubMenu("Fixed", values, true, nullptr, !adaptive);
    menu.addSeparator();
    auto triplets = grid, snap = grid;
    triplets.triplets = !grid.triplets;
    snap.snap = !grid.snap;
    menu.addItem("Triplets", true, grid.triplets, [apply, triplets] { apply(triplets); });
    menu.addItem("Snap to grid", true, grid.snap, [apply, snap] { apply(snap); });
    return menu;
}
} // namespace fd::app
