// Tests for the per-subtree UI cache (BeginCached/EndCached).
//
// A clean scope is served from the cache without running its build body.
// Invalidate(), signature changes, size changes and in-scope interaction force
// a rebuild; unrelated interaction leaves the cache intact.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/layout/gui_constraints.h>
#include <gui_core/elements/gui_element_flags.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_controls/gui_dock_node.h>

#include <optional>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class UICacheTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID scopeID, btnA, btnB, outsideID;
            int bodyRuns = 0;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                scopeID   = app.idManager.CreateStaticID();
                btnA      = app.idManager.CreateStaticID();
                btnB      = app.idManager.CreateStaticID();
                outsideID = app.idManager.CreateStaticID();
                app.InjectMouseMove({ 1900, 1000 }); // park cursor on empty area
            }
            void TearDown() override { app.Cleanup(); }

            // A sibling button (outside the scope) + a cached scope with two
            // buttons. The container is fixed-size so children are hittable.
            void RunFrame(uint64_t signature = 0, int height = 300)
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Button(outsideID, "Out", [] {});
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, signature,
                                        tachyon::LayoutOverride{}.Width(400).Height(CssPx(height))))
                    {
                        bodyRuns++;
                        app.Button(btnA, "A", [] {});
                        app.Button(btnB, "B", [] {});
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            }
        };

        // A clean frame doesn't rebuild the content into main storage: the placeholder
        // container stays, its children are absent. The pixels are blitted in OnRender
        // (GPU-only, not checked headless).
        TEST_F(UICacheTest, RetainedCacheSkipsContentConstructionOnCleanFrame)
        {
            app.SetDrawCacheEnabled(true);

            RunFrame(); // frame 1: miss -> body runs, content built into main storage
            EXPECT_EQ(bodyRuns, 1);
            EXPECT_TRUE(app.HasElement(btnA));
            EXPECT_TRUE(app.HasElement(btnB));

            RunFrame(); // frame 2: hit -> body skipped AND content NOT rebuilt
            EXPECT_EQ(bodyRuns, 1);
            EXPECT_FALSE(app.HasElement(btnA)); // content absent - never traversed
            EXPECT_FALSE(app.HasElement(btnB));
            EXPECT_TRUE(app.HasElement(scopeID)); // container placeholder remains

            RunFrame(); // frame 3: still clean
            EXPECT_EQ(bodyRuns, 1);
            EXPECT_FALSE(app.HasElement(btnA));
        }

        // The panel under the cursor rebuilds so its content is live for input; when
        // the cursor leaves it goes clean again (content absent, no rebuilds).
        TEST_F(UICacheTest, RetainedCacheWakesUnderCursorSleepsWhenLeft)
        {
            app.SetDrawCacheEnabled(true);

            RunFrame(); // miss -> content built
            const int2 aCenter = app.CenterOf(btnA);
            RunFrame(); // hit -> content absent (clean)
            EXPECT_FALSE(app.HasElement(btnA));

            // Cursor enters the panel -> it wakes (rebuilds) so content is hit-testable.
            app.InjectMouseMove(aCenter);
            RunFrame(); // clean frame; cursor-inside invalidates the scope
            RunFrame(); // rebuilt -> content live
            EXPECT_TRUE(app.HasElement(btnA));
            const int hoveredRuns = bodyRuns;
            RunFrame(); // stays live while cursor inside (rebuilds each frame)
            EXPECT_GT(bodyRuns, hoveredRuns);
            EXPECT_TRUE(app.HasElement(btnA));

            // Cursor leaves -> the panel settles back to clean (content absent).
            app.InjectMouseMove({ 1900, 1000 });
            for (int i = 0; i < 5; i++) RunFrame();
            const int settledRuns = bodyRuns;
            RunFrame();
            EXPECT_EQ(bodyRuns, settledRuns); // no more rebuilds - clean again
            EXPECT_FALSE(app.HasElement(btnA)); // content absent
        }

        // A scrollable scope keeps its scroll handle alive across clean frames (so the
        // offset persists), and a scroll change forces a rebuild.
        TEST_F(UICacheTest, RetainedCacheKeepsScrollHandleAndRebuildsOnScroll)
        {
            app.SetDrawCacheEnabled(true);
            const ID btnC = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    // Inner 100px, content 3x80px -> overflows -> gets a scroll handle.
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(200).Height(100)))
                    {
                        bodyRuns++;
                        app.Button(btnA, "A", [] {}, tachyon::LayoutOverride{}.Height(80));
                        app.Button(btnB, "B", [] {}, tachyon::LayoutOverride{}.Height(80));
                        app.Button(btnC, "C", [] {}, tachyon::LayoutOverride{}.Height(80));
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame(); // miss -> content built; container gets a scroll handle
            auto& sm = app.guiStateManager.GetScrollManagement();
            ASSERT_TRUE(sm.IsScrollOffsetHandleValid(sm.GetScrollOffsetHandle(scopeID)));

            frame(); // hit -> content absent, but the scroll handle is kept alive
            EXPECT_EQ(bodyRuns, 1);
            EXPECT_FALSE(app.HasElement(btnA));
            EXPECT_TRUE(sm.IsScrollOffsetHandleValid(sm.GetScrollOffsetHandle(scopeID)));

            // A scroll change rebuilds the (clean) panel so it re-emits at the new offset.
            sm.SetScrollOffset(sm.GetScrollOffsetHandle(scopeID), int2{ 0, 30 });
            frame();
            EXPECT_EQ(bodyRuns, 2);
        }

        // Keys are routed to the focused element by ID, so a clean scope (content absent)
        // would drop them. A pending key event must rebuild the scope holding focus.
        TEST_F(UICacheTest, KeyboardReachesFocusedControlInCleanCachedScope)
        {
            app.SetDrawCacheEnabled(true);
            int clicks = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(300)))
                    {
                        bodyRuns++;
                        app.Button(btnA, "A", [&] { clicks++; });
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame();
            const int2 center = app.CenterOf(btnA);
            app.InjectMouseMove(center); // wake the panel so the button is live
            frame(); frame();
            app.ClickAt(center);         // focuses the button
            frame(); frame();
            app.InjectMouseMove({ 1900, 1000 });
            for (int i = 0; i < 6; i++) frame(); // settle to clean
            ASSERT_FALSE(app.HasElement(btnA));

            const int before = clicks;
            app.InjectKey(tachyon::KeyCode::Return);
            frame(); // key dispatched to the (rebuilt) focused button
            frame(); // the activation is observed here, so the scope must rebuild again
            EXPECT_EQ(clicks, before + 1) << "Enter must activate the focused button even though its panel was clean";
        }

        // The tab chain is built from construction, so a Tab press needs every scope built.
        TEST_F(UICacheTest, TabKeyRebuildsCleanScopes)
        {
            app.SetDrawCacheEnabled(true);
            for (int i = 0; i < 6; i++) RunFrame();
            const int settled = bodyRuns;
            RunFrame();
            ASSERT_EQ(bodyRuns, settled) << "precondition: scope is clean";

            app.InjectKey(tachyon::KeyCode::Tab);
            RunFrame();
            EXPECT_EQ(bodyRuns, settled + 1);
        }

        TEST_F(UICacheTest, InvalidateForcesRebuild)
        {
            RunFrame();
            RunFrame();
            EXPECT_EQ(bodyRuns, 1); // cached

            app.Invalidate(scopeID);
            RunFrame();
            EXPECT_EQ(bodyRuns, 2); // rebuilt

            RunFrame();
            EXPECT_EQ(bodyRuns, 2); // cached again
        }

        TEST_F(UICacheTest, SignatureChangeForcesRebuild)
        {
            RunFrame(7);
            RunFrame(7);
            EXPECT_EQ(bodyRuns, 1);

            RunFrame(8); // signature changed -> rebuild
            EXPECT_EQ(bodyRuns, 2);

            RunFrame(8); // stable again -> cached
            EXPECT_EQ(bodyRuns, 2);
        }

        TEST_F(UICacheTest, SizeChangeForcesRebuild)
        {
            RunFrame(0, 300);
            RunFrame(0, 300);
            EXPECT_EQ(bodyRuns, 1); // cached

            // The size change is detected at PostLayout, so this frame is still a
            // hit; the rebuild lands next frame (matches virtual-table latency).
            RunFrame(0, 350);
            EXPECT_EQ(bodyRuns, 1);
            RunFrame(0, 350);
            EXPECT_EQ(bodyRuns, 2);
        }

        TEST_F(UICacheTest, HoverOutsideScopeKeepsCache)
        {
            RunFrame();
            RunFrame();
            EXPECT_EQ(bodyRuns, 1);

            app.InjectMouseMove(app.CenterOf(outsideID)); // hover a sibling outside the scope

            RunFrame();
            RunFrame();
            EXPECT_EQ(bodyRuns, 1); // cache unaffected by out-of-scope interaction
        }

        // A cached button's content is absent while clean. Moving the cursor over the
        // panel wakes it; once live, hit-testing routes to the button and its handler fires.
        TEST_F(UICacheTest, WokenButtonIsClickable)
        {
            int clicks = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(300)))
                    {
                        bodyRuns++;
                        app.Button(btnA, "A", [&] { clicks++; });
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame(); // miss -> button present
            const int2 c = app.CenterOf(btnA);
            frame(); // hit -> button absent (clean)
            EXPECT_FALSE(app.HasElement(btnA));

            app.InjectMouseMove(c); // cursor over the panel -> wakes it
            frame(); frame();       // rebuilt -> button live
            EXPECT_TRUE(app.HasElement(btnA));

            app.InjectMouseDown(c);
            frame();
            app.InjectMouseUp(c);
            frame();
            EXPECT_EQ(clicks, 1); // the live button's handler fired
        }

        // Nested scopes: invalidating the inner scope must also rebuild the outer
        // (an outer hit replays inner content wholesale, so it must go stale too).
        TEST_F(UICacheTest, NestedInnerInvalidatePropagatesToOuter)
        {
            const ID outerID = app.idManager.CreateStaticID();
            const ID innerID = app.idManager.CreateStaticID();
            int outerRuns = 0, innerRuns = 0;

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(outerID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(300)))
                    {
                        outerRuns++;
                        if (app.BeginCached(innerID, app.theme.emptyVertStyleRef, 0,
                                            tachyon::LayoutOverride{}.Width(200).Height(100)))
                        {
                            innerRuns++;
                            app.Button(btnA, "A", [] {});
                        }
                        app.EndCached();
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame(); // both miss
            frame(); // outer hit -> inner content replayed, inner body not run
            EXPECT_EQ(outerRuns, 1);
            EXPECT_EQ(innerRuns, 1);

            app.Invalidate(innerID); // must propagate up to outer
            frame();
            EXPECT_EQ(outerRuns, 2);
            EXPECT_EQ(innerRuns, 2);
        }

        // Two sibling scopes are independent: invalidating one leaves the other cached.
        TEST_F(UICacheTest, IndependentScopesDoNotCrossInvalidate)
        {
            const ID s1 = app.idManager.CreateStaticID();
            const ID s2 = app.idManager.CreateStaticID();
            int r1 = 0, r2 = 0;

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(s1, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(150)))
                    { r1++; app.Button(btnA, "A", [] {}); }
                    app.EndCached();
                    if (app.BeginCached(s2, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(150)))
                    { r2++; app.Button(btnB, "B", [] {}); }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame();
            frame();
            EXPECT_EQ(r1, 1);
            EXPECT_EQ(r2, 1);

            app.Invalidate(s1);
            frame();
            EXPECT_EQ(r1, 2); // rebuilt
            EXPECT_EQ(r2, 1); // untouched

            app.Invalidate(s2);
            frame();
            EXPECT_EQ(r1, 2); // untouched
            EXPECT_EQ(r2, 2); // rebuilt
        }

        // A static, unchanging scope must build exactly once over many frames.
        TEST_F(UICacheTest, StableAcrossManyFrames)
        {
            for (int i = 0; i < 25; i++)
                RunFrame();
            EXPECT_EQ(bodyRuns, 1);
        }

        // Scrolling a cached scope must rebuild it (virtual content re-emits rows);
        // a wheel/drag scroll isn't an interaction-state change, so this is covered
        // by the scroll snapshot rather than the hover/active checks.
        TEST_F(UICacheTest, ScrollChangeRebuilds)
        {
            const ID btnC = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    // Container 100px tall, content 240px -> overflows -> it scrolls.
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(200).Height(100)))
                    {
                        bodyRuns++;
                        app.Button(btnA, "A", [] {}, tachyon::LayoutOverride{}.Height(80));
                        app.Button(btnB, "B", [] {}, tachyon::LayoutOverride{}.Height(80));
                        app.Button(btnC, "C", [] {}, tachyon::LayoutOverride{}.Height(80));
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame(); // miss; container gets a scroll handle (content overflows)
            frame(); // hit
            EXPECT_EQ(bodyRuns, 1);

            auto& sm = app.guiStateManager.GetScrollManagement();
            const ScrollOffsetHandle h = sm.GetScrollOffsetHandle(scopeID); // container id == scopeID
            ASSERT_TRUE(sm.IsScrollOffsetHandleValid(h));
            sm.SetScrollOffset(h, int2{ 0, 30 });

            frame(); // scroll changed -> rebuild
            EXPECT_EQ(bodyRuns, 2);
            frame(); // stable again -> cached
            EXPECT_EQ(bodyRuns, 2);
        }

        // A real mouse wheel over a clean cached scope scrolls it and rebuilds it,
        // even though its content wasn't built on the frames before the wheel.
        TEST_F(UICacheTest, MouseWheelOverCachedScopeScrollsAndRebuilds)
        {
            const ID btnC = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(200).Height(100)))
                    {
                        bodyRuns++;
                        app.Button(btnA, "A", [] {}, tachyon::LayoutOverride{}.Height(80));
                        app.Button(btnB, "B", [] {}, tachyon::LayoutOverride{}.Height(80));
                        app.Button(btnC, "C", [] {}, tachyon::LayoutOverride{}.Height(80));
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            ASSERT_EQ(bodyRuns, 1) << "clean and cached before the wheel";

            const IntRect scope = app.GetElementRect(scopeID);
            app.InjectMouseScroll({ scope.x + scope.w / 2, scope.y + scope.h / 2 }, { 0, -1 });   // toward the end
            frame();
            frame();

            auto& sm = app.guiStateManager.GetScrollManagement();
            EXPECT_NE(sm.GetScrollOffset(sm.GetScrollOffsetHandle(scopeID)).y, 0) << "the wheel scrolled it";
            EXPECT_GE(bodyRuns, 2) << "and it was rebuilt at the new offset";
        }

        // A scope that uses AddPostLayout (deferred size measurement) must rebuild
        // for a couple frames after first appearing so it settles to the correct
        // content (frame 1 is built before the measured size exists) before caching.
        TEST_F(UICacheTest, DeferredMeasurementScopeWarmsUp)
        {
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(300)))
                    {
                        bodyRuns++;
                        tachyon::Element e{};
                        e.id       = btnA;
                        e.styleRef = app.theme.collectionStyleRef;
                        const tachyon::ElementRef ref = app.RegisterElement(std::move(e), {});
                        (void)app.elementStorage.AddPostLayout(ref, [](tachyon::PostLayoutContext&) {});
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame(); // miss (build 1) - first capture
            frame(); // warmup -> miss (build 2)
            frame(); // settled -> hit
            frame(); // hit
            EXPECT_EQ(bodyRuns, 2); // built twice (warmup), then served from cache
        }

        // A focused text field's scope rebuilds every frame so its editable-text
        // registration re-runs, even with the cursor elsewhere (focus is keyboard-sticky).
        TEST_F(UICacheTest, FocusedTextEditorRebuildsEvenWithoutCursor)
        {
            std::string text = "hi";
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 0,
                                        tachyon::LayoutOverride{}.Width(400).Height(300)))
                    {
                        bodyRuns++;
                        app.Field(btnA, text); // editable text field
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };

            frame(); // miss -> field present
            const int2 c = app.CenterOf(btnA);
            frame(); // hit -> field absent (clean)

            app.InjectMouseMove(c); frame(); frame(); // wake -> field live
            app.ClickAt(c);                           // focus the field
            frame();

            // Move the cursor away: no longer cursor-inside, but the field stays
            // focused, so the scope must keep rebuilding for live editing.
            app.InjectMouseMove({ 1900, 1000 });
            frame();
            const int beforeFocus = bodyRuns;
            frame(); frame();
            EXPECT_GE(bodyRuns, beforeFocus + 2);
        }

        // A docking panel's cacheSignature versions its cache in the real
        // BeginDockingContainer path: cached while constant, rebuilt on change.
        TEST(UICacheDockingTest, PanelCacheVersioningInDockingContext)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.InjectMouseMove({ 1900, 1000 });

            tachyon::DockingLayout layout;
            const tachyon::ID containerID = app.idManager.CreateStaticID();
            int draws = 0;
            uint64_t sig = 1;
            const tachyon::ID panel = app.CreatePanel(layout.panels, "P", IconID::Invalid, [&] {
                draws++;
                app.Button(app.idManager.AllocateDynamicID("cell"), "x", [] {});
            });
            layout.panels[panel].cacheSignature = [&] { return sig; };
            layout.root = MakeLeaf({ panel });

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginDockingContainer(containerID, &layout, tachyon::LayoutOverride{}.Size(800, 600));
                    app.EndDockingContainer();
                    app.EndRootWindow();
                });
            };

            frame(); EXPECT_EQ(draws, 1); // miss
            frame(); EXPECT_EQ(draws, 1); // hit
            frame(); EXPECT_EQ(draws, 1); // hit

            sig = 2;                      // panel data changed
            frame(); EXPECT_EQ(draws, 2); // rebuilt
            frame(); EXPECT_EQ(draws, 2); // cached again
            app.Cleanup();
        }

        // Panels are cached by default: a panel with no cacheSignature is still
        // served from cache after the first build.
        TEST(UICacheDockingTest, DefaultCachesWithoutSignature)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.InjectMouseMove({ 1900, 1000 });

            tachyon::DockingLayout layout;
            const tachyon::ID containerID = app.idManager.CreateStaticID();
            int draws = 0;
            const tachyon::ID panel = app.CreatePanel(layout.panels, "P", IconID::Invalid, [&] { draws++; });
            layout.root = MakeLeaf({ panel });

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginDockingContainer(containerID, &layout, tachyon::LayoutOverride{}.Size(800, 600));
                    app.EndDockingContainer();
                    app.EndRootWindow();
                });
            };

            frame(); frame(); frame();
            EXPECT_EQ(draws, 1); // cached by default after the first build
            app.Cleanup();
        }

        // A panel opts out by returning std::nullopt from cacheSignature and then
        // rebuilds every frame (as the profiler does to stay live).
        TEST(UICacheDockingTest, OptedOutPanelRebuildsEachFrame)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.InjectMouseMove({ 1900, 1000 });

            tachyon::DockingLayout layout;
            const tachyon::ID containerID = app.idManager.CreateStaticID();
            int draws = 0;
            const tachyon::ID panel = app.CreatePanel(layout.panels, "P", IconID::Invalid, [&] { draws++; });
            layout.panels[panel].cacheSignature = [] { return std::optional<uint64_t>{}; }; // opt out
            layout.root = MakeLeaf({ panel });

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginDockingContainer(containerID, &layout, tachyon::LayoutOverride{}.Size(800, 600));
                    app.EndDockingContainer();
                    app.EndRootWindow();
                });
            };

            frame(); frame(); frame();
            EXPECT_EQ(draws, 3); // opted out -> rebuilds every frame
            app.Cleanup();
        }

        // InvalidatePanel(panelId) forces a cached panel to rebuild next frame.
        TEST(UICacheDockingTest, InvalidatePanelByIdRebuilds)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.InjectMouseMove({ 1900, 1000 });

            tachyon::DockingLayout layout;
            const tachyon::ID containerID = app.idManager.CreateStaticID();
            int draws = 0;
            const tachyon::ID panel = app.CreatePanel(layout.panels, "P", IconID::Invalid, [&] { draws++; });
            layout.root = MakeLeaf({ panel });

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginDockingContainer(containerID, &layout, tachyon::LayoutOverride{}.Size(800, 600));
                    app.EndDockingContainer();
                    app.EndRootWindow();
                });
            };

            frame(); frame();
            EXPECT_EQ(draws, 1);        // cached by default
            app.InvalidatePanel(panel); // data changed -> force a rebuild
            frame();
            EXPECT_EQ(draws, 2);        // rebuilt
            frame();
            EXPECT_EQ(draws, 2);        // cached again
            app.Cleanup();
        }
    }
}
