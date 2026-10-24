// Wires between the same two nodes get their default corridor lanes spread apart
// (ComputeWireLaneOffsets and WireRouteOverrides::laneOffset in gui_node_view_helpers).

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

#include <vector>

namespace
{
    using namespace interstice;

    // Node 0 has `wires` outputs; node 1 has as many inputs, each wired to the matching output.
    NodeView WiredPair(int wires)
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(size_t(wires));
        view.nodes[1].properties.resize(size_t(wires));
        for (int i = 0; i < wires; ++i)
        {
            view.nodes[0].properties[size_t(i)].output = true;
            view.nodes[1].properties[size_t(i)].input  = PropertyEndpoint{ 0, i };
        }
        return view;
    }

    std::vector<int32_t> Offsets(const NodeView& view, int wires)
    {
        const auto offsets = ComputeWireLaneOffsets(view);
        std::vector<int32_t> result;
        for (int i = 0; i < wires; ++i)
        {
            const auto it = offsets.find(WireKey(1, i));
            result.push_back(it != offsets.end() ? it->second : 999);
        }
        return result;
    }

    TEST(NodeViewLaneOffset, SpreadsWiresBetweenTheSameNodes)
    {
        EXPECT_EQ(Offsets(WiredPair(1), 1), (std::vector<int32_t>{ 0 }));
        EXPECT_EQ(Offsets(WiredPair(2), 2), (std::vector<int32_t>{ 0, -1 }));
        EXPECT_EQ(Offsets(WiredPair(3), 3), (std::vector<int32_t>{ 1, 0, -1 }));
        EXPECT_EQ(Offsets(WiredPair(4), 4), (std::vector<int32_t>{ 1, 0, -1, -2 }));
        EXPECT_EQ(Offsets(WiredPair(5), 5), (std::vector<int32_t>{ 2, 1, 0, -1, -2 }));
    }

    TEST(NodeViewLaneOffset, WiresToDifferentNodesAreSeparateGroups)
    {
        NodeView view;
        view.nodes.resize(3);
        view.nodes[0].properties.resize(2);
        view.nodes[1].properties.resize(1);
        view.nodes[2].properties.resize(1);
        view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
        view.nodes[2].properties[0].input = PropertyEndpoint{ 0, 1 };
        const auto offsets = ComputeWireLaneOffsets(view);
        EXPECT_EQ(offsets.at(WireKey(1, 0)), 0);
        EXPECT_EQ(offsets.at(WireKey(2, 0)), 0);
    }

    TEST(NodeViewLaneOffset, MovesTheDefaultCorridorLaneByGridSteps)
    {
        constexpr int32_t grid = 20;
        const IntRect src{ 0,   0, 180, 80 };
        const IntRect tgt{ 300, 200, 180, 80 };
        const int2 outPt{ 174, 40 }, inPt{ 306, 240 };
        auto laneX = [&](int32_t offset) {
            WireRouteOverrides o;
            o.laneOffset = offset;
            return BuildWireRoute(outPt, inPt, src, tgt, grid, {}, o).points[1].x;
        };
        EXPECT_EQ(laneX(1),  laneX(0) + grid);
        EXPECT_EQ(laneX(-1), laneX(0) - grid);
        EXPECT_LE(laneX(-100), src.Max(0) + grid) << "stays in the corridor";
        EXPECT_GE(laneX(-100), src.Max(0));
    }

    TEST(NodeViewLaneOffset, APinnedLaneIgnoresTheOffset)
    {
        WireRouteOverrides o;
        o.laneX      = 240;
        o.laneOffset = 2;
        const auto route = BuildWireRoute({ 174, 40 }, { 306, 240 }, { 0, 0, 180, 80 }, { 300, 200, 180, 80 }, 20, {}, o);
        EXPECT_EQ(route.points[1].x, 240);
    }

    // A wire dragged over an input counts as connected: it takes its place in the group,
    // and the wire it would replace on that input drops out.
    TEST(NodeViewLaneOffset, ADraggedWireCountsAsConnected)
    {
        NodeView view = WiredPair(2);
        view.nodes[0].properties.resize(3);
        view.nodes[1].properties.resize(3);
        view.nodes[1].properties[2].input = PropertyEndpoint{};   // free input

        const auto offsets = ComputeWireLaneOffsets(view, PropertyEndpoint{ 0, 2 }, PropertyEndpoint{ 1, 2 });
        EXPECT_EQ(offsets.at(WireKey(1, 0)),  1);
        EXPECT_EQ(offsets.at(WireKey(1, 1)),  0);
        EXPECT_EQ(offsets.at(WireKey(1, 2)), -1) << "the dragged wire";
    }

    TEST(NodeViewLaneOffset, ADraggedWireReplacesTheInputsWire)
    {
        NodeView view = WiredPair(2);
        view.nodes[0].properties.resize(3);
        // Output 2 dragged onto input 1, which output 1 feeds now.
        const auto offsets = ComputeWireLaneOffsets(view, PropertyEndpoint{ 0, 2 }, PropertyEndpoint{ 1, 1 });
        EXPECT_EQ(offsets.at(WireKey(1, 0)),  0);
        EXPECT_EQ(offsets.at(WireKey(1, 1)), -1);
        EXPECT_EQ(offsets.size(), 2u);
    }

    TEST(NodeViewLaneOffset, ADraggedWireFromAWiredOutputKeepsItsOtherWires)
    {
        NodeView view = WiredPair(2);
        view.nodes[1].properties.resize(3);
        view.nodes[1].properties[2].input = PropertyEndpoint{};
        // Output 0 (wired to input 0) dragged onto input 2: outputs fan out, so input 0 keeps it.
        const auto offsets = ComputeWireLaneOffsets(view, PropertyEndpoint{ 0, 0 }, PropertyEndpoint{ 1, 2 });
        EXPECT_EQ(offsets.at(WireKey(1, 0)),  1);
        EXPECT_EQ(offsets.at(WireKey(1, 1)),  0);
        EXPECT_EQ(offsets.at(WireKey(1, 2)), -1);
    }
}
