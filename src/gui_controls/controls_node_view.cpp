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

        // Stable key for a wire, identified by its target (input) endpoint —
        // each input carries at most one incoming wire. Used to look up the
        // per-wire lane overrides and to match drag-handle elements to a wire.
        constexpr uint32_t WireKey(int targetNode, int targetProp)
        {
            return ((uint32_t)targetNode << 16) | (uint32_t)(uint16_t)targetProp;
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

        // Whether a wire from output endpoint `out` to input endpoint `in` is
        // allowed: never a same-node loop, otherwise the view's canConnect
        // delegate decides (absent delegate => allow). Endpoints are fully
        // bounds-checked here so the delegate can index view.nodes directly.
        bool ConnectionAllowed(const NodeView& view, PropertyEndpoint out, PropertyEndpoint in)
        {
            if (out.nodeIdx == in.nodeIdx) return false;
            const int nodeCount = (int)view.nodes.size();
            if (out.nodeIdx < 0 || out.nodeIdx >= nodeCount) return false;
            if (in.nodeIdx  < 0 || in.nodeIdx  >= nodeCount) return false;
            if (out.propIdx < 0 || out.propIdx >= (int)view.nodes[out.nodeIdx].properties.size()) return false;
            if (in.propIdx  < 0 || in.propIdx  >= (int)view.nodes[in.nodeIdx].properties.size())  return false;
            return view.canConnect ? view.canConnect(view, out, in) : true;
        }

        // Round a (possibly negative) coordinate to the nearest multiple of grid.
        constexpr int32_t SnapToGrid(int32_t v, int32_t grid)
        {
            if (grid <= 0) return v;
            const int32_t q = (v >= 0) ? (v + grid / 2) / grid
                                       : (v - grid / 2) / grid;
            return q * grid;
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
        //   CANVAS_LAY : "canvas-local LAYOUT" — pixels relative to the canvas's inner-rect
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
            view.pendingConnection = {};

        ID canvasID = idManager.AllocateDynamicID();

        // Edit-mode wiring: while this canvas has focus, set the command
        // manager to the view's mode; restore Default when focus leaves.
        if (guiStateManager.Interaction().GetFocusedID() == canvasID)
        {
            if (commandManager.GetEditMode() != view.editMode)
                commandManager.SetEditMode(view.editMode);
        } else
        if (commandManager.GetEditMode() == view.editMode)
            commandManager.SetEditMode(EditMode::Default);

        // ---- Canvas: pan/zoom + focus capture. Everything below renders as
        //      children of this element under the view's content transform.
        interstice::Element canvas
        {
            .id = canvasID,
            .eventHandler = [this, &view, canvasID](ElementEvent&& event)->bool
            {
                switch (event.type)
                {
                    case interstice::EventType::MouseDown:
                    {
                        if (!event.state.LeftDown() && !event.state.RightDown())
                            return false;
                        this->guiStateManager.Interaction().SetFocusID(canvasID);
                        if (event.state.LeftPressed())
                        {
                            // Begin marquee. Only SCREEN positions are captured here; the
                            // screen→CANVAS_LAY conversion is done from the LIVE canvas
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
                        if (view.marqueeActive && event.state.LeftReleased())
                        {
                            if (!view.marqueePastThreshold &&
                                !view.marqueeShift && !view.marqueeCtrl)
                            {
                                // Bare click on empty background — clear selection.
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
                        float scaleFactor = (event.state.scroll.y > 0) ? zoomStep : (1.0f / zoomStep);

                        float currentScale = sqrt((view.transform[0].x * view.transform[0].x +
                            view.transform[0].y * view.transform[0].y +
                            view.transform[1].x * view.transform[1].x +
                            view.transform[1].y * view.transform[1].y) * 0.5f);
                        float det = view.transform[0].x * view.transform[0].y - view.transform[1].x * view.transform[1].y;
                        if (det < 0) currentScale = -currentScale;

                        float newScale = currentScale * scaleFactor;
                        if (newScale < 0.1f || newScale > 1.0f)
                        {
                            scaleFactor = std::clamp(newScale, 0.1f, 1.0f);
                            interstice::float3x2 clamp = interstice::float3x2::Scale((1.0f / currentScale) * scaleFactor);
                            view.transform = view.transform * clamp;
                        } else
                        {
                            const int2 innerSize{ event.rect->innerRect.w, event.rect->innerRect.h };
                            int2 translate = event.state.screenPos
                                           - event.rect->innerRect.position
                                           - int2{ innerSize.x / 2, innerSize.y / 2 };
                            interstice::float3x2 zoomDelta = interstice::float3x2::Translate(translate)
                                * interstice::float3x2::Scale(scaleFactor)
                                * interstice::float3x2::Translate(-translate);
                            view.transform = zoomDelta * view.transform;
                        }
                        return true;
                    }
                    case interstice::EventType::KeyDown:
                    {
                        if (event.state.lastKey != interstice::KeyCode::Home || view.nodes.empty())
                            return false;

                        // Home = frame all nodes: centre on their bounding box and
                        // zoom to fit it in the viewport (clamped to the usual range).
                        interstice::int2 bmin = view.nodes[0].pos;
                        interstice::int2 bmax = bmin;
                        for (const auto& node : view.nodes)
                        {
                            bmin.x = std::min(bmin.x, node.pos.x);
                            bmin.y = std::min(bmin.y, node.pos.y);
                            bmax.x = std::max(bmax.x, node.pos.x + node.lastSize.x);
                            bmax.y = std::max(bmax.y, node.pos.y + node.lastSize.y);
                        }
                        const int bw = std::max(1, bmax.x - bmin.x);
                        const int bh = std::max(1, bmax.y - bmin.y);
                        const interstice::int2 bcenter{ (bmin.x + bmax.x) / 2, (bmin.y + bmax.y) / 2 };

                        const interstice::int2 vp = view.lastInnerSize;
                        float s = (vp.x > 0 && vp.y > 0)
                                ? std::min((float)vp.x / (float)bw, (float)vp.y / (float)bh) * 0.9f
                                : 1.0f;
                        s = std::clamp(s, 0.1f, 1.0f);   // same clamp as wheel zoom

                        view.transform = interstice::float3x2::Scale(s)
                                       * interstice::float3x2::Translate(interstice::int2{ -bcenter.x, -bcenter.y });
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
                                view.pendingConnection = {};
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
                                // source (clearing the reciprocal output) and hand the
                                // loose end to the ghost, which originates at the source
                                // output and now follows the cursor.
                                if (!view.pendingConnection.active)
                                {
                                    auto& prop = view.nodes[self.nodeIdx].properties[self.propIdx];
                                    if (prop.input.has_value() && prop.input->valid())
                                    {
                                        PropertyEndpoint srcEp = *prop.input;
                                        if (srcEp.nodeIdx >= 0 && srcEp.nodeIdx < (int)view.nodes.size())
                                        {
                                            auto& srcNode = view.nodes[srcEp.nodeIdx];
                                            if (srcEp.propIdx >= 0 && srcEp.propIdx < (int)srcNode.properties.size())
                                                srcNode.properties[srcEp.propIdx].output = PropertyEndpoint{};
                                        }
                                        prop.input = PropertyEndpoint{};
                                        view.pendingConnection.source = srcEp;
                                    }
                                    view.pendingConnection.active = true;
                                }
                                view.pendingConnection.currentScreen = event.state.screenPos;
                                return true;
                            }
                            case EventType::MouseUp:
                            {
                                // Ends a detach drag or an armed click; a socket drop is handled by its Drop.
                                view.pendingConnection = {};
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
                                ConnectSockets(view, src, self);
                                view.pendingConnection = {};
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
                highlight =
                    (guiStateManager.Drag().IsDropTargetFor(sockID, kSocketPayload) &&
                     ConnectionAllowed(view, view.pendingConnection.source, self))
                    || (!readonly && connected && hovered && !view.pendingConnection.active);
            }

            // A pin on the node's edge: filled when connected, a ring when not, with an
            // outline in the node's color. The hit region is the full element rect.
            const auto& socketProp = view.nodes[n].properties[propIdx];
            const auto& endpoint   = isOutput ? socketProp.output : socketProp.input;
            const bool  connected  = endpoint.has_value() && endpoint->valid();
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
                                view.wireDrag = WireSegmentDrag{
                                    .active = true, .wireKey = wireKey, .role = role,
                                    .vertical = vertical, .startScreen = event.state.screenPos,
                                    .startValue = startValue };
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
                                    view.wireDrag = {};
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
                    LayoutOverride{}.FixedSize(rw, rh).Absolute(rx, ry));
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
                                view.nodes[n].collapsed = !view.nodes[n].collapsed;
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
                            view.nodes[n].isDragging = false;
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
                .flags    = ElementFlags::PropagatesDescendantState | ElementFlags::Shadows,
                .state    = outerState
            };
            // MinWidth (not Fixed): shrink-wraps content, floored at the grid-snapped width.
            auto outerRes = elementStorage.Add(std::move(outer),
                LayoutOverride{}.MinWidth(nodeWidth).Absolute(node.pos.x, node.pos.y));
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
                headerLayout = LayoutOverride{}.Height(rowPitch - nodeTopChromeV);
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
                    BeginHorizontal(rowID, theme.nodeRowStyleRef, LayoutOverride{}.Height(rowH));

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
                            LayoutOverride{}.FixedSize(kSocketSize / 6, kSocketSize).NoGrow().NoShrink());
                        VERIFY(sp.status);
                        VERIFY(windowRegistry.Add(sp.ref));
                    }
                    // Input dot: OUT OF FLOW — absolutely placed straddling the node's
                    // left border at this property's FIRST pitch, mirroring the output
                    // dot on the right. Y is taken from the FULL pitch so it lands on a
                    // grid line even on the chrome-shortened last row.
                    if (prop.input.has_value())
                    {
                        const int16_t inY = static_cast<int16_t>((rowPitch * span) / 2 - socketYOffset - kSocketSize / 2);
                        socketRefs[n][p * 2 + 0] = emitSocket(n, p, /*isOutput=*/false, /*readonly=*/false,
                            LayoutOverride{}.FixedSize(kSocketSize, kSocketSize).Absolute(-socketInsetL, inY));
                    }

                    // Body fills the row. The LabelGroup must begin at the depth renderBody
                    // emits into. Multi-pitch content (images) is centred.
                    if (span > 1) BeginHorizontal(theme.nodeBodyCenterStyleRef, LayoutOverride{}.FlexGrow(1));
                    else          BeginHorizontal(LayoutOverride{}.FlexGrow(1));
                    BeginLabelGroup(nodeLabelGroupID, /*defaultWidth=*/60,
                                    /*minWidth=*/30, /*maxWidth=*/120,
                                    /*resizable=*/false);
                    // A text-only body (NodeText) aligns to its sockets: left
                    // dot ⇒ left, right dot ⇒ right, both ⇒ centre.
                    nodeRowTextAlign = (prop.input.has_value() && prop.output.has_value()) ? TextAlign::Center
                                     :  prop.output.has_value()                            ? TextAlign::End
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
                            LayoutOverride{}.FixedSize(kSocketSize / 6, kSocketSize).NoGrow().NoShrink());
                        VERIFY(rp.status);
                        VERIFY(windowRegistry.Add(rp.ref));
                    }
                    // Output dot: OUT OF FLOW — absolutely placed straddling the node's
                    // right border at this property's pitch (like the collapsed sockets).
                    if (prop.output.has_value())
                    {
                        const int16_t outY = static_cast<int16_t>((rowPitch * span) / 2 + socketYOffset - kSocketSize / 2);
                        socketRefs[n][p * 2 + 1] = emitSocket(n, p, /*isOutput=*/true, /*readonly=*/false,
                            LayoutOverride{}.FixedSize(kSocketSize, kSocketSize)
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
                            LayoutOverride{}.FixedSize(kSocketSize, kSocketSize).Absolute(leftX, y));
                    }
                    if (prop.output.has_value())
                    {
                        socketRefs[n][p * 2 + 1] = emitSocket(n, p, /*isOutput=*/true, /*readonly=*/true,
                            LayoutOverride{}.FixedSize(kSocketSize, kSocketSize).Absolute(rightX, y));
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
        // AddCustomDrawAfter anchors to the last element of the canvas subtree — a child
        // of the frontmost node — and is skipped with that node when it leaves view.
        // Endpoints are CANVAS_LAY, so they pan/zoom with the sockets and clip to the viewport.
        VERIFY(elementStorage.AddCustomDraw(canvasRes.ref,
            [this, &view, socketRefs = std::move(socketRefs), nodeRefs = std::move(nodeRefs)](interstice::CustomDrawContext& ctx)
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
                    // this sits on a grid line — wires run the lattice to the dot.
                    return ctx.GetElementRect(ref).Center();
                };

                // Node body rect, in the same CANVAS_LAY space as socketCenter,
                // so the router can keep wires clear of the source/target bodies.
                auto nodeRect = [&](int idx) -> interstice::IntRect
                {
                    return ctx.GetElementRect(nodeRefs[idx]);
                };

                const interstice::LineStyle ghostStyle { .colorStyleIndex = theme.lineGrayStyle,   .lineWidth = styleManager.Px(2.f), .dashSize = styleManager.Px(4.f) };

                // Draggable interior segments collected this frame (node.pos
                // space) for next frame's grab elements. Replaces the cache at
                // the end of this pass.
                std::vector<CachedWireHandle> handleCache;

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
                        const interstice::LineStyle wireStyle{ .colorStyleIndex = wireColorIdx, .lineWidth = styleManager.Px(2.f) };
                        ctx.RenderPolyline(route.points, /*closed=*/false, wireStyle, 0.5f * float(theme.nodeGridSize));

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

                if (view.pendingConnection.active)
                {
                    const PropertyEndpoint src = view.pendingConnection.source;
                    if (src.nodeIdx >= 0 && src.nodeIdx < (int)socketRefs.size())
                    {
                        const auto& srcRefs = socketRefs[src.nodeIdx];
                        const int srcOutIdx = src.propIdx * 2 + 1;
                        if (srcOutIdx >= 0 && srcOutIdx < (int)srcRefs.size() && srcRefs[srcOutIdx].IsValid())
                        {
                            const interstice::int2 outPt = socketCenter(srcRefs[srcOutIdx]);
                            const int32_t grid = theme.nodeGridSize;
                            // currentScreen is a screen-space input; ScreenToCurrent brings it
                            // into CANVAS_LAY, then snap to the grid so the dragged connector
                            // lands on grid lines just like a committed wire.
                            interstice::int2 cursorLay = ctx.ScreenToCurrent(view.pendingConnection.currentScreen);
                            if (grid > 0)
                                cursorLay = { GridLineNearest(cursorLay.x, gridOrigin.x, grid),
                                              GridLineNearest(cursorLay.y, gridOrigin.y, grid) };
                            // Same right-exit rule as a finished wire — leave the output one
                            // grid line to the right, then elbow to the (snapped) cursor.
                            const int32_t exitX = (grid > 0)
                                ? GridLineGE(outPt.x + grid, gridOrigin.x, grid) : outPt.x;
                            const interstice::int2 ghost[] = {
                                outPt, { exitX, outPt.y }, { exitX, cursorLay.y }, cursorLay };
                            ctx.RenderPolyline(ghost, /*closed=*/false, ghostStyle);
                        }
                    }
                }

                view.wireHandleCache = std::move(handleCache);
            }));

        // Marquee rectangle — drawn after the nodes so it sits on top of them.
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
    }

}
