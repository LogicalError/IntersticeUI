#pragma once

#include <gui_controls/gui_node_view_types.h>   // NodeModel, NodeView

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>

#include <span>
#include <unordered_set>
#include <vector>

namespace interstice
{
    class CustomDrawContext;
    struct LineStyle;

    // Infinite background grid for a pan/zoom canvas, drawn from a CustomDrawContext
    // callback in the canvas's input space (CANVAS_LAY). The pitch doubles per zoom-out
    // octave, so on-screen density stays roughly constant.
    //
    //   baseGridSize : pitch in CANVAS_LAY pixels at 1:1 zoom. <= 0 draws nothing.
    //   originOffset : a point the lattice passes through. Pass
    //                  ElementRect().position - ElementInnerRect().position to align
    //                  with children positioned relative to the border box.
    //   clipCurrent  : optional extra clip in the CURRENT space. The scissor is the
    //                  border box, so pass ElementInnerRect() to keep out of the padding.
    void RenderCanvasGrid(CustomDrawContext& ctx, int32_t baseGridSize,
                          const LineStyle& style, int2 originOffset = {},
                          IntRect clipCurrent = {});

    // --- Canvas grid lattice ----------------------------------------------
    // Lines fall on origin + k*grid; snap to the line at-or-below / at-or-above /
    // nearest a value. grid must be > 0.
    constexpr int32_t FloorDiv(int32_t a, int32_t b)
    {
        int32_t q = a / b, r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) --q;
        return q;
    }
    constexpr int32_t GridLineLE(int32_t v, int32_t origin, int32_t grid)
    {
        return origin + FloorDiv(v - origin, grid) * grid;
    }
    constexpr int32_t GridLineGE(int32_t v, int32_t origin, int32_t grid)
    {
        const int32_t d = GridLineLE(v, origin, grid);
        return (d == v) ? d : d + grid;
    }
    constexpr int32_t GridLineNearest(int32_t v, int32_t origin, int32_t grid)
    {
        const int32_t d = GridLineLE(v, origin, grid);
        return ((v - d) * 2 <= grid) ? d : d + grid;
    }

    // Orthogonal route for a wire from an output socket (outPt, srcRect's right edge) to
    // an input socket (inPt, tgtRect's left edge), with bends on the grid and clear of both bodies:
    //   - a horizontal corridor between the bodies -> one vertical lane in it;
    //   - else a vertical gap between them          -> detour through the gap;
    //   - else (side-by-side)                       -> around the nearer outside edge.
    // grid <= 0 or an empty rect gives a plain elbow. `overrides` pin interior lanes,
    // clamped so they never cross a body. All coordinates share one space (CANVAS_LAY).
    WireRoute BuildWireRoute(int2 outPt, int2 inPt,
                             IntRect srcRect, IntRect tgtRect,
                             int32_t grid, int2 gridOrigin,
                             const WireRouteOverrides& overrides = {});

    // Single-target on both ends: existing connections of either socket are dropped first.
    void ConnectSockets(NodeView& view, PropertyEndpoint output, PropertyEndpoint input);

    // Moves `n` to the front (last) of a z-order list, keeping the others' order.
    void BringNodeToFront(std::vector<int>& ordering, int n);

    // Stable IDs of nodes whose {pos, lastSize} rect lies fully inside the marquee, compared
    // in CANVAS_LAY (canvas inner-rect relative, pre-contentTransform, like NodeModel::pos).
    // Nodes not laid out yet (zero lastSize) are skipped.
    //
    //   canvasInnerRectScreen : canvas padding box, SCREEN pixels.
    //   viewTransform         : the pan/zoom part of the canvas's contentTransform
    //                           (after T(innerSize/2)).
    //   marqueeStartScreen,
    //   marqueeCurrentScreen  : marquee corners, SCREEN pixels.
    std::unordered_set<ID> ComputeNodesInMarquee(
        std::span<const NodeModel> nodes,
        IntRect                    canvasInnerRectScreen,
        const float3x2&            viewTransform,
        int2                       marqueeStartScreen,
        int2                       marqueeCurrentScreen);
}
