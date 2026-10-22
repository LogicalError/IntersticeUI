// Smoke tests for HeadlessGUIApp's input/time primitives (InjectMouseScroll,
// ClickAt, DragFrom, InjectText, AdvanceTime) against a stand-in element.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/state/gui_state_manager.h>

namespace GUI
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // Convenience element that records every event it receives.
        struct EventRecorder
        {
            std::vector<EventType> types;
            std::vector<int2>      mousePositions;
            std::vector<float2>    scrollDeltas;
            std::vector<KeyCode>   keys;
            std::vector<KeyAction> keyActions;
        };

        EventHandler MakeRecorder(EventRecorder& r)
        {
            return [&r](ElementEvent&& evt) -> bool
            {
                r.types.push_back(evt.type);
                if (evt.type == EventType::MouseMove ||
                    evt.type == EventType::MouseDown ||
                    evt.type == EventType::MouseUp ||
                    evt.type == EventType::MouseScroll)
                    r.mousePositions.push_back(evt.state.screenPos);
                if (evt.type == EventType::MouseScroll)
                    r.scrollDeltas.push_back(evt.state.scroll);
                if (evt.type == EventType::KeyDown || evt.type == EventType::KeyUp)
                {
                    r.keys.push_back(evt.state.lastKey);
                    r.keyActions.push_back(evt.state.lastKeyAction);
                }
                return true; // consume so dispatch stops here
            };
        }

        class HarnessSelfTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID hostID{};
            EventRecorder recorder{};
            void SetUp() override
            {
                app.Initialize();
                hostID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            void EmitHost()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    Element el{};
                    el.id = hostID;
                    el.styleRef = app.theme.emptyVertStyleRef;
                    el.eventHandler = MakeRecorder(recorder);
                    app.RegisterElement(std::move(el),
                                        LayoutOverride{}.Size(400, 300));
                    app.EndRootWindow();
                });
            }
        };

        TEST_F(HarnessSelfTest, ClickAtProducesOneDownAndOneUp)
        {
            EmitHost();
            const int2 c = app.CenterOf(hostID);
            app.ClickAt(c);
            EmitHost();

            int downs = 0, ups = 0;
            for (auto t : recorder.types)
            {
                if (t == EventType::MouseDown) downs++;
                if (t == EventType::MouseUp) ups++;
            }
            EXPECT_EQ(downs, 1);
            EXPECT_EQ(ups, 1);
        }

        TEST_F(HarnessSelfTest, InjectMouseScrollReachesHandler)
        {
            EmitHost();
            const int2 c = app.CenterOf(hostID);
            app.InjectMouseMove(c);
            app.InjectMouseScroll(c, { 0, 1 });
            EmitHost();

            ASSERT_FALSE(recorder.scrollDeltas.empty());
            EXPECT_EQ(recorder.scrollDeltas.front().y, 1);
        }

        TEST_F(HarnessSelfTest, DragFromGeneratesInBetweenMove)
        {
            EmitHost();
            const auto rect = app.GetElementRect(hostID);
            const int2 from{ rect.x + 10, rect.y + 10 };
            const int2 to  { rect.x + 100, rect.y + 50 };
            app.DragFrom(from, to);
            EmitHost();

            int downs = 0, ups = 0, moves = 0;
            for (auto t : recorder.types)
            {
                if (t == EventType::MouseDown) downs++;
                if (t == EventType::MouseUp)   ups++;
                if (t == EventType::MouseMove) moves++;
            }
            EXPECT_EQ(downs, 1);
            EXPECT_EQ(ups, 1);
            EXPECT_GE(moves, 1) << "DragFrom should emit at least one in-between MouseMove";
        }

        TEST_F(HarnessSelfTest, AdvanceTimeFlowsThroughToTooltipManager)
        {
            // Advance well past the 0.5s tooltip delay.
            ID hoverHost = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                Element el{};
                el.id = hoverHost;
                el.styleRef = app.theme.emptyVertStyleRef;
                app.RegisterElement(std::move(el), LayoutOverride{}.Size(200, 200));
                app.SetTooltip(hoverHost, "tip");
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.InjectMouseMove(app.CenterOf(hoverHost));
            app.Frame(draw); // settle hover
            app.Frame(draw); // tooltipManager seeds startTime
            app.AdvanceTime(800);
            app.Frame(draw);

            EXPECT_TRUE(app.tooltipManager.IsVisible());
        }

        TEST_F(HarnessSelfTest, InjectKeyDispatchesToFocusedElement)
        {
            EmitHost();
            app.guiStateManager.Interaction().SetFocusID(hostID);
            EmitHost(); // settle focus

            app.InjectKey(KeyCode::KeyA, KeyAction::KeyDown);
            EmitHost();

            ASSERT_FALSE(recorder.keys.empty());
            EXPECT_EQ(recorder.keys.front(), KeyCode::KeyA);
        }

        TEST_F(HarnessSelfTest, InjectTextRoutesToActiveTextEditor)
        {
            // InjectText into a Field(string&) mutates the bound string.
            ID fieldID = app.idManager.CreateStaticID();
            std::string buf = "";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, buf);
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);
            app.InjectText("xy");
            app.Frame(draw);

            EXPECT_EQ(buf, "xy");
        }

    } // namespace
} // namespace GUI
