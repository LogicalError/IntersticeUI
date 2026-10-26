#include <gui_controls/gui_application.h>
#include <gui_controls/gui_node_view_helpers.h>
#include <cmath>
#include <algorithm>
#include <utility>
#include <vector>

//
// Node-view widget. RenderNodeView(NodeView&) renders one graph editor; the
// NodeView owns nodes, pan/zoom, z-order and drag state. Nothing is global.
//

namespace interstice
{

    namespace
    {
        // Drag payload type for socket-to-socket connections (must be unique).
        constexpr uint32_t kSocketPayload = 0x534f434b; // "SOCK"

        constexpr uintptr_t EncodeEndpoint(PropertyEndpoint e)
        {
            return ((uintptr_t)(uint32_t)e.nodeIdx << 16) | (uint32_t)(uint16_t)e.propIdx;
        }

        constexpr PropertyEndpoint DecodeEndpoint(uintptr_t v)
        {
            return PropertyEndpoint{ (int)(int16_t)(v >> 16), (int)(int16_t)(v & 0xFFFF) };
        }

        // Pack a colour to an 8-bit-per-channel key, for the wire-colour cache.
        uint32_t PackColor(const interstice::color4f& c)
        {
            auto q = [](float f) -> uint32_t
            {
                const int v = (int)(f * 255.0f + 0.5f);
                return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            };
            return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
        }

        // Round a (possibly negative) coordinate to the nearest multiple of grid.
        constexpr int32_t SnapToGrid(int32_t v, int32_t grid)
        {
            if (grid <= 0) return v;
            const int32_t q = (v >= 0) ? (v + grid / 2) / grid
                                       : (v - grid / 2) / grid;
            return q * grid;
        }

        // Connects the dragged wire. Pulled off an input and dropped elsewhere, the detach
        // and the connect are one edit; dropped back where it was, nothing changed.
        void FinishConnection(NodeView& view, PropertyEndpoint output, PropertyEndpoint input)
        {
            const std::optional<PropertyEndpoint> detached = view.pendingConnection.detachedInput;
            view.pendingConnection = {};
            NodeViewEdit edit = ConnectEdit(view, output, input);
            ApplyNodeViewEdit(view, edit);
            if (detached && *detached == input)
                return;
            if (detached)
                edit.rewires.insert(edit.rewires.begin(), NodeViewEdit::Rewire{ *detached, output, PropertyEndpoint{} });
            if (view.onEdit && !edit.empty())
                view.onEdit(edit);
        }

        // The dragged wire was released nowhere: pulled off an input, it is a disconnect.
        void CancelConnection(NodeView& view)
        {
            const PendingConnection pending = view.pendingConnection;
            view.pendingConnection = {};
            if (pending.detachedInput && view.onEdit)
            {
                NodeViewEdit edit;
                edit.rewires.push_back({ *pending.detachedInput, pending.source, PropertyEndpoint{} });
                view.onEdit(edit);
            }
        }

    }


    void GUIApplication::NodeText(std::string_view text)
    {
        // Alignment is baked into the style (text-align); flex-grow makes the
        // text box span the row body so centre/right are visible. nodeRowTextAlign
        // is set by RenderNodeView from the row's sockets before renderBody runs.
        StyleRef style = theme.nodeTextLeftStyleRef;
        switch (nodeRowTextAlign)
        {
            case TextAlign::Center: style = theme.nodeTextCenterStyleRef; break;
            case TextAlign::End:    style = theme.nodeTextRightStyleRef;  break;
            default:                style = theme.nodeTextLeftStyleRef;   break;
        }
        Text(text, style, LayoutOverride{}.FlexGrow(1));
    }


    void GUIApplication::RenderNodeView(NodeView& view)
    {
        // Coordinate spaces inside the node-view (read this before touching marquee math):
        //
        //   SCREEN     : absolute pixels on the OS window. Mouse events (event.state.screenPos)
        //                and ElementRect rects (event.rect->innerRect.position/size) are here.
        //   CANVAS_LAY : "canvas-local LAYOUT" - pixels relative to the canvas's inner-rect
        //                top-left, pre-contentTransform. This is the space `node.pos` lives in.
        //                In custom-draws it is the default (DrawSpace::Local) space:
        //                ctx.GetElementRect(ref) returns CANVAS_LAY, Render* takes CANVAS_LAY
        //                input (the framework applies the canvas pan/zoom at draw), and
        //                ctx.ScreenToCurrent(screenPos) converts a SCREEN input into CANVAS_LAY.
        //
        // Event handlers (which have no custom-draw context) convert by hand using
        // view.transform plus the canvas-center offset in canvas.contentTransform
        // (= T(innerSize/2) * view.transform):
        //
        //   CANVAS_LAY = view.transform.Inverse()(SCREEN - innerRect.position - innerSize/2)
        //   SCREEN     = innerRect.position + innerSize/2 + view.transform(CANVAS_LAY)
        //
        // A drag that ended without a Drop on us (e.g. released in empty space) leaves
        // a stale pending wire; clear it.
        if (view.pendingConnection.active && !guiStateManager.Drag().IsDragging())
            CancelConnection(view);
        view.pendingConnection.target = {};

        ID canvasID = idManager.AllocateDynamicID();
        const ID wireMenuID = idManager.AllocateDynamicID("wire-menu");   // owns the wire context menu

        // Edit-mode wiring: while this canvas has focus, set the command
        // manager to the view's mode; restore Default when focus leaves.
        if (guiStateManager.Interaction().GetFocusedID() == canvasID)
        {
            if (commandManager.GetMode() != view.commandMode)
                commandManager.SetMode(view.commandMode);
        } else
        if (commandManager.GetMode() == view.commandMode)
            commandManager.SetMode(CommandMode::Default);

        // ---- Canvas: pan/zoom + focus capture. Everything below renders as
        //      children of this element under the view's content transform.
        interstice::Element canvas
        {
            .id = canvasID,
            .eventHandler = [this, &view, canvasID, wireMenuID](ElementEvent&& event)->bool
            {
                switch (event.type)
                {
                    case interstice::EventType::MouseDown:
                    {
                        // A middle click removes the wire under the mouse.
                        if (event.state.MiddlePressed() && view.hoveredWire)
                        {
                            CommitNodeViewEdit(view, DisconnectEdit(view, WireInput(*view.hoveredWire)));
                            view.hoveredWire.reset();
                            return true;
                        }
                        if (!event.state.LeftDown() && !event.state.RightDown())
                            return false;
                        this->guiStateManager.Interaction().SetFocusID(canvasID);
                        if (event.state.RightPressed())
                            view.rightPressScreen = event.state.screenPos;
                        if (event.state.LeftPressed())
                        {
                            // Begin marquee. Only SCREEN positions are captured here; the
                            // screen->CANVAS_LAY conversion is done from the LIVE canvas
                            // rect each MouseMove (see screenToCanvasLay below).
                            view.marqueeActive        = true;
                            view.marqueePastThreshold = false;
                            view.marqueeStartScreen   = event.state.screenPos;
                            view.marqueeCurrentScreen = event.state.screenPos;
                            view.marqueeBaseSelection = view.selectedNodeIDs;
                            view.marqueeShift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                            view.marqueeCtrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                        }
                        return true;
                    }
                    case interstice::EventType::MouseMove:
                    {
                        if (event.state.RightDown())
                        {
                            this->guiStateManager.Interaction().SetFocusID(canvasID);
                            interstice::float3x2 translate = interstice::float3x2::Translate(event.state.screenDelta);
                            view.transform = translate * view.transform;
                            return true;
                        }
                        if (view.marqueeActive && event.state.LeftDown())
                        {
                            view.marqueeCurrentScreen = event.state.screenPos;
                            const int2 deltaScreen = view.marqueeCurrentScreen - view.marqueeStartScreen;
                            if (!view.marqueePastThreshold &&
                                (std::abs(deltaScreen.x) > styleManager.Px(kDragStartDistance) ||
                                 std::abs(deltaScreen.y) > styleManager.Px(kDragStartDistance)))
                                view.marqueePastThreshold = true;
                            if (view.marqueePastThreshold)
                            {
                                // Select nodes fully inside the marquee (CANVAS_LAY), using the
                                // live canvas rect in case the panel moved since MouseDown.
                                const std::unordered_set<ID> hit = ComputeNodesInMarquee(
                                    view.nodes,
                                    event.rect->innerRect,
                                    view.transform,
                                    view.marqueeStartScreen,
                                    view.marqueeCurrentScreen);

                                if (view.marqueeShift)
                                {
                                    view.selectedNodeIDs = view.marqueeBaseSelection;
                                    for (ID id : hit) view.selectedNodeIDs.insert(id);
                                }
                                else if (view.marqueeCtrl)
                                {
                                    view.selectedNodeIDs = view.marqueeBaseSelection;
                                    for (ID id : hit)
                                    {
                                        auto it = view.selectedNodeIDs.find(id);
                                        if (it != view.selectedNodeIDs.end()) view.selectedNodeIDs.erase(it);
                                        else                                  view.selectedNodeIDs.insert(id);
                                    }
                                }
                                else
                                {
                                    view.selectedNodeIDs = std::move(hit);
                                }
                            }
                            return true;
                        }
                        return false;
                    }
                    case interstice::EventType::MouseUp:
                    {
                        // A right click (not a pan) on a wire offers to delete it.
                        if (event.state.RightReleased() && view.hoveredWire)
                        {
                            const int2 moved = event.state.screenPos - view.rightPressScreen;
                            const int32_t slack = styleManager.Px(kDragStartDistance);
                            if (std::abs(moved.x) <= slack && std::abs(moved.y) <= slack)
                            {
                                view.wireMenuWire = view.hoveredWire;
                                OpenContextMenu(wireMenuID, event.state.screenPos);
                                return true;
                            }
                        }
                        if (view.marqueeActive && event.state.LeftReleased())
                        {
                            if (!view.marqueePastThreshold &&
                                !view.marqueeShift && !view.marqueeCtrl)
                            {
                                // Bare click on empty background - clear selection.
                                view.selectedNodeIDs.clear();
                            }
                            view.marqueeActive = false;
                            return true;
                        }
                        return false;
                    }
                    case interstice::EventType::MouseScroll:
                    {
                        if (event.state.scroll.y == 0) return false;
                        this->guiStateManager.Interaction().SetFocusID(canvasID);

                        constexpr float zoomStep = 1.1f;
                        const int2 innerSize{ event.rect->innerRect.w, event.rect->innerRect.h };
                        ZoomNodeView(view, (event.state.scroll.y > 0) ? zoomStep : (1.0f / zoomStep),
                                     event.state.screenPos - event.rect->innerRect.position
                                     - int2{ innerSize.x / 2, innerSize.y / 2 });
                        return true;
                    }
                    case interstice::EventType::KeyDown:
                    {
                        if (event.state.lastKey != interstice::KeyCode::Home || view.nodes.empty())
                            return false;

                        // Home = frame all nodes.
                        FrameAllNodes(view);
                        return true;
                    }
                }
                return false;
            },
            .textID   = TextID::Invalid,
            .styleRef = theme.nodeViewStyleRef,
            .state    = ElementState::None
        };

        auto canvasRes = elementStorage.Add(std::move(canvas), LayoutOverride{}.Auto());
        VERIFY(canvasRes.status);
        const interstice::float3x2 effective =
            interstice::float3x2::Translate(interstice::float2{ (float)(view.lastInnerSize.x / 2),
                                                  (float)(view.lastInnerSize.y / 2) })
            * view.transform;
        elementStorage.SetContentTransform(canvasRes.ref, effective);
        VERIFY(windowRegistry.Add(canvasRes.ref));
        VERIFY(elementStorage.BeginChildren());

        const int nodeCount = (int)view.nodes.size();

        // Z-order: last == front. Keep stable across frames; if the node count
        // changed (nodes added / removed), rebuild from scratch.
        if ((int)view.ordering.size() != nodeCount)
        {
            view.ordering.clear();
            view.ordering.reserve(nodeCount);
            for (int i = 0; i < nodeCount; ++i) view.ordering.push_back(i);
        }

        // Cache a colour style per socket colour so wires can use their source socket's
        // colour (RenderPolyline takes a style index). A theme reload invalidates the cache.
        if (view.wireColorStylesGeneration != styleManager.Generation())
        {
            view.wireColorStyles.clear();
            view.wireColorStylesGeneration = styleManager.Generation();
        }
        for (const auto& cnode : view.nodes)
            for (const auto& cprop : cnode.properties)
            {
                const uint32_t ckey = PackColor(cprop.socketColor);
                if (view.wireColorStyles.find(ckey) == view.wireColorStyles.end())
                    view.wireColorStyles[ckey] =
                        styleManager.RegisterColorStyle(interstice::ColorStyle::Create(cprop.socketColor));
            }

        // Socket box size, from the .node-socket CSS width; dot and border insets derive from it.
        const int kSocketSize = theme.socketSize;

        // Per-frame socket refs for the wire pass. Index: [nodeIdx][propIdx * 2 + isOutput]
        std::vector<std::vector<ElementRef>> socketRefs(nodeCount);
        for (int n = 0; n < nodeCount; ++n)
            socketRefs[n].assign(view.nodes[n].properties.size() * 2, ElementRef{});

        // Per-frame node refs; the wire pass routes around the node bodies.
        std::vector<ElementRef> nodeRefs(nodeCount);

        // Outputs that drive at least one input, by WireKey of the output's endpoint.
        std::unordered_set<uint32_t> drivenOutputs;
        for (const auto& cnode : view.nodes)
            for (const auto& cprop : cnode.properties)
                if (cprop.input.has_value() && cprop.input->valid())
                    drivenOutputs.insert(WireKey(cprop.input->nodeIdx, cprop.input->propIdx));

        // -------- Helper: emit one socket Element (circle + drag/drop handler) --------
        auto emitSocket = [&](int n, int propIdx, bool isOutput, bool readonly,
                              LayoutOverride&& layout) -> ElementRef
        {
            ID sockID = idManager.AllocateDynamicID("node-sock");

            PropertyEndpoint self { n, propIdx };

            Element sockElem
            {
                .id = sockID,
                .eventHandler = [this, &view, self, isOutput, readonly, sockID](ElementEvent&& event)->bool
                {
                    if (readonly) return false;

                    if (isOutput)
                    {
                        switch (event.type)
                        {
                            case EventType::MouseDown:
                            {
                                if (!event.state.LeftPressed()) return false;
                                // Arm the drag; pendingConnection activates only once it passes the threshold.
                                guiStateManager.Drag().Begin(
                                    sockID, kSocketPayload, EncodeEndpoint(self),
                                    event.state.screenPos, std::any(),
                                    DragVisualizer{});
                                return true;
                            }
                            case EventType::MouseMove:
                            {
                                if ((event.state.buttons & MouseButtons::Left) == MouseButtons::None) break;
                                guiStateManager.Drag().UpdateFromInput(event.state);
                                // Post-threshold: the pending wire is real and tracks the cursor.
                                if (guiStateManager.Drag().GetDraggedID() == sockID)
                                {
                                    view.pendingConnection.active        = true;
                                    view.pendingConnection.source        = self;
                                    view.pendingConnection.currentScreen = event.state.screenPos;
                                    return true;
                                }
                                return false;
                            }
                            case EventType::MouseUp:
                            {
                                // Cancel (e.g. released on the source); a target's Drop clears it too.
                                CancelConnection(view);
                                return true;
                            }
                            default: break;
                        }
                        return false;
                    }
                    else
                    {
                        switch (event.type)
                        {
                            case EventType::MouseDown:
                            {
                                // Detach: a wired input re-drags the wire from its source output
                                // (rewire elsewhere, or disconnect in empty space). The detach
                                // happens on the first real move, so a plain click is harmless.
                                if (!event.state.LeftPressed()) return false;
                                auto& prop = view.nodes[self.nodeIdx].properties[self.propIdx];
                                if (!(prop.input.has_value() && prop.input->valid())) return false;
                                guiStateManager.Drag().Begin(
                                    sockID, kSocketPayload, EncodeEndpoint(*prop.input),
                                    event.state.screenPos, std::any(), DragVisualizer{});
                                return true;
                            }
                            case EventType::MouseMove:
                            {
                                if ((event.state.buttons & MouseButtons::Left) == MouseButtons::None) break;
                                guiStateManager.Drag().UpdateFromInput(event.state);
                                if (guiStateManager.Drag().GetDraggedID() != sockID) return false;
                                // First post-threshold move: detach this input from its
                                // source and hand the loose end to the dragged wire, which
                                // starts at the source output and follows the cursor.
                                if (!view.pendingConnection.active)
                                {
                                    auto& prop = view.nodes[self.nodeIdx].properties[self.propIdx];
                                    if (prop.input.has_value() && prop.input->valid())
                                    {
                                        view.pendingConnection.source        = *prop.input;
                                        view.pendingConnection.detachedInput = self;
                                        prop.input = PropertyEndpoint{};
                                    }
                                    view.pendingConnection.active = true;
                                }
                                view.pendingConnection.currentScreen = event.state.screenPos;
                                return true;
                            }
                            case EventType::MouseUp:
                            {
                                // Ends a detach drag or an armed click; a socket drop is handled by its Drop.
                                CancelConnection(view);
                                return true;
                            }
                            case EventType::DragEnter:
                            case EventType::DragOver:
                            {
                                if (event.state.dragPayloadType != kSocketPayload) return false;
                                // false refuses the drop here.
                                return ConnectionAllowed(view, DecodeEndpoint(event.state.dragPayloadValue), self);
                            }
                            case EventType::Drop:
                            {
                                if (event.state.dragPayloadType != kSocketPayload) return false;
                                PropertyEndpoint src = DecodeEndpoint(event.state.dragPayloadValue);
                                if (!ConnectionAllowed(view, src, self)) return false;  // delegate-gated
                                FinishConnection(view, src, self);
                                return true;
                            }
                            default: break;
                        }
                        return false;
                    }
                },
                .styleRef = theme.nodeSocketStyleRef,
                .state    = guiStateManager.GetCurrentState(sockID)
            };
            auto res = elementStorage.Add(std::move(sockElem), std::move(layout));
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));

            // Highlight rules:
            //  - OUTPUT: on hover.
            //  - INPUT, mid-drag: while the drag hovers it and the connection is allowed
            //    (IsDropTargetFor is live; GetDropTargetID is only set on release).
            //  - INPUT, at rest: on hover when it has a connection to drag out.
            const interstice::color4f dotColor = view.nodes[n].properties[propIdx].socketColor;
            const bool hovered = (guiStateManager.GetCurrentState(sockID) & ElementState::Hover) != ElementState::None;
            bool highlight;
            if (isOutput)
            {
                highlight = hovered;
            }
            else
            {
                const auto& p = view.nodes[n].properties[propIdx];
                const bool connected = p.input.has_value() && p.input->valid();
                const bool dropTarget = guiStateManager.Drag().IsDropTargetFor(sockID, kSocketPayload) &&
                                        ConnectionAllowed(view, view.pendingConnection.source, self);
                if (dropTarget)
                    view.pendingConnection.target = self;
                highlight = dropTarget || (!readonly && connected && hovered && !view.pendingConnection.active);
            }

            // A pin on the node's edge: filled when connected, a ring when not, with an
            // outline in the node's color. The hit region is the full element rect.
            const auto& socketProp = view.nodes[n].properties[propIdx];
            const bool  connected  = isOutput ? drivenOutputs.contains(WireKey(n, propIdx))
                                              : socketProp.input.has_value() && socketProp.input->valid();
            VERIFY(elementStorage.AddCustomDraw(res.ref,
                [dotColor, highlight, connected, kSocketSize, nodeColor = theme.GetColor("--bg-raised"),
                 inset = styleManager.Px(2.0f), ring = styleManager.Px(2.0f), halo = styleManager.Px(6.0f),
                 grow = styleManager.Px(1.0f)](interstice::CustomDrawContext& ctx)
                {
                    const auto inner = ctx.ElementInnerRect();
                    // Socket centre. Rows are two grid cells tall and the socket is
                    // centred in the row, so this lands on the mid-row grid line.
                    const interstice::float2 center{ inner.w * 0.5f, inner.h * 0.5f };
                    const float pin = (highlight ? grow : 0.0f) + (float)kSocketSize - inset;
                    if (highlight)   // soft white halo behind the pin
                        ctx.RenderDot(center, pin + halo, interstice::color4f{ 1.0f, 1.0f, 1.0f, 0.5f });
                    else
                        ctx.RenderDot(center, pin + 2.0f * ring, nodeColor);
                    ctx.RenderDot(center, pin, dotColor);
                    if (!connected)
                        ctx.RenderDot(center, pin - 2.0f * ring, nodeColor);
                }));

            return res.ref;
        };

        // .node top chrome (border + padding); the header absorbs it so rows start on the grid.
        int32_t nodeTopChromeV = 0;
        // Socket centre depth inside the node's left/right border-box edge (chrome + half
        // a socket). Offsetting by it centres the dot on the border; overflow:visible
        // lets the outside half still hit-test.
        int32_t socketInsetL = kSocketSize / 2;
        int32_t socketInsetR = kSocketSize / 2;
        // .node bottom chrome; the last row gives it back so the bottom lands on the grid.
        int32_t nodeBottomChromeV = 0;
        if (const interstice::LayoutStyle* nodeLS = styleManager.GetLayoutStyle(theme.nodeStyleRef.layoutStyleIndex))
        {
            nodeTopChromeV    = nodeLS->borderWidths.top + nodeLS->padding.top;
            nodeBottomChromeV = nodeLS->borderWidths.bottom + nodeLS->padding.bottom;
            socketInsetL  += nodeLS->borderWidths.left  + nodeLS->padding.left;
            socketInsetR  += nodeLS->borderWidths.right + nodeLS->padding.right;
        }

        std::optional<uint32_t> handleHoveredWire;   // the wire whose lane handle is under the mouse

        // ---- Interior wire-segment drag handles ----
        // Invisible grab bands over draggable wire segments, from last frame's route pass
        // (view.wireHandleCache, node.pos space). Emitted before the nodes so nodes win hits,
        // but above the canvas so a grab never starts a marquee. Uses mouse capture, not
        // DnD, so the requested resize cursor sticks.
        {
            // One ranged ID over the handle cache, scoped to the canvas; handle = sub-ID at its slot.
            [[maybe_unused]] bool pushedHandles = idManager.PushParent(canvasID);
            const int kGrab = styleManager.Px(5);   // grab-band half thickness (node.pos space)
            const ID handleRange = view.wireHandleCache.empty() ? ID::Invalid()
                : idManager.AllocateRangedID((uint32_t)view.wireHandleCache.size(), "wire-handle");
            for (size_t hi = 0; hi < view.wireHandleCache.size(); ++hi)
            {
                const CachedWireHandle& wh = view.wireHandleCache[hi];
                const int32_t xLo = std::min(wh.a.x, wh.b.x), xHi = std::max(wh.a.x, wh.b.x);
                const int32_t yLo = std::min(wh.a.y, wh.b.y), yHi = std::max(wh.a.y, wh.b.y);
                int32_t rx, ry, rw, rh;
                if (wh.vertical) { rx = wh.a.x - kGrab; ry = yLo; rw = 2 * kGrab; rh = yHi - yLo; }
                else             { rx = xLo; ry = wh.a.y - kGrab; rw = xHi - xLo; rh = 2 * kGrab; }
                if (rw <= 0 || rh <= 0) continue;

                const ID handleID = idManager.GetSubID(handleRange, (uint32_t)hi);
                if ((guiStateManager.GetCurrentState(handleID) & ElementState::Hover) != ElementState::None)
                    handleHoveredWire = wh.wireKey;

                const bool dragging = view.wireDrag.active &&
                                      view.wireDrag.wireKey == wh.wireKey &&
                                      view.wireDrag.role == wh.role;
                const int32_t startValue = wh.vertical ? wh.a.x : wh.a.y;

                Element handle
                {
                    .id = handleID,
                    .eventHandler = [this, &view, canvasID, wireKey = wh.wireKey,
                                     role = wh.role, vertical = wh.vertical, startValue]
                                    (ElementEvent&& event) -> bool
                    {
                        switch (event.type)
                        {
                            case EventType::MouseDown:
                            {
                                if (!event.state.LeftPressed()) return false;
                                guiStateManager.Interaction().SetFocusID(canvasID);
                                const auto lanes = view.wireOverrides.find(wireKey);
                                view.wireDrag = WireSegmentDrag{
                                    .active = true, .wireKey = wireKey, .role = role,
                                    .vertical = vertical, .startScreen = event.state.screenPos,
                                    .startValue = startValue,
                                    .lanesBefore = lanes != view.wireOverrides.end() ? lanes->second : WireRouteOverrides{} };
                                return true;
                            }
                            case EventType::MouseMove:
                            {
                                if (!event.state.LeftDown()) return false;
                                if (!(view.wireDrag.active && view.wireDrag.wireKey == wireKey &&
                                      view.wireDrag.role == role)) return false;
                                const interstice::float2 d = view.transform.Inverse().TransformVector(
                                    interstice::float2{ (float)(event.state.screenPos.x - view.wireDrag.startScreen.x),
                                                 (float)(event.state.screenPos.y - view.wireDrag.startScreen.y) });
                                const int32_t newVal = view.wireDrag.startValue +
                                    (vertical ? (int32_t)d.x : (int32_t)d.y);
                                WireRouteOverrides& o = view.wireOverrides[wireKey];
                                switch (role)
                                {
                                    case WireHandle::LaneX:  o.laneX  = newVal; break;
                                    case WireHandle::ExitX:  o.exitX  = newVal; break;
                                    case WireHandle::EntryX: o.entryX = newVal; break;
                                    case WireHandle::LaneY:  o.laneY  = newVal; break;
                                    default: break;
                                }
                                return true;
                            }
                            case EventType::MouseUp:
                            {
                                if (view.wireDrag.active && view.wireDrag.wireKey == wireKey &&
                                    view.wireDrag.role == role)
                                {
                                    const auto lanes = view.wireOverrides.find(wireKey);
                                    const WireRouteOverrides after = lanes != view.wireOverrides.end() ? lanes->second : WireRouteOverrides{};
                                    NodeViewEdit edit;
                                    if (!(after == view.wireDrag.lanesBefore))
                                        edit.lanes.push_back({ wireKey, view.wireDrag.lanesBefore, after });
                                    view.wireDrag = {};
                                    CommitNodeViewEdit(view, edit);
                                }
                                return true;
                            }
                            default: break;
                        }
                        return false;
                    },
                    .styleRef = theme.emptyHorzStyleRef,
                    .state    = guiStateManager.GetCurrentState(handleID)
                };
                auto hres = elementStorage.Add(std::move(handle),
                    LayoutOverride{}.FixedSize(Pixels{ rw }, Pixels{ rh }).Absolute(rx, ry));
                VERIFY(hres.status);
                VERIFY(windowRegistry.Add(hres.ref));

                // Resize cursor while hovered or dragged (style cursor is Default, so it isn't overridden).
                const ElementState hstate = guiStateManager.GetCurrentState(handleID);
                if (dragging || (hstate & ElementState::Hover) != ElementState::None)
                    guiStateManager.Cursor().Request(
                        wh.vertical ? MouseCursor::EwResize : MouseCursor::NsResize);
            }
            [[maybe_unused]] bool poppedHandles = idManager.PopParent();
        }

        // ---- Render each node (back to front) ----
        for (int orderIdx = 0; orderIdx < nodeCount; ++orderIdx)
        {
            const int n = view.ordering[orderIdx];
            NodeModel& node = view.nodes[n];

            // Snap the node's top-left onto the grid.
            const int32_t grid = theme.nodeGridSize;
            if (grid > 0)
                node.pos = { SnapToGrid(node.pos.x, grid), SnapToGrid(node.pos.y, grid) };
            // Width: last frame's content width rounded UP to whole grid cells (minimum on the first frame).
            const int32_t minNodeWidth = (grid > 0) ? grid * 6 : styleManager.Px(120);
            int32_t nodeWidth = minNodeWidth;
            if (grid > 0 && node.lastSize.x > 0)
                nodeWidth = std::max(minNodeWidth, ((node.lastSize.x + grid - 1) / grid) * grid);

            // Outer .node element: click-drag moves the node, double-click toggles collapse.
            // Uses the node's stable ID; a dynamic ID would change with z-order reshuffles.
            const ID outerID = node.stableID;
            ElementState outerState = guiStateManager.GetCurrentState(outerID);
            if (view.selectedNodeIDs.count(outerID)) outerState |= ElementState::Selected;
            Element outer
            {
                .id = outerID,
                .eventHandler = [this, &view, n, outerID](ElementEvent&& event)->bool
                {
                    switch (event.type)
                    {
                        case EventType::MouseDown:
                        {
                            if (!event.state.LeftPressed()) return false;

                            const auto r = view.nodeDoubleClick.Update(outerID);
                            if (r.sameAsPrevious && r.elapsedMs < 300.0f)
                            {
                                NodeViewEdit edit;
                                edit.collapses.push_back({ n, view.nodes[n].collapsed, !view.nodes[n].collapsed });
                                CommitNodeViewEdit(view, edit);
                                view.nodeDoubleClick.Reset();
                                return true;
                            }

                            const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                            const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                            const bool alreadySelected = view.selectedNodeIDs.count(outerID) > 0;

                            if (shift)
                            {
                                view.selectedNodeIDs.insert(outerID);
                            }
                            else if (ctrl)
                            {
                                if (alreadySelected) view.selectedNodeIDs.erase(outerID);
                                else                 view.selectedNodeIDs.insert(outerID);
                            }
                            else if (!alreadySelected)
                            {
                                view.selectedNodeIDs.clear();
                                view.selectedNodeIDs.insert(outerID);
                            }
                            view.lastClickedNodeID = outerID;

                            // Bring to front in `ordering` (last slot draws on top).
                            BringNodeToFront(view.ordering, n);
                            // Snapshot start positions of all moving nodes, always including
                            // the dragged one (ctrl-click may have just deselected it).
                            for (auto& other : view.nodes)
                            {
                                if (view.selectedNodeIDs.count(other.stableID) || other.stableID == outerID)
                                    other.startPos = other.pos;
                            }
                            view.nodes[n].isDragging  = true;
                            view.nodes[n].startScreen = event.state.screenPos;
                            return true;
                        }
                        case EventType::MouseUp:
                        {
                            // The whole drag is one edit, to the grid-snapped positions.
                            if (!view.nodes[n].isDragging) return true;
                            view.nodes[n].isDragging = false;
                            const int32_t grid = theme.nodeGridSize;
                            NodeViewEdit edit;
                            for (int i = 0; i < (int)view.nodes.size(); ++i)
                            {
                                const NodeModel& other = view.nodes[i];
                                if (i != n && !view.selectedNodeIDs.count(other.stableID)) continue;
                                const int2 to = grid > 0 ? int2{ SnapToGrid(other.pos.x, grid), SnapToGrid(other.pos.y, grid) } : other.pos;
                                if (to.x != other.startPos.x || to.y != other.startPos.y)
                                    edit.moves.push_back({ i, other.startPos, to });
                            }
                            CommitNodeViewEdit(view, edit);
                            return true;
                        }
                        // A wire dropped on the node connects to its first input that takes it.
                        case EventType::DragEnter:
                        case EventType::DragOver:
                        case EventType::Drop:
                        {
                            if (event.state.dragPayloadType != kSocketPayload) return false;
                            const PropertyEndpoint src = DecodeEndpoint(event.state.dragPayloadValue);
                            const PropertyEndpoint in  = FirstInputAccepting(view, src, n);
                            if (!in.valid()) return false;
                            if (event.type == EventType::Drop)
                            {
                                FinishConnection(view, src, in);
                            }
                            else
                                view.pendingConnection.target = in;
                            return true;
                        }
                        case EventType::MouseMove:
                        {
                            if (!event.state.LeftDown())   return false;
                            if (!view.nodes[n].isDragging) return false;

                            auto delta = view.transform.Inverse().TransformVector(
                                float2
                                {
                                    (float)(event.state.screenPos.x - view.nodes[n].startScreen.x),
                                    (float)(event.state.screenPos.y - view.nodes[n].startScreen.y)
                                });
                            const int2 idelta { (int32_t)delta.x, (int32_t)delta.y };
                            // Move every node in the selection by the same delta;
                            // always include the dragged node (see MouseDown).
                            for (auto& other : view.nodes)
                            {
                                if (view.selectedNodeIDs.count(other.stableID) || other.stableID == outerID)
                                    other.pos = { other.startPos.x + idelta.x, other.startPos.y + idelta.y };
                            }
                            return true;
                        }
                    }
                    return false;
                },
                .styleRef = theme.nodeStyleRef,
                .flags    = ElementFlags::PropagatesDescendantState,
                .state    = outerState
            };
            // MinWidth (not Fixed): shrink-wraps content, floored at the grid-snapped width.
            auto outerRes = elementStorage.Add(std::move(outer),
                LayoutOverride{}.MinWidth(Pixels{ nodeWidth }).Absolute(node.pos.x, node.pos.y));
            VERIFY(outerRes.status);
            VERIFY(windowRegistry.Add(outerRes.ref));
            nodeRefs[n] = outerRes.ref;

            // Cache the node's laid-out size for marquee hit-testing.
            VERIFY(elementStorage.AddPostLayout(outerRes.ref,
                [&view, n](interstice::PostLayoutContext& ctx)
                {
                    view.nodes[n].lastSize = ctx.ElementRect().size;
                }));
            VERIFY(elementStorage.BeginChildren());

            // Scope this node's dynamic IDs under its stable ID (BeginChildren doesn't push
            // an ID parent), so they don't change when z-order reshuffles the nodes.
            [[maybe_unused]] bool pushed = idManager.PushParent(node.stableID);

            // ---- Header: [category square] title ... category ----
            // Static title bar: NoInput throughout so clicks reach the node.
            ID headerID = idManager.AllocateDynamicID("node-header");
            Element header
            {
                .id       = headerID,
                .styleRef = theme.nodeHeaderStyleRef,
                .flags    = ElementFlags::NoInput,
                .state    = outerState
            };
            // Top chrome + header == one row pitch (2*grid), so rows start on a grid line.
            const int32_t rowPitch = 2 * grid;
            LayoutOverride headerLayout = LayoutOverride{}.Auto();
            if (rowPitch > nodeTopChromeV)
                headerLayout = LayoutOverride{}.Height(Pixels{ rowPitch - nodeTopChromeV });
            auto headerRes = elementStorage.Add(std::move(header), std::move(headerLayout));
            VERIFY(headerRes.status);
            VERIFY(windowRegistry.Add(headerRes.ref));
            VERIFY(elementStorage.BeginChildren());
            {
                auto addHeaderPart = [&](const char* name, StyleRef style, std::string_view text,
                                         interstice::VisualColorStyleIndex color)
                {
                    const ID partID = idManager.AllocateDynamicID(name);
                    Element part{ .id = partID, .styleRef = style, .flags = ElementFlags::NoInput, .state = outerState };
                    if (!text.empty())
                        part.textID = textSystem->RegisterText(partID, text, style, part.state);
                    part.colorStyleIndex = color;
                    auto res = elementStorage.Add(std::move(part), LayoutOverride{}.Auto());
                    VERIFY(res.status);
                    VERIFY(windowRegistry.Add(res.ref));
                };
                // Alpha 0 keeps the CSS default (the accent).
                const interstice::VisualColorStyleIndex categoryColor = node.categoryColor.a > 0.0f
                    ? styleManager.RegisterColorStyle(interstice::ColorStyle::Create(node.categoryColor))
                    : interstice::VisualColorStyleIndex{};
                addHeaderPart("node-category", theme.nodeCategoryStyleRef, {}, categoryColor);
                addHeaderPart("node-title", theme.nodeTitleStyleRef, node.title, {});
                if (!node.category.empty())
                    addHeaderPart("node-caption", theme.nodeCaptionStyleRef, node.category, {});
            }
            VERIFY(elementStorage.EndChildren());

            if (!node.collapsed)
            {
                // One LabelGroup ID per node, begun inside every row, so all labels in
                // the node share one width.
                const ID nodeLabelGroupID = idManager.AllocateDynamicID("node-labelgroup");

                const int propCount = (int)node.properties.size();
                for (int p = 0; p < propCount; ++p)
                {
                    NodeProperty& prop = node.properties[p];

                    ID rowID = idManager.AllocateDynamicID("node-row");
                    // Whole row pitches so rows and socket centres stay on grid lines; rowSpan > 1
                    // fits tall bodies. The last row gives back the bottom chrome.
                    const int32_t span = (prop.rowSpan > 0 ? prop.rowSpan : 1);
                    const int32_t rowH = rowPitch * span
                                       - (p == propCount - 1 ? nodeBottomChromeV : 0);
                    BeginHorizontal(rowID, theme.nodeRowStyleRef, LayoutOverride{}.Height(Pixels{ rowH }));

                    // Input socket at the FIRST pitch centre, output at the LAST
                    // (same as the row centre for single-pitch rows).
                    const int32_t socketYOffset = (span - 1) * rowPitch / 2;
                    // Left inset on every single-pitch row so labels/fields align with or
                    // without an input dot (mirrors row-pad-r). Multi-pitch rows fill.
                    if (span == 1)
                    {
                        Element spacer{};
                        spacer.id       = idManager.AllocateDynamicID("sock-spacer-l");
                        spacer.styleRef = theme.emptyHorzStyleRef;
                        spacer.flags    = ElementFlags::NoInput;
                        auto sp = elementStorage.Add(std::move(spacer),
                            LayoutOverride{}.FixedSize(Pixels{ kSocketSize / 6 }, Pixels{ kSocketSize }).NoGrow().NoShrink());
                        VERIFY(sp.status);
                        VERIFY(windowRegistry.Add(sp.ref));
                    }
                    // Input dot: OUT OF FLOW - absolutely placed straddling the node's
                    // left border at this property's FIRST pitch, mirroring the output
                    // dot on the right. Y is taken from the FULL pitch so it lands on a
                    // grid line even on the chrome-shortened last row.
                    if (prop.input.has_value())
                    {
                        const int16_t inY = static_cast<int16_t>((rowPitch * span) / 2 - socketYOffset - kSocketSize / 2);
                        socketRefs[n][p * 2 + 0] = emitSocket(n, p, /*isOutput=*/false, /*readonly=*/false,
                            LayoutOverride{}.FixedSize(Pixels{ kSocketSize }, Pixels{ kSocketSize }).Absolute(-socketInsetL, inY));
                    }

                    // Body fills the row. The LabelGroup must begin at the depth renderBody
                    // emits into. Multi-pitch content (images) is centred.
                    if (span > 1) BeginHorizontal(theme.nodeBodyCenterStyleRef, LayoutOverride{}.FlexGrow(1));
                    else          BeginHorizontal(LayoutOverride{}.FlexGrow(1));
                    BeginLabelGroup(nodeLabelGroupID, /*defaultWidth=*/60,
                                    /*minWidth=*/30, /*maxWidth=*/120,
                                    /*resizable=*/false);
                    // A text-only body (NodeText) aligns to its sockets: left
                    // dot => left, right dot => right, both => centre.
                    nodeRowTextAlign = (prop.input.has_value() && prop.output) ? TextAlign::Center
                                     :  prop.output                                ? TextAlign::End
                                     :                                                       TextAlign::Start;
                    // Value-fields fill the body (minus the label) with room to spare.
                    nodeRowFieldMinWidth = theme.nodeGridSize * 4;
                    if (prop.renderBody) prop.renderBody();
                    nodeRowTextAlign     = TextAlign::Start;
                    nodeRowFieldMinWidth = 0;
                    EndLabelGroup();
                    EndHorizontal();

                    // Same right inset on every single-pitch row so stacked fields line up.
                    if (span == 1)
                    {
                        Element rpad{};
                        rpad.id       = idManager.AllocateDynamicID("row-pad-r");
                        rpad.styleRef = theme.emptyHorzStyleRef;
                        rpad.flags    = ElementFlags::NoInput;
                        auto rp = elementStorage.Add(std::move(rpad),
                            LayoutOverride{}.FixedSize(Pixels{ kSocketSize / 6 }, Pixels{ kSocketSize }).NoGrow().NoShrink());
                        VERIFY(rp.status);
                        VERIFY(windowRegistry.Add(rp.ref));
                    }
                    // Output dot: OUT OF FLOW - absolutely placed straddling the node's
                    // right border at this property's pitch (like the collapsed sockets).
                    if (prop.output)
                    {
                        const int16_t outY = static_cast<int16_t>((rowPitch * span) / 2 + socketYOffset - kSocketSize / 2);
                        socketRefs[n][p * 2 + 1] = emitSocket(n, p, /*isOutput=*/true, /*readonly=*/false,
                            LayoutOverride{}.FixedSize(Pixels{ kSocketSize }, Pixels{ kSocketSize })
                                           .Absolute(nodeWidth - socketInsetR, outY));
                    }

                    EndHorizontal();
                }
            }
            else
            {
                // Collapsed: all inputs overlap on the left edge, outputs on the right,
                // overlaying the header (absolute, ignored by flex). Readonly, but still
                // laid out so wires end at the right spot.
                const int propCount = (int)node.properties.size();
                // Straddle the left/right border like expanded rows, centred on the
                // header slot (top chrome + header = one row pitch).
                const int leftX  = -socketInsetL;
                const int rightX = nodeWidth - socketInsetR;
                const int y      = rowPitch / 2 - kSocketSize / 2;
                for (int p = 0; p < propCount; ++p)
                {
                    NodeProperty& prop = node.properties[p];
                    if (prop.input.has_value())
                    {
                        socketRefs[n][p * 2 + 0] = emitSocket(n, p, /*isOutput=*/false, /*readonly=*/true,
                            LayoutOverride{}.FixedSize(Pixels{ kSocketSize }, Pixels{ kSocketSize }).Absolute(leftX, y));
                    }
                    if (prop.output)
                    {
                        socketRefs[n][p * 2 + 1] = emitSocket(n, p, /*isOutput=*/true, /*readonly=*/true,
                            LayoutOverride{}.FixedSize(Pixels{ kSocketSize }, Pixels{ kSocketSize }).Absolute(rightX, y));
                    }
                }
            }

            [[maybe_unused]] bool popped = idManager.PopParent();
            VERIFY(elementStorage.EndChildren()); // close outer .node
        }

        VERIFY(elementStorage.AddPostLayout(canvasRes.ref,
            [&view](interstice::PostLayoutContext& ctx)
            {
                view.lastInnerSize = ctx.ElementInnerRect().size;
            }));

        // Axis lines + wires draw in the canvas's OWN (pre-children) pass, so they sit
        // behind the nodes and keep drawing when a node is culled off-screen. An
        // AddCustomDrawAfter anchors to the last element of the canvas subtree - a child
        // of the frontmost node - and is skipped with that node when it leaves view.
        // Endpoints are CANVAS_LAY, so they pan/zoom with the sockets and clip to the viewport.
        VERIFY(elementStorage.AddCustomDraw(canvasRes.ref,
            [this, &view, socketRefs = std::move(socketRefs), nodeRefs = std::move(nodeRefs),
             canvasHovered = guiStateManager.Interaction().GetHoveredID() == canvasID,
             handleHoveredWire, mouseScreen = windowManager->GetScreenMousePos()](interstice::CustomDrawContext& ctx)
            {
                // Nodes are positioned relative to the canvas BORDER box, but this draw's
                // origin is the CONTENT box; offset the grid by the difference.
                const interstice::int2 gridOrigin = ctx.ElementRect().position - ctx.ElementInnerRect().position;

                // theme.nodeGridSize is the single pitch for node snap, wire snap and this
                // grid. No subdivisions; density halves per zoom-out octave.
                // Clamp to the content box: take the (window-local, pan/zoom-correct) ClipRect,
                // deflate by the canvas chrome, and map into the current space.
                const interstice::IntRect bbox = ctx.ElementRect();
                const interstice::IntRect ibox = ctx.ElementInnerRect();
                const interstice::IntRect clipWL = ctx.ClipRect();
                const interstice::IntRect contentWL{
                    clipWL.x + (ibox.Min(0) - bbox.Min(0)),
                    clipWL.y + (ibox.Min(1) - bbox.Min(1)),
                    clipWL.w - (ibox.Min(0) - bbox.Min(0)) - (bbox.Max(0) - ibox.Max(0)),
                    clipWL.h - (ibox.Min(1) - bbox.Min(1)) - (bbox.Max(1) - ibox.Max(1)) };
                const interstice::float3x2 wlToCur = ctx.CurrentTransform().Inverse();
                auto toCur = [&](interstice::int2 wl) {
                    const interstice::float2 p = wlToCur.TransformPoint({ (float)wl.x, (float)wl.y });
                    return interstice::int2{ (int)p.x, (int)p.y };
                };
                const interstice::int2 cc0 = toCur(contentWL.position);
                const interstice::int2 cc1 = toCur({ contentWL.x + contentWL.w, contentWL.y + contentWL.h });
                const interstice::IntRect content{
                    std::min(cc0.x, cc1.x), std::min(cc0.y, cc1.y),
                    std::max(cc0.x, cc1.x) - std::min(cc0.x, cc1.x),
                    std::max(cc0.y, cc1.y) - std::min(cc0.y, cc1.y) };

                RenderCanvasGrid(ctx, theme.nodeGridSize, theme.GetColor("--canvas-dot"),
                                 styleManager.Px(2.0f), gridOrigin, content);

                // Socket rects are laid out even for culled nodes. Local space = CANVAS_LAY.
                auto socketCenter = [&](const ElementRef& ref) -> interstice::int2
                {
                    // Socket centre in CANVAS_LAY. Rows are two grid cells tall, so
                    // this sits on a grid line - wires run the lattice to the dot.
                    return ctx.GetElementRect(ref).Center();
                };

                // Node body rect, in the same CANVAS_LAY space as socketCenter,
                // so the router can keep wires clear of the source/target bodies.
                auto nodeRect = [&](int idx) -> interstice::IntRect
                {
                    return ctx.GetElementRect(nodeRefs[idx]);
                };

                // Draggable interior segments collected this frame (node.pos
                // space) for next frame's grab elements. Replaces the cache at
                // the end of this pass.
                std::vector<CachedWireHandle> handleCache;

                // Wires between the same two nodes get their lanes spread apart, counting a
                // dragged wire over an input as connected.
                const bool pendingOverInput = view.pendingConnection.active && view.pendingConnection.target.valid();
                const auto laneOffsets = pendingOverInput
                    ? ComputeWireLaneOffsets(view, view.pendingConnection.source, view.pendingConnection.target)
                    : ComputeWireLaneOffsets(view);

                // Routed first, then hit-tested, then drawn, so the hovered wire draws highlighted.
                std::vector<DrawnWire> drawn;
                std::vector<interstice::VisualColorStyleIndex> drawnColors;

                const int nodeCount = (int)view.nodes.size();
                for (int n = 0; n < nodeCount; ++n)
                {
                    const auto& node = view.nodes[n];
                    const int propCount = (int)node.properties.size();
                    for (int p = 0; p < propCount; ++p)
                    {
                        const auto& prop = node.properties[p];
                        if (!prop.input.has_value() || !prop.input->valid()) continue;

                        const PropertyEndpoint src = *prop.input;
                        if (src.nodeIdx < 0 || src.nodeIdx >= (int)socketRefs.size()) continue;
                        const auto& srcRefs = socketRefs[src.nodeIdx];
                        const int srcOutIdx = src.propIdx * 2 + 1;
                        if (srcOutIdx < 0 || srcOutIdx >= (int)srcRefs.size()) continue;

                        const auto& tgtRefs = socketRefs[n];
                        const int tgtInIdx = p * 2 + 0;
                        if (tgtInIdx < 0 || tgtInIdx >= (int)tgtRefs.size()) continue;

                        if (!srcRefs[srcOutIdx].IsValid() || !tgtRefs[tgtInIdx].IsValid()) continue;

                        // Per-wire lane overrides are stored in node.pos
                        // (border-box) space; shift into this draw's CONTENT-box
                        // space by gridOrigin before routing.
                        const uint32_t wireKey = WireKey(n, p);
                        WireRouteOverrides ov;
                        if (auto it = view.wireOverrides.find(wireKey); it != view.wireOverrides.end())
                        {
                            ov = it->second;
                            if (ov.laneX)  *ov.laneX  += gridOrigin.x;
                            if (ov.exitX)  *ov.exitX  += gridOrigin.x;
                            if (ov.entryX) *ov.entryX += gridOrigin.x;
                            if (ov.laneY)  *ov.laneY  += gridOrigin.y;
                        }
                        const bool replaced = pendingOverInput && view.pendingConnection.target == PropertyEndpoint{ n, p };
                        if (auto it = laneOffsets.find(wireKey); it != laneOffsets.end() && !replaced)
                            ov.lane = it->second;

                        const auto route = BuildWireRoute(socketCenter(srcRefs[srcOutIdx]),
                                                          socketCenter(tgtRefs[tgtInIdx]),
                                                          nodeRect(src.nodeIdx), nodeRect(n),
                                                          theme.nodeGridSize, gridOrigin, ov);
                        // Wire takes its source socket's colour (style cached above).
                        const interstice::color4f srcColor =
                            view.nodes[src.nodeIdx].properties[src.propIdx].socketColor;
                        interstice::VisualColorStyleIndex wireColorIdx = theme.lineYellowStyle;
                        if (auto cit = view.wireColorStyles.find(PackColor(srcColor)); cit != view.wireColorStyles.end())
                            wireColorIdx = cit->second;
                        drawn.push_back(DrawnWire{ .wireKey = wireKey, .points = route.points });
                        drawnColors.push_back(wireColorIdx);

                        // Cache draggable segments in node.pos space (subtract
                        // gridOrigin) so next frame's grab elements align with
                        // node-relative Absolute placement.
                        for (const auto& seg : route.handles)
                            handleCache.push_back(CachedWireHandle{
                                .wireKey  = wireKey,
                                .role     = seg.role,
                                .vertical = seg.vertical,
                                .a        = seg.a - gridOrigin,
                                .b        = seg.b - gridOrigin });
                    }
                }

                // The wire under the mouse: over a lane handle that handle's, over the bare
                // canvas the nearest within a few pixels; none while something is dragged.
                view.hoveredWire.reset();
                if (!view.pendingConnection.active && !view.wireDrag.active && !view.marqueeActive)
                {
                    if (handleHoveredWire)
                        view.hoveredWire = handleHoveredWire;
                    else if (canvasHovered)
                        view.hoveredWire = WireAt(drawn, ctx.ScreenToCurrent(mouseScreen),
                                                  float(styleManager.Px(5)) / NodeViewZoom(view));
                }
                for (size_t w = 0; w < drawn.size(); ++w)
                {
                    const bool hovered = view.hoveredWire == drawn[w].wireKey;
                    const interstice::LineStyle wireStyle{ .colorStyleIndex = drawnColors[w],
                                                           .lineWidth = styleManager.Px(hovered ? 4.f : 2.f) };
                    ctx.RenderPolyline(drawn[w].points, /*closed=*/false, wireStyle, 0.5f * float(theme.nodeGridSize));
                }

                // The wire being dragged: dashed, routed like the connection it would make.
                if (view.pendingConnection.active)
                {
                    const PropertyEndpoint src = view.pendingConnection.source;
                    const PropertyEndpoint tgt = view.pendingConnection.target;
                    if (src.nodeIdx >= 0 && src.nodeIdx < (int)socketRefs.size())
                    {
                        const auto& srcRefs = socketRefs[src.nodeIdx];
                        const int srcOutIdx = src.propIdx * 2 + 1;
                        if (srcOutIdx >= 0 && srcOutIdx < (int)srcRefs.size() && srcRefs[srcOutIdx].IsValid())
                        {
                            const int32_t grid = theme.nodeGridSize;
                            // currentScreen is a screen-space input; ScreenToCurrent brings it
                            // into CANVAS_LAY, snapped to the grid like a socket.
                            interstice::int2 cursorLay = ctx.ScreenToCurrent(view.pendingConnection.currentScreen);
                            if (grid > 0)
                                cursorLay = { GridLineNearest(cursorLay.x, gridOrigin.x, grid),
                                              GridLineNearest(cursorLay.y, gridOrigin.y, grid) };

                            std::optional<interstice::int2> inPt;
                            interstice::IntRect tgtRect {};
                            if (tgt.valid() && tgt.nodeIdx < (int)socketRefs.size())
                            {
                                const auto& tgtRefs = socketRefs[tgt.nodeIdx];
                                const int tgtInIdx = tgt.propIdx * 2 + 0;
                                if (tgtInIdx >= 0 && tgtInIdx < (int)tgtRefs.size() && tgtRefs[tgtInIdx].IsValid())
                                {
                                    inPt    = socketCenter(tgtRefs[tgtInIdx]);
                                    tgtRect = nodeRect(tgt.nodeIdx);
                                }
                            }
                            WireLane lane;
                            if (inPt)
                                if (auto it = laneOffsets.find(WireKey(tgt.nodeIdx, tgt.propIdx)); it != laneOffsets.end())
                                    lane = it->second;
                            const auto route = BuildPendingWireRoute(socketCenter(srcRefs[srcOutIdx]), nodeRect(src.nodeIdx),
                                                                     cursorLay, inPt, tgtRect, grid, gridOrigin, lane);

                            const interstice::color4f srcColor =
                                view.nodes[src.nodeIdx].properties[src.propIdx].socketColor;
                            interstice::VisualColorStyleIndex wireColorIdx = theme.lineYellowStyle;
                            if (auto cit = view.wireColorStyles.find(PackColor(srcColor)); cit != view.wireColorStyles.end())
                                wireColorIdx = cit->second;
                            const interstice::LineStyle wireStyle{ .colorStyleIndex = wireColorIdx, .lineWidth = styleManager.Px(2.f),
                                                                   .dashSize = styleManager.Px(4.f) };
                            ctx.RenderPolyline(route.points, /*closed=*/false, wireStyle, 0.5f * float(grid));
                        }
                    }
                }

                view.wireHandleCache = std::move(handleCache);
            }));

        // Marquee rectangle - drawn after the nodes so it sits on top of them.
        VERIFY(elementStorage.AddCustomDrawAfter(canvasRes.ref,
            [this, &view](interstice::CustomDrawContext& ctx)
            {
                if (!view.marqueeActive || !view.marqueePastThreshold) return;

                // Marquee corners are screen-space inputs; ScreenToCurrent brings them into
                // the current (CANVAS_LAY) space so the rectangle tracks the cursor 1:1 at any zoom.
                auto toLay = [&](int2 screen) { return ctx.ScreenToCurrent(screen); };
                const int2 startLay   = toLay(view.marqueeStartScreen);
                const int2 currentLay = toLay(view.marqueeCurrentScreen);
                const int x0 = std::min(startLay.x, currentLay.x);
                const int y0 = std::min(startLay.y, currentLay.y);
                const int x1 = std::max(startLay.x, currentLay.x);
                const int y1 = std::max(startLay.y, currentLay.y);
                const IntRect marqueeRectLay{ x0, y0, x1 - x0, y1 - y0 };

                ctx.RenderQuad(marqueeRectLay, theme.marqueeFillColorStyle, 0.0f);
                const interstice::LineStyle border{ .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = styleManager.Px(1.f) };
                ctx.RenderLine({ x0, y0 }, { x1, y0 }, border);
                ctx.RenderLine({ x1, y0 }, { x1, y1 }, border);
                ctx.RenderLine({ x1, y1 }, { x0, y1 }, border);
                ctx.RenderLine({ x0, y1 }, { x0, y0 }, border);
            }));

        VERIFY(elementStorage.EndChildren());

        EmitNodeViewOverlays(view, canvasID);

        // The context menu a right click on a wire opened.
        if (view.wireMenuWire)
        {
            if (BeginContextMenu(wireMenuID))
            {
                const PropertyEndpoint input = WireInput(*view.wireMenuWire);
                MenuItem("Delete connection", [&view, input] { CommitNodeViewEdit(view, DisconnectEdit(view, input)); });
            }
            else
                view.wireMenuWire.reset();
            EndContextMenu();
        }
    }

    void GUIApplication::EmitNodeViewOverlays(NodeView& view, ID canvasID)
    {
        // Siblings after the canvas, anchored to its corners: out of the canvas's
        // pan/zoom, and out of the parent's flow.
        const int32_t inset = styleManager.Px(8);
        auto corner = [&](AnchorPlacement placement, int32_t dx, int32_t dy) {
            return LayoutOverride{}.Anchored(canvasID, placement, dx, dy);
        };

        if (!view.breadcrumb.empty())
            Breadcrumb(idManager.AllocateDynamicID("node-view-breadcrumb"), view.breadcrumb, view.onBreadcrumb,
                       theme.nodeViewOverlayStyleRef,
                       corner(AnchorPlacement::InsideLeft | AnchorPlacement::InsideTop, inset, inset));

        if (view.showZoomControls)
        {
            const ID barID = idManager.AllocateDynamicID("node-view-zoom");
            if (BeginHorizontal(barID, theme.nodeViewOverlayStyleRef,
                                corner(AnchorPlacement::InsideLeft | AnchorPlacement::InsideBottom, inset, -inset)))
            {
                [[maybe_unused]] const bool pushed = idManager.PushParent(barID);
                constexpr float kZoomStep = 1.25f;
                const StyleRef button = theme.nodeViewOverlayButtonStyleRef;
                Button(idManager.AllocateDynamicID(), "\u2212", [&view] { ZoomNodeView(view, 1.0f / kZoomStep); }, button);
                // The zoom; clicking it goes back to 100%.
                const std::string percent = std::to_string(int(std::lround(NodeViewZoom(view) * 100.0f))) + "%";
                Button(idManager.AllocateDynamicID(), percent, [&view] { ZoomNodeView(view, 1.0f / NodeViewZoom(view)); }, button);
                Button(idManager.AllocateDynamicID(), "+", [&view] { ZoomNodeView(view, kZoomStep); }, button);
                Button(idManager.AllocateDynamicID(), "Fit", [&view] { FrameAllNodes(view); }, button);
                if (pushed)
                    (void)idManager.PopParent();
                EndHorizontal();
            }
        }

        if (view.showMinimap)
        {
            // Clicking or dragging in the minimap centres the view there.
            const ID minimapID = idManager.AllocateDynamicID("node-view-minimap");
            Element minimap
            {
                .id = minimapID,
                .eventHandler = [&view](ElementEvent&& event) -> bool
                {
                    const bool press = event.type == EventType::MouseDown && event.state.LeftPressed();
                    const bool drag  = event.type == EventType::MouseMove && event.state.LeftDown();
                    if ((!press && !drag) || view.minimapScale <= 0.0f)
                        return false;
                    const int2 local = event.state.screenPos - event.rect->innerRect.position;
                    SetNodeViewCenter(view, float2{ view.minimapOrigin.x + float(local.x) / view.minimapScale,
                                                    view.minimapOrigin.y + float(local.y) / view.minimapScale },
                                      NodeViewZoom(view));
                    return true;
                },
                .styleRef = theme.nodeViewMinimapStyleRef,
            };
            auto res = elementStorage.Add(std::move(minimap),
                corner(AnchorPlacement::InsideRight | AnchorPlacement::InsideBottom, -inset, -inset)
                    .Size(140, 88));
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));
            VERIFY(elementStorage.AddCustomDraw(res.ref,
                [this, &view, nodeColor = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--line-strong"))),
                 pad = float(styleManager.Px(6))](CustomDrawContext& ctx)
                {
                    // Bounds of the nodes and the visible area, in canvas coordinates.
                    const float2 halfView{ float(view.lastInnerSize.x) * 0.5f, float(view.lastInnerSize.y) * 0.5f };
                    const float3x2 toCanvas = view.transform.Inverse();
                    const float2 v0 = toCanvas.TransformPoint({ -halfView.x, -halfView.y });
                    const float2 v1 = toCanvas.TransformPoint({  halfView.x,  halfView.y });
                    float2 bmin{ std::min(v0.x, v1.x), std::min(v0.y, v1.y) };
                    float2 bmax{ std::max(v0.x, v1.x), std::max(v0.y, v1.y) };
                    for (const auto& node : view.nodes)
                    {
                        bmin = { std::min(bmin.x, float(node.pos.x)), std::min(bmin.y, float(node.pos.y)) };
                        bmax = { std::max(bmax.x, float(node.pos.x + node.lastSize.x)), std::max(bmax.y, float(node.pos.y + node.lastSize.y)) };
                    }

                    const IntRect inner = ctx.ElementInnerRect();
                    const float bw = std::max(1.0f, bmax.x - bmin.x), bh = std::max(1.0f, bmax.y - bmin.y);
                    const float scale = std::min((float(inner.w) - 2.0f * pad) / bw, (float(inner.h) - 2.0f * pad) / bh);
                    if (scale <= 0.0f)
                        return;
                    // Centred in the minimap; the mapping is kept for clicks.
                    view.minimapScale  = scale;
                    view.minimapOrigin = { bmin.x - (float(inner.w) / scale - bw) * 0.5f,
                                           bmin.y - (float(inner.h) / scale - bh) * 0.5f };
                    auto toMap = [&](float x, float y) {
                        return int2{ int(std::lround((x - view.minimapOrigin.x) * scale)),
                                     int(std::lround((y - view.minimapOrigin.y) * scale)) };
                    };

                    for (const auto& node : view.nodes)
                    {
                        const int2 a = toMap(float(node.pos.x), float(node.pos.y));
                        const int2 b = toMap(float(node.pos.x + node.lastSize.x), float(node.pos.y + node.lastSize.y));
                        const bool selected = view.selectedNodeIDs.count(node.stableID) != 0;
                        ctx.RenderQuad(IntRect{ a.x, a.y, std::max(1, b.x - a.x), std::max(1, b.y - a.y) },
                                       selected ? theme.treeDropIndicatorStyle : nodeColor, 2.0f);
                    }

                    // The visible area.
                    const int2 a = toMap(std::min(v0.x, v1.x), std::min(v0.y, v1.y));
                    const int2 b = toMap(std::max(v0.x, v1.x), std::max(v0.y, v1.y));
                    const int2 frame[] = { a, { b.x, a.y }, b, { a.x, b.y } };
                    ctx.RenderPolyline(frame, /*closed=*/true,
                                       LineStyle{ .colorStyleIndex = theme.treeDropIndicatorStyle, .lineWidth = styleManager.Px(1.0f) });
                }));
        }
    }

}
