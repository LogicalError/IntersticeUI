#pragma once

#include <gui_controls/gui_node_view_types.h>   // NodeModel, NodeView

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace interstice
{
    class CustomDrawContext;
    struct LineStyle;

    // The node view's zoom (1 = 100%).
    float NodeViewZoom(const NodeView& view);
    // Zooms by `factor` around `pivot` (pixels from the canvas centre), within 10%..100%.
    void  ZoomNodeView(NodeView& view, float factor, int2 pivot = {});
    // Centres the view on canvas point `center` at `zoom` (clamped to 10%..100%).
    void  SetNodeViewCenter(NodeView& view, float2 center, float zoom);
    // Centres and zooms the view on all nodes (Home, the Fit button).
    void  FrameAllNodes(NodeView& view);

    // Infinite background dot grid for a pan/zoom canvas, drawn from a CustomDrawContext
    // callback in the canvas's input space (CANVAS_LAY): a dot on every grid point.
    // dotSize is the dot's diameter; 2 gives one crisp pixel. The pitch doubles per zoom-out octave, so on-screen density stays
    // roughly constant.
    //
    //   baseGridSize : pitch in CANVAS_LAY pixels at 1:1 zoom. <= 0 draws nothing.
    //   originOffset : a point the lattice passes through. Pass
    //                  ElementRect().position - ElementInnerRect().position to align
    //                  with children positioned relative to the border box.
    //   clipCurrent  : optional extra clip in the CURRENT space. The scissor is the
    //                  border box, so pass ElementInnerRect() to keep out of the padding.
    void RenderCanvasGrid(CustomDrawContext& ctx, int32_t baseGridSize,
                          color4f dotColor, float dotSize, int2 originOffset = {},
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
    // clamped so they never cross a body; laneOffset moves the unpinned corridor lane.
    // All coordinates share one space (CANVAS_LAY).
    WireRoute BuildWireRoute(int2 outPt, int2 inPt,
                             IntRect srcRect, IntRect tgtRect,
                             int32_t grid, int2 gridOrigin,
                             const WireRouteOverrides& overrides = {});

    // Stable key for a wire, identified by its target (input) endpoint — each input carries
    // at most one incoming wire. Keys the per-wire lane overrides and drag handles.
    constexpr uint32_t WireKey(int targetNode, int targetProp)
    {
        return ((uint32_t)targetNode << 16) | (uint32_t)(uint16_t)targetProp;
    }

    // The input a WireKey names.
    constexpr PropertyEndpoint WireInput(uint32_t wireKey)
    {
        return { int(wireKey >> 16), int(wireKey & 0xFFFFu) };
    }

    // The wire within `tolerance` of point `p` (the closest when several are); none when no
    // wire is. Points and tolerance share the wires' space.
    std::optional<uint32_t> WireAt(std::span<const DrawnWire> wires, int2 p, float tolerance);

    // Edits. The node view makes its changes through these, so what it reports is what it did.
    // Redoes `edit` (its `to` values), or with `undo` restores its `from` values.
    void ApplyNodeViewEdit(NodeView& view, const NodeViewEdit& edit, bool undo = false);
    // Applies `edit` and reports it to view.onEdit.
    void CommitNodeViewEdit(NodeView& view, const NodeViewEdit& edit);
    // Wires `output` into `input`; the input's old wire goes, the output keeps its others.
    NodeViewEdit ConnectEdit(const NodeView& view, PropertyEndpoint output, PropertyEndpoint input);
    // Removes the wire into `input`, with its lane overrides.
    NodeViewEdit DisconnectEdit(const NodeView& view, PropertyEndpoint input);

    // Default lane offsets (grid steps) for wires between the same two nodes, so their
    // lanes don't coincide: k wires, in input order, get (k-1)/2 counting down by one
    // (one wire: 0; two: 0,-1; three: 1,0,-1; four: 1,0,-1,-2). Keyed by WireKey.
    // A dragged wire (pendingOut -> pendingIn, both valid) counts as connected, under
    // pendingIn's key, replacing that input's wire.
    std::unordered_map<uint32_t, int32_t> ComputeWireLaneOffsets(const NodeView& view,
                                                                  PropertyEndpoint pendingOut = {},
                                                                  PropertyEndpoint pendingIn  = {});

    // A wire being dragged from an output: routed as the connection it would make when it is
    // over an input that accepts it (inPt on tgtRect), else to the cursor as if an input sat there.
    WireRoute BuildPendingWireRoute(int2 outPt, IntRect srcRect, int2 cursor,
                                    std::optional<int2> inPt, IntRect tgtRect,
                                    int32_t grid, int2 gridOrigin, int32_t laneOffset = 0);

    // Whether a wire from output `out` to input `in` is allowed: never a same-node loop,
    // otherwise the view's canConnect delegate decides (none: allowed). Endpoints are
    // bounds-checked first, so the delegate can index view.nodes directly.
    bool ConnectionAllowed(const NodeView& view, PropertyEndpoint out, PropertyEndpoint in);

    // The first input of node `nodeIdx`, top to bottom, that a wire from `output` may
    // connect to; invalid when there is none.
    PropertyEndpoint FirstInputAccepting(const NodeView& view, PropertyEndpoint output, int nodeIdx);

    // An input has one source, so its previous wire is replaced; the output keeps its others.
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
