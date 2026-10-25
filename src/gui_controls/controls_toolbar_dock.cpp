#include "controls_toolbar_dock.h"

#include <gui_controls/gui_application_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace interstice
{
    namespace
    {
        constexpr float kRectAnimationTimeSecs = 0.03f;

        bool IsHorizontalSide(ToolbarSide side)
        {
            return side == ToolbarSide::Top || side == ToolbarSide::Bottom;
        }

        IntRect LerpRect(IntRect a, IntRect b, float t)
        {
            auto lerp = [](int32_t a, int32_t b, float t) -> int32_t {
                return a + static_cast<int32_t>((b - a) * t);
            };
            return { lerp(a.x, b.x, t), lerp(a.y, b.y, t),
                     lerp(a.w, b.w, t), lerp(a.h, b.h, t) };
        }

        constexpr int32_t kEmptySideStripThickness = 24;   // CSS pixels

        // Thin edge strip standing in for an empty (invisible) side, so it can be dropped on.
        IntRect ComputePhantomSideRect(const IntRect& dock, int sideIdx)
        {
            if (dock.w <= 0 || dock.h <= 0) return {};
            const int32_t t = kEmptySideStripThickness;
            switch ((ToolbarSide)sideIdx)
            {
                case ToolbarSide::Top:
                    return { dock.x, dock.y, dock.w, t };
                case ToolbarSide::Bottom:
                    return { dock.x, dock.y + dock.h - t, dock.w, t };
                case ToolbarSide::Left:
                    return { dock.x, dock.y, t, dock.h };
                case ToolbarSide::Right:
                    return { dock.x + dock.w - t, dock.y, t, dock.h };
            }
            return {};
        }

        // A side's hit-test rect: its cached rect, else the phantom edge strip.
        // Called during construction, so never reads current layout.
        IntRect GetSideTargetRect(const ToolbarDockState& state, int sideIdx)
        {
            const IntRect& r = state.cachedSideRects[sideIdx];
            if (r.w > 0 && r.h > 0) return r;
            return ComputePhantomSideRect(state.cachedDockRect, sideIdx);
        }

        int FindToolbarSideAt(const ToolbarDockState& state, int2 mousePos)
        {
            for (int t = 0; t < kToolbarSideCount; ++t)
            {
                IntRect r = GetSideTargetRect(state, t);
                if (r.w > 0 && r.h > 0 && r.Contains(mousePos)) return t;
            }
            return -1;
        }

        // Toolbar drag thumb: at the LEFT of a horizontal item, TOP of a vertical one,
        // stretched along the cross axis. Only the floating window's thumb has a
        // handler (window move); docked thumbs let the reorderable item drive the drag.
        void EmitToolbarThumb(GUIApplication& app, bool horizontal,
                              std::function<bool(ElementEvent&&)> eventHandler)
        {
            ID handleID = app.idManager.AllocateDynamicID("toolbar_thumb");
            Element handle{};
            handle.id       = handleID;
            handle.state    = app.guiStateManager.GetCurrentState(handleID);
            handle.styleRef = horizontal
                ? app.theme.toolbarThumbHorzStyleRef
                : app.theme.toolbarThumbVertStyleRef;
            if (eventHandler)
            {
                handle.flags        = ElementFlags::IsControl;
                handle.eventHandler = std::move(eventHandler);
            }
            auto res = app.elementStorage.Add(std::move(handle), LayoutOverride{});
            VERIFY(res.status);
            VERIFY(app.windowRegistry.Add(res.ref));
        }

        // Contents of a floating toolbar window, shared by the real window and the
        // drag-out preview so they match. `eventHandler` goes to the thumb (empty for the preview).
        void EmitFloatingToolbarContents(GUIApplication& app,
                                          const ToolbarItem& item,
                                          std::function<bool(ElementEvent&&)> eventHandler)
        {
            EmitToolbarThumb(app, /*horizontal=*/true, std::move(eventHandler));
            if (item.renderBody)
            {
                app.BeginToolbar(/*horizontal=*/true);
                item.renderBody();
                app.EndToolbar();
            }
        }

        // Same as EmitDropZoneOverlayRect in controls_docking.cpp.
        void EmitDropZoneOverlayRect(GUIApplication& app, ID windowID, IntRect overlay)
        {
            if (overlay.w <= 0 || overlay.h <= 0) return;
            app.BeginGhostWindow(windowID,
                                 { overlay.x, overlay.y },
                                 { overlay.w, overlay.h },
                                 app.theme.panelDropVisualizationStyleRef);   // the window itself is the overlay
            app.EndGhostWindow();
        }

        void RenderToolbarSide(GUIApplication&                  app,
                               ToolbarDockState&                state,
                               ToolbarSide                      side,
                               const std::vector<ToolbarItem>&  items,
                               const ToolbarDockCallbacks&      callbacks)
        {
            const int  sideIdx    = (int)side;
            const bool horizontal = IsHorizontalSide(side);
            const auto dir        = horizontal ? ReorderDirection::Horizontal
                                               : ReorderDirection::Vertical;

            // Empty sides stay invisible, even mid-drag; drops use the phantom edge strip.
            const bool isEmpty = items.empty();
            const StyleRef groupStyle = horizontal
                ? (isEmpty ? app.theme.toolbarSideHorzEmptyStyleRef
                           : app.theme.toolbarSideHorzStyleRef)
                : (isEmpty ? app.theme.toolbarSideVertEmptyStyleRef
                           : app.theme.toolbarSideVertStyleRef);

            // Copy the callbacks: the handlers run during input dispatch, after the caller's struct is gone.
            auto reorderCb   = callbacks.onReorder;
            auto crossDropCb = callbacks.onCrossSideDrop;
            auto tearOffCb   = callbacks.onTearOff;

            auto onReorder = [reorderCb = std::move(reorderCb), side](int from, int to)
            {
                if (reorderCb) reorderCb(side, from, to);
            };

            auto onDragOut = [&app, &state,
                              crossDropCb = std::move(crossDropCb),
                              tearOffCb   = std::move(tearOffCb),
                              side, sideIdx]
                             (int fromIdx, int2 mousePos)
            {
                app.windowManager->RequestCloseAllowReopen(state.ghostWindowID);
                state.dropAnimHasTarget = false;

                int targetSide = FindToolbarSideAt(state, mousePos);
                if (targetSide == sideIdx) return; // dropped back on source - snap back

                if (targetSide < 0)
                {
                    // No side hit -> tear off into a floating window.
                    if (tearOffCb) tearOffCb(side, fromIdx, mousePos);
                    return;
                }

                if (crossDropCb) crossDropCb(side, fromIdx, (ToolbarSide)targetSide);
            };

            // Runs each frame the cursor is outside the source side: either an animated
            // overlay on the hovered side, or a floating-toolbar preview at the cursor.
            // Both share state.ghostWindowID.
            auto dragVisualizer = [&app, &state, &items, side, sideIdx]
                                  (int fromIdx, int2 mousePos)
            {
                if (fromIdx < 0 || fromIdx >= (int)items.size()) return;
                const auto& item = items[fromIdx];

                int targetSide = FindToolbarSideAt(state, mousePos);
                if (targetSide >= 0 && targetSide != sideIdx)
                {
                    IntRect targetRect = GetSideTargetRect(state, targetSide);
                    if (targetRect.w <= 0 || targetRect.h <= 0) return;

                    auto now = std::chrono::steady_clock::now();
                    if (state.dropAnimHasTarget)
                    {
                        float dt = std::chrono::duration<float>(now - state.dropAnimLastTime).count();
                        float t  = 1.0f - std::exp(-dt / kRectAnimationTimeSecs);
                        state.dropAnimCurrent = LerpRect(state.dropAnimCurrent, targetRect, t);
                    }
                    else
                    {
                        state.dropAnimCurrent = targetRect;
                    }
                    state.dropAnimTarget      = targetRect;
                    state.dropAnimHasTarget   = true;
                    state.dropAnimLastTime    = now;

                    EmitDropZoneOverlayRect(app, state.ghostWindowID, state.dropAnimCurrent);
                }
                else
                {
                    // No target -> preview of the floating window, top-left at the
                    // cursor where the real one will spawn.
                    state.dropAnimHasTarget = false;
                    if (app.BeginGhostWindow(state.ghostWindowID, mousePos,
                                             app.theme.toolbarFloatingWindowStyleRef))
                    {
                        EmitFloatingToolbarContents(app, item, /*eventHandler=*/{});
                    }
                    app.EndGhostWindow();
                }
            };

            // Same outside-the-group test as EndReorderableGroup: while the ghost preview
            // shows, hide the in-place body so it isn't duplicated.
            const auto& drag = GUIApplicationInternals::Reorderable(app).Drag();
            const ID groupID = state.groupIDs[sideIdx];
            const bool draggingThisGroup =
                drag.isDragging && drag.dragActivated && drag.groupID == groupID;
            bool hideDraggedItemBody = false;
            if (draggingThisGroup)
            {
                const int mainAxis  = (drag.direction == ReorderDirection::Horizontal) ? 0 : 1;
                const int crossAxis = 1 - mainAxis;
                const int2 mouse    = drag.dragStartMousePos + drag.dragOffset;
                const bool outsideCross = mouse[crossAxis] < drag.groupRect.position[crossAxis]
                                       || mouse[crossAxis] > drag.groupRect.position[crossAxis] + drag.groupRect.size[crossAxis];
                const bool outsideMain  = mouse[mainAxis]  < drag.groupRect.position[mainAxis]
                                       || mouse[mainAxis]  > drag.groupRect.position[mainAxis]  + drag.groupRect.size[mainAxis];
                hideDraggedItemBody = outsideCross || outsideMain;
            }

            if (app.BeginReorderableGroup(state.groupIDs[sideIdx], dir, /*isWrapped=*/true,
                                          std::move(onReorder), groupStyle, {},
                                          std::move(onDragOut), std::move(dragVisualizer)))
            {
                // EmitToolbarThumb rather than EmitDragHandle, which only fits flex-row items.
                const StyleRef itemStyle = horizontal
                    ? app.theme.toolbarItemHorzStyleRef
                    : app.theme.toolbarItemVertStyleRef;
                // Dragged-out source slot: still emitted (indices stay aligned) but empty
                // and zero-sized, so neighbours reflow into the gap.
                const StyleRef draggedOutSlotStyle = horizontal
                    ? app.theme.emptyHorzStyleRef
                    : app.theme.emptyVertStyleRef;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    const auto& item = items[i];
                    const bool draggedOut = hideDraggedItemBody && (int)i == drag.draggedIndex;
                    app.BeginReorderableItem(item.id,
                                             draggedOut ? draggedOutSlotStyle : itemStyle);
                    if (!draggedOut)
                    {
                        EmitToolbarThumb(app, horizontal, /*eventHandler=*/{});
                        if (item.renderBody)
                        {
                            app.BeginToolbar(horizontal);
                            item.renderBody();
                            app.EndToolbar();
                        }
                    }
                    app.EndReorderableItem();
                }
                app.EndReorderableGroup();
            }
        }
    } // anonymous namespace

    // ---------------------------------------------------------------------------

    void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app)
    {
        for (int i = 0; i < kToolbarSideCount; ++i)
            state.groupIDs[i] = app.idManager.CreateStaticID();
        state.ghostWindowID   = app.idManager.CreateStaticID();
        state.dockContainerID = app.idManager.CreateStaticID();
    }

    void ReleaseToolbarDockState(ToolbarDockState& state, GUIApplication& app)
    {
        for (int i = 0; i < kToolbarSideCount; ++i)
        {
            if (state.groupIDs[i] != ID::Invalid())
            {
                app.idManager.ReleaseStaticID(state.groupIDs[i]);
                state.groupIDs[i] = ID::Invalid();
            }
        }
        if (state.ghostWindowID != ID::Invalid())
        {
            app.idManager.ReleaseStaticID(state.ghostWindowID);
            state.ghostWindowID = ID::Invalid();
        }
        if (state.dockContainerID != ID::Invalid())
        {
            app.idManager.ReleaseStaticID(state.dockContainerID);
            state.dockContainerID = ID::Invalid();
        }
    }

    // One OS window per floating toolbar (like EmitFloatingDockWindows). Closing it
    // docks the item back on its home side through onDockBack (without one, the
    // entry is erased and its windowID released).
    void GUIApplication::EmitFloatingToolbarWindows(
        std::vector<FloatingToolbarItem>& floatingItems,
        const FloatingToolbarOptions* options)
    {
        ToolbarDockState* dockState = options ? options->dockState : nullptr;
        std::function<void(ID, ToolbarSide)> dockBack = options ? options->onDockBack : nullptr;

        // The drag ends in the thumb's MouseUp handler. If that never runs (the window was
        // closed mid-drag), drop the drag here - button up and no release still queued -
        // or the drop overlay would keep showing.
        if (m_toolbarWindowDrag.active && inputManager
            && (inputManager->GetCurrentButtons() & MouseButtons::Left) == MouseButtons::None)
        {
            bool releasePending = false;
            for (const auto& e : inputManager->GetRecordedEvents())
                if (e.type == EventType::MouseUp) { releasePending = true; break; }
            if (!releasePending)
            {
                if (dockState && m_toolbarWindowDrag.sourceDockState == dockState)
                {
                    windowManager->RequestCloseAllowReopen(dockState->ghostWindowID);
                    dockState->dropAnimHasTarget = false;
                }
                m_toolbarWindowDrag = {};
            }
        }

        // ---- Drag visualization for floating-window thumb drags ----
        // Over a source-dock side: show the animated drop overlay (shares the dock's ghost window).
        if (m_toolbarWindowDrag.active && m_toolbarWindowDrag.sourceDockState
            && m_toolbarWindowDrag.sourceDockState == dockState)
        {
            int targetSide = FindToolbarSideAt(*dockState, m_toolbarWindowDrag.currentMousePos);
            if (targetSide >= 0)
            {
                IntRect targetRect = GetSideTargetRect(*dockState, targetSide);
                if (targetRect.w > 0 && targetRect.h > 0)
                {
                    auto now = std::chrono::steady_clock::now();
                    if (dockState->dropAnimHasTarget)
                    {
                        float dt = std::chrono::duration<float>(now - dockState->dropAnimLastTime).count();
                        float t  = 1.0f - std::exp(-dt / kRectAnimationTimeSecs);
                        dockState->dropAnimCurrent = LerpRect(dockState->dropAnimCurrent, targetRect, t);
                    }
                    else
                    {
                        dockState->dropAnimCurrent = targetRect;
                    }
                    dockState->dropAnimTarget    = targetRect;
                    dockState->dropAnimHasTarget = true;
                    dockState->dropAnimLastTime  = now;
                    EmitDropZoneOverlayRect(*this, dockState->ghostWindowID,
                                            dockState->dropAnimCurrent);
                }
            }
            else
            {
                dockState->dropAnimHasTarget = false;
            }
        }
        for (size_t i = 0; i < floatingItems.size(); )
        {
            auto& fw = floatingItems[i];

            const std::string& title = fw.title.empty() ? std::string("Toolbar") : fw.title;

            // Auto-sized to content. Position is set on the first frame only; then the OS owns it.
            auto constraints = LayoutOverride{};
            if (!fw.positioned)
            {
                constraints.x = PositionConstraintAxis::Fixed(
                    static_cast<int16_t>(fw.spawnPosition.x));
                constraints.y = PositionConstraintAxis::Fixed(
                    static_cast<int16_t>(fw.spawnPosition.y));
                fw.positioned = true;
            }

            // WindowNoCaption: borderless, always-on-top, no taskbar entry. With no
            // title bar, the thumb handler below moves the window.
            const ElementFlags flags = ElementFlags::WindowNoCaption
                                     | ElementFlags::WindowCanMove
                                     | ElementFlags::WindowCloseWithParent;

            const bool open = BeginWindow(fw.windowID, title, theme.toolbarFloatingWindowStyleRef,
                                          std::move(constraints), flags);
            if (open)
            {
                // ---- Drag-from-handle ----
                // Moves the OS window; on release over a dock side, docks back (if enabled).
                ID windowIDForHandler = fw.windowID;
                auto moveHandler = [this, windowIDForHandler, dockState, dockBack]
                                   (ElementEvent&& e) -> bool
                {
                    if (e.type == EventType::MouseDown && e.state.LeftPressed())
                    {
                        m_toolbarWindowDrag.active           = true;
                        m_toolbarWindowDrag.windowID         = windowIDForHandler;
                        m_toolbarWindowDrag.startMousePos    = e.state.screenPos;
                        m_toolbarWindowDrag.currentMousePos  = e.state.screenPos;
                        m_toolbarWindowDrag.startRect        = windowManager->GetWindowRect(windowIDForHandler);
                        m_toolbarWindowDrag.sourceDockState  = dockState;
                        guiStateManager.Interaction().SetCaptureID(e.id);
                        return true;
                    }
                    if (e.type == EventType::MouseMove
                        && e.state.LeftDown()
                        && m_toolbarWindowDrag.active
                        && m_toolbarWindowDrag.windowID == windowIDForHandler)
                    {
                        m_toolbarWindowDrag.currentMousePos = e.state.screenPos;
                        int2 delta = e.state.screenPos - m_toolbarWindowDrag.startMousePos;
                        IntRect newRect = m_toolbarWindowDrag.startRect;
                        newRect.x += delta.x;
                        newRect.y += delta.y;
                        windowManager->SetWindowRect(windowIDForHandler, newRect);
                        return true;
                    }
                    if (e.type == EventType::MouseUp && m_toolbarWindowDrag.active
                        && m_toolbarWindowDrag.windowID == windowIDForHandler)
                    {
                        m_toolbarWindowDrag.active = false;
                        guiStateManager.Interaction().ReleaseCaptureID();

                        // Close the drop overlay and reset its animation.
                        if (dockState)
                        {
                            windowManager->RequestCloseAllowReopen(dockState->ghostWindowID);
                            dockState->dropAnimHasTarget = false;
                        }
                        m_toolbarWindowDrag.sourceDockState = nullptr;

                        if (dockState && dockBack)
                        {
                            int t = FindToolbarSideAt(*dockState, e.state.screenPos);
                            if (t >= 0)
                                dockBack(windowIDForHandler, (ToolbarSide)t);
                        }
                        return true;
                    }
                    return false;
                };
                EmitFloatingToolbarContents(*this, fw.item, std::move(moveHandler));
            }
            EndWindow();
            if (open)
                ++i;
            else if (dockBack)
            {
                // The window closed (OS close, Alt+F4, its parent closing): the item goes
                // back to its home side. dockBack erases the entry, so i stays.
                dockBack(fw.windowID, fw.homeSide);
            }
            else
            {
                // Nowhere to put it back: release the ID and drop the entry.
                idManager.ReleaseStaticID(fw.windowID);
                floatingItems.erase(floatingItems.begin() + (ptrdiff_t)i);
            }
        }
    }

    // Runs from EndDockingFrame, after layout: caches side rects for next frame's drag hit-tests.
    void GUIApplication::SyncToolbarDockRects()
    {
        for (ToolbarDockState* state : m_toolbarDocksThisFrame)
        {
            if (!state) continue;
            for (int i = 0; i < kToolbarSideCount; ++i)
            {
                auto ref = elementStorage.FindElementRefByID(state->groupIDs[i]);
                if (const auto* rectPtr = elementStorage.GetElementRectPtr(ref))
                    state->cachedSideRects[i] = rectPtr->rect;
            }
            auto dockRef = elementStorage.FindElementRefByID(state->dockContainerID);
            if (const auto* rectPtr = elementStorage.GetElementRectPtr(dockRef))
                state->cachedDockRect = rectPtr->rect;
        }
    }

    void RenderToolbarDock(GUIApplication&                app,
                           ToolbarDockState&              state,
                           const std::vector<ToolbarItem> (&itemsPerSide)[kToolbarSideCount],
                           const ToolbarDockCallbacks&    callbacks,
                           std::function<void()>          renderContent)
    {
        // Registered so SyncToolbarDockRects can cache rects after layout.
        GUIApplicationInternals::ToolbarDocksThisFrame(app).push_back(&state);

        // 5-zone scaffold: top / (left | content | right) / bottom. The container's
        // rect is cached for phantom-side hit-testing.
        app.BeginVertical(state.dockContainerID, app.theme.viewBackgroundStyleRef,
                          LayoutOverride{}.FlexGrow(1));
        {
            RenderToolbarSide(app, state, ToolbarSide::Top,
                              itemsPerSide[(int)ToolbarSide::Top], callbacks);

            app.BeginHorizontal(LayoutOverride{}.FlexGrow(1));
            {
                RenderToolbarSide(app, state, ToolbarSide::Left,
                                  itemsPerSide[(int)ToolbarSide::Left], callbacks);

                app.BeginArea(app.theme.emptyVertStyleRef, LayoutOverride{}.FlexGrow(1));
                {
                    if (renderContent) renderContent();
                }
                app.EndArea();

                RenderToolbarSide(app, state, ToolbarSide::Right,
                                  itemsPerSide[(int)ToolbarSide::Right], callbacks);
            }
            app.EndHorizontal();

            RenderToolbarSide(app, state, ToolbarSide::Bottom,
                              itemsPerSide[(int)ToolbarSide::Bottom], callbacks);
        }
        app.EndVertical();
    }

} // namespace interstice
