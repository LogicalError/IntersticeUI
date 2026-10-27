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

namespace tachyon
{
    /// @addtogroup controls_nodes
    /// @{

    /// A property on a node. Default-constructed is invalid: in a socket it means "present, unwired".
    struct PropertyEndpoint
    {
        int nodeIdx = -1;   ///< index into NodeView::nodes
        int propIdx = -1;   ///< index into that node's properties

        /// True when both indices are set.
        bool valid() const { return nodeIdx >= 0 && propIdx >= 0; }
        bool operator==(const PropertyEndpoint&) const = default;
    };

    /// One row of a node: an optional input socket, a body, and an optional output socket.
    struct NodeProperty
    {
        /// Left socket, rendered when has_value(); holds the source output feeding it (one at most).
        std::optional<PropertyEndpoint> input;

        /// Right socket, rendered when true. It drives every input that names it as its source.
        bool output = false;

        /// Row body, built between the two sockets; may call any widget (Label, Field, ...).
        std::function<void()> renderBody;

        /// Socket dot colour, also used for wires leaving this output.
        tachyon::color4f socketColor{ 0.9f, 0.9f, 0.9f, 1.0f };

        /// Row height in row pitches, for tall bodies (previews, multi-line). Whole pitches
        /// keep later sockets on the grid.
        int rowSpan = 1;
    };

    /// One node of a node view.
    struct NodeModel
    {
        /// Assigned once on creation; the node's element ID and the salt for its sub-IDs, so
        /// ID-keyed state survives z-order changes (dynamic IDs depend on render order).
        tachyon::ID                    stableID     = tachyon::ID::Invalid();

        std::string                title;        ///< header text
        /// Category name, shown right-aligned in the header.
        std::string                category;
        /// Color of the header's category square; alpha 0 uses the accent.
        tachyon::color4f               categoryColor = {};
        /// Top-left in canvas layout space (whole pixels relative to the canvas, before pan/zoom).
        tachyon::int2                  pos          = {};
        tachyon::int2                  startPos     = {};   ///< internal: pos at drag start
        tachyon::int2                  startScreen  = {};   ///< internal: cursor at drag start
        tachyon::int2                  lastSize     = {};   ///< outer-rect size from last frame's layout
        bool                       isDragging   = false;   ///< internal
        bool                       collapsed    = false;   ///< rows hidden, header only (double-click toggles)
        std::vector<NodeProperty>  properties;              ///< the rows, top to bottom
    };

    /// A wire being dragged. Committed wires are the reciprocal endpoints on NodeProperty.
    struct PendingConnection
    {
        bool             active        = false;  ///< a wire drag is in progress
        PropertyEndpoint source        = {};     ///< the output the wire starts from
        tachyon::int2        currentScreen = {};   ///< most recent mouse screen pos
        PropertyEndpoint target        = {};   ///< this frame: the input it is over and may connect to
        std::optional<PropertyEndpoint> detachedInput; ///< the input the wire was pulled off, if any
    };

    /// Interior lane a drag handle adjusts: the corridor route's vertical lane (LaneX), or the
    /// detour route's verticals (ExitX, EntryX) and horizontal return lane (LaneY).
    enum class WireHandle : uint8_t { None, LaneX, ExitX, EntryX, LaneY };

    /// A wire's place among the wires between the same two nodes, so their default lanes
    /// nest instead of coinciding or crossing (ComputeWireLaneOffsets).
    struct WireLane
    {
        int32_t offset = 0;   ///< grid steps right for the corridor lane
        int32_t index  = 0;   ///< by input, top first
        int32_t count  = 1;   ///< wires in the group

        bool operator==(const WireLane&) const = default;
    };

    /// User-pinned lanes of one wire (unset = default), in the route's space. BuildWireRoute
    /// clamps them clear of the bodies, so stale values never cross a node.
    struct WireRouteOverrides
    {
        std::optional<int32_t> laneX;    ///< corridor vertical lane X
        std::optional<int32_t> exitX;    ///< detour vertical lane near the output
        std::optional<int32_t> entryX;   ///< detour vertical lane near the input
        std::optional<int32_t> laneY;    ///< detour horizontal return-lane Y
        WireLane               lane;     ///< where the unpinned lanes go in its group

        bool operator==(const WireRouteOverrides&) const = default;
    };

    /// Draggable interior segment; vertical segments drag horizontally and vice versa.
    struct WireSegmentHandle
    {
        tachyon::int2  a{}, b{}; ///< segment endpoints, same space as the route points
        WireHandle role     = WireHandle::None;  ///< which lane dragging it changes
        bool       vertical = false;
    };

    /// Polyline plus its draggable interior segments (never the end stubs).
    struct WireRoute
    {
        std::vector<tachyon::int2>         points;   ///< the polyline, output to input
        std::vector<WireSegmentHandle> handles;
    };

    /// Last frame's draggable segment (node.pos space), for laying out its grab element.
    /// `wireKey` identifies the wire by its target endpoint.
    struct CachedWireHandle
    {
        uint32_t   wireKey  = 0;
        WireHandle role     = WireHandle::None;
        bool       vertical = false;
        tachyon::int2  a{}, b{};
    };

    /// One thing the user did in a node view, as values before and after, so a host can keep
    /// the undo history (NodeView::onEdit) and undo or redo it (ApplyNodeViewEdit).
    struct NodeViewEdit
    {
        /// A node (index) moved; positions in node.pos space.
        struct Move     { int node = -1; tachyon::int2 from {}, to {}; };
        /// An input's source changed; an invalid endpoint means unwired.
        struct Rewire   { PropertyEndpoint input, from, to; };
        /// A node (index) collapsed or expanded.
        struct Collapse { int node = -1; bool from = false, to = false; };
        /// A wire's (by WireKey) lane overrides changed; default means no overrides.
        struct Lanes    { uint32_t wireKey = 0; WireRouteOverrides from, to; };

        std::vector<Move>     moves;
        std::vector<Rewire>   rewires;
        std::vector<Collapse> collapses;
        std::vector<Lanes>    lanes;

        /// True when nothing changed.
        bool empty() const { return moves.empty() && rewires.empty() && collapses.empty() && lanes.empty(); }
    };

    /// A wire as drawn this frame, for hit-testing; points in node.pos space.
    struct DrawnWire
    {
        uint32_t                      wireKey = 0;
        std::vector<tachyon::int2> points;
    };

    /// Segment drag via mouse capture rather than DnD, so the resize cursor sticks.
    struct WireSegmentDrag
    {
        bool       active     = false;
        uint32_t   wireKey    = 0;
        WireHandle role       = WireHandle::None;
        bool       vertical   = false;
        tachyon::int2  startScreen = {};
        int32_t    startValue  = 0;   ///< lane coordinate at drag start (node.pos space)
        WireRouteOverrides lanesBefore {}; ///< the wire's overrides at drag start, for the edit
    };


    /// All state of one node-view widget; no global state.
    /// commandMode is active while the view has focus, for per-view shortcuts.
    struct NodeView
    {
        std::vector<NodeModel>  nodes;   ///< the graph; endpoints and edits refer to nodes by index

        /// Breadcrumb overlay at the canvas top-left, shown when non-empty.
        std::vector<std::string>        breadcrumb;
        std::function<void(int)>        onBreadcrumb;   ///< called with the clicked segment index
        bool                            showZoomControls = true;   ///< zoom overlay, bottom-left
        bool                            showMinimap      = true;   ///< minimap overlay, bottom-right

        /// Last frame's minimap mapping (canvas = origin + minimap pixels / scale), for clicks.
        float                           minimapScale  = 0.0f;
        tachyon::float2              minimapOrigin {};

        PendingConnection       pendingConnection;   ///< internal: the wire being dragged
        tachyon::float3x2           transform;                  ///< identity = centered view (no pan/zoom)
        tachyon::int2               lastInnerSize{ 0, 0 };      ///< last frame's canvas size, for centering
        /// Command mode set while the canvas has focus.
        CommandMode             commandMode = CommandMode::Default;
        /// Z-order, last == front (a clicked node moves there).
        std::vector<int>        ordering;

        /// Selected node stableIDs.
        std::unordered_set<tachyon::ID>     selectedNodeIDs;
        /// StableID of the last clicked node.
        tachyon::ID                         lastClickedNodeID = tachyon::ID::Invalid();

        /// Double-click-to-collapse, per view so views (and test frames) don't share it.
        DoubleClickTracker              nodeDoubleClick;

        /// A marquee drag from the empty background is in progress; its points are screen space.
        bool                            marqueeActive        = false;
        bool                            marqueePastThreshold = false;
        tachyon::int2                       marqueeStartScreen   {};
        tachyon::int2                       marqueeCurrentScreen {};
        std::unordered_set<tachyon::ID>     marqueeBaseSelection;     ///< selection snapshot before the drag
        bool                            marqueeShift = false;     ///< modifier snapshot at start
        bool                            marqueeCtrl  = false;

        /// Pinned wire lanes, keyed by WireKey() of the wire's target endpoint.
        std::unordered_map<uint32_t, WireRouteOverrides> wireOverrides;
        /// Last frame's draggable segments, since grab elements are laid out before routing.
        std::vector<CachedWireHandle>                    wireHandleCache;
        WireSegmentDrag                                  wireDrag;

        /// The wire under the mouse (by WireKey), from the last frame: highlighted, removed by
        /// a middle click, and offered for deletion by a right click.
        std::optional<uint32_t> hoveredWire;
        /// Called once per finished user edit (a whole drag is one), after it is applied.
        std::function<void(const NodeViewEdit&)> onEdit;
        std::optional<uint32_t> wireMenuWire;          ///< the wire the open context menu is for
        tachyon::int2        rightPressScreen {};   ///< a right press that pans isn't a click

        /// Whether an output -> input wire is allowed; null allows any. Called only for
        /// bounds-checked, cross-node endpoints, so it may index view.nodes directly.
        std::function<bool(const NodeView& view,
                           PropertyEndpoint output,
                           PropertyEndpoint input)> canConnect;

        /// Packed RGBA -> registered color style, since RenderPolyline takes a style index.
        std::unordered_map<uint32_t, tachyon::VisualColorStyleIndex> wireColorStyles;
        uint32_t wireColorStylesGeneration = 0; ///< StyleManager::Generation() of the cache
    };
    /// @}
}
