#pragma once

#include "model/Grid.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace fd::app
{
/// An editor's grid menu (ADR 0047, daw-shell.md "Setting the grid"): Adaptive ▸ the widths,
/// Fixed ▸ the values, Triplets, Snap to grid. Each item calls `apply` with the grid it makes.
/// The step chip opens it; each editor's right-click menu holds it as a Grid submenu.
juce::PopupMenu gridMenu(const model::GridSettings& grid,
                         std::function<void(const model::GridSettings&)> apply);

} // namespace fd::app
