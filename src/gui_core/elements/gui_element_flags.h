#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_enum_operators.h>

namespace interstice
{
    // ------------------------------------------------------------------------
    // Element Flags
    // ------------------------------------------------------------------------

    enum class ElementFlags : uint32_t
    {
        None                      = 0,

        IsLabel                   = 1u << 0,
        IsControl                 = 1u << 1,

            HasText               = 1u << 2,
            HasIcon               = 1u << 3,
            DoNotMeasureText      = 1u << 4,

        IsWindow                  = 1u << 5,

            WindowCanMove         = 1u << 6,
            WindowCanResize       = 1u << 7,
            WindowUserControlled  = WindowCanMove | WindowCanResize,
            WindowNoCaption       = 1u << 8,
            WindowIsCentered      = 1u << 9,
            WindowIsTransparent   = 1u << 10,
            WindowIsModal         = 1u << 11,
            WindowIsPopup         = 1u << 12,
            WindowCloseWithParent = 1u << 13,
            WindowNotFocusable    = 1u << 14,
            WindowRenderToImage   = 1u << 18, // no OS window; see WindowFlags::RenderToImage
            WindowCustomTitleBar  = 1u << 19, // draws its own title bar; see WindowFlags::CustomTitleBar
            WindowConfirmClose    = 1u << 21, // closing it asks the app first: GUIApplication::CloseRequested

        Shadows                   = 1u << 15, // The window gets the OS's drop shadow (elements use CSS box-shadow)
        PropagatesDescendantState = 1u << 16, // Hover/Focused when any descendant is
        NoInput                   = 1u << 17, // Mouse hits pass through
        IsCachedScope             = 1u << 20, // BeginCached scope; render blits its cached primitives while clean
        SharesID                  = 1u << 22, // has an earlier element's ID on purpose (a part of it, e.g. a row's tree cell)

        IsMenuWindow = IsWindow | WindowUserControlled | WindowNoCaption | WindowIsPopup | Shadows
    };
    INTERSTICE_ENUM_OPERATORS(ElementFlags)

} // namespace interstice