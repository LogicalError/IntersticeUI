#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

namespace
{
    using namespace tachyon;

    // The canvas point that view position `p` (pixels from the canvas centre) shows.
    float2 CanvasAt(const NodeView& view, float2 p) { return view.transform.Inverse().TransformPoint(p); }

    TEST(NodeViewZoom, ZoomsAroundThePivot)
    {
        NodeView view;
        view.transform = float3x2::Scale(0.5f);
        const int2 pivot{ 40, -30 };
        const float2 before = CanvasAt(view, { 40.0f, -30.0f });
        ZoomNodeView(view, 1.25f, pivot);
        EXPECT_NEAR(NodeViewZoom(view), 0.625f, 1e-4f);
        const float2 after = CanvasAt(view, { 40.0f, -30.0f });
        EXPECT_NEAR(after.x, before.x, 1e-3f);
        EXPECT_NEAR(after.y, before.y, 1e-3f);
    }

    TEST(NodeViewZoom, StaysBetweenTenAndAHundredPercent)
    {
        NodeView view;
        ZoomNodeView(view, 1.25f);
        EXPECT_NEAR(NodeViewZoom(view), 1.0f, 1e-4f);
        for (int i = 0; i < 40; ++i)
            ZoomNodeView(view, 0.5f);
        EXPECT_NEAR(NodeViewZoom(view), 0.1f, 1e-4f);
    }

    TEST(NodeViewZoom, SetCenterShowsThatPointInTheMiddle)
    {
        NodeView view;
        SetNodeViewCenter(view, { 300.0f, -120.0f }, 0.5f);
        const float2 middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 300.0f, 1e-3f);
        EXPECT_NEAR(middle.y, -120.0f, 1e-3f);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f);
    }

    TEST(NodeViewZoom, FitCentresOnAllNodesAndFitsThem)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(2);
        view.nodes[0].pos = { 0, 0 };     view.nodes[0].lastSize = { 100, 50 };
        view.nodes[1].pos = { 900, 600 }; view.nodes[1].lastSize = { 100, 50 };
        FrameAllNodes(view);

        const float2 middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 500.0f, 1.0f);
        EXPECT_NEAR(middle.y, 325.0f, 1.0f);
        // Every node inside the visible area.
        const float2 topLeft     = CanvasAt(view, { -200.0f, -150.0f });
        const float2 bottomRight = CanvasAt(view, {  200.0f,  150.0f });
        EXPECT_LE(topLeft.x, 0.0f);
        EXPECT_LE(topLeft.y, 0.0f);
        EXPECT_GE(bottomRight.x, 1000.0f);
        EXPECT_GE(bottomRight.y, 650.0f);
    }

    TEST(NodeViewScrollIntoView, ANodeInViewDoesntMove)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(1);
        view.nodes[0].pos = { -50, -20 }; view.nodes[0].lastSize = { 100, 40 };
        const float3x2 before = view.transform;
        ScrollNodeIntoView(view, 0, 10);
        EXPECT_EQ(view.transform.r0.z, before.r0.z);
        EXPECT_EQ(view.transform.r1.z, before.r1.z);
    }

    TEST(NodeViewScrollIntoView, PansByAsLittleAsBringsItIn)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.transform = float3x2::Scale(0.5f);
        view.nodes.resize(1);
        view.nodes[0].pos = { 1000, -2000 }; view.nodes[0].lastSize = { 100, 40 };
        ScrollNodeIntoView(view, 0, 10);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f) << "the zoom stays";
        const float2 tl = view.transform.TransformPoint({ 1000.0f, -2000.0f });
        const float2 br = view.transform.TransformPoint({ 1100.0f, -1960.0f });
        EXPECT_NEAR(br.x, 200.0f - 10.0f, 1e-3f) << "right edge just inside";
        EXPECT_NEAR(tl.y, -150.0f + 10.0f, 1e-3f) << "top edge just inside";
    }

    TEST(NodeViewScrollIntoView, ANodeLargerThanTheViewShowsItsTopLeft)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(1);
        view.nodes[0].pos = { 2000, 2000 }; view.nodes[0].lastSize = { 900, 700 };
        ScrollNodeIntoView(view, 0, 10);
        const float2 tl = view.transform.TransformPoint({ 2000.0f, 2000.0f });
        EXPECT_NEAR(tl.x, -200.0f + 10.0f, 1e-3f);
        EXPECT_NEAR(tl.y, -150.0f + 10.0f, 1e-3f);
    }
}
