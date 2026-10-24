#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <vector>

namespace
{
    using namespace interstice;
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

    class TitleBarTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        ID             barID    = ID::Invalid();
        ID             buttonID = ID::Invalid();
        ID             spacerID = ID::Invalid();
        ID             titleID  = ID::Invalid();

        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            barID    = app.idManager.CreateStaticID();
            buttonID = app.idManager.CreateStaticID();
            spacerID = app.idManager.CreateStaticID();
            titleID  = app.idManager.CreateStaticID();
        }

        void Frame(ElementFlags windowFlags)
        {
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 600 }, windowFlags);
                app.BeginTitleBar(barID);
                app.Button(buttonID, "File", [] {});
                app.BeginHorizontal(spacerID, LayoutOverride::Auto().FlexGrow(1));
                app.EndHorizontal();
                app.Text(titleID, "Title", app.theme.statusTextStyleRef);
                app.EndTitleBar();
                app.EndRootWindow();
            });
        }

        WindowHitArea HitAt(ID id) { return app.TitleBarHitTest(app.RootWindowID(), app.CenterOf(id)); }
    };

    TEST_F(TitleBarTest, WhatTakesNoInputIsTheCaption)
    {
        Frame(ElementFlags::None);
        EXPECT_EQ(HitAt(spacerID), WindowHitArea::Caption);
        EXPECT_EQ(HitAt(titleID),  WindowHitArea::Caption);
        EXPECT_EQ(HitAt(buttonID), WindowHitArea::Client);
    }

    TEST_F(TitleBarTest, BelowTheBarIsClient)
    {
        Frame(ElementFlags::None);
        const IntRect bar = app.GetElementRect(barID);
        EXPECT_EQ(app.TitleBarHitTest(app.RootWindowID(), { bar.x + bar.w / 2, bar.y + bar.h + 20 }), WindowHitArea::Client);
    }

    TEST_F(TitleBarTest, AWindowWithItsOwnTitleBarGetsTheWindowButtons)
    {
        Frame(ElementFlags::WindowCustomTitleBar);
        const auto children = ChildIDs(app, barID);
        ASSERT_EQ(children.size(), 6u) << "File, spacer, title, minimize, maximize, close";
        EXPECT_EQ(HitAt(children[3]), WindowHitArea::Client);
        EXPECT_EQ(HitAt(children[4]), WindowHitArea::MaximizeButton);
        EXPECT_EQ(HitAt(children[5]), WindowHitArea::Client);
    }

    TEST_F(TitleBarTest, OtherWindowsGetNoWindowButtons)
    {
        Frame(ElementFlags::None);
        EXPECT_EQ(ChildIDs(app, barID).size(), 3u);
    }

    TEST_F(TitleBarTest, AWindowWithoutATitleBarIsAllClient)
    {
        app.Frame([&] {
            app.BeginRootWindow({ 1200, 600 });
            app.Button(buttonID, "File", [] {});
            app.EndRootWindow();
        });
        EXPECT_EQ(app.TitleBarHitTest(app.RootWindowID(), { 600, 10 }), WindowHitArea::Client);
    }
}
