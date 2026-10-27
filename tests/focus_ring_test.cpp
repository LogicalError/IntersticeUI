// The focus ring (:focus-visible) shows while focus came from the keyboard: Tab
// shows it, a mouse press hides it.
#include <gtest/gtest.h>
#include "headless_gui_app.h"

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class FocusRingTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID first, second;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                first  = app.idManager.CreateStaticID();
                second = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Button(first,  "First",  [] {});
                    app.Button(second, "Second", [] {});
                    app.EndRootWindow();
                });
            }

            bool FocusVisible() { return app.guiStateManager.Interaction().IsFocusVisible(); }
        };

        // A click focuses without the ring; Tab moves focus on and shows it.
        TEST_F(FocusRingTest, TabShowsTheRing)
        {
            Frame();
            app.ClickAt(app.CenterOf(first));
            Frame();
            ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), first);
            EXPECT_FALSE(FocusVisible());
            app.InjectKey(KeyCode::Tab);
            Frame();
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), second);
            EXPECT_TRUE(FocusVisible());
        }

        TEST_F(FocusRingTest, AMousePressHidesTheRing)
        {
            Frame();
            app.ClickAt(app.CenterOf(first));
            Frame();
            app.InjectKey(KeyCode::Tab);
            Frame();
            ASSERT_TRUE(FocusVisible());
            app.ClickAt(app.CenterOf(first));
            Frame();
            EXPECT_FALSE(FocusVisible());
        }

        // Arrow keys move focus in lists and menus: they show the ring too.
        TEST_F(FocusRingTest, ArrowKeysShowTheRing)
        {
            Frame();
            app.ClickAt(app.CenterOf(first));
            Frame();
            ASSERT_FALSE(FocusVisible());
            app.InjectKey(KeyCode::Down);
            Frame();
            EXPECT_TRUE(FocusVisible());
        }

        // The theme gives focused buttons a ring.
        TEST_F(FocusRingTest, TheThemeGivesFocusedButtonsARing)
        {
            const VisualBoxStyleIndex button = app.theme.regularControls.button.visualBoxStyleIndex;
            const VisualShadowStyle* focused = app.styleManager.GetVisualShadowStyle(ComputeGPUStyleIndex(button, ElementState::Focused));
            ASSERT_NE(focused, nullptr);
            EXPECT_FALSE(focused->ring.IsNone());
            const VisualShadowStyle* normal = app.styleManager.GetVisualShadowStyle(ComputeGPUStyleIndex(button, ElementState::None));
            EXPECT_TRUE(normal == nullptr || normal->ring.IsNone()) << "the ring is for focused states only";
        }

        // List rows take an outline inside them instead.
        TEST_F(FocusRingTest, TheThemeGivesFocusedRowsAnInsetOutline)
        {
            const VisualBoxStyleIndex row = app.theme.listItemStyleRef.visualBoxStyleIndex;
            const VisualShadowStyle* focused = app.styleManager.GetVisualShadowStyle(ComputeGPUStyleIndex(row, ElementState::Focused));
            ASSERT_NE(focused, nullptr);
            EXPECT_NE(focused->insetRing, 0u);
            EXPECT_TRUE(focused->ring.IsNone());
        }
    }
}
