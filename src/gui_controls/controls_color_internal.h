#pragma once

#include <gui_core/gui_base_types.h>                     // color4f
#include <gui_core/style/gui_style.h>                    // ColorStyle
#include <gui_core/style/gui_style_manager.h>            // StyleManager
#include <gui_core/graphics/gui_custom_draw_context.h>   // VisualColorStyleIndex

// Helpers shared by the color picker (controls_color.cpp) and gradient editor (controls_gradient.cpp).

namespace interstice
{
    // Create or update a VisualColorStyleIndex with a solid color.
    inline void SetColorStyle(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx, interstice::color4f c)
    {
        interstice::ColorStyle cs = interstice::ColorStyle::Create(c);
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }
}
