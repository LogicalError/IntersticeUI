#pragma once

#include <gui_core/gui_base_types.h>     // int2, float3x2, color4f
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_command_manager.h>          // EditMode
#include <gui_core/graphics/gui_custom_draw_context.h>   // VisualColorStyleIndex

#include <gui_controls/gui_widget_state_types.h>          // DoubleClickTracker

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Node-view (graph editor) data model. The caller owns one NodeView per view.

namespace interstice
{
    // A property on a node. Default-constructed is invalid: in a socket it means "present, unwired".
    struct PropertyEndpoint
    {
        int nodeIdx = -1;
        int propIdx = -1;

        bool valid() const { return nodeIdx >= 0 && propIdx >= 0; }
        bool operator==(const PropertyEndpoint&) const = default;
    };

    struct NodeProperty
    {
        // Left socket, rendered when has_value(); holds the source output feeding it.
        std::optional<PropertyEndpoint> input;

        // Right socket. Same conventions as `input`; the contained endpoint
        // is the target input this output drives. One target only (#66).
        std::optional<PropertyEndpoint> output;

        // Row body, built between the two sockets; may call any widget (Label, Field, ...).
        std::function<void()> renderBody;

        // Socket dot colour, also used for wires leaving this output.
        interstice::color4f socketColor{ 0.9f, 0.9f, 0.9f, 1.0f };

        // Row height in row pitches, for tall bodies (previews, multi-line). Whole pitches
        // keep later sockets on the grid.
        int rowSpan = 1;
    };

    struct NodeModel
    {
        // Assigned once on creation; the node's element ID and the salt for its sub-IDs, so
        // ID-keyed state survives z-order changes (dynamic IDs depend on render order).
        interstice::ID                    stableID     = interstice::ID::Invalid();

        std::string                title;
        interstice::color4f               headerColor  = {};   // Blender-style per-type header tint; alpha 0 = theme default
        interstice::int2                  pos          = {};
        interstice::int2                  startPos     = {};
        interstice::int2                  startScreen  = {};
        interstice::int2                  lastSize     = {};   // outer-rect size from last frame's layout
        bool                       isDragging   = false;
        bool                       collapsed    = false;
        std::vector<NodeProperty>  properties;
    };

    // A wire being dragged. Committed wires are the reciprocal endpoints on NodeProperty.
    struct PendingConnection
    {
        bool             active        = false;
        PropertyEndpoint source        = {};
        interstice::int2        currentScreen = {};   // most recent mouse screen pos
    };

    // Interior lane a drag handle adjusts: the corridor route's vertical lane (LaneX), or the
    // detour route's verticals (ExitX, EntryX) and horizontal return lane (LaneY).
    enum class WireHandle : uint8_t { None, LaneX, ExitX, EntryX, LaneY };

    // User-pinned lanes of one wire (unset = default), in the route's space. BuildWireRoute
    // clamps them clear of the bodies, so stale values never cross a node.
    struct WireRouteOverrides
    {
        std::optional<int32_t> laneX;    // corridor vertical lane X
        std::optional<int32_t> exitX;    // detour vertical lane near the output
        std::optional<int32_t> entryX;   // detour vertical lane near the input
        std::optional<int32_t> laneY;    // detour horizontal return-lane Y
    };

    // Draggable interior segment; vertical segments drag horizontally and vice versa.
    struct WireSegmentHandle
    {
        interstice::int2  a, b;     // segment endpoints, same space as the route points
        WireHandle role     = WireHandle::None;
        bool       vertical = false;
    };

    // Polyline plus its draggable interior segments (never the end stubs).
    struct WireRoute
    {
        std::vector<interstice::int2>         points;
        std::vector<WireSegmentHandle> handles;
    };

    // Last frame's draggable segment (node.pos space), for laying out its grab element.
    // `wireKey` identifies the wire by its target endpoint.
    struct CachedWireHandle
    {
        uint32_t   wireKey  = 0;
        WireHandle role     = WireHandle::None;
        bool       vertical = false;
        interstice::int2  a, b;
    };

    // Segment drag via mouse capture rather than DnD, so the resize cursor sticks.
    struct WireSegmentDrag
    {
        bool       active     = false;
        uint32_t   wireKey    = 0;
        WireHandle role       = WireHandle::None;
        bool       vertical   = false;
        interstice::int2  startScreen = {};
        int32_t    startValue  = 0;   // lane coordinate at drag start (node.pos space)
    };


    // All state of one node-view widget; no global state.
    // editMode is registered while the view has focus, for per-view shortcuts.
    struct NodeView
    {
        std::vector<NodeModel>  nodes;
        PendingConnection       pendingConnection;
        interstice::float3x2           transform;                  // identity = centered view (no pan/zoom)
        interstice::int2               lastInnerSize{ 0, 0 };      // for centering
        EditMode                editMode  = EditMode::Default;
        std::vector<int>        ordering;                   // z-order, last == front (a clicked node moves there)

        // Selected node stableIDs.
        std::unordered_set<interstice::ID>     selectedNodeIDs;
        interstice::ID                         lastClickedNodeID = interstice::ID::Invalid();

        // Double-click-to-collapse, per view so views (and test frames) don't share it.
        DoubleClickTracker              nodeDoubleClick;

        // Marquee drag from the empty background; start/current in SCREEN space.
        bool                            marqueeActive        = false;
        bool                            marqueePastThreshold = false;
        interstice::int2                       marqueeStartScreen   {};
        interstice::int2                       marqueeCurrentScreen {};
        std::unordered_set<interstice::ID>     marqueeBaseSelection;     // selection snapshot before the drag
        bool                            marqueeShift = false;     // modifier snapshot at start
        bool                            marqueeCtrl  = false;

        // Wire lane editing. wireOverrides is keyed by the target endpoint as (nodeIdx<<16)|propIdx;
        // wireHandleCache is last frame's segments, since grab elements are laid out before routing.
        std::unordered_map<uint32_t, WireRouteOverrides> wireOverrides;
        std::vector<CachedWireHandle>                    wireHandleCache;
        WireSegmentDrag                                  wireDrag;

        // Whether an output -> input wire is allowed; null allows any. Called only for
        // bounds-checked, cross-node endpoints, so it may index view.nodes directly.
        std::function<bool(const NodeView& view,
                           PropertyEndpoint output,
                           PropertyEndpoint input)> canConnect;

        // Packed RGBA -> registered color style, since RenderPolyline takes a style index.
        std::unordered_map<uint32_t, interstice::VisualColorStyleIndex> wireColorStyles;
        uint32_t wireColorStylesGeneration = 0; // StyleManager::Generation() of the cache
    };
}
