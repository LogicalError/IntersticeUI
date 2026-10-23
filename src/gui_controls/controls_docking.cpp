#include <gui_controls/gui_application_internal.h>

#include <gui_controls/gui_dock_node.h>

#include <gui_core/state/gui_scroll_management.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

namespace interstice
{

    // ---------------------------------------------------------------------------
    // Panel docking: widget wiring for the dock tree (tree ops live in gui_dock_node).
    //
    // Sizing is CSS flex (.dock-split-*, .dock-leaf, .dock-content grow/shrink);
    // the only sizing here is pinning each split's sized child to `splitSize`.
    // ---------------------------------------------------------------------------

    namespace
    {
        constexpr int kSizerThickness           = 4;   // CSS pixels
        constexpr float kRectAnimationTimeSecs  = 0.02f;

        IntRect LerpRect(IntRect a, IntRect b, float t)
        {
            auto lerp = [](int32_t a, int32_t b, float t) -> int32_t {
                return a + static_cast<int32_t>((b - a) * t);
            };
            return { lerp(a.x, b.x, t), lerp(a.y, b.y, t),
                     lerp(a.w, b.w, t), lerp(a.h, b.h, t) };
        }

        // Compute the overlay rect for a given drop zone without rendering it.
        IntRect ComputeDropZoneRect(DockNode* targetLeaf, DockDropZone zone)
        {
            IntRect contentRect = targetLeaf->cachedContentRect;
            IntRect leafRect    = targetLeaf->cachedLeafRect;
            if (contentRect.w <= 0 || contentRect.h <= 0) return {};
            if (leafRect.w <= 0 || leafRect.h <= 0) leafRect = contentRect;

            IntRect fullRect {
                contentRect.x,
                leafRect.y,
                contentRect.w,
                contentRect.y + contentRect.h - leafRect.y
            };

            const int32_t edgeW = contentRect.w / 4;
            const int32_t edgeH = contentRect.h / 4;
            IntRect overlay = fullRect;
            switch (zone)
            {
                case DockDropZone::Left:   overlay.w = edgeW; break;
                case DockDropZone::Right:  overlay.x += fullRect.w - edgeW; overlay.w = edgeW; break;
                case DockDropZone::Top:    overlay.h = edgeH + (contentRect.y - leafRect.y); break;
                case DockDropZone::Bottom: overlay.y += fullRect.h - edgeH; overlay.h = edgeH; break;
                default: return {};
            }
            return overlay;
        }

        // Cross-container tab-drop hit rect: the cached bar plus one tab row. The phantom
        // can wrap onto a new row that the one-frame-old cached rect doesn't include yet;
        // the extra row keeps it from flickering. Only consulted mid-drag.
        IntRect TabBarDropHitRect(const DockNode* leaf)
        {
            IntRect r = leaf->cachedTabBarRect;
            if (r.w > 0 && r.h > 0 && !leaf->cachedTabRects.empty())
                r.h += leaf->cachedTabRects.front().h;
            return r;
        }

        // Is this cached tab's row entirely above the cursor (cursor is on a
        // lower wrapped row)? / Does the cursor sit on this tab's row?
        inline bool TabRowAboveCursor(const IntRect& r, int cursorY) { return r.y + r.h <= cursorY; }
        inline bool TabOnCursorRow  (const IntRect& r, int cursorY) { return r.y <= cursorY && cursorY < r.y + r.h; }

        // Tab insertion index for a cross-container drop (0 = before first, count = after last).
        // Counts tabs before the cursor in (row, x) order: all tabs on rows above, plus those
        // on the cursor's row left of their midpoint. Per-tab counting stays stable on a
        // wrapped row that holds only the phantom (which cachedTabRects excludes).
        int ComputeTabInsertionIndex(const DockNode* leaf, interstice::int2 mousePos)
        {
            const auto& rects = leaf->cachedTabRects;
            const int count = static_cast<int>(rects.size());
            int idx = 0;
            for (int i = 0; i < count; i++)
            {
                const IntRect& r = rects[i];
                if (TabRowAboveCursor(r, mousePos.y)
                    || (TabOnCursorRow(r, mousePos.y) && mousePos.x >= r.x + r.w / 2))
                    idx = i + 1;
            }
            return idx;
        }

        // Emit a phantom tab element matching the dragged panel's appearance.
        // Returns the element ref for position offset / render-on-top.
        interstice::ElementRef EmitPhantomTab(GUIApplication& app, const DockTabDragInfo& dragInfo)
        {
            interstice::ID phantomID = app.idManager.AllocateDynamicID("dock_phantom_tab");
            // Selected state, to match the dragged tab (always its bar's selected tab).
            auto phantomState = app.guiStateManager.GetCurrentState(phantomID)
                              | interstice::ElementState::Selected;
            interstice::Element phantom {};
            phantom.id       = phantomID;
            phantom.styleRef = app.theme.tabStyleRef;
            phantom.state    = phantomState;
            if (dragInfo.icon != IconID::Invalid)
            {
                interstice::ContentRun runs[] = {
                    { .type = interstice::ContentRun::Icon, .iconID = dragInfo.icon,
                      .iconWidth = 16.0f, .iconHeight = 16.0f },
                    { .type = interstice::ContentRun::Text, .text = dragInfo.title }
                };
                phantom.textID = app.textSystem->RegisterRichText(
                    phantomID, runs, app.theme.tabStyleRef, phantomState);
            }
            else
            {
                phantom.textID = app.textSystem->RegisterText(
                    phantomID, dragInfo.title, app.theme.tabStyleRef, phantomState);
            }
            // Use the dragged tab's real width so the target bar's wrap decision is
            // stable and matches the post-drop layout.
            auto layout = (dragInfo.tabWidth > 0)
                ? LayoutOverride::Auto().Width(dragInfo.tabWidth)
                : LayoutOverride::Auto();
            auto res = app.elementStorage.Add(std::move(phantom), std::move(layout));
            VERIFY(res.status);
            VERIFY(app.windowRegistry.Add(res.ref));
            return res.ref;
        }

        // X offset that makes the phantom tab follow the mouse, like within-bar reorder.
        // Flow-X is estimated from the cursor's row, not the previous row's right edge.
        int16_t ComputePhantomOffsetX(const DockNode* node, int phantomIdx,
                                       const ReorderableDragState& drag,
                                       interstice::int2 mousePos)
        {
            const auto& rects = node->cachedTabRects;

            // Estimate where layout will place the phantom's left edge. If the
            // tab just before the insertion point shares the cursor's row, the
            // phantom flows right after it; otherwise the phantom begins a
            // (possibly new) row, which is left-aligned to the bar.
            const bool predOnCursorRow =
                phantomIdx > 0 && phantomIdx - 1 < (int)rects.size()
                && TabOnCursorRow(rects[phantomIdx - 1], mousePos.y);
            const int estimatedX = predOnCursorRow
                ? rects[phantomIdx - 1].x + rects[phantomIdx - 1].w
                : node->cachedTabBarRect.x;

            // Grab offset: distance from tab left edge to mouse at drag start
            int grabOffset = drag.draggedIndex < (int)drag.originalRects.size()
                ? drag.dragStartMousePos.x - drag.originalRects[drag.draggedIndex].x
                : 0;
            return static_cast<int16_t>(mousePos.x - grabOffset - estimatedX);
        }

        void EmitDockNode(GUIApplication& app, interstice::DockingLayout* layout, DockNode* node);

        // Ghost panel at the cursor previewing the floating window a drop would create:
        // a dock-leaf box with the dragged tab and an empty content area.
        void EmitGhostTab(GUIApplication& app, const interstice::Panel& panel, interstice::int2 mousePos)
        {
            // Smaller than the real 400x300 spawn so it doesn't swamp the cursor.
            const interstice::int2 kGhostSize { app.styleManager.Px(220), app.styleManager.Px(150) };
            if (app.BeginCursorGhostWindow(GUIApplicationInternals::TabDragGhostWindowID(app), mousePos, kGhostSize,
                                           app.theme.dockLeafStyleRef))
            {
                // Tab bar across the top, holding the single dragged tab.
                interstice::ID barID = app.idManager.AllocateDynamicID("dock_ghost_bar");
                interstice::Element bar{};
                bar.id       = barID;
                bar.styleRef = app.theme.tabBarStyleRef;
                bar.state    = app.guiStateManager.GetCurrentState(barID);
                app.RegisterElement(std::move(bar), {});
                VERIFY(app.elementStorage.BeginChildren());
                {
                    interstice::ID ghostTabID = app.idManager.AllocateDynamicID("dock_tab_ghost");
                    // Selected, to match the dragged tab.
                    interstice::ElementState state = app.guiStateManager.GetCurrentState(ghostTabID)
                                            | interstice::ElementState::Selected;
                    interstice::Element ghostTab{};
                    ghostTab.id       = ghostTabID;
                    ghostTab.styleRef = app.theme.tabStyleRef;
                    ghostTab.state    = state;
                    if (panel.icon != interstice::IconID::Invalid)
                    {
                        interstice::ContentRun runs[] = {
                            { .type = interstice::ContentRun::Icon, .iconID = panel.icon,
                              .iconWidth = 16.0f, .iconHeight = 16.0f },
                            { .type = interstice::ContentRun::Text, .text = panel.title }
                        };
                        ghostTab.textID = app.textSystem->RegisterRichText(
                            ghostTabID, runs, app.theme.tabStyleRef, state);
                    }
                    else
                    {
                        ghostTab.textID = app.textSystem->RegisterText(
                            ghostTabID, panel.title, app.theme.tabStyleRef, state);
                    }
                    auto tabRes = app.elementStorage.Add(std::move(ghostTab), LayoutOverride::Auto());
                    VERIFY(tabRes.status);
                    VERIFY(app.windowRegistry.Add(tabRes.ref));
                }
                VERIFY(app.elementStorage.EndChildren());

                // Empty content area filling the rest of the panel.
                interstice::ID contentID = app.idManager.AllocateDynamicID("dock_ghost_content");
                interstice::Element content{};
                content.id       = contentID;
                content.styleRef = app.theme.dockContentStyleRef;
                content.state    = app.guiStateManager.GetCurrentState(contentID);
                app.RegisterElement(std::move(content), interstice::LayoutOverride{}.FlexGrow(1));

                app.EndGhostWindow();
            }
        }

        // Render a drop-zone overlay as a ghost window at the given rect.
        void EmitDropZoneOverlayRect(GUIApplication& app, IntRect overlay)
        {
            if (overlay.w <= 0 || overlay.h <= 0) return;
            if (app.BeginGhostWindow(GUIApplicationInternals::TabDragGhostWindowID(app),
                                     { overlay.x, overlay.y },
                                     { overlay.w, overlay.h }, app.theme.panelDropVisualizationStyleRef))
            {
                app.EndGhostWindow();
            }
        }

        void EmitDockLeaf(GUIApplication& app, interstice::DockingLayout* layout, DockNode* node)
        {
            interstice::Element leafElem{};
            leafElem.id       = node->nodeID;
            leafElem.styleRef = app.theme.dockLeafStyleRef;
            leafElem.state    = app.guiStateManager.GetCurrentState(node->nodeID);
            app.RegisterElement(std::move(leafElem), {});
            VERIFY(app.elementStorage.BeginChildren());

            auto onReorder = [layout, node](int from, int to) {
                (void)layout;
                ReorderLeafTabs(*node, from, to);
            };
            auto onClose = [&app, layout, node](int tabIndex) {
                app.CloseDockedPanel(*layout, *node, tabIndex);
            };

            // Snapshot the selection to restore on drag-out (MouseDown selected the dragged tab).
            if (!GUIApplicationInternals::Reorderable(app).Drag().isDragging || GUIApplicationInternals::Reorderable(app).Drag().groupID != node->tabBarID)
                node->selectedTabBeforeDrag = node->selectedTab;

            // Publish cross-container drag info when this leaf is the drag source
            // (reset in BeginDockingFrame when no drag is active).
            {
                auto& drag = GUIApplicationInternals::Reorderable(app).Drag();
                if (drag.isDragging && drag.dragActivated && drag.groupID == node->tabBarID
                    && drag.draggedIndex >= 0 && drag.draggedIndex < (int)node->panelIds.size())
                {
                    auto& info = GUIApplicationInternals::DockTabDrag(app).Info();
                    interstice::ID pid = node->panelIds[drag.draggedIndex];
                    auto panelIt = layout->panels.find(pid);
                    if (panelIt != layout->panels.end())
                    {
                        info.panelID  = pid;
                        info.title    = panelIt->second.title;
                        info.icon     = panelIt->second.icon;
                        info.tabWidth = drag.draggedIndex < (int)drag.originalRects.size()
                                      ? drag.originalRects[drag.draggedIndex].w : 0;
                    }
                }
            }

            // ----- onDragOut: full priority chain -----
            // Fires end-of-frame when a tab is released outside the bar bounds.
            auto onDragOut = [&app, layout, node](int tabIndex, interstice::int2 mousePos)
            {
                GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().hasTarget = false;
                app.windowManager->RequestCloseAllowReopen(GUIApplicationInternals::TabDragGhostWindowID(app));

                if (tabIndex < 0 || tabIndex >= (int)node->panelIds.size()) return;

                node->selectedTab = node->selectedTabBeforeDrag;

                interstice::ID panelID = node->panelIds[tabIndex];

                // 1. Tab-bar drop — insert as tab at the hovered position.
                //    TransferPanel moves the Panel object too when crossing layouts.
                auto tabResult = app.FindDockLeafAtScreenPos(mousePos);
                if (tabResult.leaf && tabResult.leaf != node)
                {
                    int insertIdx = ComputeTabInsertionIndex(tabResult.leaf, mousePos);
                    insertIdx = std::clamp(insertIdx, 0, (int)tabResult.leaf->panelIds.size());
                    GUIApplication::TransferPanel(panelID, node, layout, tabResult.layout);
                    tabResult.leaf->panelIds.insert(tabResult.leaf->panelIds.begin() + insertIdx, panelID);
                    tabResult.leaf->selectedTab = insertIdx;
                    // Raise the destination, or the OS may focus an unrelated window
                    // when an emptied source window is destroyed.
                    if (tabResult.layout->hostWindowID != interstice::ID::Invalid())
                        app.windowManager->BringToFront(tabResult.layout->hostWindowID);
                    return;
                }

                // 2. Content-area edge drop → split the target leaf.
                //    Center is not a target, so a floating window can always be made.
                auto dropResult = app.FindDockDropTargetAtScreenPos(mousePos);
                if (dropResult.target.leaf
                    && dropResult.target.zone != DockDropZone::None
                    && dropResult.target.zone != DockDropZone::Center)
                {
                    auto* ownerPtr = FindNodeOwner(dropResult.layout->root, dropResult.target.leaf);
                    if (ownerPtr)
                    {
                        GUIApplication::TransferPanel(panelID, node, layout, dropResult.layout);
                        SplitLeafAtZone(*ownerPtr, panelID, dropResult.target.zone);
                        if (dropResult.layout->hostWindowID != interstice::ID::Invalid())
                            app.windowManager->BringToFront(dropResult.layout->hostWindowID);
                    }
                    return;
                }

                // 3. No valid target → tear off into a floating window
                GUIApplication::TransferPanel(panelID, node, layout, nullptr);

                auto fw = std::make_unique<interstice::FloatingDockWindow>();
                fw->windowID      = app.idManager.CreateStaticID();
                fw->spawnPosition = mousePos;
                fw->layout.root   = MakeLeaf({ panelID });
                // Move Panel object to the floating layout
                auto panelIt = layout->panels.find(panelID);
                if (panelIt != layout->panels.end())
                {
                    fw->layout.panels[panelID] = std::move(panelIt->second);
                    layout->panels.erase(panelIt);
                }
                GUIApplicationInternals::FloatingDockWindows(app).push_back(std::move(fw));
            };

            // ----- dragVisualizer: drop-zone overlay OR ghost tab -----
            // Animated overlay over an edge drop zone, otherwise a ghost panel at the
            // cursor. Both share the ghost window ID; only one renders per frame.
            auto dragVisualizer = [&app, layout, node](int tabIndex, interstice::int2 mousePos)
            {
                if (tabIndex < 0 || tabIndex >= (int)node->panelIds.size()) return;
                interstice::ID panelID = node->panelIds[tabIndex];
                auto panelIt = layout->panels.find(panelID);
                if (panelIt == layout->panels.end()) return;

                // 1. Tab-bar hover → the target bar's phantom tab is the visual.
                DockNode* tabTarget = GUIApplicationInternals::FindDockLeafTabBarCachedAtScreenPos(app, mousePos);
                if (tabTarget && tabTarget != node && !tabTarget->cachedTabRects.empty())
                {
                    GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().hasTarget = false;
                    return;
                }

                // 2. Content-area edge drop zone → animated overlay
                auto dropResult = app.FindDockDropTargetAtScreenPos(mousePos);
                if (dropResult.target.leaf
                    && dropResult.target.zone != DockDropZone::None
                    && dropResult.target.zone != DockDropZone::Center)
                {
                    IntRect targetRect = ComputeDropZoneRect(dropResult.target.leaf, dropResult.target.zone);
                    if (targetRect.w <= 0 || targetRect.h <= 0) return;

                    auto now = std::chrono::steady_clock::now();
                    if (GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().hasTarget)
                    {
                        // Animate from current position toward target
                        float dt = std::chrono::duration<float>(now - GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().lastTime).count();
                        float t = 1.0f - std::exp(-dt / kRectAnimationTimeSecs);
                        GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().currentRect = LerpRect(GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().currentRect, targetRect, t);
                    }
                    else
                    {
                        // First frame with a drop zone — snap immediately
                        GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().currentRect = targetRect;
                    }
                    GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().targetRect = targetRect;
                    GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().hasTarget  = true;
                    GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().lastTime   = now;

                    EmitDropZoneOverlayRect(app, GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().currentRect);
                }
                else
                {
                    // Center zone or no target → ghost panel (drop makes a floating window)
                    GUIApplicationInternals::DockTabDrag(app).DropZoneAnim().hasTarget = false;
                    EmitGhostTab(app, panelIt->second, mousePos);
                }
            };

            if (app.BeginTabBar(node->tabBarID, &node->selectedTab,
                                std::move(onReorder), std::move(onClose),
                                std::move(onDragOut), std::move(dragVisualizer),
                                app.theme.tabBarStyleRef))
            {
                const int count = static_cast<int>(node->panelIds.size());
                if (count > 0 && (node->selectedTab < 0 || node->selectedTab >= count))
                    node->selectedTab = std::clamp(node->selectedTab, 0, count - 1);

                // ----- Cross-container phantom tab detection -----
                // A tab dragged from another bar over this one gets a phantom at the
                // insertion point so the real tabs shift apart, like within-bar reorder.
                auto& drag = GUIApplicationInternals::Reorderable(app).Drag();
                const auto& dragInfo = GUIApplicationInternals::DockTabDrag(app).Info();
                interstice::int2 mousePos = drag.dragStartMousePos + drag.dragOffset;
                bool emitPhantom = false;
                int phantomIdx = 0;
                interstice::ElementRef phantomRef {};

                if (drag.isDragging && drag.groupID != node->tabBarID
                    && dragInfo.panelID != interstice::ID::Invalid()
                    && !node->cachedTabRects.empty()
                    && node->cachedTabBarRect.w > 0 && node->cachedTabBarRect.h > 0
                    && TabBarDropHitRect(node).Contains(mousePos))
                {
                    emitPhantom = true;
                    phantomIdx = ComputeTabInsertionIndex(node, mousePos);
                    phantomIdx = std::clamp(phantomIdx, 0, count);
                }

                for (int i = 0; i < count; ++i)
                {
                    if (emitPhantom && i == phantomIdx)
                        phantomRef = EmitPhantomTab(app, dragInfo);

                    auto it = layout->panels.find(node->panelIds[i]);
                    if (it == layout->panels.end()) continue;
                    app.Tab(it->second.id, it->second.title, it->second.icon);
                }

                if (emitPhantom && phantomIdx >= count)
                    phantomRef = EmitPhantomTab(app, dragInfo);

                // Phantom follows the mouse and renders on top.
                if (emitPhantom && app.elementStorage.IsValidRef(phantomRef))
                {
                    int16_t offsetX = ComputePhantomOffsetX(node, phantomIdx, drag, mousePos);
                    auto& elemRect = app.elementStorage.GetElementRectUnsafe(phantomRef);
                    auto pc = LayoutOverride{}.Relative(offsetX, 0);
                    elemRect.constraints.x = pc.x;
                    elemRect.constraints.y = pc.y;

                    app.windowRegistry.MoveSubtreeToEnd(phantomRef);
                }

                app.EndTabBar();
            }

            // ----- Content area -----
            node->lastContentID      = interstice::ID::Invalid();
            node->lastContentPanelID = interstice::ID::Invalid();
            const int count = static_cast<int>(node->panelIds.size());
            if (count > 0 && node->selectedTab >= 0 && node->selectedTab < count)
            {
                const interstice::ID selectedPanelID = node->panelIds[node->selectedTab];
                (void)app.idManager.PushParent(selectedPanelID);
                interstice::ID contentID = app.idManager.AllocateDynamicID("dock_content");
                (void)app.idManager.PopParent();
                node->lastContentID      = contentID;
                node->lastContentPanelID = selectedPanelID;
                // Record panel→content so InvalidatePanel(panelId) can resolve it.
                GUIApplicationInternals::NotePanelContentID(app, selectedPanelID, contentID);

                auto panelIt = layout->panels.find(selectedPanelID);
                if (panelIt != layout->panels.end())
                {
                    auto& scrollManagement = app.guiStateManager.GetScrollManagement();
                    auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(contentID);
                    scrollManagement.SetScrollOffset(handle, panelIt->second.scrollOffset);
                }

                // Panels are cached by default; a panel's cacheSignature can
                // version the cache (return a value) or opt out (return nullopt).
                std::optional<uint64_t> cacheKey; // nullopt → not cached (plain BeginArea)
                if (panelIt != layout->panels.end())
                {
                    cacheKey = uint64_t(0); // default: cache with a constant signature
                    if (panelIt->second.cacheSignature)
                        cacheKey = panelIt->second.cacheSignature();
                }

                if (cacheKey.has_value())
                {
                    // BeginCached is the content container: it scopes the panel's
                    // dynamic IDs under contentID and skips onPanelGUI on a cache hit.
                    if (app.BeginCached(contentID, app.theme.dockContentStyleRef, *cacheKey, {}))
                    {
                        if (panelIt != layout->panels.end() && panelIt->second.onPanelGUI)
                            panelIt->second.onPanelGUI();
                        app.EndCached();
                    }
                }
                else if (app.BeginArea(contentID, app.theme.dockContentStyleRef, {}))
                {
                    // Opted out (or no panel): scope the panel's dynamic IDs under
                    // its content area so they can't leak their counter into the
                    // dock or sibling panels.
                    (void)app.idManager.PushParent(contentID);
                    if (panelIt != layout->panels.end() && panelIt->second.onPanelGUI)
                        panelIt->second.onPanelGUI();
                    (void)app.idManager.PopParent();
                    app.EndArea();
                }
            }

            VERIFY(app.elementStorage.EndChildren());
        }

        void EmitDockSplit(GUIApplication& app, interstice::DockingLayout* layout, DockNode* node)
        {
            const bool horizontal = IsHorizontalSplit(node->mode);
            const bool pinSecond  = PinsSecondChild(node->mode);

            interstice::Element container{};
            container.id       = node->nodeID;
            container.styleRef = horizontal ? app.theme.dockSplitHorzStyleRef : app.theme.dockSplitVertStyleRef;
            container.state    = app.guiStateManager.GetCurrentState(node->nodeID);
            app.RegisterElement(std::move(container), {});
            VERIFY(app.elementStorage.BeginChildren());

            EmitDockNode(app, layout, node->first.get());

            // Pin the sized child: first by default, second when pinSecond.
            if (!pinSecond)
            {
                auto childRef = app.elementStorage.FindElementRefByID(node->first->nodeID);
                if (auto* childRect = app.elementStorage.GetElementRectPtr(childRef))
                {
                    if (horizontal) childRect->constraints.width.value  = app.styleManager.Px(node->splitSize);
                    else            childRect->constraints.height.value = app.styleManager.Px(node->splitSize);
                    childRect->constraints.flex.grow   = 0;
                    childRect->constraints.flex.shrink = 0;
                }
            }

            {
                interstice::ID sizerID = app.idManager.AllocateDynamicID("dock_sizer");
                interstice::Element sizer{};
                sizer.id       = sizerID;
                sizer.styleRef = horizontal ? app.theme.dockSizerHorzStyleRef : app.theme.dockSizerVertStyleRef;
                sizer.state    = app.guiStateManager.GetCurrentState(sizerID);
                // Last frame's laid-out extent bounds the split so a shrunken window or a long
                // drag can't squeeze the other child below its minimum. No extent yet: unbounded.
                // Split sizes are CSS pixels, so the extent is converted to them.
                const float uiScale = app.styleManager.GetUIScale();
                const int extent = int(float(horizontal ? node->cachedSplitRect.w : node->cachedSplitRect.h) / uiScale);
                SplitSizeRange range { node->minFirstSize, node->minFirstSize + 10000 };
                if (extent > 0)
                {
                    range = ComputeSplitSizeRange(*node, extent, kSizerThickness);
                    node->splitSize = std::clamp(node->splitSize, range.min, range.max);
                }
                if (horizontal)
                {
                    sizer.eventHandler = MakeHorizontalSizerHandler(
                        app.guiStateManager, node->splitSize, range.min, range.max,
                        nullptr, nullptr, pinSecond, uiScale);
                    app.RegisterElement(std::move(sizer), LayoutOverride::FixedWidth(app.styleManager.Px(kSizerThickness)));
                }
                else
                {
                    sizer.eventHandler = MakeVerticalSizerHandler(
                        app.guiStateManager, node->splitSize, range.min, range.max,
                        nullptr, nullptr, pinSecond, uiScale);
                    app.RegisterElement(std::move(sizer), LayoutOverride::FixedHeight(app.styleManager.Px(kSizerThickness)));
                }
            }

            EmitDockNode(app, layout, node->second.get());

            // When pinSecond, pin the second child after it's been emitted.
            if (pinSecond)
            {
                auto childRef = app.elementStorage.FindElementRefByID(node->second->nodeID);
                if (auto* childRect = app.elementStorage.GetElementRectPtr(childRef))
                {
                    if (horizontal) childRect->constraints.width.value  = app.styleManager.Px(node->splitSize);
                    else            childRect->constraints.height.value = app.styleManager.Px(node->splitSize);
                    childRect->constraints.flex.grow   = 0;
                    childRect->constraints.flex.shrink = 0;
                }
            }

            VERIFY(app.elementStorage.EndChildren());
        }

        void EmitDockNode(GUIApplication& app, interstice::DockingLayout* layout, DockNode* node)
        {
            if (!node) return;
            if (node->isLeaf) EmitDockLeaf(app, layout, node);
            else              EmitDockSplit(app, layout, node);
        }
    } // namespace

    // ---------------------------------------------------------------------------
    // Per-frame docking bookkeeping
    // ---------------------------------------------------------------------------

    void GUIApplication::BeginDockingFrame()
    {
        m_dockingLayoutsThisFrame.clear();
        m_toolbarDocksThisFrame.clear();
        if (!m_reorderable->Drag().isDragging)
            m_dockTabDragSub->Info() = {};
    }

    void GUIApplication::EndDockingFrame()
    {
        SyncDockingScrollOffsets();
        SyncToolbarDockRects();
    }

    // ---------------------------------------------------------------------------
    // Panel lifecycle (onEnable / onDisable)
    // ---------------------------------------------------------------------------

    namespace
    {
        // Walk the dock tree, calling visit(panelId) for each leaf's currently
        // selected tab. Panels in non-selected tabs and panels not in the tree
        // at all are skipped — those are the "hidden" ones.
        template <typename F>
        void ForEachVisiblePanelID(const DockNode* node, F&& visit)
        {
            if (!node) return;
            if (node->isLeaf)
            {
                const int n = (int)node->panelIds.size();
                if (n > 0 && node->selectedTab >= 0 && node->selectedTab < n)
                    visit(node->panelIds[node->selectedTab]);
                return;
            }
            ForEachVisiblePanelID(node->first.get(),  visit);
            ForEachVisiblePanelID(node->second.get(), visit);
        }
    }

    void GUIApplication::DisableAllPanels(DockingLayout& layout)
    {
        for (auto& [id, panel] : layout.panels)
        {
            if (!panel.internal.enabled) continue;
            if (panel.onDisable) panel.onDisable();
            panel.internal.enabled = false;
        }
    }

    void GUIApplication::EnableVisiblePanels(DockingLayout& layout)
    {
        // Mark visible panels first so we can do the disable pass with a
        // single tree walk instead of one walk per panel.
        std::unordered_set<interstice::ID> visible;
        ForEachVisiblePanelID(layout.root.get(), [&](interstice::ID pid) { visible.insert(pid); });

        // Disable panels that are enabled but no longer visible.
        for (auto& [id, panel] : layout.panels)
        {
            if (panel.internal.enabled && !visible.count(id))
            {
                if (panel.onDisable) panel.onDisable();
                panel.internal.enabled = false;
            }
        }

        // Enable panels that are visible but not yet enabled.
        for (interstice::ID pid : visible)
        {
            auto it = layout.panels.find(pid);
            if (it == layout.panels.end()) continue;
            Panel& panel = it->second;
            if (panel.internal.enabled) continue;
            if (panel.onEnable) panel.onEnable();
            panel.internal.enabled = true;
        }
    }

    // ---------------------------------------------------------------------------
    // Application API
    // ---------------------------------------------------------------------------

    void GUIApplication::BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints)
    {
        m_dockingState = {};
        m_dockingState.containerID = id;
        m_dockingState.layout      = layout;

        if (layout)
        {
            m_dockingLayoutsThisFrame.push_back(layout);
            // Stamp the hosting OS window so cross-window drops can raise
            // the destination after the source window is destroyed.
            layout->hostWindowID = activeWindowID;
        }

        interstice::Element container{};
        container.id       = id;
        container.styleRef = theme.dockContainerStyleRef;
        container.state    = guiStateManager.GetCurrentState(id);
        RegisterElement(std::move(container), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        if (!layout) return;

        CollapseEmptyLeaves(layout->root, &idManager);
        AssignMissingNodeIDs(layout->root.get(), idManager);

        // Fire onEnable/onDisable transitions before any panel's onPanelGUI
        // runs, so a freshly-visible panel's resources are ready by emit time.
        EnableVisiblePanels(*layout);

        if (layout->root && !(layout->root->isLeaf && layout->root->panelIds.empty()))
            EmitDockNode(*this, layout, layout->root.get());
    }

    void GUIApplication::EndDockingContainer()
    {
        VERIFY(elementStorage.EndChildren());
        m_dockingState = {};
    }

    // ---------------------------------------------------------------------------
    // Panel transfer helper
    // ---------------------------------------------------------------------------

    void GUIApplication::TransferPanel(interstice::ID panelID, DockNode* sourceNode,
                                    DockingLayout* sourceLayout,
                                    DockingLayout* targetLayout)
    {
        // Remove from source node's panel list
        auto it = std::find(sourceNode->panelIds.begin(), sourceNode->panelIds.end(), panelID);
        if (it != sourceNode->panelIds.end())
        {
            int removedIdx = (int)(it - sourceNode->panelIds.begin());
            sourceNode->panelIds.erase(it);
            int cnt = (int)sourceNode->panelIds.size();
            if (cnt == 0) { sourceNode->selectedTab = 0; }
            else
            {
                int sel = sourceNode->selectedTab;
                if (removedIdx < sel) sel -= 1;
                if (sel >= cnt) sel = cnt - 1;
                if (sel < 0) sel = 0;
                sourceNode->selectedTab = sel;
            }
        }

        // Move Panel object between layouts (if they differ and targetLayout is given)
        if (targetLayout && targetLayout != sourceLayout && sourceLayout)
        {
            auto panelIt = sourceLayout->panels.find(panelID);
            if (panelIt != sourceLayout->panels.end())
            {
                targetLayout->panels[panelID] = std::move(panelIt->second);
                sourceLayout->panels.erase(panelIt);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Hit-testing
    // ---------------------------------------------------------------------------

    interstice::DockLeafResult GUIApplication::FindDockLeafAtScreenPos(int2 screenPos)
    {
        DockLeafResult found {};
        auto walk = [this, screenPos, &found](auto& self, DockNode* node, DockingLayout* layout) -> void
        {
            if (!node || found.leaf) return;
            if (node->isLeaf)
            {
                auto ref = elementStorage.FindElementRefByID(node->tabBarID);
                if (const auto* rectPtr = elementStorage.GetElementRectPtr(ref))
                    if (rectPtr->rect.Contains(screenPos))
                        found = { node, layout };
                return;
            }
            self(self, node->first.get(),  layout);
            if (!found.leaf) self(self, node->second.get(), layout);
        };

        for (DockingLayout* layout : m_dockingLayoutsThisFrame)
        {
            if (!layout || found.leaf) break;
            walk(walk, layout->root.get(), layout);
        }
        return found;
    }

    DockNode* GUIApplication::FindDockLeafTabBarCachedAtScreenPos(int2 screenPos)
    {
        DockNode* found = nullptr;
        auto walk = [screenPos, &found](auto& self, DockNode* node) -> void
        {
            if (!node || found) return;
            if (node->isLeaf)
            {
                if (node->cachedTabBarRect.w > 0 && node->cachedTabBarRect.h > 0
                    && TabBarDropHitRect(node).Contains(screenPos))
                    found = node;
                return;
            }
            self(self, node->first.get());
            if (!found) self(self, node->second.get());
        };

        for (DockingLayout* layout : m_dockingLayoutsThisFrame)
        {
            if (!layout || found) break;
            walk(walk, layout->root.get());
        }
        return found;
    }

    interstice::DockDropResult GUIApplication::FindDockDropTargetAtScreenPos(int2 screenPos)
    {
        // Uses last frame's cachedContentRect; called during construction, before layout.
        DockDropResult result {};
        auto walk = [screenPos, &result](auto& self, DockNode* node, DockingLayout* layout) -> void
        {
            if (!node || result.target.leaf) return;
            if (node->isLeaf)
            {
                if (node->cachedContentRect.w > 0 && node->cachedContentRect.h > 0)
                {
                    DockDropZone zone = ComputeDropZone(node->cachedContentRect, screenPos);
                    if (zone != DockDropZone::None)
                    {
                        result = { { node, zone }, layout };
                        return;
                    }
                }
                return;
            }
            self(self, node->first.get(), layout);
            if (!result.target.leaf) self(self, node->second.get(), layout);
        };

        for (DockingLayout* layout : m_dockingLayoutsThisFrame)
        {
            if (!layout || result.target.leaf) break;
            walk(walk, layout->root.get(), layout);
        }
        return result;
    }

    // ---------------------------------------------------------------------------
    // Floating dock windows
    // ---------------------------------------------------------------------------

    void GUIApplication::CloseDockedPanel(DockingLayout& layout, DockNode& leaf, int tabIndex)
    {
        if (!leaf.isLeaf || tabIndex < 0 || tabIndex >= int(leaf.panelIds.size())) return;
        const interstice::ID panelID = leaf.panelIds[size_t(tabIndex)];
        CloseLeafTab(leaf, tabIndex);

        // Closing is permanent: the panel and its state are destroyed (a new
        // panel can be created later), after its onDisable if it was shown.
        auto it = layout.panels.find(panelID);
        if (it == layout.panels.end()) return;
        if (it->second.internal.enabled && it->second.onDisable) it->second.onDisable();
        layout.panels.erase(it);
    }

    void GUIApplication::CloseFloatingDockWindow(size_t index)
    {
        if (index >= m_floatingDockWindows.size()) return;
        auto& fw = *m_floatingDockWindows[index];
        // Closing the window closes its panels for good: they're disabled and
        // destroyed with the window's layout, and every ID it held is released.
        DisableAllPanels(fw.layout);
        ReleaseDockTreeIDs(fw.layout.root.get(), &idManager);
        idManager.ReleaseStaticID(fw.windowID);
        m_floatingDockWindows.erase(m_floatingDockWindows.begin() + (ptrdiff_t)index);
    }

    void GUIApplication::EmitFloatingDockWindows()
    {
        for (size_t i = 0; i < m_floatingDockWindows.size(); )
        {
            auto& fw = *m_floatingDockWindows[i];

            // Check if layout is empty (all panels were dragged out)
            bool isEmpty = !fw.layout.root
                        || (fw.layout.root->isLeaf && fw.layout.root->panelIds.empty());
            if (isEmpty)
            {
                CloseFloatingDockWindow(i);
                continue;
            }

            // Determine window title from selected tab
            std::string title = "Panel";
            if (fw.layout.root && fw.layout.root->isLeaf && !fw.layout.root->panelIds.empty())
            {
                int sel = std::clamp(fw.layout.root->selectedTab, 0,
                                     (int)fw.layout.root->panelIds.size() - 1);
                auto pIt = fw.layout.panels.find(fw.layout.root->panelIds[sel]);
                if (pIt != fw.layout.panels.end())
                    title = pIt->second.title;
            }

            auto constraints = LayoutOverride::FixedSize(styleManager.Px(400), styleManager.Px(300));
            if (!fw.positioned)
            {
                // First frame: place the window at the mouse position where
                // the tab was released. After this the OS window manager tracks
                // the position (UserControlled), so we only set Fixed once.
                constraints.x = PositionConstraintAxis::Fixed(
                    static_cast<int16_t>(fw.spawnPosition.x));
                constraints.y = PositionConstraintAxis::Fixed(
                    static_cast<int16_t>(fw.spawnPosition.y));
                fw.positioned = true;
            }
            if (BeginWindow(fw.windowID, title,
                            std::move(constraints), theme.dockWindowStyleRef,
                            ElementFlags::WindowUserControlled))
            {
                interstice::ID containerID = idManager.AllocateDynamicID("floating_dock_container");
                BeginDockingContainer(containerID, &fw.layout);
                EndDockingContainer();
                EndWindow();
                ++i;
            }
            else
            {
                // Window closed by user
                CloseFloatingDockWindow(i);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Scroll offset sync
    // ---------------------------------------------------------------------------

    void GUIApplication::SyncDockingScrollOffsets()
    {
        auto walk = [this](auto& self, DockNode* node, DockingLayout* layout) -> void
        {
            if (!node || !layout) return;
            if (node->isLeaf)
            {
                if (node->lastContentID == ID::Invalid())      return;
                if (node->lastContentPanelID == ID::Invalid())  return;
                auto panelIt = layout->panels.find(node->lastContentPanelID);
                if (panelIt == layout->panels.end()) return;

                auto& scrollManagement = guiStateManager.GetScrollManagement();
                auto handle = scrollManagement.GetScrollOffsetHandle(node->lastContentID);
                if (handle != ScrollOffsetHandle::Invalid)
                    panelIt->second.scrollOffset = scrollManagement.GetScrollOffset(handle);

                // Cache rects for next frame's construction-time hit-testing and overlays.
                auto contentRef = elementStorage.FindElementRefByID(node->lastContentID);
                if (const auto* rectPtr = elementStorage.GetElementRectPtr(contentRef))
                    node->cachedContentRect = rectPtr->rect;
                auto leafRef = elementStorage.FindElementRefByID(node->nodeID);
                if (const auto* leafRectPtr = elementStorage.GetElementRectPtr(leafRef))
                    node->cachedLeafRect = leafRectPtr->rect;

                // Cache tab bar and tab rects for cross-container insertion. Tabs are looked
                // up by panel ID so the phantom is excluded (avoids feedback oscillation).
                node->cachedTabRects.clear();
                auto tabBarRef = elementStorage.FindElementRefByID(node->tabBarID);
                if (const auto* tabBarRectPtr = elementStorage.GetElementRectPtr(tabBarRef))
                    node->cachedTabBarRect = tabBarRectPtr->rect;
                for (const auto& panelId : node->panelIds)
                {
                    auto tabRef = elementStorage.FindElementRefByID(panelId);
                    if (const auto* r = elementStorage.GetElementRectPtr(tabRef))
                        node->cachedTabRects.push_back(r->rect);
                }
                return;
            }
            auto splitRef = elementStorage.FindElementRefByID(node->nodeID);
            if (const auto* splitRectPtr = elementStorage.GetElementRectPtr(splitRef))
                node->cachedSplitRect = splitRectPtr->rect;
            self(self, node->first.get(),  layout);
            self(self, node->second.get(), layout);
        };

        for (DockingLayout* layout : m_dockingLayoutsThisFrame)
        {
            if (!layout) continue;
            walk(walk, layout->root.get(), layout);
        }
        // Don't clear m_dockingLayoutsThisFrame here: onDragOut runs during input dispatch
        // after this and needs it. BeginDockingFrame clears it.
    }
}