// Tests for the TooltipManager hover delay and SetTooltip / BeginTooltip /
// BeginContextMenu. AdvanceTime() steps the virtual clock past delaySeconds.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_controls/gui_tooltip_manager.h>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsTooltipTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp()    override { app.Initialize(); }
            void TearDown() override { app.Cleanup(); }
        };

        // ---------------------------------------------------------------------
        // TooltipManager direct unit tests (no widget construction)
        // ---------------------------------------------------------------------

        TEST_F(ControlsTooltipTest, ShouldShowFalseUntilDelayElapses)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;

            ID target{}; target.value.dynamicParts.mode = IDMode::Static;
            target.value.dynamicParts.hashValue = 7;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));

            // 0.4s — still under threshold.
            tm.BeginFrame(target, ID::Invalid(), 0.4f, false, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));

            // 0.6s — past threshold, should show now.
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, MovingHoverToDifferentTargetRestartsDelay)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;

            ID first{}; first.value.dynamicParts.hashValue = 1;
            ID second{}; second.value.dynamicParts.hashValue = 2;

            tm.BeginFrame(first, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(first, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(first));

            // Move hover to second — visibility resets, no immediate show.
            tm.BeginFrame(second, ID::Invalid(), 0.7f, true, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(second));

            // After the delay elapses for second, it shows.
            tm.BeginFrame(second, ID::Invalid(), 1.3f, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(second));
        }

        TEST_F(ControlsTooltipTest, MousePressDuringHoverDismissesTooltip)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID target{}; target.value.dynamicParts.hashValue = 3;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));

            // hasMousePress = true → dismiss.
            tm.BeginFrame(target, ID::Invalid(), 0.7f, false, true, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, KeyboardActivityDismissesTooltip)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID target{}; target.value.dynamicParts.hashValue = 4;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));

            tm.BeginFrame(target, ID::Invalid(), 0.7f, false, false, true, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, OpenMenuKillsTooltip)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID target{}; target.value.dynamicParts.hashValue = 5;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));

            // menuIsOpen = true.
            tm.BeginFrame(target, ID::Invalid(), 0.7f, false, false, false, false, true);
            EXPECT_FALSE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, KeyboardFocusChangeShowsTooltipImmediately)
        {
            // When focus moves to a new element via keyboard (no mouse press),
            // the tooltip for that element shows immediately — no hover delay.
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID focused{}; focused.value.dynamicParts.hashValue = 9;

            tm.BeginFrame(ID::Invalid(), focused, 0.0f, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(focused));
        }

        // A click moves focus during that frame's dispatch, which runs after
        // BeginFrame; the next frame sees the focus change with no press. That
        // is still a mouse focus change and must not pop the tooltip instantly.
        TEST_F(ControlsTooltipTest, FocusChangeFromAClickDoesNotShowTooltipInstantly)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID a{}; a.value.dynamicParts.hashValue = 21;
            ID b{}; b.value.dynamicParts.hashValue = 22;

            tm.BeginFrame(b, a, 0.00f, true,  false, false, false, false);   // hover B, A focused
            tm.BeginFrame(b, a, 0.02f, false, true,  false, false, false);   // press on B
            tm.BeginFrame(b, b, 0.04f, false, false, false, false, false);   // focus moved to B
            EXPECT_FALSE(tm.ShouldShow(b));
        }

        TEST_F(ControlsTooltipTest, EndFrameHidesTooltipNotDeclaredThisFrame)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.0f;
            ID target{}; target.value.dynamicParts.hashValue = 6;

            // Two BeginFrames: the first sets the target (resetting isReady), the
            // second sees the elapsed delay.
            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.0f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));
            tm.EndFrame();
            EXPECT_TRUE(tm.IsVisible());

            // Next frame: hover persists but caller never calls ShouldShow.
            tm.BeginFrame(target, ID::Invalid(), 0.1f, false, false, false, false, false);
            tm.EndFrame();
            EXPECT_FALSE(tm.IsVisible());
        }

        // ---------------------------------------------------------------------
        // Integration: hover an element with SetTooltip across frames.
        // ---------------------------------------------------------------------

        TEST_F(ControlsTooltipTest, SetTooltipDoesNothingBeforeDelayElapses)
        {
            ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                app.SetTooltip(buttonID, "Help text");
                app.EndRootWindow();
            };
            app.Frame(draw);

            // Hover the button but advance only 0.1s.
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.AdvanceTime(100);
            app.Frame(draw);

            EXPECT_FALSE(app.tooltipManager.IsVisible());
        }

        TEST_F(ControlsTooltipTest, SetTooltipShowsAfterHoverDelayElapses)
        {
            ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                app.SetTooltip(buttonID, "Help text");
                app.EndRootWindow();
            };
            // Frame 1: layout the button.
            app.Frame(draw);

            // Frame 2: cursor moves over the button; dispatch sets hoveredID.
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.Frame(draw);

            // Frame 3: tooltipManager sees the new hoveredID and seeds startTime.
            app.Frame(draw);

            // Advance past delaySeconds and run a frame with no input.
            app.AdvanceTime(700);
            app.Frame(draw);

            EXPECT_TRUE(app.tooltipManager.IsVisible());
        }

        TEST_F(ControlsTooltipTest, MovingMouseAwayHidesTooltip)
        {
            ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                app.SetTooltip(buttonID, "Help text");
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.Frame(draw);
            app.Frame(draw);  // seed startTime
            app.AdvanceTime(700);
            app.Frame(draw);
            ASSERT_TRUE(app.tooltipManager.IsVisible());

            // Moving off the button resets the tooltip.
            app.InjectMouseMove({ 4, 4 });
            app.Frame(draw);   // settles hoveredID
            app.Frame(draw);   // tooltipManager observes new hovered

            EXPECT_FALSE(app.tooltipManager.IsVisible());
        }

    } // namespace
} // namespace interstice
