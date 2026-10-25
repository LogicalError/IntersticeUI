#include <gui_controls/gui_node_view_helpers.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/graphics/gui_renderer.h>   // LineStyle

#include <algorithm>
#include <cmath>

namespace interstice
{
    namespace
    {
        constexpr float kMinZoom = 0.1f;
        constexpr float kMaxZoom = 1.0f;
    }

    float NodeViewZoom(const NodeView& view)
    {
        const float3x2& t = view.transform;
        float zoom = std::sqrt((t[0].x * t[0].x + t[0].y * t[0].y + t[1].x * t[1].x + t[1].y * t[1].y) * 0.5f);
        if (t[0].x * t[0].y - t[1].x * t[1].y < 0.0f) zoom = -zoom;
        return zoom;
    }

    void ZoomNodeView(NodeView& view, float factor, int2 pivot)
    {
        const float current = NodeViewZoom(view);
        const float target  = current * factor;
        if (current == 0.0f)
            return;
        if (target < kMinZoom || target > kMaxZoom)
            view.transform = view.transform * float3x2::Scale(std::clamp(target, kMinZoom, kMaxZoom) / current);
        else
            view.transform = float3x2::Translate(pivot) * float3x2::Scale(factor) * float3x2::Translate(-pivot) * view.transform;
    }

    void SetNodeViewCenter(NodeView& view, float2 center, float zoom)
    {
        view.transform = float3x2::Scale(std::clamp(zoom, kMinZoom, kMaxZoom))
                       * float3x2::Translate(float2{ -center.x, -center.y });
    }

    void FrameAllNodes(NodeView& view)
    {
        if (view.nodes.empty())
            return;
        int2 bmin = view.nodes[0].pos;
        int2 bmax = bmin;
        for (const auto& node : view.nodes)
        {
            bmin.x = std::min(bmin.x, node.pos.x);
            bmin.y = std::min(bmin.y, node.pos.y);
            bmax.x = std::max(bmax.x, node.pos.x + node.lastSize.x);
            bmax.y = std::max(bmax.y, node.pos.y + node.lastSize.y);
        }
        const int bw = std::max(1, bmax.x - bmin.x);
        const int bh = std::max(1, bmax.y - bmin.y);
        const int2 vp = view.lastInnerSize;
        const float zoom = (vp.x > 0 && vp.y > 0)
                         ? std::min((float)vp.x / (float)bw, (float)vp.y / (float)bh) * 0.9f
                         : 1.0f;
        SetNodeViewCenter(view, float2{ (float)((bmin.x + bmax.x) / 2), (float)((bmin.y + bmax.y) / 2) }, zoom);
    }

    void RenderCanvasGrid(CustomDrawContext& ctx, int32_t baseGridSize,
                          color4f dotColor, float dotSize, int2 originOffset, IntRect clipCurrent)
    {
        if (baseGridSize <= 0) return;

        // Visible region in the current space: bounding box of all four clip corners
        // mapped back (all four, so axis flips work).
        const IntRect clip = ctx.ClipRect();
        if (clip.w <= 0 || clip.h <= 0) return;
        const float3x2 wlToCurrent = ctx.CurrentTransform().Inverse();
        auto toCurrent = [&](int2 wl) {
            const float2 p = wlToCurrent.TransformPoint({ (float)wl.x, (float)wl.y });
            return int2{ (int)p.x, (int)p.y };
        };
        const int2 c0 = toCurrent({ clip.x,          clip.y          });
        const int2 c1 = toCurrent({ clip.x + clip.w, clip.y          });
        const int2 c2 = toCurrent({ clip.x,          clip.y + clip.h });
        const int2 c3 = toCurrent({ clip.x + clip.w, clip.y + clip.h });
        int minX = std::min({ c0.x, c1.x, c2.x, c3.x });
        int maxX = std::max({ c0.x, c1.x, c2.x, c3.x });
        int minY = std::min({ c0.y, c1.y, c2.y, c3.y });
        int maxY = std::max({ c0.y, c1.y, c2.y, c3.y });

        // Clamp to the content box (the screen scissor is the wider border box, so dots
        // would otherwise bleed into the padding).
        if (clipCurrent.w > 0 && clipCurrent.h > 0)
        {
            minX = std::max(minX, clipCurrent.x);
            minY = std::max(minY, clipCurrent.y);
            maxX = std::min(maxX, clipCurrent.x + clipCurrent.w);
            maxY = std::min(maxY, clipCurrent.y + clipCurrent.h);
            if (minX > maxX || minY > maxY) return;
        }

        // One base cell in window-local pixels, measured over a wide span for precision at low zoom.
        constexpr int kSpan = 1024;
        const float3x2 currentToWL = ctx.CurrentTransform();
        const float2 s0 = currentToWL.TransformPoint({ 0.0f, 0.0f });
        const float2 sx = currentToWL.TransformPoint({ (float)(baseGridSize * kSpan), 0.0f });
        const float  screenPitch = std::hypot(sx.x - s0.x, sx.y - s0.y) / (float)kSpan;
        if (screenPitch <= 0.01f) return;

        // LOD: double the pitch each time the view is zoomed out by 2x.
        // ratio == baseGridSize / screenPitch == 1 / zoomScale.
        int   level = 0;
        float ratio = (float)baseGridSize / screenPitch;
        while (ratio >= 2.0f && level < 24) { ratio *= 0.5f; ++level; }
        const int step = baseGridSize << level;

        // First line at or below minV on the lattice originOffset + k*step.
        auto firstLine = [step](int minV, int origin) {
            int rel = minV - origin;
            int q   = rel / step;
            if ((rel % step) != 0 && rel < 0) --q;   // floor for negatives
            return origin + q * step;
        };

        // Emit in window-local space, snapped to whole pixels (the dot shader adds the half
        // pixel): transformed canvas coords land on sub-pixels, and a 1px dot would smear across two.
        ctx.SetSpaceRaw(float3x2::Identity());
        const int firstX = firstLine(minX, originOffset.x);
        for (int y = firstLine(minY, originOffset.y); y <= maxY; y += step)
            for (int x = firstX; x <= maxX; x += step)
            {
                const float2 p = currentToWL.TransformPoint({ (float)x, (float)y });
                ctx.RenderDot({ std::floor(p.x), std::floor(p.y) }, dotSize, dotColor);
            }
        ctx.SetSpace(DrawSpace::Local);
    }

    std::unordered_map<uint32_t, int32_t> ComputeWireLaneOffsets(const NodeView& view,
                                                                  PropertyEndpoint pendingOut,
                                                                  PropertyEndpoint pendingIn)
    {
        auto pairKey = [](int srcNode, int tgtNode) { return (uint64_t(uint32_t(srcNode)) << 32) | uint32_t(tgtNode); };
        const bool pending = pendingOut.valid() && pendingIn.valid();

        // Wires per (source node, target node). Connecting replaces the input's wire.
        std::unordered_map<uint64_t, std::vector<uint32_t>> groups;
        for (int n = 0; n < (int)view.nodes.size(); ++n)
            for (int p = 0; p < (int)view.nodes[n].properties.size(); ++p)
            {
                const auto& input = view.nodes[n].properties[p].input;
                if (!input.has_value() || !input->valid())
                    continue;
                if (pending && PropertyEndpoint{ n, p } == pendingIn)
                    continue;
                groups[pairKey(input->nodeIdx, n)].push_back(WireKey(n, p));
            }
        if (pending)
            groups[pairKey(pendingOut.nodeIdx, pendingIn.nodeIdx)].push_back(WireKey(pendingIn.nodeIdx, pendingIn.propIdx));

        std::unordered_map<uint32_t, int32_t> offsets;
        for (auto& [pair, wires] : groups)
        {
            std::sort(wires.begin(), wires.end());   // input order: same target node, by property
            const int32_t first = (int32_t(wires.size()) - 1) / 2;
            for (size_t i = 0; i < wires.size(); ++i)
                offsets[wires[i]] = first - int32_t(i);
        }
        return offsets;
    }

    std::optional<uint32_t> WireAt(std::span<const DrawnWire> wires, int2 p, float tolerance)
    {
        std::optional<uint32_t> best;
        float bestDistance = tolerance;
        for (const DrawnWire& wire : wires)
            for (size_t i = 0; i + 1 < wire.points.size(); ++i)
            {
                // Distance from p to the segment.
                const float ax = float(wire.points[i].x),     ay = float(wire.points[i].y);
                const float bx = float(wire.points[i + 1].x), by = float(wire.points[i + 1].y);
                const float dx = bx - ax, dy = by - ay;
                const float lengthSq = dx * dx + dy * dy;
                float t = lengthSq > 0.0f ? ((float(p.x) - ax) * dx + (float(p.y) - ay) * dy) / lengthSq : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const float ex = float(p.x) - (ax + t * dx), ey = float(p.y) - (ay + t * dy);
                const float distance = std::sqrt(ex * ex + ey * ey);
                if (distance <= bestDistance)
                {
                    bestDistance = distance;
                    best = wire.wireKey;
                }
            }
        return best;
    }

    namespace
    {
        NodeProperty* PropertyAt(NodeView& view, PropertyEndpoint e)
        {
            if (e.nodeIdx < 0 || e.nodeIdx >= (int)view.nodes.size()) return nullptr;
            auto& props = view.nodes[e.nodeIdx].properties;
            return (e.propIdx >= 0 && e.propIdx < (int)props.size()) ? &props[e.propIdx] : nullptr;
        }
    }

    void ApplyNodeViewEdit(NodeView& view, const NodeViewEdit& edit, bool undo)
    {
        // Undo walks the changes backwards, so one that builds on another is undone first.
        auto each = [undo](const auto& changes, auto&& apply)
        {
            if (undo) for (auto it = changes.rbegin(); it != changes.rend(); ++it) apply(*it, it->from);
            else      for (const auto& change : changes)                         apply(change, change.to);
        };
        const int nodeCount = (int)view.nodes.size();
        each(edit.moves, [&](const NodeViewEdit::Move& m, int2 pos)
        {
            if (m.node >= 0 && m.node < nodeCount) view.nodes[m.node].pos = pos;
        });
        each(edit.collapses, [&](const NodeViewEdit::Collapse& c, bool collapsed)
        {
            if (c.node >= 0 && c.node < nodeCount) view.nodes[c.node].collapsed = collapsed;
        });
        each(edit.rewires, [&](const NodeViewEdit::Rewire& r, PropertyEndpoint source)
        {
            if (NodeProperty* prop = PropertyAt(view, r.input); prop && prop->input.has_value())
                prop->input = source;
        });
        each(edit.lanes, [&](const NodeViewEdit::Lanes& l, const WireRouteOverrides& lanes)
        {
            if (lanes == WireRouteOverrides{}) view.wireOverrides.erase(l.wireKey);
            else                               view.wireOverrides[l.wireKey] = lanes;
        });
    }

    void CommitNodeViewEdit(NodeView& view, const NodeViewEdit& edit)
    {
        if (edit.empty()) return;
        ApplyNodeViewEdit(view, edit);
        if (view.onEdit) view.onEdit(edit);
    }

    NodeViewEdit ConnectEdit(const NodeView& view, PropertyEndpoint output, PropertyEndpoint input)
    {
        NodeViewEdit edit;
        if (input.nodeIdx < 0 || input.nodeIdx >= (int)view.nodes.size()) return edit;
        const auto& props = view.nodes[input.nodeIdx].properties;
        if (input.propIdx < 0 || input.propIdx >= (int)props.size() || !props[input.propIdx].input.has_value()) return edit;
        const PropertyEndpoint before = *props[input.propIdx].input;
        if (!(before == output))
            edit.rewires.push_back({ input, before, output });
        return edit;
    }

    NodeViewEdit DisconnectEdit(const NodeView& view, PropertyEndpoint input)
    {
        NodeViewEdit edit = ConnectEdit(view, PropertyEndpoint{}, input);
        if (const auto it = view.wireOverrides.find(WireKey(input.nodeIdx, input.propIdx)); it != view.wireOverrides.end())
            edit.lanes.push_back({ it->first, it->second, WireRouteOverrides{} });
        return edit;
    }

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

    PropertyEndpoint FirstInputAccepting(const NodeView& view, PropertyEndpoint output, int nodeIdx)
    {
        if (nodeIdx < 0 || nodeIdx >= (int)view.nodes.size())
            return {};
        const auto& properties = view.nodes[nodeIdx].properties;
        for (int p = 0; p < (int)properties.size(); ++p)
        {
            const PropertyEndpoint in{ nodeIdx, p };
            if (properties[p].input.has_value() && ConnectionAllowed(view, output, in))
                return in;
        }
        return {};
    }

    WireRoute BuildPendingWireRoute(int2 outPt, IntRect srcRect, int2 cursor,
                                    std::optional<int2> inPt, IntRect tgtRect,
                                    int32_t grid, int2 gridOrigin, int32_t laneOffset)
    {
        if (inPt)
        {
            WireRouteOverrides overrides;
            overrides.laneOffset = laneOffset;
            return BuildWireRoute(outPt, *inPt, srcRect, tgtRect, grid, gridOrigin, overrides);
        }
        return BuildWireRoute(outPt, cursor, srcRect, IntRect{ cursor.x, cursor.y, 1, 1 }, grid, gridOrigin);
    }

    WireRoute BuildWireRoute(int2 outPt, int2 inPt,
                             IntRect srcRect, IntRect tgtRect,
                             int32_t grid, int2 gridOrigin,
                             const WireRouteOverrides& overrides)
    {
        WireRoute route;
        std::vector<int2>& pts = route.points;
        pts.reserve(6);
        pts.push_back(outPt);

        // No grid, or a node not laid out yet: fall back to a plain elbow.
        if (grid <= 0 || srcRect.IsEmpty() || tgtRect.IsEmpty())
        {
            pts.push_back(inPt);
            return route;
        }

        const int32_t srcR = srcRect.Max(0);   // source right edge
        const int32_t tgtL = tgtRect.Min(0);   // target left edge

        if (tgtL > srcR)
        {
            // Corridor between the bodies: one vertical lane, on the grid when it fits.
            // An override is clamped to the corridor.
            int32_t laneX = std::clamp(
                GridLineNearest((srcR + tgtL) / 2, gridOrigin.x, grid) + overrides.laneOffset * grid, srcR, tgtL);
            if (overrides.laneX)
                laneX = std::clamp(GridLineNearest(*overrides.laneX, gridOrigin.x, grid), srcR, tgtL);

            pts.push_back({ laneX, outPt.y });
            pts.push_back({ laneX, inPt.y  });
            route.handles.push_back(WireSegmentHandle{
                { laneX, outPt.y }, { laneX, inPt.y }, WireHandle::LaneX, /*vertical=*/true });
        }
        else
        {
            // No corridor: exit a cell right of the output, enter a cell left of the input,
            // joined by a return lane clear of both bodies. Overrides are clamped likewise.
            int32_t exitX  = GridLineGE(srcR + grid, gridOrigin.x, grid);
            int32_t entryX = GridLineLE(tgtL - grid, gridOrigin.x, grid);
            if (overrides.exitX)
                exitX  = std::max(GridLineNearest(*overrides.exitX, gridOrigin.x, grid),
                                  GridLineGE(srcR + 1, gridOrigin.x, grid));   // nearest clearing grid line
            if (overrides.entryX)
                entryX = std::min(GridLineNearest(*overrides.entryX, gridOrigin.x, grid),
                                  GridLineLE(tgtL - 1, gridOrigin.x, grid));

            int32_t laneY;
            if (srcRect.Max(1) < tgtRect.Min(1))        // source fully above target
            {
                const int32_t lo = srcRect.Max(1), hi = tgtRect.Min(1);
                laneY = std::clamp(GridLineNearest((lo + hi) / 2, gridOrigin.y, grid), lo, hi);
                if (overrides.laneY)
                    laneY = std::clamp(GridLineNearest(*overrides.laneY, gridOrigin.y, grid), lo, hi);
            }
            else if (tgtRect.Max(1) < srcRect.Min(1))   // target fully above source
            {
                const int32_t lo = tgtRect.Max(1), hi = srcRect.Min(1);
                laneY = std::clamp(GridLineNearest((lo + hi) / 2, gridOrigin.y, grid), lo, hi);
                if (overrides.laneY)
                    laneY = std::clamp(GridLineNearest(*overrides.laneY, gridOrigin.y, grid), lo, hi);
            }
            else
            {
                // Bodies overlap vertically - go around the nearer outside edge.
                const int32_t minTop = std::min(srcRect.Min(1), tgtRect.Min(1));
                const int32_t maxBot = std::max(srcRect.Max(1), tgtRect.Max(1));
                const int32_t topY = GridLineLE(minTop - grid, gridOrigin.y, grid);
                const int32_t botY = GridLineGE(maxBot + grid, gridOrigin.y, grid);
                const int32_t ref  = (outPt.y + inPt.y) / 2;
                laneY = (ref - topY <= botY - ref) ? topY : botY;
                if (overrides.laneY)
                {
                    // Keep the lane outside the bodies' combined vertical span;
                    // an override that lands inside snaps to the nearer edge.
                    const int32_t v = GridLineNearest(*overrides.laneY, gridOrigin.y, grid);
                    if      (v <= minTop) laneY = v;
                    else if (v >= maxBot) laneY = v;
                    else laneY = (v - minTop <= maxBot - v) ? (minTop - 1) : (maxBot + 1);
                }
            }

            pts.push_back({ exitX,  outPt.y });
            pts.push_back({ exitX,  laneY   });
            pts.push_back({ entryX, laneY   });
            pts.push_back({ entryX, inPt.y  });
            route.handles.push_back(WireSegmentHandle{ { exitX, outPt.y }, { exitX, laneY  }, WireHandle::ExitX,  true  });
            route.handles.push_back(WireSegmentHandle{ { exitX, laneY   }, { entryX, laneY }, WireHandle::LaneY,  false });
            route.handles.push_back(WireSegmentHandle{ { entryX, laneY  }, { entryX, inPt.y }, WireHandle::EntryX, true  });
        }
        pts.push_back(inPt);

        pts.erase(std::unique(pts.begin(), pts.end(),
                              [](int2 a, int2 b){ return a.x == b.x && a.y == b.y; }),
                  pts.end());
        // Bends that go straight on (sockets in line with a lane) aren't bends.
        for (size_t i = 1; i + 1 < pts.size(); )
        {
            auto between = [](int32_t a, int32_t v, int32_t b) { return std::min(a, b) <= v && v <= std::max(a, b); };
            const bool straight = (pts[i - 1].x == pts[i].x && pts[i].x == pts[i + 1].x && between(pts[i - 1].y, pts[i].y, pts[i + 1].y))
                               || (pts[i - 1].y == pts[i].y && pts[i].y == pts[i + 1].y && between(pts[i - 1].x, pts[i].x, pts[i + 1].x));
            if (straight) pts.erase(pts.begin() + ptrdiff_t(i));
            else          ++i;
        }

        // A lane can collapse onto a socket (e.g. the sockets share a row), in
        // which case its segment is zero-length and not draggable.
        route.handles.erase(
            std::remove_if(route.handles.begin(), route.handles.end(),
                           [](const WireSegmentHandle& h){ return h.a.x == h.b.x && h.a.y == h.b.y; }),
            route.handles.end());

        return route;
    }

    std::unordered_set<ID> ComputeNodesInMarquee(
        std::span<const NodeModel> nodes,
        IntRect                    canvasInnerRectScreen,
        const float3x2&            viewTransform,
        int2                       marqueeStartScreen,
        int2                       marqueeCurrentScreen)
    {
        std::unordered_set<ID> hits;

        // SCREEN -> CANVAS_LAY via the inverse of the canvas's contentTransform,
        // which is T(innerSize/2) * viewTransform. So:
        //   CANVAS_LAY = inverse(viewTransform)(SCREEN - innerRect.position - innerSize/2)
        const int2 canvasCenter = canvasInnerRectScreen.position
                                + int2{ canvasInnerRectScreen.w / 2,
                                        canvasInnerRectScreen.h / 2 };
        const float3x2 viewInv = viewTransform.Inverse();
        auto screenToCanvasLay = [&](int2 screenPt) -> int2 {
            const float2 v = viewInv.TransformPoint(
                float2{ (float)(screenPt.x - canvasCenter.x),
                        (float)(screenPt.y - canvasCenter.y) });
            return int2{ (int)v.x, (int)v.y };
        };

        const int2 startLay   = screenToCanvasLay(marqueeStartScreen);
        const int2 currentLay = screenToCanvasLay(marqueeCurrentScreen);
        const int mqMinX = std::min(startLay.x, currentLay.x);
        const int mqMinY = std::min(startLay.y, currentLay.y);
        const int mqMaxX = std::max(startLay.x, currentLay.x);
        const int mqMaxY = std::max(startLay.y, currentLay.y);

        for (const auto& node : nodes)
        {
            if (node.lastSize.x == 0 || node.lastSize.y == 0) continue;
            const int nodeMinX = node.pos.x;
            const int nodeMinY = node.pos.y;
            const int nodeMaxX = node.pos.x + node.lastSize.x;
            const int nodeMaxY = node.pos.y + node.lastSize.y;
            // Strict containment: node fully inside marquee.
            if (nodeMinX >= mqMinX && nodeMaxX <= mqMaxX &&
                nodeMinY >= mqMinY && nodeMaxY <= mqMaxY)
            {
                hits.insert(node.stableID);
            }
        }
        return hits;
    }

    void ConnectSockets(NodeView& view, PropertyEndpoint output, PropertyEndpoint input)
    {
        ApplyNodeViewEdit(view, ConnectEdit(view, output, input));
    }

    void BringNodeToFront(std::vector<int>& ordering, int n)
    {
        const auto it = std::find(ordering.begin(), ordering.end(), n);
        if (it == ordering.end() || it + 1 == ordering.end()) return;
        std::rotate(it, it + 1, ordering.end());
    }
}
