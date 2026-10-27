#pragma once

#include <gui_core/gui_base_types.h>

namespace tachyon
{
    /// @addtogroup windows
    /// @{

    class OSWindowManager;
    class WindowRegistry;
    class ElementStorage;
    class ITextSystem;

    // Keeps a frame's window elements (WindowRegistry, ElementStorage) and the
    // OS windows (OSWindowManager) in step; neither side knows about the other.

    /// Before layout: releases the windows no longer declared, and pins the elements
    /// of user-moved/resized windows to their OS rects.
    void SyncWindowsBeforeLayout(OSWindowManager& windowManager, const WindowRegistry& windowRegistry,
                                 ElementStorage& elementStorage);

    /// After layout: creates or updates an OS window for each declared window element.
    void SyncWindowsAfterLayout(OSWindowManager& windowManager, const WindowRegistry& windowRegistry,
                                ElementStorage& elementStorage, ITextSystem* textSystem, OnGuiFunc onGui);
    /// @}
}
