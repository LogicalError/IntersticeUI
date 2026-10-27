#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <string>
#include <vector>

namespace
{
    using tachyon::ElementRef;
    using tachyon::ID;
    using GUITest::HeadlessGUIApp;

    // The IDs of an element's children, in order.
    std::vector<ID> ChildIDs(HeadlessGUIApp& app, ID parent)
    {
        std::vector<ID> ids;
        const ElementRef ref = app.elementStorage.FindElementRefByID(parent);
        if (!ref.IsValid())
            return ids;
        const auto range = app.elementStorage.GetChildRange(ref);
        for (uint32_t i = range.begin; i < range.end; ++i)
            if (const auto* element = app.elementStorage.GetElementPtr(ElementRef{ ref.depth + 1, i }))
                ids.push_back(element->id);
        return ids;
    }

    class BreadcrumbTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp                 app;
        const std::vector<std::string> path{ "Graphs", "Materials", "Terrain" };
        ID                             trailID = ID::Invalid();
        int                            selected = -1;
        bool                           returned = false;

        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            trailID = app.idManager.CreateStaticID();
        }

        void Frame()
        {
            app.Frame([&] {
                app.BeginRootWindow();
                returned = app.Breadcrumb(trailID, path, [&](int index) { selected = index; }) || returned;
                app.EndRootWindow();
            });
        }
    };

    TEST_F(BreadcrumbTest, ShowsTheSegmentsWithSeparatorsBetween)
    {
        Frame();
        EXPECT_EQ(ChildIDs(app, trailID).size(), 5u) << "three segments, two separators";
    }

    TEST_F(BreadcrumbTest, ClickingASegmentSelectsIt)
    {
        Frame();
        const auto children = ChildIDs(app, trailID);
        ASSERT_EQ(children.size(), 5u);
        app.ClickAt(app.CenterOf(children[2]));   // "Materials"
        Frame();
        Frame();
        EXPECT_EQ(selected, 1);
        EXPECT_TRUE(returned);
    }

    TEST_F(BreadcrumbTest, TheCurrentSegmentIsNotClickable)
    {
        Frame();
        const auto children = ChildIDs(app, trailID);
        ASSERT_EQ(children.size(), 5u);
        app.ClickAt(app.CenterOf(children[4]));   // "Terrain", the current one
        Frame();
        Frame();
        EXPECT_EQ(selected, -1);
        EXPECT_FALSE(returned);
    }
}
