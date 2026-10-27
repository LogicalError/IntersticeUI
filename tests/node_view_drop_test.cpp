// A wire dropped on a node's body connects to the node's first input that takes it
// (FirstInputAccepting in gui_node_view_helpers).

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

namespace
{
    using namespace tachyon;

    // Node 0 has one output; node 1 has a row without an input, then two inputs.
    NodeView MakeView()
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(3);
        view.nodes[1].properties[1].input = PropertyEndpoint{};
        view.nodes[1].properties[2].input = PropertyEndpoint{};
        return view;
    }

    constexpr PropertyEndpoint kOutput{ 0, 0 };

    TEST(NodeViewDrop, PicksTheFirstInput)
    {
        const NodeView view = MakeView();
        EXPECT_EQ(FirstInputAccepting(view, kOutput, 1), (PropertyEndpoint{ 1, 1 }));
    }

    TEST(NodeViewDrop, SkipsInputsTheViewRefuses)
    {
        NodeView view = MakeView();
        view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint in) { return in.propIdx != 1; };
        EXPECT_EQ(FirstInputAccepting(view, kOutput, 1), (PropertyEndpoint{ 1, 2 }));
    }

    TEST(NodeViewDrop, NothingWhenNoInputTakesIt)
    {
        NodeView view = MakeView();
        view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint) { return false; };
        EXPECT_FALSE(FirstInputAccepting(view, kOutput, 1).valid());
    }

    TEST(NodeViewDrop, NeverTheSourceNodeItself)
    {
        NodeView view = MakeView();
        view.nodes[0].properties[0].input = PropertyEndpoint{};
        EXPECT_FALSE(FirstInputAccepting(view, kOutput, 0).valid());
    }

    TEST(NodeViewDrop, AnInputAlreadyInUseStillTakesIt)
    {
        NodeView view = MakeView();
        view.nodes[1].properties[1].input = PropertyEndpoint{ 0, 0 };
        EXPECT_EQ(FirstInputAccepting(view, kOutput, 1), (PropertyEndpoint{ 1, 1 }));
    }
}
