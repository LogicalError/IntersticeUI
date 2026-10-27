// Node-view edits as values a host can undo and redo: what the view changes is built
// as a NodeViewEdit, applied with ApplyNodeViewEdit and reported to NodeView::onEdit.

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

#include <vector>

namespace
{
    using namespace tachyon;

    // Node 0: one output. Node 1: two inputs, the first wired to node 0's output.
    NodeView MakeView()
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(2);
        view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
        view.nodes[1].properties[1].input = PropertyEndpoint{};
        view.nodes[1].pos = { 300, 40 };
        return view;
    }

    PropertyEndpoint SourceOf(const NodeView& view, int node, int prop)
    {
        return *view.nodes[size_t(node)].properties[size_t(prop)].input;
    }

    TEST(NodeViewEdit, UndoAndRedoAMove)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        edit.moves.push_back({ 1, { 300, 40 }, { 420, 80 } });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(view.nodes[1].pos.x, 420);
        ApplyNodeViewEdit(view, edit, /*undo*/ true);
        EXPECT_EQ(view.nodes[1].pos.x, 300);
        EXPECT_EQ(view.nodes[1].pos.y, 40);
    }

    TEST(NodeViewEdit, UndoAndRedoACollapse)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        edit.collapses.push_back({ 1, false, true });
        ApplyNodeViewEdit(view, edit);
        EXPECT_TRUE(view.nodes[1].collapsed);
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_FALSE(view.nodes[1].collapsed);
    }

    TEST(NodeViewEdit, AConnectRemembersTheInputsOldSource)
    {
        NodeView view = MakeView();
        view.nodes.resize(3);
        view.nodes[2].properties.resize(1);
        view.nodes[2].properties[0].output = true;

        const NodeViewEdit edit = ConnectEdit(view, { 2, 0 }, { 1, 0 });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 2, 0 }));
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 0, 0 }));
    }

    TEST(NodeViewEdit, ConnectingWhatIsConnectedIsNoEdit)
    {
        const NodeView view = MakeView();
        EXPECT_TRUE(ConnectEdit(view, { 0, 0 }, { 1, 0 }).empty());
        EXPECT_TRUE(ConnectEdit(view, { 0, 0 }, { 0, 0 }).empty()) << "not an input";
    }

    TEST(NodeViewEdit, UndoingADisconnectBringsBackTheWireAndItsLanes)
    {
        NodeView view = MakeView();
        view.wireOverrides[WireKey(1, 0)].laneX = 160;

        const NodeViewEdit edit = DisconnectEdit(view, { 1, 0 });
        ApplyNodeViewEdit(view, edit);
        EXPECT_FALSE(SourceOf(view, 1, 0).valid());
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);

        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 0, 0 }));
        ASSERT_EQ(view.wireOverrides.count(WireKey(1, 0)), 1u);
        EXPECT_EQ(view.wireOverrides[WireKey(1, 0)].laneX, 160);
    }

    // Pulling a wire off one input and dropping it on another is one edit: a detach and
    // a connect, undone in reverse order.
    TEST(NodeViewEdit, AMovedWireUndoesInOneStep)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        edit.rewires.push_back({ { 1, 0 }, { 0, 0 }, {} });
        edit.rewires.push_back({ { 1, 1 }, {}, { 0, 0 } });
        ApplyNodeViewEdit(view, edit);
        EXPECT_FALSE(SourceOf(view, 1, 0).valid());
        EXPECT_EQ(SourceOf(view, 1, 1), (PropertyEndpoint{ 0, 0 }));

        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 0, 0 }));
        EXPECT_FALSE(SourceOf(view, 1, 1).valid());
    }

    TEST(NodeViewEdit, LaneEditsRestoreNoOverridesAsNone)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        WireRouteOverrides moved;
        moved.laneX = 200;
        edit.lanes.push_back({ WireKey(1, 0), {}, moved });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(view.wireOverrides[WireKey(1, 0)].laneX, 200);
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);
    }

    TEST(NodeViewEdit, CommitAppliesAndReportsOnce)
    {
        NodeView view = MakeView();
        std::vector<NodeViewEdit> reported;
        view.onEdit = [&](const NodeViewEdit& e) { reported.push_back(e); };

        CommitNodeViewEdit(view, DisconnectEdit(view, { 1, 0 }));
        EXPECT_FALSE(SourceOf(view, 1, 0).valid());
        ASSERT_EQ(reported.size(), 1u);
        ASSERT_EQ(reported[0].rewires.size(), 1u);
        EXPECT_EQ(reported[0].rewires[0].from, (PropertyEndpoint{ 0, 0 }));

        CommitNodeViewEdit(view, NodeViewEdit{});
        EXPECT_EQ(reported.size(), 1u) << "an empty edit isn't reported";
    }
}
