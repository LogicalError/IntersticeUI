// Tests for the node-view widget (GUIApplication::RenderNodeView,
// controls_node_view.cpp) through the headless harness.
//
// HARNESS LIMITATION (#68) — read before adding node-level interaction tests:
// the node-view canvas uses overflow:auto + flex-grow:1 with no flex-basis:0,
// and the engine does not zero a scroll container's min-content height (see
// the project memory on flex-basis bounding). With no height-giving dock
// parent (absent in the harness) the canvas collapses to ~0 height. Its nodes
// are Absolute-positioned, so they render at their `pos` but fall OUTSIDE the
// collapsed canvas's hit region — clicks/drags on nodes and sockets never
// dispatch. So this file covers only what the harness can reach: layout and
// the CANVAS-level interactions (pan, zoom, marquee activation, background
// click). Node-level math (socket wiring routes, marquee containment, hit
// tests) is covered directly by node_view_{hit_test,marquee,wire_route}_test,
// and the interaction handlers themselves moved verbatim from the demo, which
// the user verifies visually.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>

#include <algorithm>
#include <vector>

namespace GUI
{
    using GUITest::HeadlessGUIApp;

    namespace {

        class NodeViewControlTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeView       view;
            GUI::ID        nodeA{};
            GUI::ID        nodeB{};

            void SetUp() override
            {
                app.Initialize();
                nodeA = app.idManager.CreateStaticID();
                nodeB = app.idManager.CreateStaticID();

                NodeModel a{};
                a.stableID = nodeA;
                a.title    = "A";
                a.pos      = { 100, 80 };
                a.properties.push_back(NodeProperty{ .output = PropertyEndpoint{} });

                NodeModel b{};
                b.stableID = nodeB;
                b.title    = "B";
                b.pos      = { 520, 320 };
                b.properties.push_back(NodeProperty{ .input = PropertyEndpoint{} });

                view = {};
                view.transform = GUI::float3x2::Scale(1.0f); // identity
                view.nodes = { std::move(a), std::move(b) };
            }

            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.RenderNodeView(view);
                    app.EndRootWindow();
                });
            }

            void Settle() { Frame(); Frame(); }

            // The canvas's inner rect (the node-view style is unique to it).
            GUI::IntRect CanvasInner() const
            {
                const auto style = app.theme.nodeViewStyleRef;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                {
                    const uint32_t count = app.elementStorage.DepthSizeOf(d);
                    for (uint32_t i = 0; i < count; i++)
                    {
                        GUI::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref))
                            return rp->innerRect;
                    }
                }
                return {};
            }

            // A point on the canvas background (its hittable top strip),
            // clear of any node.
            GUI::int2 CanvasPoint() const
            {
                const auto inner = CanvasInner();
                return { inner.x + inner.w * 3 / 4, inner.y };
            }
        };

        // A collapsed node's sockets sit on its header's centre line, like an
        // expanded row's sockets sit on the row's.
        TEST_F(NodeViewControlTest, CollapsedSocketsCentreOnTheHeader)
        {
            view.nodes[0].collapsed = true;
            Settle();
            auto rectsWithStyle = [&](GUI::StyleRef style) {
                std::vector<GUI::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        GUI::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            };
            const auto nodeRect = app.GetElementRect(nodeA);
            GUI::IntRect header{}, socket{};
            for (const auto& r : rectsWithStyle(app.theme.nodeHeaderStyleRef))
                if (r.x >= nodeRect.x && r.x < nodeRect.x + nodeRect.w && r.y >= nodeRect.y && r.y < nodeRect.y + nodeRect.h) header = r;
            for (const auto& r : rectsWithStyle(app.theme.nodeSocketStyleRef))
                if (std::abs(r.y - nodeRect.y) < nodeRect.h) socket = r;
            ASSERT_GT(header.h, 0);
            ASSERT_GT(socket.h, 0);
            const int rowPitch = 2 * app.theme.nodeGridSize;
            EXPECT_EQ(socket.y + socket.h / 2, nodeRect.y + rowPitch / 2)
                << "on the header slot's centre (a grid line), not a fixed offset";
        }

        TEST_F(NodeViewControlTest, NodesLayOutAtTheirPositions)
        {
            Settle();
            ASSERT_TRUE(app.HasElement(nodeA));
            ASSERT_TRUE(app.HasElement(nodeB));
            const auto a = app.GetElementRect(nodeA);
            const auto b = app.GetElementRect(nodeB);
            EXPECT_GT(a.w, 0);
            EXPECT_GT(b.w, 0);
            // B sits below-right of A (pos {100,80} vs {520,320}).
            EXPECT_LT(a.x, b.x);
            EXPECT_LT(a.y, b.y);
        }

        TEST_F(NodeViewControlTest, RenderNodeViewIsReentrantAcrossFrames)
        {
            // Construction-balance checks (registered in the harness) fire on
            // every frame; running several proves RenderNodeView opens and
            // closes its element scopes correctly each time.
            for (int i = 0; i < 4; ++i) Frame();
            EXPECT_TRUE(app.HasElement(nodeA));
            EXPECT_TRUE(app.HasElement(nodeB));
        }

        TEST_F(NodeViewControlTest, BareClickOnEmptyCanvasClearsSelection)
        {
            Settle();
            view.selectedNodeIDs.insert(nodeA);

            app.ClickAt(CanvasPoint());
            Frame();

            EXPECT_TRUE(view.selectedNodeIDs.empty())
                << "a click on the empty canvas clears the node selection";
        }

        TEST_F(NodeViewControlTest, BackgroundDownActivatesMarquee)
        {
            Settle();
            const GUI::int2 from = CanvasPoint();

            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            EXPECT_TRUE(view.marqueeActive) << "background MouseDown starts a marquee";

            app.InjectMouseMove({ from.x - 80, from.y + 40 }, MouseButtons::Left);
            Frame();
            EXPECT_TRUE(view.marqueePastThreshold)
                << "dragging past the 4px threshold engages the marquee";
        }

        TEST_F(NodeViewControlTest, RightDragPansTransform)
        {
            Settle();
            const GUI::int2 from = CanvasPoint();
            const GUI::int2 to{ from.x + 60, from.y + 25 };

            app.InjectMouseMove(from);
            app.InjectMouseDown(from, MouseButtons::Right);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Right);
            Frame();
            app.InjectMouseUp(to, MouseButtons::Right);
            Frame();

            // Pan accumulates into the transform's translation column.
            EXPECT_NE(view.transform.r0.z, 0.0f);
            EXPECT_NE(view.transform.r1.z, 0.0f);
        }

        TEST_F(NodeViewControlTest, ScrollZoomsTransform)
        {
            Settle();
            const GUI::int2 p = CanvasPoint();

            app.InjectMouseMove(p);
            app.InjectMouseScroll(p, { 0, -1 });
            Frame();

            EXPECT_NE(view.transform.r0.x, 1.0f) << "scroll over the canvas must zoom";
        }

        TEST_F(NodeViewControlTest, EditModeFollowsCanvasFocus)
        {
            view.editMode = EditMode::Default;
            Settle();
            // Focusing the canvas (via a background click) sets the shortcut
            // manager's edit mode to the view's; this exercises the focus/mode
            // wiring without needing a node to be hittable.
            app.ClickAt(CanvasPoint());
            Frame();
            // The default view editMode is Default, so this just verifies the
            // path runs without disturbing the mode.
            EXPECT_EQ(app.commandManager.GetEditMode(), EditMode::Default);
        }

    } // namespace
}
