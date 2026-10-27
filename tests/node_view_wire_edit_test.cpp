// Picking and removing wires: WireAt finds the wire under the mouse, DisconnectEdit
// removes it (gui_node_view_helpers).

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

#include <vector>

namespace
{
    using namespace tachyon;

    const std::vector<DrawnWire> kWires{
        { .wireKey = WireKey(1, 0), .points = { { 0, 0 },  { 100, 0 }, { 100, 100 } } },
        { .wireKey = WireKey(2, 3), .points = { { 0, 20 }, { 60, 20 },  { 60, 100 } } },
    };

    TEST(NodeViewWireEdit, WireAtFindsTheWireUnderThePoint)
    {
        EXPECT_EQ(WireAt(kWires, { 50, 2 },   5.0f), WireKey(1, 0)) << "near a horizontal run";
        EXPECT_EQ(WireAt(kWires, { 103, 60 }, 5.0f), WireKey(1, 0)) << "near a vertical run";
        EXPECT_EQ(WireAt(kWires, { 30, 18 },  5.0f), WireKey(2, 3));
    }

    TEST(NodeViewWireEdit, WireAtTakesTheClosest)
    {
        EXPECT_EQ(WireAt(kWires, { 30, 8 },  20.0f), WireKey(1, 0));
        EXPECT_EQ(WireAt(kWires, { 30, 13 }, 20.0f), WireKey(2, 3));
    }

    TEST(NodeViewWireEdit, WireAtFindsNothingFarAway)
    {
        EXPECT_FALSE(WireAt(kWires, { 30, 60 }, 5.0f).has_value());
        EXPECT_FALSE(WireAt(kWires, { 108, 0 }, 5.0f).has_value()) << "past the end of a run";
        EXPECT_FALSE(WireAt({}, { 0, 0 }, 5.0f).has_value());
    }

    TEST(NodeViewWireEdit, WireInputUndoesWireKey)
    {
        EXPECT_EQ(WireInput(WireKey(7, 12)), (PropertyEndpoint{ 7, 12 }));
    }

    TEST(NodeViewWireEdit, DisconnectRemovesTheWireAndItsLanes)
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(1);
        view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
        view.wireOverrides[WireKey(1, 0)].laneX = 120;

        ApplyNodeViewEdit(view, DisconnectEdit(view, { 1, 0 }));
        ASSERT_TRUE(view.nodes[1].properties[0].input.has_value()) << "the socket stays";
        EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);

        EXPECT_TRUE(DisconnectEdit(view, { 5, 0 }).empty()) << "out of range: nothing to do";
    }
}
