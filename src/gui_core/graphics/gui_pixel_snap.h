#pragma once

// Pixel snapping of nested boxes. At fractional scales a 1-unit border or inset
// covers 1.5 pixels; rounding every edge to the absolute pixel grid makes such
// features 1 or 2 pixels depending on where they land. Snapping each edge
// relative to the parent's snapped edge keeps them the same everywhere, and
// content placed against a parent's border meets it exactly.
//
// Unsnapped rects are { x0, y0, x1, y1 } in window pixels.

#include <gui_core/gui_base_types.h>

#include <algorithm>
#include <cmath>

namespace GUI
{
    // Round half up, the same rule the box shader uses.
    inline int32_t SnapPx(float v) { return int32_t(std::floor(v + 0.5f)); }

    // A box with no snapped parent: each edge to the nearest pixel.
    inline IntRect SnapAbsolute(float4 px)
    {
        const int32_t x0 = SnapPx(px.x), y0 = SnapPx(px.y);
        return IntRect{ x0, y0, SnapPx(px.z) - x0, SnapPx(px.w) - y0 };
    }

    // The bounds of a rect mapped to window pixels, { x0, y0, x1, y1 }.
    inline float4 PixelBounds(const float3x2& toPixels, IntRect r)
    {
        const float2 a = toPixels.TransformPoint(float2{ float(r.x),       float(r.y) });
        const float2 b = toPixels.TransformPoint(float2{ float(r.x + r.w), float(r.y + r.h) });
        return float4{ std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) };
    }

    // A nested box: each edge keeps its rounded distance from the parent's
    // snapped edge on the same side.
    inline IntRect SnapToParent(float4 parentPx, IntRect parentSnapped, float4 childPx)
    {
        const int32_t x0 = parentSnapped.x + SnapPx(childPx.x - parentPx.x);
        const int32_t y0 = parentSnapped.y + SnapPx(childPx.y - parentPx.y);
        const int32_t x1 = parentSnapped.x + parentSnapped.w - SnapPx(parentPx.z - childPx.z);
        const int32_t y1 = parentSnapped.y + parentSnapped.h - SnapPx(parentPx.w - childPx.w);
        return IntRect{ x0, y0, x1 - x0, y1 - y0 };
    }
}
