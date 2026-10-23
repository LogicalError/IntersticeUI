#pragma once

#include <gui_core/gui_enum_operators.h>

namespace interstice
{
    enum class WindowFlags : uint32_t
    {
        None = 0,

        AllowHighDPI    = 1,
        Hidden          = 2,
        Borderless      = 4,
        Moveable        = 8,
        Resizable       = 16,
        UserControlled  = Moveable | Resizable,
        AlwaysOnTop     = 32,
        SkipTaskbar     = 64,

        Utility         = 128,
        Transparent     = 256,

        // Modal windows block interaction with other windows managed by OSWindowManager
        Modal           = 512,

        // Stays on top without blocking other windows; closes when another (non-child) window gets focus.
        PopupMenu       = 1024,

        // Request window shadow (if supported by OS)
		Shadow          = 2048,

        // Window cannot receive keyboard focus (e.g. tooltips)
        NotFocusable    = 4096,

        // No OS window: the window renders into an image the host reads or provides,
        // and the host passes in its input (GUIApplication::Inject*).
        RenderToImage   = 8192,


		DefaultFlags = AllowHighDPI
    };
    INTERSTICE_ENUM_OPERATORS(WindowFlags)
};