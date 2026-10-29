// Tests for the node-view widget (GUIApplication::RenderNodeView,
// controls_node_view.cpp) through the headless harness.
//
// The canvas fills its parent's height the way it does in a docked panel, so the
// root here is a main-window root (a stretching column): the default `.window`
// root is a wrapping row, where the canvas would collapse to its padding.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_controls/gui_node_view_helpers.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace {

        class NodeViewControlTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeView       view;
            tachyon::ID        nodeA{};
            tachyon::ID        nodeB{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
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
                view.transform = tachyon::float3x2::Scale(1.0f); // identity
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
            tachyon::IntRect CanvasInner() const
            {
                const auto style = app.theme.nodeViewStyleRef;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                {
                    const uint32_t count = app.elementStorage.DepthSizeOf(d);
                    for (uint32_t i = 0; i < count; i++)
                    {
                        tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref))
                            return rp->innerRect;
                    }
                }
                return {};
            }

            // A point on the canvas background, clear of the nodes and the overlays.
            tachyon::int2 CanvasPoint() const
            {
                const auto inner = CanvasInner();
                return { inner.x + inner.w * 3 / 4, inner.y + inner.h / 2 };
            }

            // The rects of the elements with `style`, in layout order.
            std::vector<tachyon::IntRect> RectsWithStyle(tachyon::StyleRef style) const
            {
                std::vector<tachyon::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            }

            // The centre of the first socket inside `node`'s rect.
            tachyon::int2 SocketOf(tachyon::ID node) const
            {
                const auto n = app.GetElementRect(node);
                for (const auto& r : RectsWithStyle(app.theme.nodeSocketStyleRef))
                {
                    const tachyon::int2 c{ r.x + r.w / 2, r.y + r.h / 2 };
                    if (c.x >= n.x - r.w && c.x <= n.x + n.w + r.w && c.y >= n.y && c.y < n.y + n.h)
                        return c;
                }
                return { -1, -1 };
            }

            // A left drag the way a mouse makes one: press, moves and release on frames of
            // their own.
            void Drag(tachyon::int2 from, tachyon::int2 to)
            {
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                const tachyon::int2 mid{ (from.x + to.x) / 2, (from.y + to.y) / 2 };
                app.InjectMouseMove(mid, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                app.InjectMouseUp(to);
                Frame();
            }

            // A point on `node`'s header, clear of its sockets.
            tachyon::int2 HeaderOf(tachyon::ID node) const
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
            auto rectsWithStyle = [&](tachyon::StyleRef style) {
                std::vector<tachyon::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            };
            const auto nodeRect = app.GetElementRect(nodeA);
            tachyon::IntRect header{}, socket{};
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
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<tachyon::ID>{ nodeB });
        }

        // A drag on a node's header moves it (on the grid) and reports one edit.
        TEST_F(NodeViewControlTest, DraggingANodeMovesIt)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit& edit) { edits++; EXPECT_EQ(edit.moves.size(), 1u); };
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 from = HeaderOf(nodeB);
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
            const tachyon::int2 out = SocketOf(nodeA), in = SocketOf(nodeB);
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
            const tachyon::int2 from = CanvasPoint();

            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            EXPECT_TRUE(view.marquee.IsHeld()) << "background MouseDown starts a marquee";

            app.InjectMouseMove({ from.x - 80, from.y + 40 }, MouseButtons::Left);
            Frame();
            EXPECT_TRUE(view.marquee.dragging)
                << "dragging past the 4px threshold engages the marquee";
        }

        TEST_F(NodeViewControlTest, RightDragPansTransform)
        {
            Settle();
            const tachyon::int2 from = CanvasPoint();
            const tachyon::int2 to{ from.x + 60, from.y + 25 };

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
            const tachyon::int2 p = CanvasPoint();

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

        // With the canvas focused, the keys do what the mouse does.
        TEST_F(NodeViewControlTest, TheKeyboardPansZoomsSelectsAndMovesNodes)
        {
            Settle();
            app.ClickAt(CanvasPoint());   // focuses the canvas, selects nothing
            Settle();
            auto press = [&](KeyCode key, KeyModifier mods = KeyModifier::None) {
                app.InjectKey(key, KeyAction::KeyDown, mods);
                Frame();
            };

            const float panBefore = view.transform.r0.z;
            press(KeyCode::Right);
            EXPECT_LT(view.transform.r0.z, panBefore) << "Right with nothing selected pans";

            press(KeyCode::Equals);
            EXPECT_GT(NodeViewZoom(view), 0.99f);
            view.transform = tachyon::float3x2::Scale(0.5f);
            press(KeyCode::Equals);
            EXPECT_NEAR(NodeViewZoom(view), 0.55f, 1e-3f) << "+ zooms in";
            press(KeyCode::Minus);
            EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-3f) << "- zooms out";

            press(KeyCode::KeyA, KeyModifier::Control);
            EXPECT_EQ(view.selectedNodeIDs.size(), 2u) << "Ctrl+A selects every node";

            const int2 a = view.nodes[0].pos, b = view.nodes[1].pos;
            const int32_t grid = app.theme.nodeGridSize;
            press(KeyCode::Down);
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x, a.y + grid })) << "Down moves the selected nodes a grid step";
            EXPECT_EQ(view.nodes[1].pos, (int2{ b.x, b.y + grid }));
            press(KeyCode::Left, KeyModifier::Shift);
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x - 4 * grid, a.y + grid })) << "Shift: four steps";

            press(KeyCode::Return);
            EXPECT_TRUE(view.nodes[0].collapsed && view.nodes[1].collapsed) << "Enter collapses them";
            press(KeyCode::Return);
            EXPECT_FALSE(view.nodes[0].collapsed) << "and expands them";

            press(KeyCode::Escape);
            EXPECT_TRUE(view.selectedNodeIDs.empty()) << "Escape selects none";
        }

        // Tab reaches the fields of a node out of view, and the view pans to that node.
        TEST_F(NodeViewControlTest, TabToANodeOutOfViewPansToIt)
        {
            app.runPostLayoutCallbacks = true;   // the view learns its size and the nodes theirs there
            const ID farID = app.idManager.CreateStaticID();
            NodeModel far{};
            far.stableID = farID;
            far.title    = "Far";
            far.pos      = { 4000, 80 };
            far.properties.push_back(NodeProperty{ .renderBody = [this] { app.Button("Go", [] {}); } });
            view.nodes.push_back(std::move(far));
            Settle();

            // The node's rect on screen: the canvas centre plus the view transform of its
            // canvas position.
            const tachyon::IntRect inner = CanvasInner();
            auto onScreen = [&] {
                const NodeModel& node = view.nodes[2];
                const float2 a = view.transform.TransformPoint(float2{ (float)node.pos.x, (float)node.pos.y });
                const float2 b = view.transform.TransformPoint(float2{ (float)(node.pos.x + node.lastSize.x),
                                                                       (float)(node.pos.y + node.lastSize.y) });
                const float2 c{ inner.x + inner.w * 0.5f, inner.y + inner.h * 0.5f };
                return tachyon::IntRect{ int(c.x + a.x), int(c.y + a.y), int(b.x - a.x), int(b.y - a.y) };
            };
            ASSERT_GT(view.nodes[2].lastSize.x, 0);
            ASSERT_GT(onScreen().x, inner.x + inner.w) << "test setup: the node starts out of view";

            auto focusInFar = [&] {
                return (app.guiStateManager.GetCurrentState(farID) & ElementState::Focused) != ElementState::None;
            };
            for (int i = 0; i < 8 && !focusInFar(); ++i)
            {
                app.InjectKey(KeyCode::Tab);
                Settle();
            }
            ASSERT_TRUE(focusInFar()) << "Tab reaches the far node's button";
            Settle();

            const tachyon::IntRect node = onScreen();
            EXPECT_GE(node.x, inner.x);
            EXPECT_LE(node.x + node.w, inner.x + inner.w) << "the view panned to the node";
            EXPECT_GE(node.y, inner.y);
            EXPECT_LE(node.y + node.h, inner.y + inner.h);
        }

        // Panning away from the focused node keeps the view where it was put.
        TEST_F(NodeViewControlTest, PanningAwayFromTheFocusedNodeStays)
        {
            app.runPostLayoutCallbacks = true;
            view.nodes[0].properties.push_back(NodeProperty{ .renderBody = [this] { app.Button("Go", [] {}); } });
            Settle();
            app.InjectKey(KeyCode::Tab);
            Settle();
            ASSERT_TRUE((app.guiStateManager.GetCurrentState(nodeA) & ElementState::Focused) != ElementState::None);

            view.transform = tachyon::float3x2::Translate(tachyon::float2{ -5000.0f, 0.0f });
            Settle();
            EXPECT_EQ(view.transform.r0.z, -5000.0f) << "focus that didn't move doesn't pan";
        }

        // ---- Pressing and dragging: a click isn't a move, Escape puts things back ----

        // A press that moves less than the drag distance is a click: the node stays where it
        // was and nothing is recorded.
        TEST_F(NodeViewControlTest, AClickOnANodeDoesNotMoveIt)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 from = HeaderOf(nodeB);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 2, from.y + 1 }, MouseButtons::Left);
            Frame();
            EXPECT_TRUE(view.nodeDrag.IsPressed(nodeB));
            EXPECT_FALSE(view.nodeDrag.dragging) << "under the drag distance it isn't a move yet";
            app.InjectMouseUp({ from.x + 2, from.y + 1 });
            Settle();
            EXPECT_EQ(view.nodes[1].pos, before);
            EXPECT_EQ(edits, 0);
            EXPECT_TRUE(view.selectedNodeIDs.count(nodeB)) << "the click still selects it";
        }

        TEST_F(NodeViewControlTest, EscapeDuringANodeMovePutsItBack)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 from = HeaderOf(nodeB);
            const tachyon::int2 to{ from.x + 6 * app.theme.nodeGridSize, from.y + 2 * app.theme.nodeGridSize };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            ASSERT_NE(view.nodes[1].pos, before) << "test setup: the node follows the drag";

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.nodes[1].pos, before);
            app.InjectMouseMove({ to.x + 40, to.y }, MouseButtons::Left);
            app.InjectMouseUp({ to.x + 40, to.y });
            Settle();
            EXPECT_EQ(view.nodes[1].pos, before) << "the moves after Escape are ignored";
            EXPECT_EQ(edits, 0) << "a cancelled move isn't recorded";
        }

        // The node follows the cursor at any zoom: twice as far on screen at 200%.
        TEST_F(NodeViewControlTest, ANodeFollowsTheCursorWhenZoomed)
        {
            view.transform = tachyon::float3x2::Scale(2.0f);
            Settle();
            const int32_t grid = app.theme.nodeGridSize;
            const tachyon::int2 before = view.nodes[0].pos;
            // The canvas scales its content around its inner rect's top-left.
            const tachyon::int2 pivot = CanvasInner().position;
            const tachyon::int2 header = HeaderOf(nodeA);
            const tachyon::int2 from{ pivot.x + 2 * (header.x - pivot.x), pivot.y + 2 * (header.y - pivot.y) };
            Drag(from, { from.x + 2 * 4 * grid, from.y + 2 * 2 * grid });
            Settle();
            EXPECT_EQ(view.nodes[0].pos.x - before.x, 4 * grid);
            EXPECT_EQ(view.nodes[0].pos.y - before.y, 2 * grid);
        }

        TEST_F(NodeViewControlTest, EscapeDuringAMarqueePutsTheSelectionBack)
        {
            Settle();
            view.selectedNodeIDs = { nodeA };
            const tachyon::int2 from = CanvasPoint();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 30, from.y + 30 }, MouseButtons::Left);
            Frame();
            ASSERT_TRUE(view.marquee.dragging);
            ASSERT_TRUE(view.selectedNodeIDs.empty()) << "test setup: the marquee selects what it covers, nothing";

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.selectedNodeIDs, (std::unordered_set<tachyon::ID>{ nodeA }));
            EXPECT_FALSE(view.marquee.IsHeld());
            app.InjectMouseUp({ from.x + 30, from.y + 30 });
            Frame();
            EXPECT_EQ(view.selectedNodeIDs, (std::unordered_set<tachyon::ID>{ nodeA })) << "the release isn't a click either";
        }

        TEST_F(NodeViewControlTest, EscapeDuringAPanPutsTheViewBack)
        {
            Settle();
            const tachyon::float3x2 before = view.transform;
            const tachyon::int2 from = CanvasPoint();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from, MouseButtons::Right);
            Frame();
            app.InjectMouseMove({ from.x + 60, from.y + 25 }, MouseButtons::Right);
            Frame();
            ASSERT_NE(view.transform.r0.z, before.r0.z);

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
            app.InjectMouseMove({ from.x + 90, from.y + 25 }, MouseButtons::Right);
            app.InjectMouseUp({ from.x + 90, from.y + 25 }, MouseButtons::Right);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z) << "the moves after Escape don't pan";
        }

        // The minimap follows only a press of its own: a drag from the canvas that crosses it
        // leaves the view alone.
        TEST_F(NodeViewControlTest, TheMinimapFollowsOnlyItsOwnPress)
        {
            Settle();
            const auto minimaps = RectsWithStyle(app.theme.nodeViewMinimapStyleRef);
            ASSERT_EQ(minimaps.size(), 1u);
            const tachyon::IntRect m = minimaps[0];
            const tachyon::int2 inMinimap{ m.x + m.w / 4, m.y + m.h / 4 };
            // Drawing sets the minimap's mapping; headless, nothing is drawn.
            view.minimapScale  = 0.25f;
            view.minimapOrigin = { -200.0f, -100.0f };
            const tachyon::float3x2 before = view.transform;

            const tachyon::int2 from = CanvasPoint();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(inMinimap, MouseButtons::Left);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
            app.InjectMouseUp(inMinimap);
            Frame();

            // Its own press centres the view there; Escape puts it back.
            app.InjectMouseMove(inMinimap);
            app.InjectMouseDown(inMinimap);
            Frame();
            ASSERT_NE(view.transform.r0.z, before.r0.z) << "a press in the minimap centres the view";
            app.InjectMouseMove({ inMinimap.x + 10, inMinimap.y + 5 }, MouseButtons::Left);
            Frame();
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
            app.InjectMouseUp({ inMinimap.x + 10, inMinimap.y + 5 });
            Frame();
        }

        // Dragging a wire's lane moves it; Escape puts it back and records nothing.
        TEST_F(NodeViewControlTest, EscapeDuringALaneDragPutsTheLaneBack)
        {
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const uint32_t key = WireKey(1, 0);
            // Each frame's route pass caches the wires' draggable segments for the next one; this
            // wire is straight, so the test gives it a vertical segment before every frame.
            const CachedWireHandle handle{ .wireKey = key, .role = WireHandle::LaneX, .vertical = true,
                                           .a = { 300, 100 }, .b = { 300, 300 } };
            auto frame = [&] { view.wireHandleCache = { handle }; Frame(); };
            frame();
            // Node A's laid-out rect against its pos gives where node.pos space starts.
            const tachyon::int2 origin = app.GetElementRect(nodeA).position - view.nodes[0].pos;
            const tachyon::int2 grab = origin + tachyon::int2{ 300, 200 };
            app.InjectMouseMove(grab);
            app.InjectMouseDown(grab);
            frame();
            app.InjectMouseMove({ grab.x + 40, grab.y }, MouseButtons::Left);
            frame();
            const auto moved = view.wireOverrides.find(key);
            ASSERT_NE(moved, view.wireOverrides.end()) << "test setup: the lane follows the drag";
            EXPECT_EQ(moved->second.laneX, 340);

            app.InjectKey(KeyCode::Escape);
            frame();
            EXPECT_EQ(view.wireOverrides.count(key), 0u) << "the wire had no lane of its own";
            app.InjectMouseMove({ grab.x + 80, grab.y }, MouseButtons::Left);
            app.InjectMouseUp({ grab.x + 80, grab.y });
            frame();
            EXPECT_EQ(view.wireOverrides.count(key), 0u);
            EXPECT_EQ(edits, 0);
        }

        // A move with the button held over the minimap, from a press that went to an element
        // that didn't take the mouse, leaves the view alone.
        TEST_F(NodeViewControlTest, TheMinimapIgnoresAMoveWithoutItsPress)
        {
            Settle();
            view.minimapScale  = 0.25f;
            view.minimapOrigin = { -200.0f, -100.0f };
            ElementRef minimapRef = ElementRef::Invalid();
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels() && !minimapRef.IsValid(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                {
                    const tachyon::ElementRef ref{ d, i };
                    if (app.elementStorage.IsValidRef(ref) &&
                        app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.nodeViewMinimapStyleRef) { minimapRef = ref; break; }
                }
            ASSERT_TRUE(minimapRef.IsValid());
            const Element& minimap = app.elementStorage.GetElementUnsafe(minimapRef);
            const ElementRect& rect = app.elementStorage.GetElementRectUnsafe(minimapRef);
            InputState move{};
            move.screenPos   = { rect.innerRect.x + 10, rect.innerRect.y + 10 };
            move.buttons     = MouseButtons::Left;
            move.prevButtons = MouseButtons::Left;
            const tachyon::float3x2 before = view.transform;
            EXPECT_FALSE(minimap.eventHandler(ElementEvent{ .id = minimap.id, .type = EventType::MouseMove, .state = move,
                                                            .rect = &rect, .elementRef = minimapRef }));
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
        }
    } // namespace
}
