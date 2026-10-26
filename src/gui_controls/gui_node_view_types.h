#pragma once

#include <gui_core/gui_base_types.h>     // int2, float3x2, color4f
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_command_manager.h>          // CommandMode
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
        // Left socket, rendered when has_value(); holds the source output feeding it (one at most).
        std::optional<PropertyEndpoint> input;

        // Right socket, rendered when true. It drives every input that names it as its source.
        bool output = false;

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
        // Category name, shown right-aligned in the header, and the color of the
        // header's category square (alpha 0 = the accent).
        std::string                category;
        interstice::color4f               categoryColor = {};
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
        PropertyEndpoint target        = {};   // this frame: the input it is over and may connect to
        std::optional<PropertyEndpoint> detachedInput; // the input the wire was pulled off, if any
    };

    // Interior lane a drag handle adjusts: the corridor route's vertical lane (LaneX), or the
    // detour route's verticals (ExitX, EntryX) and horizontal return lane (LaneY).
    enum class WireHandle : uint8_t { None, LaneX, ExitX, EntryX, LaneY };

    // A wire's place among the wires between the same two nodes, so their default lanes
    // nest instead of coinciding or crossing (ComputeWireLaneOffsets).
    struct WireLane
    {
        int32_t offset = 0;   // grid steps right for the corridor lane
        int32_t index  = 0;   // by input, top first
        int32_t count  = 1;   // wires in the group

        bool operator==(const WireLane&) const = default;
    };

    // User-pinned lanes of one wire (unset = default), in the route's space. BuildWireRoute
    // clamps them clear of the bodies, so stale values never cross a node.
    struct WireRouteOverrides
    {
        std::optional<int32_t> laneX;    // corridor vertical lane X
        std::optional<int32_t> exitX;    // detour vertical lane near the output
        std::optional<int32_t> entryX;   // detour vertical lane near the input
        std::optional<int32_t> laneY;    // detour horizontal return-lane Y
        WireLane               lane;     // where the unpinned lanes go in its group

        bool operator==(const WireRouteOverrides&) const = default;
    };

    // Draggable interior segment; vertical segments drag horizontally and vice versa.
    struct WireSegmentHandle
    {
        interstice::int2  a{}, b{}; // segment endpoints, same space as the route points
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
        interstice::int2  a{}, b{};
    };

    // One thing the user did in a node view, as values before and after, so a host can keep
    // the undo history (NodeView::onEdit) and undo or redo it (ApplyNodeViewEdit).
    struct NodeViewEdit
    {
        struct Move     { int node = -1; interstice::int2 from {}, to {}; };
        struct Rewire   { PropertyEndpoint input, from, to; };               // the input's source; invalid: none
        struct Collapse { int node = -1; bool from = false, to = false; };
        struct Lanes    { uint32_t wireKey = 0; WireRouteOverrides from, to; }; // default: no overrides

        std::vector<Move>     moves;
        std::vector<Rewire>   rewires;
        std::vector<Collapse> collapses;
        std::vector<Lanes>    lanes;

        bool empty() const { return moves.empty() && rewires.empty() && collapses.empty() && lanes.empty(); }
    };

    // A wire as drawn this frame, for hit-testing (route space, CANVAS_LAY content box).
    struct DrawnWire
    {
        uint32_t                      wireKey = 0;
        std::vector<interstice::int2> points;
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
        WireRouteOverrides lanesBefore {}; // the wire's overrides at drag start, for the edit
    };


    // All state of one node-view widget; no global state.
    // commandMode is active while the view has focus, for per-view shortcuts.
    struct NodeView
    {
        std::vector<NodeModel>  nodes;

        // Overlays over the canvas: a breadcrumb top-left (shown when non-empty; a click
        // on segment i calls onBreadcrumb(i)), zoom controls bottom-left, a minimap
        // bottom-right.
        std::vector<std::string>        breadcrumb;
        std::function<void(int)>        onBreadcrumb;
        bool                            showZoomControls = true;
        bool                            showMinimap      = true;

        // Last frame's minimap mapping (canvas = origin + minimap pixels / scale), for clicks.
        float                           minimapScale  = 0.0f;
        interstice::float2              minimapOrigin {};

        PendingConnection       pendingConnection;
        interstice::float3x2           transform;                  // identity = centered view (no pan/zoom)
        interstice::int2               lastInnerSize{ 0, 0 };      // for centering
        CommandMode             commandMode = CommandMode::Default;
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

        // The wire under the mouse (by WireKey), from the last frame: highlighted, removed by
        // a middle click, and offered for deletion by a right click.
        std::optional<uint32_t> hoveredWire;
        // Called once per finished user edit (a whole drag is one), after it is applied.
        std::function<void(const NodeViewEdit&)> onEdit;
        std::optional<uint32_t> wireMenuWire;          // the wire the open context menu is for
        interstice::int2        rightPressScreen {};   // a right press that pans isn't a click

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
