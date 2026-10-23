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
                GridLineNearest((srcR + tgtL) / 2, gridOrigin.x, grid), srcR, tgtL);
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
                // Bodies overlap vertically — go around the nearer outside edge.
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

        // SCREEN → CANVAS_LAY via the inverse of the canvas's contentTransform,
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
        auto propertyAt = [&view](PropertyEndpoint e) -> NodeProperty*
        {
            if (e.nodeIdx < 0 || e.nodeIdx >= (int)view.nodes.size()) return nullptr;
            auto& props = view.nodes[e.nodeIdx].properties;
            return (e.propIdx >= 0 && e.propIdx < (int)props.size()) ? &props[e.propIdx] : nullptr;
        };
        NodeProperty* srcProp = propertyAt(output);
        NodeProperty* tgtProp = propertyAt(input);
        if (!srcProp || !tgtProp) return;

        // Detach the input's previous source and the output's previous target.
        if (tgtProp->input && tgtProp->input->valid())
            if (NodeProperty* prev = propertyAt(*tgtProp->input)) prev->output = PropertyEndpoint{};
        if (srcProp->output && srcProp->output->valid())
            if (NodeProperty* prev = propertyAt(*srcProp->output)) prev->input = PropertyEndpoint{};

        srcProp->output = input;
        tgtProp->input  = output;
    }

    void BringNodeToFront(std::vector<int>& ordering, int n)
    {
        const auto it = std::find(ordering.begin(), ordering.end(), n);
        if (it == ordering.end() || it + 1 == ordering.end()) return;
        std::rotate(it, it + 1, ordering.end());
    }
}
