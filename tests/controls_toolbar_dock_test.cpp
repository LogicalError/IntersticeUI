// Integration tests for GUI::RenderToolbarDock: ID lifetime, side groups, the
// side rect cache, and reorder / cross-side drop / tear-off callbacks.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/elements/gui_element_storage.h>

namespace GUI
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

    class ToolbarDockTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        ToolbarDockState dock{};

        // Stable per-item static IDs across frames.
        ID itemTop0 {}, itemTop1 {};
        ID itemRight0 {};
        ID itemBottom0 {};
        ID itemLeft0 {};

        // Captured callback invocations.
        struct ReorderEvent { ToolbarSide side; int from, to; };
        struct CrossDropEvent { ToolbarSide srcSide; int srcIdx; ToolbarSide tgtSide; };
        std::vector<ReorderEvent>   reorders;
        std::vector<CrossDropEvent> crossDrops;

        // Which side each item lives on; mutated by the dock's callbacks.
        std::vector<ID> sideItems[kToolbarSideCount];

        void SetUp() override
        {
            app.Initialize();
            InitializeToolbarDockState(dock, app);
            itemTop0    = app.idManager.CreateStaticID();
            itemTop1    = app.idManager.CreateStaticID();
            itemRight0  = app.idManager.CreateStaticID();
            itemBottom0 = app.idManager.CreateStaticID();
            itemLeft0   = app.idManager.CreateStaticID();
            sideItems[(int)ToolbarSide::Top]    = { itemTop0, itemTop1 };
            sideItems[(int)ToolbarSide::Right]  = { itemRight0 };
            sideItems[(int)ToolbarSide::Bottom] = { itemBottom0 };
            sideItems[(int)ToolbarSide::Left]   = { itemLeft0 };
        }
        void TearDown() override
        {
            ReleaseToolbarDockState(dock, app);
            app.Cleanup();
        }

        // Per-frame ToolbarItem lists from sideItems; each body is a fixed-size button.
        void EmitDock()
        {
            std::vector<ToolbarItem> items[kToolbarSideCount];
            for (int s = 0; s < kToolbarSideCount; ++s)
            {
                items[s].reserve(sideItems[s].size());
                for (size_t i = 0; i < sideItems[s].size(); ++i)
                {
                    ID id = sideItems[s][i];
                    ToolbarItem it;
                    it.id        = id;
                    it.debugName = "tb";
                    it.renderBody = [this, id]()
                    {
                        // Stable rect, and an IsControl child for focus tests.
                        (void)app.idManager.PushParent(id);
                        ID btnID = app.idManager.AllocateDynamicID("btn");
                        (void)app.idManager.PopParent();
                        app.Button(btnID, "B", []{}, LayoutOverride{}.Size(60, 24));
                    };
                    items[s].push_back(std::move(it));
                }
            }

            ToolbarDockCallbacks cb;
            cb.onReorder = [this](ToolbarSide side, int from, int to)
            {
                reorders.push_back({ side, from, to });
                auto& v = sideItems[(int)side];
                if (from < 0 || from >= (int)v.size()) return;
                to = std::clamp(to, 0, (int)v.size() - 1);
                if (from == to) return;
                ID tmp = v[from];
                v.erase(v.begin() + from);
                v.insert(v.begin() + to, tmp);
            };
            cb.onCrossSideDrop = [this](ToolbarSide srcSide, int srcIdx, ToolbarSide tgtSide)
            {
                crossDrops.push_back({ srcSide, srcIdx, tgtSide });
                auto& src = sideItems[(int)srcSide];
                if (srcIdx < 0 || srcIdx >= (int)src.size()) return;
                ID id = src[srcIdx];
                src.erase(src.begin() + srcIdx);
                sideItems[(int)tgtSide].push_back(id);
            };
                        
            app.BeginRootWindow();
            RenderToolbarDock(app, dock, items, cb, [this]()
                {
                    // Growing central area so the sides get their full width/height.
                    app.BeginVertical(LayoutOverride{}.FlexGrow(1));
                    app.EndVertical();
                });
            app.EndRootWindow();
        }

        // Center of a side's group element rect (after layout completes).
        int2 SideCenter(ToolbarSide s) const
        {
            const auto ref = app.elementStorage.FindElementRefByID(dock.groupIDs[(int)s]);
            const auto* rp = app.elementStorage.GetElementRectPtr(ref);
            if (!rp) return {};
            const auto& r = rp->rect;
            return { r.x + r.w / 2, r.y + r.h / 2 };
        }

        // Drag thumb is the first child of a reorderable item.
        int2 ThumbCenter(ID itemID) const
        {
            const auto itemRef = app.elementStorage.FindElementRefByID(itemID);
            if (!itemRef.IsValid()) return {};
            const auto kids = app.elementStorage.GetChildRange(itemRef);
            if (kids.Empty()) return {};
            const ElementRef thumbRef{ itemRef.depth + 1, kids.begin };
            const auto* tr = app.elementStorage.GetElementRectPtr(thumbRef);
            if (!tr) return {};
            const auto& r = tr->rect;
            return { r.x + r.w / 2, r.y + r.h / 2 };
        }
    };

    // -------------------------------------------------------------------------
    // 1. Initialize allocates 4 side group IDs + a ghost window ID; Release
    //    returns them all to Invalid.
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, InitializeAllocatesAllIDs)
    {
        for (int i = 0; i < kToolbarSideCount; ++i)
            EXPECT_NE(dock.groupIDs[i], ID::Invalid()) << "side " << i;
        EXPECT_NE(dock.ghostWindowID, ID::Invalid());

        // All four side IDs must be distinct.
        for (int i = 0; i < kToolbarSideCount; ++i)
            for (int j = i + 1; j < kToolbarSideCount; ++j)
                EXPECT_NE(dock.groupIDs[i], dock.groupIDs[j])
                    << "groupIDs " << i << " and " << j << " collide";
    }

    TEST_F(ToolbarDockTest, ReleaseClearsAllIDs)
    {
        ReleaseToolbarDockState(dock, app);
        for (int i = 0; i < kToolbarSideCount; ++i)
            EXPECT_EQ(dock.groupIDs[i], ID::Invalid()) << "side " << i;
        EXPECT_EQ(dock.ghostWindowID, ID::Invalid());

        // Re-init for TearDown to release cleanly.
        InitializeToolbarDockState(dock, app);
    }

    // -------------------------------------------------------------------------
    // 2. After one frame, all four side group elements exist in storage with
    //    real (non-zero) rects.
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, SideGroupsExistAfterOneFrame)
    {
        app.Frame([&] { EmitDock(); });
        for (int i = 0; i < kToolbarSideCount; ++i)
        {
            const auto ref = app.elementStorage.FindElementRefByID(dock.groupIDs[i]);
            ASSERT_TRUE(ref.IsValid()) << "side " << i << " group element missing";
            const auto* rp = app.elementStorage.GetElementRectPtr(ref);
            ASSERT_NE(rp, nullptr) << "side " << i;
            EXPECT_GT(rp->rect.w, 0) << "side " << i << " has zero width";
            EXPECT_GT(rp->rect.h, 0) << "side " << i << " has zero height";
        }
    }

    // -------------------------------------------------------------------------
    // 3. Rect cache is populated after one frame's render pass and contains
    //    real screen-space rects matching what FindElementRefByID returns.
    //
    // FindToolbarSideAt reads this cache because storage rects are zero
    // mid-construction; it is written at render time.
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, CachedSideRectsPopulateAfterOneFrame)
    {
        // Frame 1: cache should be all zeros before render runs.
        for (int i = 0; i < kToolbarSideCount; ++i)
        {
            EXPECT_EQ(dock.cachedSideRects[i].w, 0) << "side " << i << " pre-frame";
            EXPECT_EQ(dock.cachedSideRects[i].h, 0) << "side " << i << " pre-frame";
        }

        app.Frame([&] { EmitDock(); });

        // After frame: every side cached a non-zero rect, and it matches the
        // element's actual rect from storage.
        for (int i = 0; i < kToolbarSideCount; ++i)
        {
            const auto& cached = dock.cachedSideRects[i];
            EXPECT_GT(cached.w, 0) << "side " << i << " cache w";
            EXPECT_GT(cached.h, 0) << "side " << i << " cache h";

            const auto ref = app.elementStorage.FindElementRefByID(dock.groupIDs[i]);
            const auto* rp = app.elementStorage.GetElementRectPtr(ref);
            ASSERT_NE(rp, nullptr);
            EXPECT_EQ(cached.x, rp->rect.x) << "side " << i << " cache x";
            EXPECT_EQ(cached.y, rp->rect.y) << "side " << i << " cache y";
            EXPECT_EQ(cached.w, rp->rect.w) << "side " << i << " cache w match";
            EXPECT_EQ(cached.h, rp->rect.h) << "side " << i << " cache h match";
        }
    }

    // -------------------------------------------------------------------------
    // 4. Dragging an item past its sibling within the same side fires
    //    onReorder(Top, 0, 1) (and not onCrossSideDrop).
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, InSideDragFiresOnReorder)
    {
        app.Frame([&] { EmitDock(); });

        const auto thumb = ThumbCenter(itemTop0);
        ASSERT_NE(thumb, int2{}) << "couldn't find Top[0] drag thumb";

        // Reorder needs the cursor past item 1's midpoint, so target its center.
        const auto target = app.CenterOf(itemTop1);
        ASSERT_NE(target, int2{}) << "couldn't find Top[1] center for drop position";

        app.InjectMouseMove(thumb);
        app.InjectMouseDown(thumb);
        app.Frame([&] { EmitDock(); });

        // Move with Left held (moves without a button are ignored).
        app.InjectMouseMove(target, MouseButtons::Left);
        app.Frame([&] { EmitDock(); });

        app.InjectMouseUp(target);
        app.Frame([&] { EmitDock(); });

        EXPECT_TRUE(crossDrops.empty()) << "in-side drag should not trigger cross-side drop";
        ASSERT_FALSE(reorders.empty()) << "expected an onReorder call";
        EXPECT_EQ(reorders.back().side, ToolbarSide::Top);
        EXPECT_EQ(reorders.back().from, 0);
        EXPECT_EQ(reorders.back().to, 1);

        // Backing data was updated: itemTop1 now precedes itemTop0.
        ASSERT_EQ(sideItems[(int)ToolbarSide::Top].size(), 2u);
        EXPECT_EQ(sideItems[(int)ToolbarSide::Top][0], itemTop1);
        EXPECT_EQ(sideItems[(int)ToolbarSide::Top][1], itemTop0);
    }

    // -------------------------------------------------------------------------
    // 5. Dragging an item from one side and dropping over a different side
    //    fires onCrossSideDrop with correct source/target indices.
    //
    // The drag must leave the source side by more than 8 px for onDragOut to
    // fire; the dock then hit-tests the rect cache (test 3).
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, CrossSideDragFiresOnCrossSideDrop)
    {
        // First frame: lay out + populate rect cache.
        app.Frame([&] { EmitDock(); });

        const auto thumb = ThumbCenter(itemTop0);
        ASSERT_NE(thumb, int2{});
        const auto bottomCenter = SideCenter(ToolbarSide::Bottom);
        ASSERT_NE(bottomCenter, int2{});

        app.InjectMouseMove(thumb);
        app.InjectMouseDown(thumb);
        app.Frame([&] { EmitDock(); });

        // Move with Left held cleanly outside Top into Bottom.
        app.InjectMouseMove(bottomCenter, MouseButtons::Left);
        app.Frame([&] { EmitDock(); });

        app.InjectMouseUp(bottomCenter);
        app.Frame([&] { EmitDock(); });

        ASSERT_FALSE(crossDrops.empty()) << "expected an onCrossSideDrop call";
        EXPECT_EQ(crossDrops.back().srcSide, ToolbarSide::Top);
        EXPECT_EQ(crossDrops.back().srcIdx,  0);
        EXPECT_EQ(crossDrops.back().tgtSide, ToolbarSide::Bottom);

        // Backing data: itemTop0 moved from Top → Bottom.
        EXPECT_EQ(sideItems[(int)ToolbarSide::Top].size(), 1u);
        EXPECT_EQ(sideItems[(int)ToolbarSide::Top][0], itemTop1);
        EXPECT_EQ(sideItems[(int)ToolbarSide::Bottom].size(), 2u);
        EXPECT_EQ(sideItems[(int)ToolbarSide::Bottom].back(), itemTop0);
    }

    // -------------------------------------------------------------------------
    // 6. Releasing outside ALL sides fires onTearOff — caller is expected to
    //    move the toolbar from sides[srcSide] into their own floating list.
    //    The reorder/cross-drop callbacks must NOT fire.
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, DropOutsideAllSidesFiresTearOff)
    {
        // SetUp's EmitDock has no onTearOff; this one captures tear-off calls.
        struct TearOffEvent { ToolbarSide srcSide; int srcIdx; int2 mousePos; };
        std::vector<TearOffEvent> tearOffs;

        auto emitWithTearOff = [&]()
        {
            std::vector<ToolbarItem> items[kToolbarSideCount];
            for (int s = 0; s < kToolbarSideCount; ++s)
            {
                items[s].reserve(sideItems[s].size());
                for (size_t i = 0; i < sideItems[s].size(); ++i)
                {
                    ID id = sideItems[s][i];
                    ToolbarItem it;
                    it.id        = id;
                    it.debugName = "tb";
                    it.renderBody = [this, id]()
                    {
                        (void)app.idManager.PushParent(id);
                        ID btnID = app.idManager.AllocateDynamicID("btn");
                        (void)app.idManager.PopParent();
                        app.Button(btnID, "B", []{}, LayoutOverride{}.Size(60, 24));
                    };
                    items[s].push_back(std::move(it));
                }
            }

            ToolbarDockCallbacks cb;
            cb.onReorder = [this](ToolbarSide side, int from, int to)
            { reorders.push_back({ side, from, to }); };
            cb.onCrossSideDrop = [this](ToolbarSide src, int srcIdx, ToolbarSide tgt)
            { crossDrops.push_back({ src, srcIdx, tgt }); };
            cb.onTearOff = [&tearOffs, this](ToolbarSide src, int srcIdx, int2 m)
            {
                tearOffs.push_back({ src, srcIdx, m });
                auto& v = sideItems[(int)src];
                if (srcIdx >= 0 && srcIdx < (int)v.size())
                    v.erase(v.begin() + srcIdx);
            };
            
            app.BeginRootWindow();
            RenderToolbarDock(app, dock, items, cb, [this]()
            {
                app.BeginVertical(LayoutOverride{}.FlexGrow(1));
                app.EndVertical();
            });
            app.EndRootWindow();
        };

        app.Frame([&] { emitWithTearOff(); });

        const auto thumb = ThumbCenter(itemTop0);
        ASSERT_NE(thumb, int2{});

        app.InjectMouseMove(thumb);
        app.InjectMouseDown(thumb);
        app.Frame([&] { emitWithTearOff(); });

        // Release in the center, outside every side rect.
        const int2 deadCenter{ 960, 540 };
        app.InjectMouseMove(deadCenter, MouseButtons::Left);
        app.Frame([&] { emitWithTearOff(); });

        app.InjectMouseUp(deadCenter);
        app.Frame([&] { emitWithTearOff(); });

        EXPECT_TRUE(crossDrops.empty()) << "tear-off must not fire cross-side drop";
        ASSERT_EQ(tearOffs.size(), 1u) << "expected exactly one onTearOff";
        EXPECT_EQ(tearOffs[0].srcSide, ToolbarSide::Top);
        EXPECT_EQ(tearOffs[0].srcIdx,  0);
        EXPECT_EQ(tearOffs[0].mousePos.x, deadCenter.x);
        EXPECT_EQ(tearOffs[0].mousePos.y, deadCenter.y);

        // Caller's tear-off handler removed itemTop0 from sides[Top].
        ASSERT_EQ(sideItems[(int)ToolbarSide::Top].size(), 1u);
        EXPECT_EQ(sideItems[(int)ToolbarSide::Top][0], itemTop1);
    }

    // -------------------------------------------------------------------------
    // 7. EmitFloatingToolbarWindows renders one window per FloatingToolbarItem
    //    and erases entries whose window the user closed.
    // -------------------------------------------------------------------------
    TEST_F(ToolbarDockTest, EmitFloatingToolbarWindowsRendersAndCleansUp)
    {
        std::vector<FloatingToolbarItem> floating;

        // One floating toolbar at a known position.
        ID windowID = app.idManager.CreateStaticID();
        ID buttonID = app.idManager.CreateStaticID();
        FloatingToolbarItem fw;
        fw.windowID      = windowID;
        fw.spawnPosition = { 300, 200 };
        fw.item.id        = buttonID;
        fw.item.debugName = "Floating";
        fw.item.renderBody = [&]()
        {
            app.Button(buttonID, "X", []{}, LayoutOverride{}.Size(40, 24));
        };
        floating.push_back(std::move(fw));

        app.Frame([&]
        {
            // Outside BeginRootWindow: these are independent OS windows.
            app.EmitFloatingToolbarWindows(floating, nullptr);
        });

        // The window and its button must both be emitted.
        EXPECT_TRUE(app.HasElement(windowID));
        EXPECT_TRUE(app.HasElement(buttonID));
        EXPECT_EQ(floating.size(), 1u);

        // The window is still open, so release its ID manually (EmitFloatingToolbarWindows
        // only releases closed ones).
        app.idManager.ReleaseStaticID(windowID);
        app.idManager.ReleaseStaticID(buttonID);
    }

    } // anonymous namespace
} // namespace GUI
