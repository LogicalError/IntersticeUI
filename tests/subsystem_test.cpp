// Unit tests for GUIApplication's small subsystems: per-frame reset semantics,
// style-cycling order, state-machine transitions.

#include <gtest/gtest.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_controls/subsystems/gui_label_group_subsystem.h>
#include <gui_controls/subsystems/gui_button_group_subsystem.h>
#include <gui_controls/subsystems/gui_subsystem_state.h>
#include <gui_controls/subsystems/gui_reorderable_subsystem.h>
#include <gui_controls/gui_menu_manager.h>
#include <gui_core/state/gui_state_atomic_values.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/gui_id_manager.h>

#include "headless_gui_app.h"

#include <string>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        FrameContext MakeCtx(HeadlessGUIApp& app)
        {
            return FrameContext{
                .app               = app,
                .idManager         = app.idManager,
                .guiStateManager   = app.guiStateManager,
                .styleManager      = app.styleManager,
                .elementStorage    = app.elementStorage,
                .windowRegistry    = app.windowRegistry,
                .windowManager     = *app.windowManager,
                .transitionManager = app.transitionManager,
                .tabChainManager   = app.tabChainManager,
                .menuManager       = app.menuManager,
                .tooltipManager    = app.tooltipManager,
                .theme             = app.theme,
                .textSystem        = app.textSystem,
                .inputManager      = app.inputManager.get(),
                .windowBackend     = app.windowBackend,
                .elapsedTimeSecs   = 0.0f,
            };
        }
    }

    // ---------------------------------------------------------------------
    // LabelGroupSubsystem — verify per-frame reset semantics.
    //
    // OnPreConstruct: clears the transient anyLabelHovered flag and the
    //                 labelTextIDs scratch vector. labelWidth/min/max and
    //                 edgeHovered/edgeDragging are PERSISTENT and must
    //                 survive the reset.
    // OnPostConstruct: clears edgeHovered ONLY when neither
    //                  anyLabelHovered nor edgeDragging is true.
    // ---------------------------------------------------------------------

    TEST(LabelGroupSubsystemTest, OnPreConstructResetsTransientsButKeepsPersistent)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        LabelGroupSubsystem sub(pipeline, app.elementStorage, app.guiStateManager, app.theme);

        const interstice::ID groupID = app.idManager.CreateStaticID();
        sub.BeginGroup(groupID, /*defaultW*/ 150, /*minW*/ 50, /*maxW*/ 400);
        auto* state = sub.GetActiveGroup();
        ASSERT_NE(state, nullptr);

        // Seed transient fields (would normally be set during construction).
        state->anyLabelHovered = true;
        state->labelTextIDs.push_back(interstice::TextID{});
        state->labelTextIDs.push_back(interstice::TextID{});
        // And a persistent field — must survive the reset.
        state->edgeHovered = true;
        sub.EndGroup();

        pipeline.RunStage(FrameStage::PreConstruct, ctx);

        sub.BeginGroup(groupID, 150, 50, 400);  // re-acquire (existing entry kept)
        state = sub.GetActiveGroup();
        ASSERT_NE(state, nullptr);
        EXPECT_FALSE(state->anyLabelHovered);
        EXPECT_TRUE(state->labelTextIDs.empty());
        // Persistent fields preserved.
        EXPECT_TRUE(state->edgeHovered);
        EXPECT_EQ(state->labelWidth, 150);
        EXPECT_EQ(state->minWidth,   50);
        EXPECT_EQ(state->maxWidth,   400);
        sub.EndGroup();
    }

    TEST(LabelGroupSubsystemTest, OnPostConstructClearsEdgeHoveredOnlyWhenNotHoveredOrDragging)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        LabelGroupSubsystem sub(pipeline, app.elementStorage, app.guiStateManager, app.theme);

        const interstice::ID groupID = app.idManager.CreateStaticID();

        auto setup = [&](bool anyHovered, bool dragging, bool edgeHovered) {
            sub.BeginGroup(groupID, 120, 40, 500);
            auto* s = sub.GetActiveGroup();
            s->anyLabelHovered = anyHovered;
            s->edgeDragging    = dragging;
            s->edgeHovered     = edgeHovered;
            sub.EndGroup();
        };
        auto readEdgeHovered = [&]() {
            sub.BeginGroup(groupID, 120, 40, 500);
            const bool v = sub.GetActiveGroup()->edgeHovered;
            sub.EndGroup();
            return v;
        };

        // Mouse has left the group → edgeHovered should be cleared.
        setup(/*anyHovered*/ false, /*dragging*/ false, /*edgeHovered*/ true);
        pipeline.RunStage(FrameStage::PostConstruct, ctx);
        EXPECT_FALSE(readEdgeHovered());

        // A label is hovered → edgeHovered preserved (user may move onto edge).
        setup(true, false, true);
        pipeline.RunStage(FrameStage::PostConstruct, ctx);
        EXPECT_TRUE(readEdgeHovered());

        // Edge is being dragged → edgeHovered preserved regardless of hover.
        setup(false, true, true);
        pipeline.RunStage(FrameStage::PostConstruct, ctx);
        EXPECT_TRUE(readEdgeHovered());
    }

    // Non-resizable groups: OnPostConstruct commits the accumulated max-label
    // width into labelWidth; OnPreConstruct resets the accumulator. Resizable
    // groups must NOT have labelWidth touched (the drag handler owns it).
    TEST(LabelGroupSubsystemTest, NonResizableAutoSizesLabelWidthFromAccumulator)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        LabelGroupSubsystem sub(pipeline, app.elementStorage, app.guiStateManager, app.theme);

        const interstice::ID groupID = app.idManager.CreateStaticID();

        sub.BeginGroup(groupID, /*defaultW*/ 60, /*minW*/ 30, /*maxW*/ 120, /*resizable*/ false);
        auto* state = sub.GetActiveGroup();
        ASSERT_NE(state, nullptr);
        EXPECT_FALSE(state->resizable);
        state->maxLabelWidthThisFrame = 200;  // larger than maxW — must NOT be clamped
        sub.EndGroup();

        pipeline.RunStage(FrameStage::PostConstruct, ctx);
        sub.BeginGroup(groupID, 60, 30, 120, false);
        EXPECT_EQ(sub.GetActiveGroup()->labelWidth, 200);
        sub.EndGroup();

        pipeline.RunStage(FrameStage::PreConstruct, ctx);
        sub.BeginGroup(groupID, 60, 30, 120, false);
        EXPECT_EQ(sub.GetActiveGroup()->maxLabelWidthThisFrame, 0);
        EXPECT_EQ(sub.GetActiveGroup()->labelWidth, 200);  // committed value persists
        sub.EndGroup();

        // Resizable groups: accumulator is ignored.
        const interstice::ID resizableID = app.idManager.CreateStaticID();
        sub.BeginGroup(resizableID, 120, 40, 500, /*resizable*/ true);
        sub.GetActiveGroup()->maxLabelWidthThisFrame = 999;
        sub.EndGroup();
        pipeline.RunStage(FrameStage::PostConstruct, ctx);
        sub.BeginGroup(resizableID, 120, 40, 500, true);
        EXPECT_EQ(sub.GetActiveGroup()->labelWidth, 120);
        sub.EndGroup();
    }

    TEST(LabelGroupSubsystemTest, GetActiveGroupReturnsNullOutsideNestingDepth)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        LabelGroupSubsystem sub(pipeline, app.elementStorage, app.guiStateManager, app.theme);

        // Empty stack → nullptr.
        EXPECT_EQ(sub.GetActiveGroup(), nullptr);

        sub.BeginGroup(app.idManager.CreateStaticID(), 120, 40, 500);
        // Active at current depth.
        EXPECT_NE(sub.GetActiveGroup(), nullptr);
        sub.EndGroup();
        // Pop empties the stack.
        EXPECT_EQ(sub.GetActiveGroup(), nullptr);
    }

    TEST(LabelGroupSubsystemTest, IsBalancedTracksStack)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        LabelGroupSubsystem sub(pipeline, app.elementStorage, app.guiStateManager, app.theme);

        EXPECT_TRUE(sub.IsBalanced());

        const interstice::ID a = app.idManager.CreateStaticID();
        const interstice::ID b = app.idManager.CreateStaticID();
        sub.BeginGroup(a, 120, 40, 500);
        EXPECT_FALSE(sub.IsBalanced());
        sub.BeginGroup(b, 120, 40, 500);  // nested
        EXPECT_FALSE(sub.IsBalanced());
        sub.EndGroup();
        EXPECT_FALSE(sub.IsBalanced());
        sub.EndGroup();
        EXPECT_TRUE(sub.IsBalanced());
    }

#ifndef NDEBUG
    TEST(LabelGroupSubsystemTest, EndGroupWithoutBeginAsserts)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        LabelGroupSubsystem sub(pipeline, app.elementStorage, app.guiStateManager, app.theme);

        EXPECT_DEATH({ sub.EndGroup(); }, ".*");
    }
#endif

    // ---------------------------------------------------------------------
    // ButtonGroupSubsystem — verify the first/mid/last/solo style cycle.
    //
    // Item 1   → first
    // Item 2   → last (item 1 stays as first)
    // Item 3   → last (item 2 demoted to mid via storage mutation)
    // Item 4+  → last (item N-1 demoted to mid)
    // EndGroup → if itemCount == 1, item 1 reverted to solo.
    // ---------------------------------------------------------------------

    TEST(ButtonGroupSubsystemTest, NextItemStyleRefCyclesFirstThenLast)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        ButtonGroupSubsystem sub(pipeline, app.elementStorage, app.styleManager);

        ButtonGroupSubsystem::ItemState s{};
        // Distinct sentinel layoutStyleIndex per role so we can compare.
        s.firstStyleRef.layoutStyleIndex = static_cast<interstice::LayoutStyleIndex>(101);
        s.midStyleRef.layoutStyleIndex   = static_cast<interstice::LayoutStyleIndex>(202);
        s.lastStyleRef.layoutStyleIndex  = static_cast<interstice::LayoutStyleIndex>(303);
        s.soloStyleRef.layoutStyleIndex  = static_cast<interstice::LayoutStyleIndex>(404);

        EXPECT_EQ(sub.NextItemStyleRef(s).layoutStyleIndex, s.firstStyleRef.layoutStyleIndex);
        EXPECT_EQ(s.itemCount, 1);

        EXPECT_EQ(sub.NextItemStyleRef(s).layoutStyleIndex, s.lastStyleRef.layoutStyleIndex);
        EXPECT_EQ(s.itemCount, 2);

        EXPECT_EQ(sub.NextItemStyleRef(s).layoutStyleIndex, s.lastStyleRef.layoutStyleIndex);
        EXPECT_EQ(s.itemCount, 3);

        EXPECT_EQ(sub.NextItemStyleRef(s).layoutStyleIndex, s.lastStyleRef.layoutStyleIndex);
        EXPECT_EQ(s.itemCount, 4);
    }

    TEST(ButtonGroupSubsystemTest, GetActiveReturnsNullOutsideStack)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        ButtonGroupSubsystem sub(pipeline, app.elementStorage, app.styleManager);
        EXPECT_EQ(sub.GetActiveButton(), nullptr);
        EXPECT_EQ(sub.GetActiveToggle(), nullptr);

        ButtonGroupSubsystem::ItemState s{};
        sub.BeginButtonGroup(s);
        EXPECT_NE(sub.GetActiveButton(), nullptr);
        EXPECT_EQ(sub.GetActiveToggle(), nullptr);
        sub.EndButtonGroup();
        EXPECT_EQ(sub.GetActiveButton(), nullptr);
    }

    TEST(ButtonGroupSubsystemTest, EndGroupOnSingleItemDoesNotCrash)
    {
        // The solo-style revert must no-op on an Invalid firstRef, not dereference it.
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        ButtonGroupSubsystem sub(pipeline, app.elementStorage, app.styleManager);

        ButtonGroupSubsystem::ItemState s{};
        sub.BeginButtonGroup(s);
        // Simulate one item registered but with Invalid firstRef (test setup
        // shortcut — production registers a real element here).
        auto* active = sub.GetActiveButton();
        ASSERT_NE(active, nullptr);
        active->itemCount = 1;
        sub.EndButtonGroup();  // exercises the solo-revert branch
    }

    TEST(ButtonGroupSubsystemTest, IsBalancedTracksBothStacks)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        ButtonGroupSubsystem sub(pipeline, app.elementStorage, app.styleManager);

        EXPECT_TRUE(sub.IsBalanced());

        ButtonGroupSubsystem::ItemState s{};
        sub.BeginButtonGroup(s);
        EXPECT_FALSE(sub.IsBalanced());  // button stack non-empty
        sub.BeginToggleGroup(s);
        EXPECT_FALSE(sub.IsBalanced());  // both stacks non-empty
        sub.EndButtonGroup();
        EXPECT_FALSE(sub.IsBalanced());  // toggle stack still non-empty
        sub.EndToggleGroup();
        EXPECT_TRUE(sub.IsBalanced());
    }

#ifndef NDEBUG
    TEST(ButtonGroupSubsystemTest, EndButtonGroupWithoutBeginAsserts)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        ButtonGroupSubsystem sub(pipeline, app.elementStorage, app.styleManager);

        EXPECT_DEATH({ sub.EndButtonGroup(); }, ".*");
    }

    TEST(ButtonGroupSubsystemTest, EndToggleGroupWithoutBeginAsserts)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());

        FramePipeline pipeline;
        ButtonGroupSubsystem sub(pipeline, app.elementStorage, app.styleManager);

        EXPECT_DEATH({ sub.EndToggleGroup(); }, ".*");
    }
#endif

    // ---------------------------------------------------------------------
    // RenameSubsystem — verify the Start/Cancel state machine.
    //
    // Start writes all four fields atomically (targetID, isActive,
    // firstFrame, editBuffer) and seeds the buffer text. Cancel resets the
    // first three and nulls the buffer pointer — but does NOT scrub the
    // buffer contents, since the caller still owns the string.
    // ---------------------------------------------------------------------

    TEST(RenameSubsystemTest, DefaultStateIsInactive)
    {
        RenameSubsystem r;
        EXPECT_EQ(r.targetID, interstice::ID::Invalid());
        EXPECT_FALSE(r.isActive);
        EXPECT_FALSE(r.firstFrame);
        EXPECT_EQ(r.editBuffer, nullptr);
    }

    TEST(RenameSubsystemTest, StartSetsAllFieldsAndSeedsBuffer)
    {
        RenameSubsystem r;
        std::string buf = "old text";
        interstice::ID id{};
        id.value.dynamicParts.mode      = interstice::IDMode::Static;
        id.value.dynamicParts.hashValue = 0xC0FFEE;

        r.Start(id, &buf, "new seed");

        EXPECT_EQ(r.targetID, id);
        EXPECT_TRUE(r.isActive);
        EXPECT_TRUE(r.firstFrame);
        EXPECT_EQ(r.editBuffer, &buf);
        EXPECT_EQ(buf, "new seed");   // seed text overwrites prior contents
    }

    TEST(RenameSubsystemTest, CancelResetsStateButLeavesBufferContents)
    {
        RenameSubsystem r;
        std::string buf;
        interstice::ID id{};
        id.value.dynamicParts.mode      = interstice::IDMode::Static;
        id.value.dynamicParts.hashValue = 0xC0FFEE;

        r.Start(id, &buf, "edited value");
        ASSERT_TRUE(r.isActive);
        ASSERT_EQ(buf, "edited value");

        r.Cancel();
        EXPECT_EQ(r.targetID,   interstice::ID::Invalid());
        EXPECT_FALSE(r.isActive);
        EXPECT_FALSE(r.firstFrame);
        EXPECT_EQ(r.editBuffer, nullptr);
        EXPECT_EQ(buf, "edited value");  // caller-owned buffer untouched
    }

    // ---------------------------------------------------------------------
    // ConstructionScopes: per-call asserts plus a frame-end balance check in
    // GUIStateManager::EndFrame. These tests pin both layers.
    // ---------------------------------------------------------------------

    TEST(ConstructionScopesTest, BalancedAfterMatchedPushPop)
    {
        ConstructionScopes scopes;
        EXPECT_TRUE(scopes.IsBalanced());

        scopes.PushDisabled();
        scopes.PushSelected();
        scopes.PushIndeterminate();
        EXPECT_FALSE(scopes.IsBalanced());

        scopes.PopIndeterminate();
        scopes.PopSelected();
        scopes.PopDisabled();
        EXPECT_TRUE(scopes.IsBalanced());
    }

    TEST(ConstructionScopesTest, ResetClearsAllLevels)
    {
        ConstructionScopes scopes;
        scopes.PushDisabled();
        scopes.PushDisabled();
        scopes.PushSelected();
        EXPECT_FALSE(scopes.IsBalanced());

        scopes.Reset();
        EXPECT_TRUE(scopes.IsBalanced());
        EXPECT_FALSE(scopes.IsDisabled());
        EXPECT_FALSE(scopes.IsSelected());
    }

    // ---------------------------------------------------------------------
    // GUIApplication frame-end balance — verifies the OnEndFrame callback
    // registered by RegisterFrameBalanceChecks() catches unmatched
    // BeginWindow / BeginToolbar at end of frame.
    // ---------------------------------------------------------------------

#ifndef NDEBUG
    TEST(GUIApplicationBalanceTest, UnmatchedBeginToolbarAssertsAtFrameEnd)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.Frame([&]{
                app.BeginRootWindow();
                app.BeginToolbar();        // leak — no EndToolbar before frame end
                app.EndRootWindow();
            });
        }, ".*");
    }

    // onReorder handlers run at end of frame and resolve item IDs through
    // FindListItemIndex; a second list view in the same frame must not wipe
    // the first one's entries.
    TEST(ListViewItemIndexTest, SecondListInFrameKeepsFirstListsIndices)
    {
        GUITest::HeadlessGUIApp app;
        app.Initialize();
        const ID listA = app.idManager.CreateStaticID();
        const ID listB = app.idManager.CreateStaticID();
        const ID itemA0 = app.idManager.CreateStaticID();
        const ID itemA1 = app.idManager.CreateStaticID();
        const ID itemB0 = app.idManager.CreateStaticID();
        ItemDragDropConfig dd{};
        dd.payloadType = 1;
        int selA = 0, selB = 0;

        app.Frame([&] {
            app.BeginRootWindow();
            if (app.BeginListView(listA, &selA, 2, &dd))
            {
                app.ListItem(itemA0, "a0", LayoutOverride{});
                app.ListItem(itemA1, "a1", LayoutOverride{});
                app.EndListView();
            }
            if (app.BeginListView(listB, &selB, 1, &dd))
            {
                app.ListItem(itemB0, "b0", LayoutOverride{});
                app.EndListView();
            }
            app.EndRootWindow();
        });

        EXPECT_EQ(app.FindListItemIndex(itemA1), 1);
        EXPECT_EQ(app.FindListItemIndex(itemB0), 0);
        app.Cleanup();
    }

    TEST(GUIApplicationBalanceTest, UnmatchedBeginListViewAssertsAtFrameEnd)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.Frame([&]{
                app.BeginRootWindow();
                int sel = 0;
                app.BeginListView(&sel, 0);  // leak — no EndListView
                app.EndRootWindow();
            });
        }, ".*");
    }

    TEST(GUIApplicationBalanceTest, UnmatchedBeginGridViewAssertsAtFrameEnd)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.Frame([&]{
                app.BeginRootWindow();
                int sel = 0;
                const interstice::ID gridID = app.idManager.CreateStaticID();
                app.BeginGridView(gridID, &sel);   // leak — no EndGridView
                app.EndRootWindow();
            });
        }, ".*");
    }

    TEST(GUIApplicationBalanceTest, UnmatchedBeginReorderableGroupAssertsAtFrameEnd)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.Frame([&]{
                app.BeginRootWindow();
                const interstice::ID groupID = app.idManager.CreateStaticID();
                app.BeginReorderableGroup(groupID, interstice::ReorderDirection::Vertical,
                                          [](int, int){});      // leak — no End
                app.EndRootWindow();
            });
        }, ".*");
    }

    TEST(GUIApplicationBalanceTest, UnmatchedBeginMenuAssertsAtFrameEnd)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            // BeginMenu opens a popup window, which is hard to set up headless
            // (#68): drive currentDepth directly to simulate a leaked BeginMenu
            // and verify the OnEndFrame check fires.
            app.Frame([&]{
                app.BeginRootWindow();
                app.menuManager.currentDepth = 0;  // simulate leaked BeginMenu
                app.EndRootWindow();
            });
        }, ".*");
    }

    // BeginWindow is protected on GUIApplication, so a leak case can't be
    // built from outside (#68). The same balance check
    // (m_windowIdScopeStack non-empty at EndFrame) is wired up alongside
    // controlStylesStack — exercising the toolbar case above covers the
    // OnEndFrame callback's overall mechanism.
#endif

    // ---------------------------------------------------------------------
    // Widget-level Begin/End mismatch detection — verifies that a top-level
    // widget Begin without a matching End propagates through to the
    // elementStorage layer's MissingEndChildren assert (via VERIFY in the
    // harness Frame()).
    // ---------------------------------------------------------------------

#ifndef NDEBUG
    TEST(WidgetBalanceTest, BeginAreaWithoutEndAreaAssertsAtFrameEnd)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.Frame([&]{
                app.BeginRootWindow();
                app.BeginArea();   // leak — no EndArea
                app.EndRootWindow();
            });
        }, ".*");
    }

    TEST(WidgetBalanceTest, EndAreaWithoutBeginAreaAssertsImmediately)
    {
        EXPECT_DEATH({
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.Frame([&]{
                app.BeginRootWindow();
                app.EndArea();   // no matching BeginArea — EndChildren fails
                app.EndRootWindow();
            });
        }, ".*");
    }
#endif

#ifndef NDEBUG
    TEST(ConstructionScopesTest, PopWithoutPushAsserts)
    {
        EXPECT_DEATH({ ConstructionScopes scopes; scopes.PopDisabled();      }, ".*");
        EXPECT_DEATH({ ConstructionScopes scopes; scopes.PopSelected();      }, ".*");
        EXPECT_DEATH({ ConstructionScopes scopes; scopes.PopIndeterminate(); }, ".*");
    }

    TEST(ConstructionScopesTest, GUIStateManagerEndFrameAssertsOnUnbalancedScopes)
    {
        // GUIStateManager::EndFrame calls IsBalanced(); when unbalanced it
        // asserts (then Resets, so production limps on, but the assert fires).
        EXPECT_DEATH({
            GUIStateManager sm;
            IDManager       idm;
            ASSERT_TRUE(sm.BeginFrame());
            ASSERT_TRUE(idm.BeginFrame());
            sm.Construction().PushDisabled();   // leak — no matching Pop
            sm.EndFrame({}, idm);
        }, ".*");
    }
#endif
}
