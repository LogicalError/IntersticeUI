#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_enum_operators.h>

namespace GUI
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

        Shadows                   = 1u << 15, // Window or element casts a shadow on elements behind it
        PropagatesDescendantState = 1u << 16, // Hover/Focused when any descendant is
        NoInput                   = 1u << 17, // Mouse hits pass through
        IsCachedScope             = 1u << 20, // BeginCached scope; render blits its cached primitives while clean

        IsMenuWindow = IsWindow | WindowUserControlled | WindowNoCaption | WindowIsPopup | Shadows
    };
    ENUM_OPERATORS(ElementFlags)

} // namespace GUI