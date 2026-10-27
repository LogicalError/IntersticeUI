#pragma once

#include <gui_core/gui_enum_operators.h>

namespace tachyon
{
    /// How an OS window is made and behaves (IWindowBackend::CreateWindow); flags combine with |.
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

        /// Blocks interaction with the other windows OSWindowManager manages.
        Modal           = 512,

        /// Stays on top without blocking other windows; closes when another (non-child) window gets focus.
        PopupMenu       = 1024,

        /// The OS drop shadow, where supported.
		Shadow          = 2048,

        /// Never takes keyboard focus (e.g. tooltips).
        NotFocusable    = 4096,

        /// No OS window: the window renders into an image the host reads or provides,
        /// and the host passes in its input (GUIApplication::Inject*).
        RenderToImage   = 8192,

        /// Closes with its parent window; without it the window becomes top-level
        /// when the parent closes.
        CloseWithParent = 16384,

        /// Keeps the OS frame (resizing, snapping, shadow) but the client area covers the
        /// title bar, which the window draws itself (GUIApplication::BeginTitleBar).
        CustomTitleBar  = 32768,

        /// A close from the OS (its close button, Alt+F4, quitting) is only recorded for the
        /// app to decide on (IWindowBackend::TakeCloseRequest); the window stays.
        ConfirmClose    = 65536,


		DefaultFlags = AllowHighDPI
    };
    TACHYON_ENUM_OPERATORS(WindowFlags)
};