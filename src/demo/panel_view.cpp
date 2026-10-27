#include "application.h"

#include <gui_controls/controls_toolbar_dock.h>

//
// View panel: thin adapter over tachyon::RenderToolbarDock. ToolbarItem lists
// live directly in toolbars.sides[] and are seeded in InitializeDemoGUI;
// the reorder / cross-side / tear-off callbacks mutate those side vectors.
//

namespace tachyon
{
    void Application::RenderViewportPanel()
    {
        RenderToolbarDock(*this, toolbars.dockState, toolbars.sides, toolbarCallbacks,
            []()
            {
                // Placeholder for actual view content (3D viewport etc.). Currently empty.
            });
    }
} // namespace tachyon
