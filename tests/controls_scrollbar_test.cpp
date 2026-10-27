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

namespace tachyon
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
            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
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
                ASSERT_TRUE(app.Initialize());
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
                app.RegisterElement(std::move(el), LayoutOverride{}.Size(CssPx(w), CssPx(h)));
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

        // Track clicks and thumb drags, end to end: a 200px area over 1000px of content.
        class ScrollbarDragTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID areaID{}, barID{}, contentID{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                areaID    = app.idManager.CreateStaticID();
                barID     = app.idManager.CreateStaticID();
                contentID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginArea(areaID, app.theme.listAreaStyleRef, LayoutOverride{}.Size(200, 200)))
                    {
                        Element content{};
                        content.id       = contentID;
                        content.styleRef = app.theme.emptyVertStyleRef;
                        app.RegisterElement(std::move(content), LayoutOverride{}.Size(160, 1000));
                        app.VerticalScrollbar(barID, areaID);
                        app.EndArea();
                    }
                    app.EndRootWindow();
                });
            }

            int ScrollY() { return -app.guiStateManager.GetScrollManagement().GetScrollOffset(
                                       app.guiStateManager.GetScrollManagement().GetOrCreateScrollOffsetHandle(areaID)).y; }
            const ElementRect& Area() { return *app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(areaID)); }
            int BarX() { const IntRect bar = app.GetElementRect(barID); return bar.x + bar.w / 2; }
            int ScrollRange() { return Area().contentBounds.h - Area().innerRect.h; }
        };

        // A focused field below the fold: the input manager scrolls its text cursor into view
        // in the enclosing area (MakeTextCursorVisibleInTextBox, with the real element storage).
        TEST_F(ScrollbarDragTest, TheTextCursorScrollsIntoView)
        {
            const ID fieldID = app.idManager.CreateStaticID();
            std::string text = "below the fold";
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginArea(areaID, app.theme.listAreaStyleRef, LayoutOverride{}.Size(200, 200)))
                {
                    Element spacer{};
                    spacer.id       = contentID;
                    spacer.styleRef = app.theme.emptyVertStyleRef;
                    app.RegisterElement(std::move(spacer), LayoutOverride{}.Size(160, 600));
                    app.Field(fieldID, text);
                    app.EndArea();
                }
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_EQ(ScrollY(), 0);

            // Focus first (focusing scrolls the field into view by itself), then scroll back
            // to the top: from here only the cursor can move the area.
            app.guiStateManager.Interaction().SetFocusID(fieldID);
            app.Frame(draw);
            app.Frame(draw);
            auto& scroll = app.guiStateManager.GetScrollManagement();
            scroll.SetScrollOffset(scroll.GetOrCreateScrollOffsetHandle(areaID), int2{ 0, 0 });
            app.Frame(draw);
            ASSERT_EQ(ScrollY(), 0) << "test setup: nothing else scrolls the area";

            app.ownedTextSystem.cursorRect = IntRect{ 4, 0, 1, 10 };   // fits the field: it has no scrolling of its own to do
            app.Frame(draw);
            app.Frame(draw);
            // The cursor rect is local to the field's inner rect.
            const IntRect field = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(fieldID))->innerRect;
            const IntRect inner = Area().innerRect;
            EXPECT_GT(ScrollY(), 0) << "the area didn't scroll to the cursor";
            EXPECT_GE(field.y, inner.y);
            EXPECT_LE(field.y + 10, inner.y + inner.h) << "the cursor is still out of view";
        }

        TEST_F(ScrollbarDragTest, TheAreaOverflows)
        {
            Frame();
            Frame();
            ASSERT_GT(ScrollRange(), 600) << "content " << Area().contentBounds.h << ", visible " << Area().innerRect.h;
            EXPECT_EQ(ScrollY(), 0);
        }

        // A click on the empty track jumps there: at the bottom end, to the end.
        TEST_F(ScrollbarDragTest, ClickingTheTrackJumpsThere)
        {
            Frame();
            Frame();
            const IntRect inner = Area().innerRect;
            app.ClickAt({ BarX(), inner.y + inner.h - 2 });
            Frame();
            EXPECT_EQ(ScrollY(), ScrollRange());

            app.ClickAt({ BarX(), inner.y + 2 });   // the top end: back to the start
            Frame();
            EXPECT_EQ(ScrollY(), 0);
        }

        // Dragging the thumb scrolls in proportion: thumb travel over the track's spare length
        // is scroll over the scroll range. The release ends the drag.
        TEST_F(ScrollbarDragTest, DraggingTheThumbScrollsInProportion)
        {
            Frame();
            Frame();
            const IntRect inner = Area().innerRect;
            const int range = ScrollRange();
            const int track = inner.h;
            const int thumb = std::max(int(int64_t(track) * inner.h / Area().contentBounds.h), app.styleManager.Px(kScrollbarMinThumbLength));
            const int2 grab{ BarX(), inner.y + 5 };   // on the thumb, at the top

            app.InjectMouseMove(grab);
            app.InjectMouseDown(grab);
            Frame();
            EXPECT_EQ(ScrollY(), 0) << "a press on the thumb doesn't jump";
            app.InjectMouseMove({ grab.x, grab.y + 40 }, MouseButtons::Left);
            Frame();
            EXPECT_NEAR(ScrollY(), 40 * range / (track - thumb), 2);
            app.InjectMouseMove({ grab.x + 60, grab.y + 80 }, MouseButtons::Left);   // off the bar: still dragging
            Frame();
            EXPECT_NEAR(ScrollY(), 80 * range / (track - thumb), 2);

            app.InjectMouseUp({ grab.x + 60, grab.y + 80 });
            Frame();
            const int afterRelease = ScrollY();
            app.InjectMouseMove({ grab.x, grab.y + 120 });
            Frame();
            EXPECT_EQ(ScrollY(), afterRelease) << "the drag ended with the release";
        }


    } // namespace
} // namespace tachyon
