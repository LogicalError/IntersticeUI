// Event-handler builders attached by BeginTable / BeginRow:
//   - BuildTableContainerHandler: MouseDown (focus), MouseScroll (routed to the
//     section body under the cursor), KeyDown (keyboard nav).
//   - BuildRowBackdropHandler: row click — single/double-click,
//     Ctrl+Shift / slow re-click (rename), Shift / Ctrl multi-select.
//
// Cell-emission: controls_table_cells.cpp. Structural emission:
// controls_table.cpp. Shared helpers: controls_table_internal.h.

#include "controls_table_internal.h"

#include <gui_core/layout/gui_layout_helpers.h>

#include <algorithm>
#include <chrono>

namespace interstice
{
    using namespace interstice::TableInternal;

    interstice::EventHandler GUIApplication::BuildTableContainerHandler(TableState* state,
                                                                 interstice::ID containerID)
    {
        TableState* statePtr = state;
        return [this, statePtr, containerID](ElementEvent&& event) -> bool
        {
            switch (event.type)
            {
                case interstice::EventType::MouseDown:
                    if (!event.state.LeftPressed()) break;
                    guiStateManager.Interaction().SetFocusID(event.id);
                    guiStateManager.TextEditorFocus().Clear();
                    return true;

                case interstice::EventType::MouseScroll:
                {
                    // Find which section's body the cursor is over and route
                    // the wheel there. X scrolls that section directly; Y
                    // gets mirrored across all sections by MirrorTableScroll.
                    const int n = statePtr->internal.sectionCount;
                    if (n <= 0) break;
                    const float step = styleManager.Px(kWheelScrollStep);
                    const int2 delta {
                        static_cast<int32_t>(event.state.scroll.x * step),
                        static_cast<int32_t>(event.state.scroll.y * step)
                    };
                    if (delta.x == 0 && delta.y == 0) return false;

                    interstice::ID targetBodyID = interstice::ID::Invalid();
                    // Compare against rect coords in layout space — convert the
                    // screen cursor through any ancestor contentTransform first.
                    const int2 localCursor = event.LocalCursor(elementStorage);
                    for (int i = 0; i < n; i++)
                    {
                        const interstice::ID id = statePtr->SectionBodyID(i);
                        auto ref = elementStorage.FindElementRefByID(id);
                        if (!ref.IsValid()) continue;
                        const auto& r = elementStorage.GetElementRectUnsafe(ref).rect;
                        if (localCursor.x >= r.x && localCursor.x < r.x + r.w
                         && localCursor.y >= r.y && localCursor.y < r.y + r.h)
                        {
                            targetBodyID = id;
                            break;
                        }
                    }
                    if (targetBodyID == interstice::ID::Invalid())
                        targetBodyID = statePtr->SectionBodyID(0);

                    auto bodyRef = elementStorage.FindElementRefByID(targetBodyID);
                    if (!bodyRef.IsValid()) return false;
                    const auto& bodyRect = elementStorage.GetElementRectUnsafe(bodyRef);

                    auto& scrollManagement = guiStateManager.GetScrollManagement();
                    auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(targetBodyID);
                    const int2 offset = scrollManagement.GetScrollOffset(handle);
                    const int2 requested { offset.x + delta.x, offset.y + delta.y };
                    const int2 clamped = LayoutHelpers::ClampScrollOffsetToContent(requested, bodyRect);
                    if (clamped != offset)
                        scrollManagement.SetScrollOffset(handle, clamped);

                    // Always consume, even at the scroll limit, or hierarchical scroll
                    // leaks the wheel into an ancestor or another section.
                    return true;
                }

                case interstice::EventType::KeyDown:
                {
                    if (statePtr->internal.totalRowCount <= 0
                        || statePtr->internal.rowsRangeID == interstice::ID::Invalid()) break;

                    // The cursor is selectedRowID (every click and navigation
                    // moves it). MultiSelectState::lastClickedID is only the
                    // Shift-range anchor, which a Shift-click leaves in place.
                    interstice::ID cursorID = (statePtr->selectedRowID == interstice::ID::Invalid() && statePtr->multiSelect)
                        ? statePtr->multiSelect->lastClickedID
                        : statePtr->selectedRowID;

                    // Navigation uses dataset row indices from the sub-range ID, so it
                    // can move outside the virtualization window.
                    auto rowIndexOf = [](interstice::ID id) -> int {
                        if (id.GetMode() != interstice::IDMode::SubRange) return -1;
                        return static_cast<int>(id.value.subRangeParts.index);
                    };
                    const int totalRows = statePtr->internal.totalRowCount;
                    const int idx = rowIndexOf(cursorID);

                    const int colCount = int(statePtr->columnWidths.size());
                    const bool cellMode = (statePtr->selectionMode == TableSelectionMode::Cell);

                    // Fully visible rows per page: viewport minus the sticky header and,
                    // when shown, the H-scrollbar, so nav never lands on an occluded row.
                    const int32_t rh     = std::max<int32_t>(1, statePtr->internal.actualRowHeight);
                    // One row's worth before the first render has sampled the viewport.
                    const int32_t innerH = statePtr->internal.lastViewportH > 0
                        ? statePtr->internal.lastViewportH : rh;
                    // H-overflow from current layout: internal.anyHorzOverflow is set at render,
                    // which runs after input dispatch.
                    bool hOverflow = false;
                    {
                        const int n = statePtr->internal.sectionCount;
                        for (int i = 0; i < n; i++)
                        {
                            const interstice::ID sid = statePtr->SectionBodyID(i);
                            if (sid == interstice::ID::Invalid()) continue;
                            auto sref = elementStorage.FindElementRefByID(sid);
                            if (!sref.IsValid()) continue;
                            const auto& sr = elementStorage.GetElementRectUnsafe(sref);
                            if (sr.contentBounds.w > sr.innerRect.w)
                            {
                                hOverflow = true;
                                break;
                            }
                        }
                    }
                    const int32_t hPadKb = hOverflow
                        ? theme.scrollbarThickness : 0;
                    const int32_t headerRowH = statePtr->internal.hasHeaders ? rh : 0;
                    const int     rowsPerPage = std::max(1, (innerH - headerRowH - hPadKb) / rh);
                    // First fully visible row, from the body's scroll-Y.
                    int firstFullVis = 0;
                    {
                        auto& sm = guiStateManager.GetScrollManagement();
                        const auto h = sm.GetScrollOffsetHandle(statePtr->internal.bodyID);
                        if (sm.IsScrollOffsetHandleValid(h))
                            firstFullVis = std::max(0, -sm.GetScrollOffset(h).y / rh);
                    }
                    const int lastFullVis = std::min(totalRows - 1,
                                                     firstFullVis + rowsPerPage - 1);

                    // Up/Down/Page/Home/End move in VISUAL (sorted) order; idx is the
                    // dataset index, visIdx its visual position.
                    const int visIdx = (idx >= 0) ? statePtr->DatasetToVisual(idx) : -1;
                    int nextVis = visIdx;
                    int next = idx;
                    int nextCol = statePtr->selectedColumn;
                    switch (event.state.lastKey)
                    {
                        case interstice::KeyCode::Down:
                            nextVis = (visIdx < 0) ? 0 : std::min(visIdx + 1, totalRows - 1);
                            break;
                        case interstice::KeyCode::Up:
                            nextVis = (visIdx < 0) ? totalRows - 1 : std::max(visIdx - 1, 0);
                            break;
                        case interstice::KeyCode::Home:
                            if (cellMode) nextCol = 0;
                            else          nextVis = 0;
                            break;
                        case interstice::KeyCode::End:
                            if (cellMode) nextCol = std::max(0, colCount - 1);
                            else          nextVis = totalRows - 1;
                            break;
                        case interstice::KeyCode::PageUp:
                            if (visIdx < 0) nextVis = firstFullVis;
                            else if (visIdx > firstFullVis) nextVis = firstFullVis;
                            else nextVis = std::max(0, visIdx - (rowsPerPage - 1));
                            break;
                        case interstice::KeyCode::PageDown:
                            if (visIdx < 0) nextVis = lastFullVis;
                            else if (visIdx < lastFullVis) nextVis = lastFullVis;
                            else nextVis = std::min(totalRows - 1, visIdx + (rowsPerPage - 1));
                            break;
                        case interstice::KeyCode::Space:
                        case interstice::KeyCode::Return:
                            if (idx >= 0)
                            {
                                const auto* info = statePtr->FindTreeRow(statePtr->selectedRowID);
                                if (info && info->isOpen)
                                    { *info->isOpen = !*info->isOpen; guiStateManager.Interaction().SuppressHover(); return true; }
                            }
                            return false;
                        case interstice::KeyCode::F2:
                            if (idx >= 0)
                            {
                                const auto* info = statePtr->FindTreeRow(statePtr->selectedRowID);
                                if (info && info->renameBuf)
                                {
                                    (*m_renameSub).Start(statePtr->selectedRowID,
                                                        info->renameBuf,
                                                        info->renameSeed);
                                    guiStateManager.Interaction().SuppressHover();
                                    return true;
                                }
                            }
                            return false;
                        case interstice::KeyCode::Left:
                            if (cellMode) { nextCol = std::max(0, nextCol - 1); break; }
                            if (idx >= 0)
                            {
                                const auto* info = statePtr->FindTreeRow(statePtr->selectedRowID);
                                const bool isOpenBranch = (info && info->isOpen && *info->isOpen);
                                if (isOpenBranch) { *info->isOpen = false; guiStateManager.Interaction().SuppressHover(); return true; }
                                if (info && info->depth > 0)
                                {
                                    // Walk back to the parent (nearest shallower row);
                                    // limited to the emitted window.
                                    const int myDepth = info->depth;
                                    for (int i = idx - 1; i >= 0; --i)
                                    {
                                        const interstice::ID prevID = idManager.GetSubID(
                                            statePtr->internal.rowsRangeID, static_cast<uint32_t>(i));
                                        const auto* sib = statePtr->FindTreeRow(prevID);
                                        if (!sib) break;  // outside emit window
                                        if (sib->depth < myDepth) { next = i; break; }
                                    }
                                }
                            }
                            break;
                        case interstice::KeyCode::Right:
                            if (cellMode) { nextCol = std::min(std::max(0, colCount - 1), nextCol + 1); break; }
                            if (idx >= 0)
                            {
                                const auto* info = statePtr->FindTreeRow(statePtr->selectedRowID);
                                if (info && info->isOpen)
                                {
                                    if (!*info->isOpen) { *info->isOpen = true; guiStateManager.Interaction().SuppressHover(); return true; }
                                    if (idx + 1 < totalRows)
                                    {
                                        const interstice::ID childID = idManager.GetSubID(
                                            statePtr->internal.rowsRangeID, static_cast<uint32_t>(idx + 1));
                                        const auto* child = statePtr->FindTreeRow(childID);
                                        if (child && child->depth == info->depth + 1)
                                            { next = idx + 1; break; }
                                    }
                                }
                            }
                            return false;
                        default: return false;
                    }

                    // Visual keys set nextVis; tree keys (Left/Right) set `next` directly.
                    if (nextVis != visIdx && nextVis >= 0 && nextVis < totalRows)
                        next = statePtr->VisualToDataset(nextVis);

                    bool changed = false;
                    interstice::ID newID = interstice::ID::Invalid();
                    if (next != idx && next >= 0 && next < totalRows)
                    {
                        newID = idManager.GetSubID(statePtr->internal.rowsRangeID,
                                                   static_cast<uint32_t>(next));
                    }
                    if (newID != interstice::ID::Invalid() && newID != statePtr->selectedRowID)
                    {
                        statePtr->selectedRowID = newID;
                        if (statePtr->multiSelect)
                        {
                            auto& ms = *statePtr->multiSelect;
                            ms.selectedIDs.clear();
                            ms.selectedIDs.insert(newID);
                            ms.lastClickedID = newID;
                        }
                        changed = true;
                    }
                    if (cellMode && nextCol != statePtr->selectedColumn)
                    {
                        statePtr->selectedColumn = nextCol;
                        changed = true;
                    }
                    if (changed)
                    {
                        guiStateManager.Interaction().SuppressHover();
                        interstice::ElementRef rowRef = elementStorage.FindElementRefByID(statePtr->selectedRowID);
                        auto& scrollManagement = guiStateManager.GetScrollManagement();

                        // Scroll the row clear of the sticky header (top) and the
                        // H-scrollbar overlay (bottom, when shown).
                        const int32_t headerH = statePtr->internal.hasHeaders
                            ? std::max<int32_t>(1, statePtr->internal.actualRowHeight)
                            : 0;
                        const int32_t hPad    = hOverflow
                            ? theme.scrollbarThickness : 0;
                        if (rowRef != interstice::ElementRef::Invalid())
                        {
                            // Inflate the target by the header (top) and scrollbar (bottom).
                            const auto& rrect = elementStorage.GetElementRectUnsafe(rowRef).rect;
                            interstice::IntRect target = rrect;
                            target.y -= headerH;
                            target.h += headerH + hPad;
                            scrollManagement.MakeRectVisibleHierarchically(
                                elementStorage, target, rowRef);
                        }
                        else if (statePtr->internal.rowsRangeID != interstice::ID::Invalid()
                                 && statePtr->internal.actualRowHeight > 0
                                 && statePtr->internal.bodyID != interstice::ID::Invalid())
                        {
                            // Row not emitted: scroll by its VISUAL position (sorting
                            // makes dataset index differ from scroll row).
                            const int datasetIdx = (newID.GetMode() == interstice::IDMode::SubRange)
                                ? static_cast<int>(newID.value.subRangeParts.index) : 0;
                            const int rowIdx = statePtr->DatasetToVisual(datasetIdx);
                            if (rowIdx < 0) return true;  // not in dataset; nothing to scroll to
                            // Target row content-Y (includes the sticky header).
                            const int32_t rowTopContent = headerH + rowIdx * rh;
                            const int32_t rowBotContent = rowTopContent + rh;
                            // Apply to every section body (frozen tables stay in sync) and
                            // to internal.bodyID, VirtualTable's canonical scroll-Y, so it takes
                            // effect this frame. Clamp against real layout rects: arithmetic
                            // alone misses accumulated row gaps.
                            int32_t writtenY = 0;
                            bool anyChange = false;
                            const int n = statePtr->internal.sectionCount;
                            for (int i = 0; i < n; i++)
                            {
                                const interstice::ID sid = statePtr->SectionBodyID(i);
                                if (sid == interstice::ID::Invalid()) continue;
                                auto sref = elementStorage.FindElementRefByID(sid);
                                if (!sref.IsValid()) continue;
                                const auto& srect = elementStorage.GetElementRectUnsafe(sref);

                                auto h = scrollManagement.GetOrCreateScrollOffsetHandle(sid);
                                const int2 cur = scrollManagement.GetScrollOffset(h);
                                int32_t newY = cur.y;
                                const int32_t visibleTop = -cur.y + headerH;
                                const int32_t visibleBot = -cur.y + innerH - hPad;
                                if (rowTopContent < visibleTop)
                                    newY = -(rowTopContent - headerH);
                                else if (rowBotContent > visibleBot)
                                {
                                    // Overshoot; the clamp below lands on max scroll.
                                    newY = std::numeric_limits<int32_t>::min() / 2;
                                }
                                const int2 clamped = LayoutHelpers::ClampScrollOffsetToContent(
                                    int2{ cur.x, newY }, srect);
                                if (clamped != cur)
                                {
                                    scrollManagement.SetScrollOffset(h, clamped);
                                    anyChange = true;
                                }
                                writtenY = clamped.y;
                            }
                            if (anyChange && statePtr->internal.bodyID != interstice::ID::Invalid())
                            {
                                auto bh = scrollManagement.GetOrCreateScrollOffsetHandle(statePtr->internal.bodyID);
                                const int2 bcur = scrollManagement.GetScrollOffset(bh);
                                if (bcur.y != writtenY)
                                    scrollManagement.SetScrollOffset(bh, int2{ 0, writtenY });
                            }
                        }
                    }
                    return changed;
                }

                default: break;
            }
            return false;
        };
    }


    // One row-backdrop handler shared by every row; the row ID is the event's
    // element ID. With state->rowDragDrop set it also handles drag-and-drop with
    // Before/Inside/After zones (same contract as MakeTreeRowDragDropHandler).
    interstice::EventHandler GUIApplication::BuildRowBackdropHandler(TableState* state,
                                                              interstice::ID containerID)
    {
        return [this, state, containerID](ElementEvent&& event) -> bool
        {
            const interstice::ID rowID = event.id;
            ItemDragDropConfig* dd = state->rowDragDrop;
            const bool dragDropEnabled = (dd && dd->payloadType != 0);

            // With drag-and-drop, selection is deferred to MouseUp
            // (TableState::internal.pendingRowClick) and dropped if a drag starts.

            // Selection shared by the immediate (MouseDown) and deferred (MouseUp) paths.
            auto applyClickSelection = [&](interstice::ID id, interstice::KeyModifier mods) {
                const bool ctrlSel  = (mods & interstice::KeyModifier::Control) != interstice::KeyModifier::None;
                const bool shiftSel = (mods & interstice::KeyModifier::Shift)   != interstice::KeyModifier::None;
                if (state->multiSelect)
                {
                    auto& ms = *state->multiSelect;
                    if (shiftSel && ms.lastClickedID != interstice::ID::Invalid())
                    {
                        const bool anchorRanged  = ms.lastClickedID.GetMode() == interstice::IDMode::SubRange;
                        const bool currentRanged = id.GetMode() == interstice::IDMode::SubRange;
                        if (anchorRanged && currentRanged
                            && state->internal.rowsRangeID != interstice::ID::Invalid())
                        {
                            const int anchorDs = static_cast<int>(ms.lastClickedID.value.subRangeParts.index);
                            const int currDs   = static_cast<int>(id.value.subRangeParts.index);
                            const int anchorVis = state->DatasetToVisual(anchorDs);
                            const int currVis   = state->DatasetToVisual(currDs);
                            if (anchorVis >= 0 && currVis >= 0)
                            {
                                const int firstVis = std::min(anchorVis, currVis);
                                const int lastVis  = std::max(anchorVis, currVis);
                                if (!ctrlSel) ms.selectedIDs.clear();
                                for (int v = firstVis; v <= lastVis; ++v)
                                {
                                    const int ds = state->VisualToDataset(v);
                                    ms.selectedIDs.insert(idManager.GetSubID(
                                        state->internal.rowsRangeID, static_cast<uint32_t>(ds)));
                                }
                            }
                        }
                    }
                    else if (ctrlSel)
                    {
                        if (ms.selectedIDs.count(id)) ms.selectedIDs.erase(id);
                        else                          ms.selectedIDs.insert(id);
                        ms.lastClickedID = id;
                    }
                    else
                    {
                        ms.selectedIDs.clear();
                        ms.selectedIDs.insert(id);
                        ms.lastClickedID = id;
                    }
                    state->selectedRowID = id;
                }
                else
                {
                    state->selectedRowID = id;
                }
            };

            // ---- MouseUp: resolve a deferred click selection, if any ----
            if (event.type == interstice::EventType::MouseUp && event.state.LeftReleased())
            {
                if (dragDropEnabled
                    && state->internal.pendingRowClick.rowID == rowID
                    && !state->internal.pendingRowClick.dragPromoted)
                {
                    applyClickSelection(rowID, state->internal.pendingRowClick.mods);
                }
                // Always clear on any LeftReleased — the click is over.
                state->internal.pendingRowClick = {};
                return false;
            }

            // ---- Drag-and-drop event branches (only when enabled) ----
            // MouseDown (selection + drag prime) is handled below.
            if (dragDropEnabled && event.type != interstice::EventType::MouseDown)
            {
                auto computeZone = [&](const interstice::IntRect& rect) -> TreeDropPosition
                {
                    if (dd->dropInsideOnly) return TreeDropPosition::InsideLast;
                    // rect is in layout space — convert cursor through any
                    // ancestor contentTransform first.
                    const int32_t cursorY = event.LocalCursor(elementStorage).y;
                    int relY = cursorY - rect.position.y;
                    int h    = rect.size.y;
                    if (h <= 0) return TreeDropPosition::None;
                    const auto* info = state->FindTreeRow(rowID);
                    const bool branch = info && info->hasChildren;
                    if (branch)
                    {
                        // Branch zones (thirds):
                        //   top    → Before
                        //   middle → InsideLast (append child)
                        //   bottom → Inside (prepend child) when open, After when closed
                        const bool openBranch = info->isOpen && *info->isOpen;
                        if (relY <  h / 3)      return TreeDropPosition::Before;
                        if (relY < (2 * h) / 3) return TreeDropPosition::InsideLast;
                        return openBranch ? TreeDropPosition::Inside
                                          : TreeDropPosition::After;
                    }
                    if (relY < h / 2)      return TreeDropPosition::Before;
                    return TreeDropPosition::After;
                };

                // Auto-scroll near the body's top/bottom edge during a drag. Edge zones
                // start below the sticky header and above the H-scrollbar.
                auto autoScrollY = [&]() {
                    if (state->internal.sectionCount <= 0) return;
                    const interstice::ID body0 = state->SectionBodyID(0);
                    if (body0 == interstice::ID::Invalid()) return;
                    auto bodyRef = elementStorage.FindElementRefByID(body0);
                    if (!bodyRef.IsValid()) return;
                    const auto& bodyRect = elementStorage.GetElementRectUnsafe(bodyRef);
                    const int kEdgeZoneHeight     = styleManager.Px(30);
                    const int kAutoScrollPerEvent = styleManager.Px(12);
                    const int32_t headerH = state->internal.hasHeaders
                        ? std::max<int32_t>(0, state->internal.actualRowHeight) : 0;
                    const int32_t hPad    = state->internal.anyHorzOverflow
                        ? theme.scrollbarThickness : 0;
                    // bodyRect.innerRect is layout-space — convert cursor too.
                    const int32_t mouseY    = event.LocalCursor(elementStorage).y;
                    const int32_t innerTop  = bodyRect.innerRect.y + headerH;
                    const int32_t innerBot  = bodyRect.innerRect.y + bodyRect.innerRect.h - hPad;
                    int32_t scrollDelta = 0;
                    if (mouseY < innerTop + kEdgeZoneHeight)
                        scrollDelta = +kAutoScrollPerEvent;   // content moves down (offset toward 0)
                    else if (mouseY > innerBot - kEdgeZoneHeight)
                        scrollDelta = -kAutoScrollPerEvent;
                    if (scrollDelta == 0) return;

                    auto& sm = guiStateManager.GetScrollManagement();
                    const int n = state->internal.sectionCount;
                    for (int i = 0; i < n; i++)
                    {
                        const interstice::ID sid = state->SectionBodyID(i);
                        if (sid == interstice::ID::Invalid()) continue;
                        auto sref = elementStorage.FindElementRefByID(sid);
                        if (!sref.IsValid()) continue;
                        const auto& srect = elementStorage.GetElementRectUnsafe(sref);
                        auto h = sm.GetOrCreateScrollOffsetHandle(sid);
                        const int2 cur = sm.GetScrollOffset(h);
                        const int2 requested { cur.x, cur.y + scrollDelta };
                        const int2 clamped = LayoutHelpers::ClampScrollOffsetToContent(requested, srect);
                        if (clamped != cur) sm.SetScrollOffset(h, clamped);
                    }
                };

                switch (event.type)
                {
                    case interstice::EventType::MouseMove:
                    {
                        if ((event.state.buttons & interstice::MouseButtons::Left) == interstice::MouseButtons::None) break;
                        guiStateManager.Drag().UpdateFromInput(event.state);
                        // A real drag cancels the deferred click selection.
                        if (guiStateManager.Drag().GetDraggedID() == rowID)
                            state->internal.pendingRowClick.dragPromoted = true;
                        return guiStateManager.Drag().GetDraggedID() == rowID;
                    }
                    case interstice::EventType::DragEnter:
                    case interstice::EventType::DragOver:
                    {
                        if (event.state.dragPayloadType != dd->payloadType) return false;
                        if (!event.rect) return true;
                        autoScrollY();
                        // No indicator on the row being dragged itself, nor on
                        // rows that refuse this payload (canDrop).
                        if (event.state.dragSourceID == rowID
                            || (dd->canDrop && !dd->canDrop(event.state.dragPayloadValue, rowID)))
                        {
                            if (m_treeDropState.targetRowID == rowID) m_treeDropState = {};
                            return true;
                        }
                        const auto* info = state->FindTreeRow(rowID);
                        m_treeDropState.targetRowID  = rowID;
                        m_treeDropState.position     = computeZone(event.rect->rect);
                        m_treeDropState.targetDepth  = info ? info->depth : 0;
                        return true;
                    }
                    case interstice::EventType::DragOut:
                    {
                        if (event.state.dragPayloadType != dd->payloadType) return false;
                        if (m_treeDropState.targetRowID == rowID) m_treeDropState = {};
                        return true;
                    }
                    case interstice::EventType::Drop:
                    {
                        if (event.state.dragPayloadType != dd->payloadType) return false;
                        if (event.state.dragSourceID == rowID) { m_treeDropState = {}; return true; }
                        if (dd->canDrop && !dd->canDrop(event.state.dragPayloadValue, rowID))
                        {
                            m_treeDropState = {};
                            return true;
                        }
                        TreeDropPosition pos = event.rect
                            ? computeZone(event.rect->rect)
                            : TreeDropPosition::None;
                        if (pos != TreeDropPosition::None && dd->onReorder)
                            dd->onReorder(event.state.dragPayloadValue, rowID, pos);
                        m_treeDropState = {};
                        return true;
                    }
                    default: break;
                }
                return false;
            }

            if (event.type != interstice::EventType::MouseDown) return false;
            if (!event.state.LeftPressed()) return false;

            // One tracker for double-click (<300ms: toggle branch; in row mode the tree
            // cell shares rowID, so it lands here) and slow re-click (500-3000ms: rename).
            const auto clickInfo = state->internal.rowClickTracker.Update(rowID);
            const bool  sameDbl = clickInfo.sameAsPrevious;
            const float dblMs   = clickInfo.elapsedMs;
            const bool wasSelected = (state->selectedRowID == rowID)
                || (state->multiSelect && state->multiSelect->selectedIDs.count(rowID) > 0);

            const bool ctrl  = (event.state.modifiers & interstice::KeyModifier::Control) != interstice::KeyModifier::None;
            const bool shift = (event.state.modifiers & interstice::KeyModifier::Shift)   != interstice::KeyModifier::None;
            const bool ctrlShift = ctrl && shift;

            const auto* info = state->FindTreeRow(rowID);
            const bool hasRename = (info && info->renameBuf);

            // Ctrl+Shift+Click — start rename. Don't touch focus here:
            // next frame's TreeItemRenameCheck moves focus to the editor.
            if (hasRename && ctrlShift)
            {
                (*m_renameSub).Start(rowID, info->renameBuf, info->renameSeed);
                state->internal.rowClickTracker.Reset();
                return true;
            }

            // Double-click → caller's row handler (if set) else toggle tree branch.
            if (sameDbl && dblMs < 300.0f)
            {
                if (state->onRowDoubleClick)
                {
                    state->onRowDoubleClick(rowID);
                    state->internal.rowClickTracker.Reset();
                    if (!(*m_renameSub).firstFrame) (*m_renameSub).Cancel();
                    return true;
                }
                if (info && info->isOpen)
                {
                    *info->isOpen = !*info->isOpen;
                    state->internal.rowClickTracker.Reset();
                    if (!(*m_renameSub).firstFrame) (*m_renameSub).Cancel();
                    return true;
                }
            }

            // Slow re-click (500-3000ms) on a selected renameable row → rename.
            // Off with drag-and-drop (it would fire before the drag threshold);
            // F2 and Ctrl+Shift+Click still work.
            if (hasRename && wasSelected && sameDbl
                && dblMs >= 500.0f && dblMs < 3000.0f
                && !dragDropEnabled)
            {
                (*m_renameSub).Start(rowID, info->renameBuf, info->renameSeed);
                state->internal.rowClickTracker.Reset();
                return true;
            }

            // Plain click cancels rename (not on its activation frame).
            if (!(*m_renameSub).firstFrame) (*m_renameSub).Cancel();

            guiStateManager.Interaction().SetFocusID(containerID);
            guiStateManager.TextEditorFocus().Clear();
            guiStateManager.Interaction().SetClicked(rowID);

            if (dragDropEnabled)
            {
                // Defer selection to MouseUp so a drag doesn't select the source row.
                state->internal.pendingRowClick.rowID        = rowID;
                state->internal.pendingRowClick.mods         = event.state.modifiers;
                state->internal.pendingRowClick.dragPromoted = false;

                // Prime a drag (inlined: Chain stops at our `return true`).
                uintptr_t payloadValue = dd->getPayload ? dd->getPayload(rowID) : 0;
                guiStateManager.Drag().Begin(rowID, dd->payloadType, payloadValue,
                                             event.state.screenPos, std::any(),
                                             dd->visualizer);
            }
            else
            {
                // No drag-and-drop → select immediately on MouseDown.
                applyClickSelection(rowID, event.state.modifiers);
            }
            return true;
        };
    }
}
