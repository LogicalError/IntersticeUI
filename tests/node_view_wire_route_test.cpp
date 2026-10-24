// Unit tests for the node-view wire router (BuildWireRoute in
// gui_node_view_helpers). The router lays out one orthogonal wire from an
// output socket (on the source node's right edge) to an input socket (on the
// target node's left edge) and must keep the wire clear of both node bodies:
// thread the corridor/gap when there is one, go around both when there isn't.
//
// All coordinates live in one space (CANVAS_LAY at the call site); these tests
// just feed the router node rects + socket points and inspect the polyline.

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>
#include <gui_core/gui_base_types.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace interstice
{
    namespace
    {
        constexpr int32_t kGrid = 20;
        constexpr int2    kOrigin{ 0, 0 };

        // Inclusive AABB overlap. Every wire segment is axis-aligned, so a
        // segment equals its own bounding box and this is an exact hit test.
        bool SegHitsRect(int2 a, int2 b, IntRect r)
        {
            const int32_t sx0 = std::min(a.x, b.x), sx1 = std::max(a.x, b.x);
            const int32_t sy0 = std::min(a.y, b.y), sy1 = std::max(a.y, b.y);
            return sx0 <= r.x + r.w && sx1 >= r.x &&
                   sy0 <= r.y + r.h && sy1 >= r.y;
        }

        bool RectsOverlap(IntRect a, IntRect b)
        {
            return a.x < b.x + b.w && b.x < a.x + a.w &&
                   a.y < b.y + b.h && b.y < a.y + a.h;
        }

        // Every segment must be horizontal or vertical — never diagonal.
        void ExpectOrthogonal(const std::vector<int2>& route)
        {
            for (size_t i = 1; i < route.size(); ++i)
                EXPECT_TRUE(route[i].x == route[i - 1].x || route[i].y == route[i - 1].y)
                    << "segment " << (i - 1) << " is diagonal";
        }

        void ExpectEndpoints(const std::vector<int2>& route, int2 outPt, int2 inPt)
        {
            ASSERT_GE(route.size(), 2u);
            EXPECT_EQ(route.front().x, outPt.x);
            EXPECT_EQ(route.front().y, outPt.y);
            EXPECT_EQ(route.back().x,  inPt.x);
            EXPECT_EQ(route.back().y,  inPt.y);
        }

        // The wire body must not cross either node interior. Only the first stub
        // may touch the source (its output socket) and only the last stub may
        // touch the target (its input socket); nothing else touches either.
        // Interiors are deflated 1px so a wire grazing an edge isn't "through".
        void ExpectClearOfBodies(const std::vector<int2>& route, IntRect src, IntRect tgt)
        {
            ASSERT_GE(route.size(), 2u);
            const IntRect srcI = src.Deflate(1);
            const IntRect tgtI = tgt.Deflate(1);
            const int last = (int)route.size() - 2;   // index of the final segment
            for (int i = 0; i <= last; ++i)
            {
                const int2 a = route[i], b = route[i + 1];
                if (i != 0)
                    EXPECT_FALSE(SegHitsRect(a, b, srcI)) << "segment " << i << " crosses the source body";
                if (i != last)
                    EXPECT_FALSE(SegHitsRect(a, b, tgtI)) << "segment " << i << " crosses the target body";
            }
        }

        // Does the route contain a horizontal run whose y lands in [lo, hi]?
        bool HasHorizontalRunInY(const std::vector<int2>& route, int32_t lo, int32_t hi)
        {
            for (size_t i = 1; i < route.size(); ++i)
                if (route[i].y == route[i - 1].y && route[i].y >= lo && route[i].y <= hi)
                    return true;
            return false;
        }

        // Does the route contain a vertical run whose x lands in [lo, hi]?
        bool HasVerticalRunInX(const std::vector<int2>& route, int32_t lo, int32_t hi)
        {
            for (size_t i = 1; i < route.size(); ++i)
                if (route[i].x == route[i - 1].x && route[i].x >= lo && route[i].x <= hi)
                    return true;
            return false;
        }
    }

    TEST(NodeViewWireRouteTest, TargetToTheRight_ThreadsCorridorBetweenBodies)
    {
        const IntRect src{ 0,   0, 180, 80 };
        const IntRect tgt{ 300, 200, 180, 80 };      // clear horizontal corridor [180, 300]
        const int2 outPt{ src.Max(0) - 6,  40 };
        const int2 inPt { tgt.Min(0) + 6, 240 };

        const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin).points;

        ExpectEndpoints(route, outPt, inPt);
        ExpectOrthogonal(route);
        ExpectClearOfBodies(route, src, tgt);
        EXPECT_TRUE(HasVerticalRunInX(route, src.Max(0), tgt.Min(0)))
            << "expected the vertical lane to run through the corridor between the bodies";
    }

    TEST(NodeViewWireRouteTest, TargetBelowAndLeft_ThreadsVerticalGap)
    {
        // Target overlaps the source in X (no corridor) but sits below it with a
        // clear vertical gap [80, 160]. The return lane should thread that gap.
        const IntRect src{ 0,    0, 180, 80 };
        const IntRect tgt{ -100, 160, 180, 80 };
        const int2 outPt{ src.Max(0) - 6,  30 };
        const int2 inPt { tgt.Min(0) + 6, 200 };

        const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin).points;

        ExpectEndpoints(route, outPt, inPt);
        ExpectOrthogonal(route);
        ExpectClearOfBodies(route, src, tgt);
        EXPECT_TRUE(HasHorizontalRunInY(route, src.Max(1), tgt.Min(1)))
            << "expected the return lane to thread the vertical gap between the bodies";
    }

    TEST(NodeViewWireRouteTest, TargetLeftSameHeight_RoutesAroundBothBodies)
    {
        // Target sits entirely to the LEFT and overlaps in Y — there is no gap
        // to thread, so the wire must go around the outside of both bodies.
        const IntRect src{ 0,    0, 180, 80 };
        const IntRect tgt{ -300, 20, 180, 80 };
        const int2 outPt{ src.Max(0) - 6, 40 };
        const int2 inPt { tgt.Min(0) + 6, 60 };

        const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin).points;

        ExpectEndpoints(route, outPt, inPt);
        ExpectOrthogonal(route);
        ExpectClearOfBodies(route, src, tgt);

        const int32_t top = std::min(src.Min(1), tgt.Min(1));
        const int32_t bot = std::max(src.Max(1), tgt.Max(1));
        const bool around = HasHorizontalRunInY(route, INT32_MIN, top) ||
                            HasHorizontalRunInY(route, bot, INT32_MAX);
        EXPECT_TRUE(around) << "expected the return lane to route above or below both bodies";
    }

    TEST(NodeViewWireRouteTest, TargetDirectlyRightSameRow_IsStraightLine)
    {
        const IntRect src{ 0,   0, 180, 80 };
        const IntRect tgt{ 300, 0, 180, 80 };
        const int2 outPt{ src.Max(0) - 6, 40 };
        const int2 inPt { tgt.Min(0) + 6, 40 };       // same row

        const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin).points;

        ExpectEndpoints(route, outPt, inPt);
        ExpectOrthogonal(route);
        ExpectClearOfBodies(route, src, tgt);
        for (const int2 v : route)
            EXPECT_EQ(v.y, 40) << "a same-row wire should stay on one horizontal line";
    }

    TEST(NodeViewWireRouteTest, EmptyNodeRect_FallsBackToDirectElbow)
    {
        const int2 outPt{ 10, 10 };
        const int2 inPt { 90, 70 };
        const auto route = BuildWireRoute(outPt, inPt, IntRect{}, IntRect{ 80, 40, 40, 40 }, kGrid, kOrigin).points;

        ASSERT_EQ(route.size(), 2u);
        ExpectEndpoints(route, outPt, inPt);
    }

    TEST(NodeViewWireRouteTest, ZeroGrid_FallsBackToDirectElbow)
    {
        const int2 outPt{ 10, 10 };
        const int2 inPt { 90, 70 };
        const auto route = BuildWireRoute(outPt, inPt, IntRect{ 0, 0, 40, 40 }, IntRect{ 80, 40, 40, 40 },
                                          /*grid=*/0, kOrigin).points;

        ASSERT_EQ(route.size(), 2u);
        ExpectEndpoints(route, outPt, inPt);
    }

    TEST(NodeViewWireRouteTest, SweepOfTargetPositions_WireNeverCrossesABody)
    {
        // Property sweep: a fixed source, the target slid through a grid of
        // non-overlapping positions all around it. For every layout the wire
        // must stay orthogonal, keep its endpoints, and never cross a body.
        const IntRect src{ 0, 0, 180, 80 };
        const int2 outPt{ src.Max(0) - 6, src.Center().y };

        int tested = 0;
        for (int ty = -300; ty <= 300; ty += 20)
        for (int tx = -440; tx <= 440; tx += 20)
        {
            const IntRect tgt{ tx, ty, 180, 80 };
            // The router assumes nodes don't overlap; skip touching/overlapping layouts.
            if (RectsOverlap(tgt, src.Inflate(2)))
                continue;
            const int2 inPt{ tgt.Min(0) + 6, tgt.Center().y };

            const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin).points;
            ExpectEndpoints(route, outPt, inPt);
            ExpectOrthogonal(route);
            ExpectClearOfBodies(route, src, tgt);
            ++tested;
        }
        EXPECT_GT(tested, 100) << "sweep should exercise many non-overlapping layouts";
    }

    TEST(NodeViewWireRouteTest, Handles_CorridorHasOneVerticalLane)
    {
        const IntRect src{ 0, 0, 180, 80 };
        const IntRect tgt{ 300, 200, 180, 80 };
        const auto route = BuildWireRoute({ src.Max(0) - 6, 40 }, { tgt.Min(0) + 6, 240 },
                                          src, tgt, kGrid, kOrigin);
        ASSERT_EQ(route.handles.size(), 1u);
        EXPECT_EQ(route.handles[0].role, WireHandle::LaneX);
        EXPECT_TRUE(route.handles[0].vertical);
        EXPECT_EQ(route.handles[0].a.x, route.handles[0].b.x);   // genuinely vertical
    }

    TEST(NodeViewWireRouteTest, Handles_DetourHasTwoVerticalsAndOneHorizontal)
    {
        const IntRect src{ 0, 0, 180, 80 };
        const IntRect tgt{ -300, 20, 180, 80 };   // around-case detour shape
        const auto route = BuildWireRoute({ src.Max(0) - 6, 40 }, { tgt.Min(0) + 6, 60 },
                                          src, tgt, kGrid, kOrigin);
        ASSERT_EQ(route.handles.size(), 3u);
        EXPECT_EQ(route.handles[0].role, WireHandle::ExitX);  EXPECT_TRUE (route.handles[0].vertical);
        EXPECT_EQ(route.handles[1].role, WireHandle::LaneY);  EXPECT_FALSE(route.handles[1].vertical);
        EXPECT_EQ(route.handles[2].role, WireHandle::EntryX); EXPECT_TRUE (route.handles[2].vertical);
        EXPECT_EQ(route.handles[0].a.x, route.handles[0].b.x);   // vertical
        EXPECT_EQ(route.handles[1].a.y, route.handles[1].b.y);   // horizontal
        EXPECT_EQ(route.handles[2].a.x, route.handles[2].b.x);   // vertical
    }

    TEST(NodeViewWireRouteTest, Override_SnapsCorridorLaneToGridAndClampsIntoCorridor)
    {
        const IntRect src{ 0,   0, 180, 80 };
        const IntRect tgt{ 300, 200, 180, 80 };
        const int2 outPt{ src.Max(0) - 6,  40 };
        const int2 inPt { tgt.Min(0) + 6, 240 };

        // An in-range override snaps to the nearest grid line (250 -> 240 @ grid 20).
        WireRouteOverrides ov; ov.laneX = 250;
        const auto r1 = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin, ov);
        ASSERT_EQ(r1.handles.size(), 1u);
        EXPECT_EQ(r1.handles[0].a.x, 240);
        EXPECT_EQ(r1.handles[0].a.x % kGrid, 0) << "dragged lane must land on a grid line";

        // Out-of-range override (deep inside the source) clamps into the
        // corridor, stays on the grid, and the wire still clears both bodies.
        ov.laneX = -1000;
        const auto r2 = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin, ov);
        ASSERT_EQ(r2.handles.size(), 1u);
        EXPECT_GE(r2.handles[0].a.x, src.Max(0));
        EXPECT_LE(r2.handles[0].a.x, tgt.Min(0));
        EXPECT_EQ(r2.handles[0].a.x % kGrid, 0);
        ExpectClearOfBodies(r2.points, src, tgt);
    }

    TEST(NodeViewWireRouteTest, Override_ReturnLaneClampedToGapStaysClear)
    {
        // Source above target with a vertical gap [80,160]. Dragging the return
        // lane far down (past the target) clamps back into the gap.
        const IntRect src{ 0,    0, 180, 80 };
        const IntRect tgt{ -100, 160, 180, 80 };
        const int2 outPt{ src.Max(0) - 6,  30 };
        const int2 inPt { tgt.Min(0) + 6, 200 };

        WireRouteOverrides ov; ov.laneY = 10000;   // far below the target
        const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin, ov);
        ExpectClearOfBodies(route.points, src, tgt);

        bool foundReturnLane = false;
        for (const auto& h : route.handles)
            if (h.role == WireHandle::LaneY)
            {
                foundReturnLane = true;
                EXPECT_GE(h.a.y, src.Max(1));
                EXPECT_LE(h.a.y, tgt.Min(1));
            }
        EXPECT_TRUE(foundReturnLane);
    }

    // A wire dragged over an input that accepts it has exactly the route of the
    // connection it will make.
    TEST(NodeViewWireRouteTest, ADraggedWireOverAnInputMatchesTheConnection)
    {
        const IntRect src{ 0,  0,   180, 80 };
        const IntRect tgt{ 40, 200, 180, 80 };   // below, overlapping in X: a detour
        const int2 outPt{ src.Max(0) - 6, 40 };
        const int2 inPt { tgt.Min(0) + 6, 240 };
        const int2 cursor{ inPt.x + 20, inPt.y };

        const auto dragged   = BuildPendingWireRoute(outPt, src, cursor, inPt, tgt, kGrid, kOrigin).points;
        const auto connected = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin).points;
        ASSERT_EQ(dragged.size(), connected.size());
        for (size_t i = 0; i < dragged.size(); ++i)
        {
            EXPECT_EQ(dragged[i].x, connected[i].x) << i;
            EXPECT_EQ(dragged[i].y, connected[i].y) << i;
        }
    }

    // Elsewhere it runs to the cursor as a connection to an input there would: out to
    // the right, bending on the grid and around the source, never as a plain diagonal.
    TEST(NodeViewWireRouteTest, ADraggedWireRunsToTheCursorLikeAConnection)
    {
        const IntRect src{ 200, 0, 180, 80 };
        const int2 outPt { src.Max(0) - 6, 40 };
        for (const int2 cursor : { int2{ 600, 200 }, int2{ 100, 300 }, int2{ 300, 40 } })
        {
            const auto route = BuildPendingWireRoute(outPt, src, cursor, std::nullopt, {}, kGrid, kOrigin).points;
            ExpectEndpoints(route, outPt, cursor);
            ExpectOrthogonal(route);
            ASSERT_GE(route.size(), 3u);
            EXPECT_GT(route[1].x, outPt.x) << "leaves the output to the right";
            const IntRect atCursor{ cursor.x, cursor.y, 1, 1 };
            const auto expected = BuildWireRoute(outPt, cursor, src, atCursor, kGrid, kOrigin).points;
            EXPECT_EQ(route.size(), expected.size());
        }
    }

    // Sockets in line with each other: one straight run, no bends that go straight on
    // (a rounded corner there would cut a gap into the line).
    TEST(NodeViewWireRouteTest, SocketsInLineGiveOneStraightRun)
    {
        const IntRect src{ 0,   0, 180, 80 };
        const IntRect tgt{ 300, 0, 180, 80 };
        const int2 outPt{ 174, 40 }, inPt{ 306, 40 };
        for (const int32_t offset : { 0, -1, 1 })
        {
            WireRouteOverrides o;
            o.laneOffset = offset;
            const auto route = BuildWireRoute(outPt, inPt, src, tgt, kGrid, kOrigin, o).points;
            ASSERT_EQ(route.size(), 2u) << offset;
            ExpectEndpoints(route, outPt, inPt);
        }
    }
}
