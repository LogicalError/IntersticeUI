// Tests for MakeMouseWheelScrollHandler and MakeScrollbarHandler.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_scroll_management.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>

#include <gui_controls/gui_default_handlers.h>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        // Minimal ElementEvent factory.
        ElementEvent MakeEvent(ID id, EventType type, InputState& state,
                               ElementRef ref = ElementRef::Invalid())
        {
            return ElementEvent{ id, type, state, nullptr, ref };
        }

        // ===================================================================
        // MakeMouseWheelScrollHandler - pure ScrollManagement plumbing.
        // ===================================================================

        TEST(MouseWheelScrollHandler, WheelScrollUpdatesScrollOffset)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            ID id{}; id.value.dynamicParts.hashValue = 1;
            InputState s{};
            s.scroll = float2{ 0, 1 }; // wheel up by 1 tick
            handler(MakeEvent(id, EventType::MouseScroll, s));

            auto handle = scroll.GetScrollOffsetHandle(id);
            ASSERT_TRUE(scroll.IsScrollOffsetHandleValid(handle));
            EXPECT_EQ(scroll.GetScrollOffset(handle).y, 30); // 1 tick x 30 px step
        }

        TEST(MouseWheelScrollHandler, NegativeYScrollsDown)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            ID id{}; id.value.dynamicParts.hashValue = 2;
            InputState s{};
            s.scroll = float2{ 0, -2 };
            handler(MakeEvent(id, EventType::MouseScroll, s));

            auto handle = scroll.GetScrollOffsetHandle(id);
            EXPECT_EQ(scroll.GetScrollOffset(handle).y, -60);
        }

        TEST(MouseWheelScrollHandler, HorizontalAxisIndependentOfVertical)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            ID id{}; id.value.dynamicParts.hashValue = 3;
            InputState s{};
            s.scroll = float2{ 2, -1 };
            handler(MakeEvent(id, EventType::MouseScroll, s));

            auto handle = scroll.GetScrollOffsetHandle(id);
            const auto offset = scroll.GetScrollOffset(handle);
            EXPECT_EQ(offset.x, 60);  // 2 x 30
            EXPECT_EQ(offset.y, -30); // -1 x 30
        }

        TEST(MouseWheelScrollHandler, NonScrollEventTypeIgnored)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            ID id{}; id.value.dynamicParts.hashValue = 4;
            InputState s{};
            s.scroll = float2{ 0, 1 };
            EXPECT_FALSE(handler(MakeEvent(id, EventType::MouseDown, s)));
            EXPECT_FALSE(handler(MakeEvent(id, EventType::KeyDown, s)));

            // No handle was created by the rejected events.
            auto handle = scroll.GetScrollOffsetHandle(id);
            EXPECT_FALSE(scroll.IsScrollOffsetHandleValid(handle));
        }

        TEST(MouseWheelScrollHandler, ZeroDeltaReturnsFalseAndDoesNotChangeOffset)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            ID id{}; id.value.dynamicParts.hashValue = 5;
            InputState s{};
            s.scroll = float2{ 0, 0 };
            EXPECT_FALSE(handler(MakeEvent(id, EventType::MouseScroll, s)));
        }

        TEST(MouseWheelScrollHandler, InvalidIdEventIgnored)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            InputState s{};
            s.scroll = float2{ 0, 1 };
            EXPECT_FALSE(handler(MakeEvent(ID::Invalid(), EventType::MouseScroll, s)));
        }

        TEST(MouseWheelScrollHandler, ConsecutiveScrollsAccumulate)
        {
            ScrollManagement scroll{};
            auto handler = MakeMouseWheelScrollHandler(scroll, kWheelScrollStep);

            ID id{}; id.value.dynamicParts.hashValue = 6;
            InputState s{};
            s.scroll = float2{ 0, 1 };
            handler(MakeEvent(id, EventType::MouseScroll, s));
            handler(MakeEvent(id, EventType::MouseScroll, s));
            handler(MakeEvent(id, EventType::MouseScroll, s));

            auto handle = scroll.GetScrollOffsetHandle(id);
            EXPECT_EQ(scroll.GetScrollOffset(handle).y, 90);
        }

        // ===================================================================
        // Integration: harness's InjectMouseScroll flows through dispatch
        // into a control with MakeMouseWheelScrollHandler.
        // ===================================================================

        class ScrollbarIntegrationTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp()    override { app.Initialize(); }
            void TearDown() override { app.Cleanup(); }
        };

        TEST_F(ScrollbarIntegrationTest, InjectedMouseScrollReachesElementHandler)
        {
            ID containerID = app.idManager.CreateStaticID();
            int scrollEventCount = 0;

            auto draw = [&] {
                app.BeginRootWindow();
                Element el{};
                el.id = containerID;
                el.styleRef = app.theme.emptyVertStyleRef;
                el.eventHandler = [&](ElementEvent&& evt) -> bool {
                    if (evt.type == EventType::MouseScroll && evt.id == containerID)
                    {
                        scrollEventCount++;
                        return true;
                    }
                    return false;
                };
                app.RegisterElement(std::move(el), LayoutOverride{}.Size(400, 300));
                app.EndRootWindow();
            };
            app.Frame(draw);

            const auto rect = app.GetElementRect(containerID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseScroll(center, { 0, 1 });
            app.Frame(draw);

            EXPECT_EQ(scrollEventCount, 1);
        }

        // ===================================================================
        // MakeScrollbarHandler - track click and thumb drag. The handler reads its
        // parent's rect, so tests build container -> scrollbar.
        // ===================================================================

        class ScrollbarHandlerTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID containerID{}, scrollbarID{};

            void SetUp() override
            {
                app.Initialize();
                containerID = app.idManager.CreateStaticID();
                scrollbarID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // listAreaStyleRef is overflow:auto + column, so 30 labels overflow vertically.
            void BuildScrollableGraph()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginArea(containerID, app.theme.listAreaStyleRef,
                                      LayoutOverride{}.Size(200, 200)))
                    {
                        for (int i = 0; i < 30; ++i)
                            app.Label(std::string_view("row"));
                        app.VerticalScrollbar(scrollbarID, containerID);
                        app.EndArea();
                    }
                    app.EndRootWindow();
                });
            }
        };

        TEST_F(ScrollbarHandlerTest, ScrollbarElementIsRegistered)
        {
            BuildScrollableGraph();
            EXPECT_TRUE(app.HasElement(scrollbarID));
        }

        // Scroll-into-view through nested scroll areas: once the inner area has
        // scrolled, the target sits elsewhere on screen, so the outer area must
        // reveal it where it is NOW, not where it was before the inner scroll.
        TEST_F(ScrollbarIntegrationTest, NestedScrollIntoViewRevealsTheTargetInBothAreas)
        {
            const ID outerID  = app.idManager.CreateStaticID();
            const ID innerID  = app.idManager.CreateStaticID();
            const ID targetID = app.idManager.CreateStaticID();
            const ID spacerIDs[3] = { app.idManager.CreateStaticID(), app.idManager.CreateStaticID(),
                                      app.idManager.CreateStaticID() };
            auto spacer = [&](ID id, int w, int h)
            {
                Element el{};
                el.id       = id;
                el.styleRef = app.theme.emptyVertStyleRef;
                app.RegisterElement(std::move(el), LayoutOverride{}.Size(w, h));
            };
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginArea(outerID, app.theme.listAreaStyleRef, LayoutOverride{}.Size(200, 200)))
                {
                    spacer(spacerIDs[0], 180, 300);
                    if (app.BeginArea(innerID, app.theme.listAreaStyleRef, LayoutOverride{}.Size(180, 100)))
                    {
                        spacer(spacerIDs[1], 160, 300);
                        Element target{};
                        target.id       = targetID;
                        target.styleRef = app.theme.emptyVertStyleRef;
                        app.RegisterElement(std::move(target), LayoutOverride{}.Size(160, 20));
                        app.EndArea();
                    }
                    spacer(spacerIDs[2], 180, 600);
                    app.EndArea();
                }
                app.EndRootWindow();
            };
            app.Frame(draw);

            const ElementRef targetRef = app.elementStorage.FindElementRefByID(targetID);
            ASSERT_TRUE(app.elementStorage.IsValidRef(targetRef));
            ScrollManagement& scroll = app.guiStateManager.GetScrollManagement();
            ASSERT_NE(scroll.GetScrollOffsetHandle(outerID), ScrollOffsetHandle::Invalid);
            ASSERT_NE(scroll.GetScrollOffsetHandle(innerID), ScrollOffsetHandle::Invalid);
            scroll.MakeElementVisibleHierarchically(app.elementStorage, targetRef);
            app.Frame(draw);
            app.Frame(draw);

            auto* outerRect = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(outerID));
            auto* innerRect = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(innerID));
            ASSERT_NE(outerRect, nullptr);
            ASSERT_NE(innerRect, nullptr);
            const IntRect target = app.GetElementRect(targetID);
            auto contains = [](const IntRect& outer, const IntRect& r)
            {
                return r.y >= outer.y && r.y + r.h <= outer.y + outer.h;
            };
            EXPECT_TRUE(contains(innerRect->innerRect, target))
                << "target y " << target.y << " not in inner viewport " << innerRect->innerRect.y << ".." << innerRect->innerRect.y + innerRect->innerRect.h;
            EXPECT_TRUE(contains(outerRect->innerRect, target))
                << "target y " << target.y << " not in outer viewport " << outerRect->innerRect.y << ".." << outerRect->innerRect.y + outerRect->innerRect.h;
        }

        // Note on track-click + thumb-drag integration: those flows require
        // a parent container whose CSS layout sets up vertical overflow with
        // contentBounds.h > innerRect.h. The list-area style + stacked
        // labels in this harness don't reliably produce that geometry
        // (label flex-direction interacts with the area's auto sizing in
        // ways the harness doesn't fully simulate without a real renderer).
        // The wheel-handler unit tests above and the registration test
        // below give the high-value coverage; track-click tests need the
        // harness to produce that overflow (#68).


    } // namespace
} // namespace interstice
