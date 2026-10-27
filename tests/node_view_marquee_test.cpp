// Unit tests for the node-view marquee selection math (panel_node_view.cpp's
// rect-drag -> selected-node-set computation). The helper lives in
// gui_node_view_helpers.h and is pure (no GUI pipeline / framework state),
// so these tests just construct inputs and check the output set.
//
// Coordinate spaces (see panel_node_view.cpp top-of-function docs):
//   SCREEN     : absolute window pixels (mouse positions).
//   CANVAS_LAY : canvas-local LAYOUT, where NodeModel::pos lives.
// The helper converts the marquee corners SCREEN -> CANVAS_LAY using the
// canvas's inner-rect screen position + view.transform, then strict-contains.

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>

#include <vector>

namespace tachyon
{
    namespace
    {
        // NodeModel at a CANVAS_LAY pos with a layout size; idx becomes its stableID.
        NodeModel MakeNode(uint32_t idx, int2 pos, int2 size)
        {
            NodeModel n;
            // The helper only compares stableIDs, so any distinct IDs work.
            n.stableID = ID{ idx };
            n.pos      = pos;
            n.lastSize = size;
            return n;
        }

        // Canvas geometry used across the tests. innerRect is the canvas's
        // padding-box in SCREEN; center is at (500, 400).
        constexpr IntRect kCanvasInner { 100, 100, 800, 600 };
    }

    // Identity transform: SCREEN (X, Y) maps to CANVAS_LAY (X - 500, Y - 400)
    // because the canvas-center is at (500, 400). A node at CANVAS_LAY (0, 0)
    // therefore renders at SCREEN (500, 400).
    TEST(NodeViewMarqueeTest, ScreenToCanvasLayMapsCenterToOrigin)
    {
        std::vector<NodeModel> nodes;
        // Node at CANVAS_LAY (0, 0), 100x100. Renders at SCREEN (500..600, 400..500).
        nodes.push_back(MakeNode(1, { 0, 0 }, { 100, 100 }));

        // Marquee in SCREEN: just covering the node's rendered area.
        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   { 500, 400 },
            /*current=*/ { 600, 500 });

        EXPECT_EQ(hits.size(), 1u);
        EXPECT_TRUE(hits.count(ID{ 1 }));
    }

    TEST(NodeViewMarqueeTest, MarqueeFarFromAllNodes_SelectsNothing)
    {
        // Node at CANVAS_LAY (-340, -60), 180x80. Renders at SCREEN around
        // (160..340, 340..420). A marquee in the bottom-right corner of the
        // canvas is nowhere near it.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 180, 80 }));
        nodes.push_back(MakeNode(2, {  200,  80 }, { 180, 80 }));

        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   { 820, 620 },
            /*current=*/ { 880, 680 });

        EXPECT_TRUE(hits.empty()) << "Marquee far from both nodes selected something: size=" << hits.size();
    }

    TEST(NodeViewMarqueeTest, MarqueeFullyContainingOneNode_SelectsOnlyThatNode)
    {
        // Two nodes, one negative-positioned and one positive.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 180, 80 }));  // SCREEN ~ (160..340, 340..420)
        nodes.push_back(MakeNode(2, {  200,  80 }, { 180, 80 }));  // SCREEN ~ (700..880, 480..560)

        // Marquee fully around node 1's rendered position.
        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   { 150, 330 },
            /*current=*/ { 360, 430 });

        EXPECT_EQ(hits.size(), 1u);
        EXPECT_TRUE(hits.count(ID{ 1 }));
        EXPECT_FALSE(hits.count(ID{ 2 }));
    }

    TEST(NodeViewMarqueeTest, MarqueePartiallyOverlappingNode_DoesNotSelect)
    {
        // Strict containment: a marquee that only partially overlaps a node doesn't select it.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 180, 80 }));

        // Marquee covers the left half of node 1's rendered area only.
        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   { 100, 330 },
            /*current=*/ { 250, 430 });

        EXPECT_TRUE(hits.empty())
            << "Marquee partially overlapping a node should NOT contain it (strict containment).";
    }

    TEST(NodeViewMarqueeTest, MarqueeInsideANode_DoesNotSelect)
    {
        // Marquee is smaller than the node and lives strictly inside it.
        // Strict containment means the NODE isn't inside the MARQUEE, so no hit.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 180, 80 }));

        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   { 200, 360 },
            /*current=*/ { 240, 400 });

        EXPECT_TRUE(hits.empty());
    }

    TEST(NodeViewMarqueeTest, GiantMarqueeSelectsAllNodes)
    {
        // A marquee containing everything selects every node.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 180, 80 }));
        nodes.push_back(MakeNode(2, {  200,  80 }, { 180, 80 }));

        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   {   0,   0 },
            /*current=*/ { 1000, 1000 });

        EXPECT_EQ(hits.size(), 2u);
        EXPECT_TRUE(hits.count(ID{ 1 }));
        EXPECT_TRUE(hits.count(ID{ 2 }));
    }

    TEST(NodeViewMarqueeTest, NodeWithZeroLastSize_Skipped)
    {
        // A node not yet laid out (lastSize == 0) is skipped even if its pos lies
        // inside the marquee: it has no real bounds yet.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 0, 0 }));   // not yet laid out
        nodes.push_back(MakeNode(2, {  200,  80 }, { 180, 80 }));

        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, float3x2::Identity(),
            /*start=*/   {   0,   0 },
            /*current=*/ { 1000, 1000 });

        EXPECT_EQ(hits.size(), 1u);
        EXPECT_FALSE(hits.count(ID{ 1 }));
        EXPECT_TRUE (hits.count(ID{ 2 }));
    }

    // ---- view.transform (pan / zoom) ----

    TEST(NodeViewMarqueeTest, PannedView_MarqueeStillSelectsScreenNodes)
    {
        // Pan the view by +100 in X (content shifts right on screen by 100).
        // A node at CANVAS_LAY (0, 0) now renders at SCREEN (500+100, 400).
        // A marquee at the new rendered position must still select that node.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { 0, 0 }, { 100, 100 }));

        const float3x2 panRight = float3x2::Translate(100.0f, 0.0f);

        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, panRight,
            /*start=*/   { 590, 390 },
            /*current=*/ { 710, 510 });

        EXPECT_EQ(hits.size(), 1u);
        EXPECT_TRUE(hits.count(ID{ 1 }));
    }

    TEST(NodeViewMarqueeTest, PannedView_MarqueeWherePreviousNodeWas_SelectsNothing)
    {
        // Same setup, but marquee where the node USED to render before the pan
        // (i.e., near the canvas center). After the pan, no node is there.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { 0, 0 }, { 100, 100 }));

        const float3x2 panRight = float3x2::Translate(200.0f, 0.0f);

        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, panRight,
            /*start=*/   { 490, 390 },
            /*current=*/ { 610, 510 });

        EXPECT_TRUE(hits.empty());
    }

    TEST(NodeViewMarqueeTest, ZoomedOutView_MarqueeContainsMoreNodes)
    {
        // At zoom 0.5, nodes render at half their size and distance from
        // canvas center. The same screen-pixel marquee covers more CANVAS_LAY.
        std::vector<NodeModel> nodes;
        nodes.push_back(MakeNode(1, { -340, -60 }, { 180, 80 }));
        nodes.push_back(MakeNode(2, {  200,  80 }, { 180, 80 }));

        const float3x2 zoomHalf = float3x2::Scale(0.5f);

        // Marquee covering most of the screen - at zoom 0.5, this corresponds
        // to a CANVAS_LAY rect twice as wide, easily containing both nodes.
        const auto hits = ComputeNodesInMarquee(
            nodes, kCanvasInner, zoomHalf,
            /*start=*/   { 200, 200 },
            /*current=*/ { 800, 600 });

        EXPECT_EQ(hits.size(), 2u);
        EXPECT_TRUE(hits.count(ID{ 1 }));
        EXPECT_TRUE(hits.count(ID{ 2 }));
    }

    // ---- Z-order -------------------------------------------------------------

    TEST(NodeViewZOrderTest, BringToFrontMovesOnlyTheClickedNode)
    {
        std::vector<int> ordering{ 0, 1, 2, 3 };   // last == front
        BringNodeToFront(ordering, 1);
        // The others keep their relative order; only node 1 changes position.
        EXPECT_EQ(ordering, (std::vector<int>{ 0, 2, 3, 1 }));
    }

    TEST(NodeViewZOrderTest, BringToFrontIsANoOpForTheFrontNodeAndUnknownNodes)
    {
        std::vector<int> ordering{ 2, 0, 1 };
        BringNodeToFront(ordering, 1);
        EXPECT_EQ(ordering, (std::vector<int>{ 2, 0, 1 }));
        BringNodeToFront(ordering, 7);
        EXPECT_EQ(ordering, (std::vector<int>{ 2, 0, 1 }));
    }

    // ---- ConnectSockets: an output drives any number of inputs, an input has one source ----

    static NodeView ThreeNodeView()
    {
        NodeView view;
        view.nodes.resize(3);
        for (auto& n : view.nodes)
            n.properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .output = true });
        return view;
    }

    TEST(NodeViewConnectTest, AnOutputFansOutToSeveralInputs)
    {
        NodeView view = ThreeNodeView();
        const PropertyEndpoint out{ 0, 0 }, x{ 1, 0 }, y{ 2, 0 };
        ConnectSockets(view, out, x);
        ConnectSockets(view, out, y);

        EXPECT_EQ(view.nodes[1].properties[0].input, out);
        EXPECT_EQ(view.nodes[2].properties[0].input, out);
    }

    TEST(NodeViewConnectTest, RewiringAnInputReplacesItsSource)
    {
        NodeView view = ThreeNodeView();
        const PropertyEndpoint a{ 0, 0 }, b{ 1, 0 }, in{ 2, 0 };
        ConnectSockets(view, a, in);
        ConnectSockets(view, b, in);

        EXPECT_EQ(view.nodes[2].properties[0].input, b);
    }
}
