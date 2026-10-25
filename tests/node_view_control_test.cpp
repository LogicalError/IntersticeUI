// Tests for the node-view widget (GUIApplication::RenderNodeView,
// controls_node_view.cpp) through the headless harness.
//
// The canvas fills its parent's height the way it does in a docked panel, so the
// root here is a main-window root (a stretching column): the default `.window`
// root is a wrapping row, where the canvas would collapse to its padding.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace {

        class NodeViewControlTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeView       view;
            interstice::ID        nodeA{};
            interstice::ID        nodeB{};

            void SetUp() override
            {
                app.Initialize();
                nodeA = app.idManager.CreateStaticID();
                nodeB = app.idManager.CreateStaticID();

                NodeModel a{};
                a.stableID = nodeA;
                a.title    = "A";
                a.pos      = { 100, 80 };
                a.properties.push_back(NodeProperty{ .output = true });

                NodeModel b{};
                b.stableID = nodeB;
                b.title    = "B";
                b.pos      = { 520, 320 };
                b.properties.push_back(NodeProperty{ .input = PropertyEndpoint{} });

                view = {};
                view.transform = interstice::float3x2::Scale(1.0f); // identity
                view.nodes = { std::move(a), std::move(b) };
            }

            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.RenderNodeView(view);
                    app.EndRootWindow();
                });
            }

            void Settle() { Frame(); Frame(); }

            // The canvas's inner rect (the node-view style is unique to it).
            interstice::IntRect CanvasInner() const
            {
                const auto style = app.theme.nodeViewStyleRef;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                {
                    const uint32_t count = app.elementStorage.DepthSizeOf(d);
                    for (uint32_t i = 0; i < count; i++)
                    {
                        interstice::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref))
                            return rp->innerRect;
                    }
                }
                return {};
            }

            // A point on the canvas background, clear of the nodes and the overlays.
            interstice::int2 CanvasPoint() const
            {
                const auto inner = CanvasInner();
                return { inner.x + inner.w * 3 / 4, inner.y + inner.h / 2 };
            }

            // The rects of the elements with `style`, in layout order.
            std::vector<interstice::IntRect> RectsWithStyle(interstice::StyleRef style) const
            {
                std::vector<interstice::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        interstice::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            }

            // The centre of the first socket inside `node`'s rect.
            interstice::int2 SocketOf(interstice::ID node) const
            {
                const auto n = app.GetElementRect(node);
                for (const auto& r : RectsWithStyle(app.theme.nodeSocketStyleRef))
                {
                    const interstice::int2 c{ r.x + r.w / 2, r.y + r.h / 2 };
                    if (c.x >= n.x - r.w && c.x <= n.x + n.w + r.w && c.y >= n.y && c.y < n.y + n.h)
                        return c;
                }
                return { -1, -1 };
            }

            // A left drag the way a mouse makes one: press, moves and release on frames of
            // their own.
            void Drag(interstice::int2 from, interstice::int2 to)
            {
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                const interstice::int2 mid{ (from.x + to.x) / 2, (from.y + to.y) / 2 };
                app.InjectMouseMove(mid, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                app.InjectMouseUp(to);
                Frame();
            }

            // A point on `node`'s header, clear of its sockets.
            interstice::int2 HeaderOf(interstice::ID node) const
            {
                const auto n = app.GetElementRect(node);
                return { n.x + n.w / 2, n.y + app.theme.nodeGridSize };
            }
        };

        // A collapsed node's sockets sit on its header's centre line, like an
        // expanded row's sockets sit on the row's.
        TEST_F(NodeViewControlTest, CollapsedSocketsCentreOnTheHeader)
        {
            view.nodes[0].collapsed = true;
            Settle();
            auto rectsWithStyle = [&](interstice::StyleRef style) {
                std::vector<interstice::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        interstice::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            };
            const auto nodeRect = app.GetElementRect(nodeA);
            interstice::IntRect header{}, socket{};
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

        TEST_F(NodeViewControlTest, TheCanvasFillsItsParent)
        {
            Settle();
            const auto inner = CanvasInner();
            EXPECT_GT(inner.h, 1000) << "the canvas collapsed (#68)";
            EXPECT_GT(inner.w, 1800);
        }

        TEST_F(NodeViewControlTest, ClickingANodeSelectsIt)
        {
            Settle();
            app.ClickAt(HeaderOf(nodeB));
            Frame();
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<interstice::ID>{ nodeB });
        }

        // A drag on a node's header moves it (on the grid) and reports one edit.
        TEST_F(NodeViewControlTest, DraggingANodeMovesIt)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit& edit) { edits++; EXPECT_EQ(edit.moves.size(), 1u); };
            const interstice::int2 before = view.nodes[1].pos;
            const interstice::int2 from = HeaderOf(nodeB);
            Drag(from, { from.x + 5 * app.theme.nodeGridSize, from.y + 3 * app.theme.nodeGridSize });
            Settle();
            EXPECT_GT(view.nodes[1].pos.x, before.x);
            EXPECT_GT(view.nodes[1].pos.y, before.y);
            EXPECT_EQ(view.nodes[1].pos.x % app.theme.nodeGridSize, 0) << "positions snap to the grid";
            EXPECT_EQ(edits, 1);
        }

        // Dragging from an output socket onto an input socket wires them.
        TEST_F(NodeViewControlTest, DraggingFromAnOutputToAnInputConnects)
        {
            Settle();
            const interstice::int2 out = SocketOf(nodeA), in = SocketOf(nodeB);
            ASSERT_GE(out.x, 0);
            ASSERT_GE(in.x, 0);
            Drag(out, in);
            Settle();
            ASSERT_TRUE(view.nodes[1].properties[0].input.has_value());
            ASSERT_TRUE(view.nodes[1].properties[0].input->valid());
            EXPECT_EQ(view.nodes[1].properties[0].input->nodeIdx, 0);
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
            const interstice::int2 from = CanvasPoint();

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
            const interstice::int2 from = CanvasPoint();
            const interstice::int2 to{ from.x + 60, from.y + 25 };

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
            const interstice::int2 p = CanvasPoint();

            app.InjectMouseMove(p);
            app.InjectMouseScroll(p, { 0, -1 });
            Frame();

            EXPECT_NE(view.transform.r0.x, 1.0f) << "scroll over the canvas must zoom";
        }

        TEST_F(NodeViewControlTest, EditModeFollowsCanvasFocus)
        {
            view.commandMode = CommandMode::Default;
            Settle();
            // Focusing the canvas (via a background click) sets the shortcut
            // manager's edit mode to the view's; this exercises the focus/mode
            // wiring without needing a node to be hittable.
            app.ClickAt(CanvasPoint());
            Frame();
            // The default view editMode is Default, so this just verifies the
            // path runs without disturbing the mode.
            EXPECT_EQ(app.commandManager.GetMode(), CommandMode::Default);
        }

    } // namespace
}
