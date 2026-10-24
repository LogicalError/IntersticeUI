// VirtualTable: the geometry pipeline. Builds the header section, body
// sections, scrollbar overlays, and column sizers for one frame's worth of
// the table widget. The body-row loop calls each column's user-supplied
// generateCell, which routes through Cell()/TreeCell() (in
// controls_table_cells.cpp) to emit cells directly.
//
// Event-handler builders live in controls_table_handlers.cpp.

#include "controls_table_internal.h"

#include <gui_core/layout/gui_layout_helpers.h>

#include <algorithm>
#include <assert.h>
#include <chrono>
#include <limits>

namespace interstice
{
    using namespace interstice::TableInternal;

    void GUIApplication::EmitTableHeaderCell(TableEmit& emit, interstice::ID tableID, int c, int16_t gridCol,
                                             bool isStretchTrack, int sectionMin, int sectionMax,
                                             int effectiveSortCol, IconID sortIcon)
    {
        TableState*               state    = emit.userState;
        VirtualTableColumn*       columns  = emit.columns;
        const size_t              colCount = emit.columnCount;
        TableState*               statePtr = state;

        const int colIdx = c;
        // Which section this column is in, resolved now while `columns` is alive.
        int sectionIdx = 0;
        for (int i = 1; i <= c; i++)
            if (columns[i].sectionId != columns[i - 1].sectionId && sectionIdx + 1 < TableState::kMaxTableSections)
                sectionIdx++;
        // Providing sortAscending implies sortable.
        const bool isSortable    = columns[c].column.sortable
                                || static_cast<bool>(columns[c].sortAscending);
        const bool isLocked      = columns[c].column.locked;
        const bool isReorderable = static_cast<bool>(state->onReorderColumns) && !isLocked;
        const bool isInteractive = isSortable || isReorderable;
        const StyleRef headerStyle = isInteractive ? theme.tableHeaderStyleRef
                                                   : theme.tableHeaderLockedStyleRef;

        // Stable per-column header ID from VirtualTable's prologue: emit order changes
        // during a drag (dragged column last), which would shift a dynamic ID and lose capture.
        const interstice::ID     cellID    = emit.columns[c].internal.headerCellID != interstice::ID::Invalid()
            ? emit.columns[c].internal.headerCellID
            : idManager.AllocateDynamicID();
        interstice::ElementState cellState = guiStateManager.GetCurrentState(cellID);
        interstice::Element cell{};
        cell.id       = cellID;
        cell.state    = cellState;
        cell.styleRef = headerStyle;

        const bool isSortCol = (c == effectiveSortCol);
        if (isSortCol)
            cell.iconID = sortIcon;

        if (!columns[c].column.header.empty())
        {
            cell.textID = textSystem->RegisterText(cellID,
                                                   isSortCol ? sortIcon : IconID::Invalid,
                                                   columns[c].column.header, headerStyle, cellState);
            const int32_t headerW = textSystem->GetTextMeasurement(cell.textID).maxWidth;
            columns[c].internal.maxContentWidth = std::max(columns[c].internal.maxContentWidth, headerW);
        }

        // Drag bounds: clamped to the section, then further constricted by
        // any locked column inside it. Drags never cross sections.
        int minTarget = sectionMin;
        int maxTarget = sectionMax;
        if (isReorderable)
        {
            for (int i = colIdx - 1; i >= sectionMin; --i)
                if (columns[i].column.locked) { minTarget = i + 1; break; }
            for (int i = colIdx + 1; i <= sectionMax; ++i)
                if (columns[i].column.locked) { maxTarget = i - 1; break; }
        }

        if (isReorderable)
        {
            cell.eventHandler = [this, statePtr, tableID, colIdx, isSortable, minTarget, maxTarget,
                                 sectionIdx, colCount]
                (ElementEvent&& event) -> bool
            {
                auto& drag = m_columnDrag;
                switch (event.type)
                {
                    case interstice::EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed()) return false;
                        drag = {};
                        drag.tableID   = tableID;
                        drag.fromCol   = colIdx;
                        drag.toCol     = colIdx;
                        drag.minTarget = minTarget;
                        drag.maxTarget = maxTarget;
                        drag.startPos  = event.state.screenPos;
                        drag.headerRects.reserve(colCount);
                        for (size_t i = 0; i < colCount; i++)
                        {
                            interstice::IntRect r{};
                            if (i < statePtr->internal.headerRefs.size())
                                if (auto* rp = elementStorage.GetElementRectPtr(statePtr->internal.headerRefs[i]))
                                    r = rp->rect;
                            drag.headerRects.push_back(r);
                        }
                        // Snapshot scroll-X so MouseMove can compensate for scrolling mid-drag.
                        drag.sectionBodyID = statePtr->SectionBodyID(sectionIdx);
                        if (drag.sectionBodyID != interstice::ID::Invalid())
                        {
                            auto& sm = guiStateManager.GetScrollManagement();
                            const auto h = sm.GetScrollOffsetHandle(drag.sectionBodyID);
                            if (sm.IsScrollOffsetHandleValid(h))
                                drag.startScrollX = sm.GetScrollOffset(h).x;
                        }
                        guiStateManager.Interaction().SetCaptureID(event.id);
                        return true;
                    }

                    case interstice::EventType::MouseMove:
                    {
                        if (drag.tableID != tableID || drag.fromCol != colIdx) break;
                        if (!event.state.LeftDown()) break;
                        interstice::int2 delta = event.state.screenPos - drag.startPos;
                        drag.offset = delta;
                        if (std::abs(delta.x) > styleManager.Px(kDragStartDistance)) drag.active = true;
                        if (drag.active && !drag.headerRects.empty())
                        {
                            // Auto-scroll near the body's left/right edges, before
                            // resolving the drop target so it uses the new scroll.
                            int32_t curScrollX = drag.startScrollX;
                            if (drag.sectionBodyID != interstice::ID::Invalid())
                            {
                                auto bodyRef = elementStorage.FindElementRefByID(drag.sectionBodyID);
                                if (bodyRef.IsValid())
                                {
                                    const auto& bodyRect = elementStorage.GetElementRectUnsafe(bodyRef);
                                    const int kEdgeZoneWidth      = styleManager.Px(30);
                                    const int kAutoScrollPerEvent = styleManager.Px(12);
                                    // bodyRect.innerRect is in layout space — convert cursor too.
                                    const int32_t mouseX    = event.LocalCursor(elementStorage).x;
                                    const int32_t innerLeft  = bodyRect.innerRect.x;
                                    const int32_t innerRight = bodyRect.innerRect.x + bodyRect.innerRect.w;
                                    int32_t scrollDelta = 0;
                                    if (mouseX < innerLeft + kEdgeZoneWidth)
                                        scrollDelta = +kAutoScrollPerEvent; // scroll content right (offset → 0)
                                    else if (mouseX > innerRight - kEdgeZoneWidth)
                                        scrollDelta = -kAutoScrollPerEvent; // scroll content left

                                    auto& sm = guiStateManager.GetScrollManagement();
                                    const auto h = sm.GetOrCreateScrollOffsetHandle(drag.sectionBodyID);
                                    const int2 cur = sm.GetScrollOffset(h);
                                    if (scrollDelta != 0)
                                    {
                                        const int2 requested { cur.x + scrollDelta, cur.y };
                                        const int2 clamped = LayoutHelpers::ClampScrollOffsetToContent(requested, bodyRect);
                                        if (clamped != cur) sm.SetScrollOffset(h, clamped);
                                        curScrollX = clamped.x;
                                    }
                                    else
                                        curScrollX = cur.x;
                                }
                            }
                            // headerRects are from click time; undo the scroll delta on the cursor.
                            const int32_t adjustedMouseX =
                                event.state.screenPos.x - (curScrollX - drag.startScrollX);
                            int target = TableHelpers::ComputeColumnInsertionIndex(
                                adjustedMouseX, drag.fromCol,
                                std::span<const interstice::IntRect>(drag.headerRects.data(),
                                                              drag.headerRects.size()));
                            drag.toCol = std::clamp(target, drag.minTarget, drag.maxTarget);
                        }
                        return true;
                    }

                    case interstice::EventType::MouseUp:
                    {
                        if (drag.tableID != tableID || drag.fromCol != colIdx) break;
                        const bool  committed = drag.active;
                        const int   from      = drag.fromCol;
                        const int   to        = drag.toCol;
                        // Snapshot the callback before clearing drag state (owned by TableState).
                        TableState::ReorderColumnsFn* onReorder = statePtr->onReorderColumns.get();
                        drag = {};
                        guiStateManager.Interaction().ReleaseCaptureID();
                        if (committed)
                        {
                            if (from != to && onReorder && *onReorder)
                            {
                                (*onReorder)(from, to);
                                // Rotate the column-indexed state the same way so it follows its column.
                                auto applyRotate = [from, to](int idx) {
                                    if (idx == from) return to;
                                    if (from < to && idx > from && idx <= to) return idx - 1;
                                    if (from > to && idx >= to && idx < from) return idx + 1;
                                    return idx;
                                };
                                if (statePtr->sortColumn >= 0)
                                    statePtr->sortColumn = applyRotate(statePtr->sortColumn);
                                statePtr->selectedColumn = applyRotate(statePtr->selectedColumn);
                                // Widths belong to columns as well.
                                auto& widths = statePtr->columnWidths;
                                if (from < int(widths.size()) && to < int(widths.size()))
                                {
                                    if (from < to) std::rotate(widths.begin() + from, widths.begin() + from + 1, widths.begin() + to + 1);
                                    else           std::rotate(widths.begin() + to, widths.begin() + from, widths.begin() + from + 1);
                                }
                                guiStateManager.Interaction().SetFocusID(interstice::ID::Invalid());
                                guiStateManager.TextEditorFocus().Clear();
                                transitionManager.Clear();
                            }
                        }
                        else if (isSortable)
                        {
                            if (statePtr->sortColumn == colIdx)
                                statePtr->sortDescending = !statePtr->sortDescending;
                            else
                            {
                                statePtr->sortColumn     = colIdx;
                                statePtr->sortDescending = false;
                            }
                        }
                        return true;
                    }

                    case interstice::EventType::KeyDown:
                    {
                        if (drag.tableID == tableID && drag.fromCol == colIdx
                            && event.state.lastKey == interstice::KeyCode::Escape)
                        {
                            drag = {};
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        }
                        break;
                    }

                    default: break;
                }
                return false;
            };
        }
        else if (isSortable)
        {
            cell.eventHandler = [statePtr, colIdx](ElementEvent&& event) -> bool
            {
                if (event.type == interstice::EventType::MouseDown && event.state.LeftPressed())
                {
                    if (statePtr->sortColumn == colIdx)
                        statePtr->sortDescending = !statePtr->sortDescending;
                    else
                    {
                        statePtr->sortColumn     = colIdx;
                        statePtr->sortDescending = false;
                    }
                    return true;
                }
                return false;
            };
        }

        const GridTrack track = ComputeColumnTrack(styleManager,
            c, int(colCount), columns, state->columnWidths,
            emit.dragActive, emit.lastColDragFixedWidth,
            isStretchTrack);

        // Row 0 of the section's body grid, pinned by StickyY during vertical scroll.
        interstice::LayoutOverride lo;
        lo = ApplyColumnDragOffsetX(
                 std::move(lo).GridColumn(gridCol).GridRow(0).ColumnTrack(track).StickyY(), emit, c);
        auto hRes = elementStorage.Add(std::move(cell), std::move(lo));
        VERIFY(hRes.status);
        VERIFY(windowRegistry.Add(hRes.ref));
        columns[c].internal.headerRef = hRes.ref;
        if (state->internal.headerRefs.size() != colCount) state->internal.headerRefs.resize(colCount);
        state->internal.headerRefs[size_t(c)] = hRes.ref;
    }


    void GUIApplication::EmitTableHeaderSection(TableEmit& emit, interstice::ID tableID,
                                                int colStart, int colEnd, bool lastColIsStretch,
                                                int effectiveSortCol, IconID sortIcon)
    {
        // Header cells emit directly into the caller's open children scope
        // (the section's body grid). They occupy row 0 with StickyY so
        // vertical scroll leaves them pinned to the grid's viewport top.
        // The reorder-dragged column is left to EmitDraggedColumnOnTop.
        for (int c = colStart; c < colEnd; ++c)
        {
            if (c == emit.draggedCol) continue;
            const bool isLastInSection = (c + 1 == colEnd);
            const bool explicitStretch = emit.columns[c].column.sizing == ColumnSizing::Stretch;
            const bool implicitStretch = lastColIsStretch && isLastInSection
                                      && emit.columns[c].column.sizing != ColumnSizing::Fixed;
            EmitTableHeaderCell(emit, tableID, c, static_cast<int16_t>(c - colStart),
                                explicitStretch || implicitStretch,
                                colStart, colEnd - 1, effectiveSortCol, sortIcon);
        }
    }


    GridTrack GUIApplication::ComputeSectionColumnTrack(const TableEmit& emit, int c, int colEnd,
                                                        bool isMiddle, bool fitContent) const
    {
        const bool isLastInSection = (c + 1 == colEnd);
        const bool explicitStretch = emit.columns[c].column.sizing == ColumnSizing::Stretch;
        const bool implicitStretch = isMiddle && isLastInSection
                                  && emit.columns[c].column.sizing != ColumnSizing::Fixed;
        const bool stretch         = !fitContent && (explicitStretch || implicitStretch);
        return ComputeColumnTrack(styleManager, c, int(emit.columnCount),
                                  emit.columns, emit.userState->columnWidths,
                                  emit.dragActive,
                                  emit.lastColDragFixedWidth,
                                  stretch);
    }


    // ---- Direct-emit primitives ----

    void GUIApplication::EmitDirectRowBackdrop(interstice::ID rowID, int16_t gridRow,
                                               int colStart, int colEnd,
                                               interstice::ElementState backdropState,
                                               interstice::ElementFlags backdropFlags,
                                               interstice::StyleRef rowStyle,
                                               interstice::EventHandler backdropHandler,
                                               TableState* tableState,
                                               bool isLastSection)
    {
        interstice::Element backdrop{};
        backdrop.id           = rowID;
        backdrop.flags        = backdropFlags;
        backdrop.state        = backdropState;
        backdrop.styleRef     = rowStyle;
        backdrop.eventHandler = std::move(backdropHandler);
        auto bRes = elementStorage.Add(std::move(backdrop),
            interstice::LayoutOverride{}
                .GridColumn(0, static_cast<int16_t>(colEnd - colStart))
                .GridRow(gridRow));
        VERIFY(bRes.status);
        VERIFY(windowRegistry.Add(bRes.ref));

        // Tree-reorder drop indicator on the row backdrop (spans the whole section):
        // a line at Before / After, or Inside (indented). The line starts at the
        // target's depth indent, added explicitly since the backdrop isn't indented.
        [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(bRes.ref,
            [this, rowID, tableState, isLastSection,
             lineColor = theme.treeDropIndicatorStyle]
            (interstice::CustomDrawContext& ctx)
            {
                // A cancelled drag fires neither Drop nor DragOut, so m_treeDropState
                // can be stale; gate on the live drag.
                if (!guiStateManager.Drag().IsDragging()) return;
                if (m_treeDropState.targetRowID != rowID
                    || m_treeDropState.position == TreeDropPosition::None) return;

                // Span the outer rect so insets match the row's visual edges.
                const auto outer = ctx.ElementRect();
                const auto inner = ctx.ElementInnerRect();
                const int32_t w    = outer.w;
                const int32_t h    = outer.h;
                const int32_t topY = outer.y - inner.y;
                const int32_t leftX = outer.x - inner.x;
                const int32_t kIndent = theme.treeTableIndent;
                const int32_t kThick  = styleManager.Px(3);
                const int32_t half    = kThick / 2;   // insets that keep the line inside the row
                // Same x as the target's chevron / leaf-spacer column, so the
                // line aligns with the row's existing visual structure.
                const int32_t depthX = leftX + m_treeDropState.targetDepth * kIndent;

                // On the rightmost section, always stop short of the V-scrollbar
                // overlay (+2 px gap), whether or not the bar is currently shown.
                (void)tableState;
                const int32_t scrollbarPad = isLastSection
                    ? theme.scrollbarThickness + styleManager.Px(2) : 0;
                const int32_t rightEdge = leftX + w - scrollbarPad;

                interstice::LineStyle indStyle { .colorStyleIndex = lineColor, .lineWidth = float(kThick) };

                // InsideLast → rectangle around the row (drop as last child).
                if (m_treeDropState.position == TreeDropPosition::InsideLast)
                {
                    const int32_t leftL  = depthX;
                    const int32_t rightL = rightEdge - 1;
                    const int32_t top    = topY + half;          // lineWidth fits inside
                    const int32_t bot    = topY + h - 1 - half;  // clears row-gap + opaque
                    ctx.RenderLine({ leftL,  top }, { rightL, top }, indStyle); // top
                    ctx.RenderLine({ leftL,  bot }, { rightL, bot }, indStyle); // bottom
                    ctx.RenderLine({ leftL,  top }, { leftL,  bot }, indStyle); // left
                    ctx.RenderLine({ rightL, top }, { rightL, bot }, indStyle); // right
                    return;
                }

                // Keep lines inside the row: rows are opaque and the grid has a row gap,
                // so a line at y=h would be hidden by the next row.
                int32_t lineLeft;
                int32_t y;
                switch (m_treeDropState.position)
                {
                    case TreeDropPosition::Before:
                        lineLeft = depthX;
                        y        = topY + half;       // just inside row top
                        break;
                    case TreeDropPosition::After:
                        lineLeft = depthX;
                        y        = topY + h - 1 - half;
                        break;
                    case TreeDropPosition::Inside:
                        lineLeft = depthX + kIndent;
                        y        = topY + h - 1 - half;
                        break;
                    default: return;
                }

                ctx.RenderLine({ lineLeft, y }, { rightEdge, y }, indStyle);
            });
    }


    void GUIApplication::EmitDirectRowSpacer(int16_t gridRow,
                                             int colStart, int colEnd, int32_t height)
    {
        if (height <= 0) return;
        interstice::ID spacerID = idManager.AllocateDynamicID("_table_spacer");
        interstice::Element spacer{};
        spacer.id       = spacerID;
        spacer.styleRef = theme.tableCellStyleRef;
        spacer.flags    = interstice::ElementFlags::NoInput;
        spacer.state    = guiStateManager.GetCurrentState(spacerID);
        auto rSpRes = elementStorage.Add(std::move(spacer),
            interstice::LayoutOverride{}
                .GridColumn(0, static_cast<int16_t>(colEnd - colStart))
                .GridRow(gridRow)
                .RowTrack(GridTrack::Fixed(static_cast<float>(height))));
        VERIFY(rSpRes.status);
        VERIFY(windowRegistry.Add(rSpRes.ref));
    }


    void GUIApplication::OpenTableBodySection(TableEmit& emit, interstice::ID sectionID, interstice::StyleRef sectionStyle,
                                              interstice::LayoutOverride&& layout, bool sampleViewport)
    {
        auto bsRes = elementStorage.Add(
            interstice::Element
            {
                .id = sectionID,
                .styleRef = sectionStyle,
                .state = guiStateManager.GetCurrentState(sectionID)
            }, std::move(layout));
        VERIFY(bsRes.status);
        VERIFY(windowRegistry.Add(bsRes.ref));

        // Sample overflow on every section so next frame knows which scrollbars to emit.
        // sampleViewport (rightmost section only) also records the viewport size.
        TableState* statePtr = emit.userState;
        [[maybe_unused]]
        auto cdResult = elementStorage.AddPostLayout(bsRes.ref,
            [this, statePtr, sampleViewport](interstice::PostLayoutContext& plctx)
            {
                const auto innerRect = plctx.ElementInnerRect();
                const int2 contentSize = plctx.GetContentSize(plctx.Ref());
                if (contentSize.x > innerRect.w) statePtr->internal.anyHorzOverflow = true;
                if (contentSize.y > innerRect.h) statePtr->internal.anyVertOverflow = true;
                if (sampleViewport)
                {
                    statePtr->internal.lastViewportH = innerRect.h;
                    statePtr->internal.lastViewportW = innerRect.w;
                    // Stick-to-bottom runs here, where viewport + content height
                    // are final (and after the keyboard/scroll handlers, so it
                    // observes this frame's scroll position).
                    if (statePtr->followTail)
                        UpdateTableFollowTail(*statePtr, innerRect.h, contentSize.y);
                }
            });

        VERIFY(elementStorage.BeginChildren());
    }


    // Snap every section body (and the canonical internal.bodyID) to the bottom. Same
    // trick as the keyboard End handler: aim the scroll way past the end and let
    // ClampScrollOffsetToContent settle on the true max (which accounts for row
    // gaps + the bottom pad). Runs post-layout, so the section rects are final.
    void GUIApplication::ScrollTableSectionsToBottom(TableState& state)
    {
        auto& sm = guiStateManager.GetScrollManagement();
        int32_t writtenY = 0;
        for (int i = 0; i < state.internal.sectionCount; i++)
        {
            const interstice::ID sid = state.SectionBodyID(i);
            if (sid == interstice::ID::Invalid()) continue;
            auto sref = elementStorage.FindElementRefByID(sid);
            if (!sref.IsValid()) continue;
            const auto& srect = elementStorage.GetElementRectUnsafe(sref);
            auto h = sm.GetOrCreateScrollOffsetHandle(sid);
            const interstice::int2 cur = sm.GetScrollOffset(h);
            const interstice::int2 clamped = LayoutHelpers::ClampScrollOffsetToContent(
                interstice::int2{ cur.x, std::numeric_limits<int32_t>::min() / 2 }, srect);
            if (clamped != cur) sm.SetScrollOffset(h, clamped);
            writtenY = clamped.y;
        }
        if (state.internal.bodyID != interstice::ID::Invalid())
        {
            auto bh = sm.GetOrCreateScrollOffsetHandle(state.internal.bodyID);
            if (sm.GetScrollOffset(bh).y != writtenY)
                sm.SetScrollOffset(bh, interstice::int2{ 0, writtenY });
        }
    }

    // followTail decision, run from the body section's post-layout hook (viewport
    // + content height are final there). The view is "at the bottom" when the
    // scroll magnitude is within ~half a row of the max. It pins (scroll + select
    // last) on the very first laid-out frame, and again whenever the row count
    // grows while still pinned; scrolling up clears the pin, scrolling back sets
    // it. Scroll offset Y is ≤ 0 (content shifts up), so -y is the magnitude.
    void GUIApplication::UpdateTableFollowTail(TableState& state, int32_t viewportH, int32_t contentH)
    {
        auto& sm = guiStateManager.GetScrollManagement();
        const int32_t maxScroll = std::max(0, contentH - viewportH);
        int32_t curMag = 0;
        if (state.internal.bodyID != interstice::ID::Invalid())
        {
            auto h = sm.GetScrollOffsetHandle(state.internal.bodyID);
            if (sm.IsScrollOffsetHandleValid(h)) curMag = -sm.GetScrollOffset(h).y;
        }
        const int32_t eps      = std::max(1, state.internal.actualRowHeight / 2);
        const bool    atBottom = curMag >= maxScroll - eps;
        const bool    grew     = state.internal.totalRowCount > state.internal.prevFollowRowCount;

        bool pin = false;
        if (!state.internal.followInit && viewportH > 0 && state.internal.totalRowCount > 0)
        {
            state.internal.followInit = true;        // first laid-out frame → initial scroll-to-bottom
            pin = true;
        }
        else if (state.internal.followInit && state.internal.followPinned && grew)
        {
            pin = true;                      // new rows while still at the bottom → follow
        }

        if (pin)
        {
            ScrollTableSectionsToBottom(state);
            // Select the row shown at the bottom (the last VISUAL row under a sort).
            if (state.internal.rowsRangeID != interstice::ID::Invalid() && state.internal.totalRowCount > 0)
            {
                state.selectedRowID = state.RowIDInRange(state.VisualToDataset(state.internal.totalRowCount - 1));
                if (state.multiSelect)
                {
                    state.multiSelect->selectedIDs.clear();
                    state.multiSelect->selectedIDs.insert(state.selectedRowID);
                    state.multiSelect->lastClickedID = state.selectedRowID;
                }
            }
            state.internal.followPinned = true;
        }
        else if (state.internal.followInit)
        {
            state.internal.followPinned = atBottom;  // user scrolled up clears, back to bottom re-pins
        }
        state.internal.prevFollowRowCount = state.internal.totalRowCount;
    }


    // Emit the dragged column's cells, header and sizer last in the section so it
    // paints over its neighbours. The other emit phases skip it.
    void GUIApplication::EmitDraggedColumnOnTop(TableEmit& emit, interstice::ID tableID,
                                                int colStart, int colEnd,
                                                int firstVisible, int lastVisible,
                                                interstice::ID rowsRangeID,
                                                bool lastColIsStretch,
                                                int effectiveSortCol, IconID sortIcon)
    {
        const int draggedCol = emit.draggedCol;
        if (draggedCol < colStart || draggedCol >= colEnd) return;

        // 1) Body cells for the dragged column, each over a backdrop in the
        // row's colour so neighbours don't show through. Spacers and row
        // backdrops came from EmitVirtualBodyRows.
        const auto& generateCell = emit.columns[draggedCol].generateCell;
        if (generateCell)
        {
            TableState* state = emit.userState;
            const bool cellMode = emit.cellMode;
            emit.directColStart = colStart;
            emit.currentCol     = draggedCol;
            const int headerRowOffset = emit.showHeaders ? 1 : 0;
            const int gridRowFirst    = headerRowOffset + (firstVisible > 0 ? 1 : 0);
            for (int visualIdx = firstVisible; visualIdx < lastVisible; visualIdx++)
            {
                const int datasetIdx = state->VisualToDataset(visualIdx);
                const interstice::ID rowID = idManager.GetSubID(rowsRangeID,
                                                        static_cast<uint32_t>(datasetIdx));
                const bool rowSelected = state->multiSelect
                    ? state->multiSelect->selectedIDs.count(rowID) > 0
                    : (state->selectedRowID == rowID);
                const bool rowModeSelected = !cellMode && rowSelected;
                emit.directGridRow = gridRowFirst + (visualIdx - firstVisible);
                emit.currentRowID  = rowID;

                // Drag-column backdrop (matches the row's alternating
                // colour) so the dragged column isn't transparent over
                // overlapped neighbour content.
                const StyleRef rowStyle = (visualIdx % 2 == 0)
                    ? theme.tableRowStyleRef : theme.tableRowAltStyleRef;
                const interstice::ID bgID = idManager.AllocateDynamicID("_table_drag_col_bg");
                interstice::ElementState bgState = guiStateManager.GetCurrentState(bgID);
                if (!cellMode && rowSelected) bgState |= interstice::ElementState::Selected;
                interstice::Element bg{};
                bg.id       = bgID;
                bg.flags    = interstice::ElementFlags::NoInput;
                bg.state    = bgState;
                bg.styleRef = rowStyle;
                auto bgLo = interstice::LayoutOverride{}
                    .GridColumn(static_cast<int16_t>(draggedCol - colStart))
                    .GridRow(static_cast<int16_t>(emit.directGridRow));
                bgLo = ApplyColumnDragOffsetX(std::move(bgLo), emit, draggedCol);
                auto bgRes = elementStorage.Add(std::move(bg), std::move(bgLo));
                VERIFY(bgRes.status);
                VERIFY(windowRegistry.Add(bgRes.ref));

                if (rowModeSelected) guiStateManager.Construction().PushSelected();
                generateCell(datasetIdx);
                if (rowModeSelected) guiStateManager.Construction().PopSelected();
            }
            emit.currentRowID = interstice::ID::Invalid();
        }

        // 2) Header for the dragged column (only when headers are shown).
        if (emit.showHeaders)
        {
            const bool isLastInSection = (draggedCol + 1 == colEnd);
            const bool explicitStretch = emit.columns[draggedCol].column.sizing == ColumnSizing::Stretch;
            const bool implicitStretch = lastColIsStretch && isLastInSection
                                      && emit.columns[draggedCol].column.sizing != ColumnSizing::Fixed;
            EmitTableHeaderCell(emit, tableID, draggedCol,
                                static_cast<int16_t>(draggedCol - colStart),
                                explicitStretch || implicitStretch,
                                colStart, colEnd - 1, effectiveSortCol, sortIcon);
        }

        // 3) Sizer for the dragged column's right boundary (if any).
        // Sizers ride along with headers — no headers, no sizers.
        if (emit.showHeaders && draggedCol + 1 < colEnd)
        {
            // A 2-column slice emits exactly this one sizer.
            EmitColumnSizers(emit, draggedCol, draggedCol + 2, /*skipDragged*/false);
        }
    }


    void GUIApplication::CloseTableBodySection(TableEmit& emit,
                                               interstice::ID sectionBodyID, bool emitVerticalScrollbar,
                                               bool suppressHorzScrollbar)
    {
        // Column sizers are emitted by the caller, before EmitDraggedColumnOnTop.

        // Per-section H scrollbar; the V scrollbar is on the rightmost section only
        // (Y is mirrored in MirrorTableScroll). Bars are emitted only when their axis
        // overflowed last frame, since a track captures input even when blank.
        TableState* state = emit.userState;
        const bool emitHorz = emit.anyHorzOverflow && !suppressHorzScrollbar;
        const bool emitVert = emit.anyVertOverflow;
        if (emitVerticalScrollbar)
        {
            ScrollbarOverlay(sectionBodyID,
                             int2{ state->internal.lastViewportW, state->internal.lastViewportH },
                             state->internal.vertScrollbarID,
                             state->internal.horzScrollbarID,
                             emitVert,
                             emitHorz);
        }
        else if (emitHorz)
        {
            const interstice::ID hbarID = idManager.AllocateDynamicID("_table_section_hbar");
            HorizontalScrollbar(hbarID, sectionBodyID);
        }

        VERIFY(elementStorage.EndChildren());
    }


    void GUIApplication::EmitVirtualBodyRows(TableEmit& emit,
                                             int colStart, int colEnd,
                                             int totalRowCount, int32_t actualRowHeight,
                                             int firstVisible, int lastVisible,
                                             interstice::ID rowsRangeID,
                                             bool isLastSection)
    {
        if (colStart >= colEnd) return;

        TableState* state                 = emit.userState;
        VirtualTableColumn* const columns = emit.columns;

        // Hot-path locals.
        const bool cellMode = emit.cellMode;
        const StyleRef rowStyleEven = theme.tableRowStyleRef;
        const StyleRef rowStyleOdd  = theme.tableRowAltStyleRef;
        const interstice::EventHandler& sharedHandler = emit.sharedRowHandler;

        // Section grid rows: [header] · [top spacer] · visible rows · [bottom spacer].
        // Cells of one logical row share a GridRow, which keeps column-major emission aligned.
        const int headerRowOffset = emit.showHeaders ? 1 : 0;
        const int gridRowFirst    = headerRowOffset + (firstVisible > 0 ? 1 : 0);
        const int visibleCount = lastVisible - firstVisible;
        auto gridRowOf = [gridRowFirst, firstVisible](int rowIdx) {
            return gridRowFirst + (rowIdx - firstVisible);
        };

        emit.directColStart = colStart;

        // ---- Top spacer ----
        // Between the optional sticky header and the first visible row.
        if (firstVisible > 0)
        {
            EmitDirectRowSpacer(static_cast<int16_t>(headerRowOffset),
                                colStart, colEnd, firstVisible * actualRowHeight);
        }

        // ---- Row backdrops (one per visible row) ----
        // Emitted before cells so cells draw over them. visualIdx drives placement and
        // striping; datasetIdx drives identity, so selection survives sort changes.
        for (int visualIdx = firstVisible; visualIdx < lastVisible; visualIdx++)
        {
            const int datasetIdx = state->VisualToDataset(visualIdx);
            const interstice::ID rowID = idManager.GetSubID(rowsRangeID,
                                                    static_cast<uint32_t>(datasetIdx));
            const bool rowSelected = state->multiSelect
                ? state->multiSelect->selectedIDs.count(rowID) > 0
                : (state->selectedRowID == rowID);
            const StyleRef rowStyle = (visualIdx % 2 == 0) ? rowStyleEven : rowStyleOdd;
            interstice::ElementState backdropState = guiStateManager.GetCurrentState(rowID);
            if (!cellMode && rowSelected) backdropState |= interstice::ElementState::Selected;
            const interstice::ElementFlags backdropFlags =
                cellMode ? interstice::ElementFlags::NoInput : interstice::ElementFlags::None;

            EmitDirectRowBackdrop(rowID, static_cast<int16_t>(gridRowOf(visualIdx)),
                                  colStart, colEnd,
                                  backdropState, backdropFlags, rowStyle, sharedHandler,
                                  state, isLastSection);

        }

        // ---- Column backgrounds (full-height, over backdrops, under cells) ----
        EmitColumnBackgrounds(emit, colStart, colEnd);

        // ---- Cells, column by column ----
        emit.currentRowHandler = sharedHandler;

        // The reorder-dragged column is left to EmitDraggedColumnOnTop, which
        // emits it last so it paints on top of the columns it overlaps.
        for (int c = colStart; c < colEnd && size_t(c) < emit.columnCount; c++)
        {
            if (c == emit.draggedCol) continue;
            const auto& generateCell = columns[c].generateCell;
            if (!generateCell) continue;
            emit.currentCol = c;
            for (int visualIdx = firstVisible; visualIdx < lastVisible; visualIdx++)
            {
                const int datasetIdx = state->VisualToDataset(visualIdx);
                const interstice::ID rowID = idManager.GetSubID(rowsRangeID,
                                                        static_cast<uint32_t>(datasetIdx));
                const bool rowSelected = state->multiSelect
                    ? state->multiSelect->selectedIDs.count(rowID) > 0
                    : (state->selectedRowID == rowID);
                const bool rowModeSelected = !cellMode && rowSelected;

                emit.directGridRow = gridRowOf(visualIdx);
                emit.currentRowID  = rowID;

                if (rowModeSelected) guiStateManager.Construction().PushSelected();
                generateCell(datasetIdx);
                if (rowModeSelected) guiStateManager.Construction().PopSelected();
            }
        }

        // ---- Selected-row bars: over the cells, at the table's left edge ----
        if (!cellMode && colStart == 0)
        {
            for (int visualIdx = firstVisible; visualIdx < lastVisible; visualIdx++)
            {
                const interstice::ID rowID = idManager.GetSubID(rowsRangeID,
                                                        static_cast<uint32_t>(state->VisualToDataset(visualIdx)));
                const bool rowSelected = state->multiSelect
                    ? state->multiSelect->selectedIDs.count(rowID) > 0
                    : (state->selectedRowID == rowID);
                if (!rowSelected) continue;
                interstice::Element bar{};
                bar.id       = idManager.AllocateDynamicID("_table_row_bar");
                bar.flags    = interstice::ElementFlags::NoInput;
                bar.styleRef = theme.tableRowBarStyleRef;
                auto barRes = elementStorage.Add(std::move(bar), interstice::LayoutOverride{}
                    .GridColumn(0).GridRow(static_cast<int16_t>(gridRowOf(visualIdx))));
                VERIFY(barRes.status);
                VERIFY(windowRegistry.Add(barRes.ref));
            }
        }

        emit.currentRowID      = interstice::ID::Invalid();
        emit.currentRowHandler = {};

        // ---- Bottom spacer ----
        // Pad by the scrollbar thickness only when some section overflows X, so the
        // H-scrollbar doesn't cover the last row.
        const int32_t hScrollPad = emit.anyHorzOverflow ? theme.scrollbarThickness : 0;
        const int32_t belowVisibleH =
            (totalRowCount - lastVisible) * actualRowHeight + hScrollPad;
        EmitDirectRowSpacer(static_cast<int16_t>(gridRowFirst + visibleCount),
                            colStart, colEnd, belowVisibleH);
    }


    void GUIApplication::MirrorTableScroll(TableState* state)
    {
        // Y is shared: the section the user scrolled wins and is copied to the rest.
        // X isn't mirrored; headers live in each section's own scroll container.
        const int n = state->internal.sectionCount;
        if (n <= 0) return;

        auto& sm = guiStateManager.GetScrollManagement();

        std::array<int2, TableState::kMaxTableSections> offsets{};
        for (int i = 0; i < n; i++)
            offsets[i] = sm.GetScrollOffset(sm.GetOrCreateScrollOffsetHandle(state->SectionBodyID(i)));

        // The wrapper holds last frame's mirrored Y, so any section that differs
        // from it is the one the user just scrolled. Single-section tables fall
        // through to offsets[0].y.
        int32_t targetY = offsets[0].y;
        const ScrollOffsetHandle wrapperHandle = (n > 1 && state->internal.bodyID != interstice::ID::Invalid())
            ? sm.GetScrollOffsetHandle(state->internal.bodyID) : ScrollOffsetHandle::Invalid;
        if (n > 1 && sm.IsScrollOffsetHandleValid(wrapperHandle))
        {
            targetY = sm.GetScrollOffset(wrapperHandle).y;
            for (int i = 0; i < n; i++)
                if (offsets[i].y != targetY) { targetY = offsets[i].y; break; }
        }
        else if (n > 1)
        {
            // No history yet: majority Y, with the lone odd section winning.
            int bestCount = 0;
            for (int i = 0; i < n; i++)
            {
                int count = 0;
                for (int j = 0; j < n; j++)
                    if (offsets[j].y == offsets[i].y) count++;
                if (count > bestCount) { bestCount = count; targetY = offsets[i].y; }
            }
            // If exactly one section disagrees with the majority, that's the
            // user's scroll — adopt its Y instead of the majority's stale one.
            int oddCount = 0;
            int32_t oddY = targetY;
            for (int i = 0; i < n; i++)
                if (offsets[i].y != targetY) { oddCount++; oddY = offsets[i].y; }
            if (oddCount == 1) targetY = oddY;
        }

        for (int i = 0; i < n; i++)
        {
            if (offsets[i].y != targetY)
                sm.SetScrollOffset(sm.GetOrCreateScrollOffsetHandle(state->SectionBodyID(i)),
                                   int2{ offsets[i].x, targetY });
        }

        // Store the canonical Y on the bodies-row wrapper (internal.bodyID).
        if (state->internal.bodyID != interstice::ID::Invalid())
        {
            auto bodiesHandle = sm.GetOrCreateScrollOffsetHandle(state->internal.bodyID);
            const int2 cur = sm.GetScrollOffset(bodiesHandle);
            if (cur.y != targetY)
                sm.SetScrollOffset(bodiesHandle, int2{ 0, targetY });
        }
    }


    void GUIApplication::EmitTableSectionSeparator()
    {
        const interstice::ID sepID = idManager.AllocateDynamicID();
        interstice::Element sep{};
        sep.id       = sepID;
        sep.styleRef = theme.tableSeparatorStyleRef;
        sep.flags    = interstice::ElementFlags::NoInput;
        sep.state    = guiStateManager.GetCurrentState(sepID);
        auto sRes = elementStorage.Add(std::move(sep),
            interstice::LayoutOverride{}.NoGrow().NoShrink());
        VERIFY(sRes.status);
        VERIFY(windowRegistry.Add(sRes.ref));
    }


    void GUIApplication::EmitColumnSeparators(TableEmit& emit, int colStart, int colEnd)
    {
        // 1px dividers between columns for headerless tables (sizers mark them otherwise).
        // Placed by absolute X from the column tracks; boundaries past a Stretch track are skipped.
        if (colEnd - colStart < 2) return;
        int32_t gap = 0;
        if (const interstice::LayoutStyle* ls = styleManager.GetLayoutStyle(theme.tableScrollableBodyStyleRef.layoutStyleIndex))
            gap = ls->layoutData.grid.column.gap;

        int32_t x = 0;
        bool    sawFractional = false;
        for (int c = colStart; c < colEnd - 1; ++c)
        {
            const GridTrack& track = emit.columns[c].internal.columnTrack;
            if (track.type != GridTrack::Type::Fixed) { sawFractional = true; break; }
            x += static_cast<int32_t>(track.value) + gap;

            const interstice::ID sepID = idManager.AllocateDynamicID("_table_col_sep");
            auto sRes = elementStorage.Add(
                interstice::Element{
                    .id       = sepID,
                    .styleRef = theme.tableSizerStyleRef,
                    .flags    = interstice::ElementFlags::NoInput,
                    .state    = guiStateManager.GetCurrentState(sepID)
                },
                interstice::LayoutOverride{}
                    .PositionX(interstice::PositionConstraintAxis::Absolute(static_cast<int16_t>(x - gap)))
                    .PositionY(interstice::PositionConstraintAxis::Absolute(0))
                    .Size(styleManager.Px(1), kStretchHeight));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }
        (void)sawFractional;
    }

    void GUIApplication::EmitColumnBackgrounds(TableEmit& emit, int colStart, int colEnd)
    {
        // Full-height fill behind a fixed-width column's cells. Positioned by
        // absolute X from the resolved tracks (same cumulative walk as the
        // separators) and stretched to the body height, so it covers the rows
        // AND the empty space past the last one. Skips anything past a
        // fractional track, whose pixel offset isn't known here.
        int32_t gap = 0;
        if (const interstice::LayoutStyle* ls = styleManager.GetLayoutStyle(theme.tableScrollableBodyStyleRef.layoutStyleIndex))
            gap = ls->layoutData.grid.column.gap;

        int32_t x = 0;
        for (int c = colStart; c < colEnd; ++c)
        {
            const GridTrack& track = emit.columns[c].internal.columnTrack;
            if (track.type != GridTrack::Type::Fixed) break;

            const interstice::StyleRef bg = emit.columns[c].column.columnBackground;
            if (bg != interstice::StyleRef::Invalid())
            {
                const interstice::ID bgID = idManager.AllocateDynamicID("_table_col_bg");
                auto bRes = elementStorage.Add(
                    interstice::Element{
                        .id       = bgID,
                        .styleRef = bg,
                        .flags    = interstice::ElementFlags::NoInput,
                        .state    = guiStateManager.GetCurrentState(bgID)
                    },
                    interstice::LayoutOverride{}
                        .PositionX(interstice::PositionConstraintAxis::Absolute(static_cast<int16_t>(x)))
                        .PositionY(interstice::PositionConstraintAxis::Absolute(0))
                        .Size(static_cast<int>(track.value), kStretchHeight));
                VERIFY(bRes.status);
                VERIFY(windowRegistry.Add(bRes.ref));
            }
            x += static_cast<int32_t>(track.value) + gap;
        }
    }

    void GUIApplication::EmitColumnSizers(TableEmit& emit, int colStart, int colEnd,
                                          bool skipDragged)
    {
        // Each sizer is anchored to header cell c's right edge, so layout handles
        // widths, stretch and drag offsets.
        TableState* state = emit.userState;

        // Sizers after a Stretch column are plain 1px separators (dragging would fight the 1fr fill).
        int firstStretchCol = colEnd;
        for (int c = colStart; c < colEnd; ++c)
            if (emit.columns[c].column.sizing == ColumnSizing::Stretch)
                { firstStretchCol = c; break; }

        auto emitSizer = [&](int c)
        {
            VirtualTableColumn& vcol = emit.columns[c];
            const TableColumn& col = vcol.column;
            const bool isDraggable = (col.sizing == ColumnSizing::Resizable) && (c < firstStretchCol);

            interstice::EventHandler sizerHandler{};
            interstice::ElementFlags sizerFlags = interstice::ElementFlags::NoInput;
            const int sizerWidth = styleManager.Px(isDraggable ? kActiveSizerWidth : 1);

            if (isDraggable)
            {
                // No upper bound: when columns can't grow further the table just
                // starts scrolling horizontally.
                sizerFlags = interstice::ElementFlags::None;
                auto  headerStyleIndex = theme.tableHeaderStyleRef.layoutStyleIndex;
                auto  cellStyleIndex   = theme.tableCellStyleRef.layoutStyleIndex;
                // By pointer: max content width is only final when the handler runs.
                auto* maxWidth         = &vcol.internal.maxContentWidth;
                sizerHandler = interstice::MakeHorizontalSizerHandler(
                    guiStateManager,
                    state->columnWidths[c],
                    col.minWidth, std::numeric_limits<int>::max(),
                    nullptr,
                    // when double-clicking the sizer, snap to content width (plus padding/borders)
                    [maxWidth, sm = &styleManager, cellStyleIndex, headerStyleIndex]() -> int {
                        return *maxWidth + 1 +
                            std::max(sm->GetBorderBoxWidth(headerStyleIndex),
                                sm->GetBorderBoxWidth(cellStyleIndex));
                    },
                    false, styleManager.GetUIScale());
            }

            const interstice::ID sizerID = vcol.internal.sizerID;
            const interstice::ID leftHeaderID = elementStorage.GetElementUnsafe(vcol.internal.headerRef).id;
            auto sRes = elementStorage.Add(
                interstice::Element{
                    .id           = sizerID,
                    .eventHandler = std::move(sizerHandler),
                    .styleRef     = theme.tableSizerStyleRef,
                    .flags        = sizerFlags,
                    .state        = guiStateManager.GetCurrentState(sizerID)
                },
                interstice::LayoutOverride{}
                    .PositionX(interstice::PositionConstraintAxis::Anchored(
                        leftHeaderID, interstice::AnchorPlacementAxis::OutsideMax, 0))
                    .PositionY(interstice::PositionConstraintAxis::Absolute(0))
                    .Size(sizerWidth, kStretchHeight));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        };

        // The dragged column's sizer is emitted by EmitDraggedColumnOnTop when skipDragged.
        for (int c = colStart; c + 1 < colEnd; ++c)
            if (c != emit.draggedCol) emitSizer(c);
        if (!skipDragged && emit.draggedCol >= colStart && emit.draggedCol + 1 < colEnd)
            emitSizer(emit.draggedCol);
    }


    void GUIApplication::VirtualTable(ID id, TableState& state,
                                      std::span<VirtualTableColumn> columns,
                                      int totalRowCount,
                                      int32_t rowHeight,
                                      StyleRef style,
                                      LayoutOverride&& constraints,
                                      bool showHeaders,
                                      StyleRef cellStyle,
                                      bool columnSeparators)
    {
        const size_t columnCount = columns.size();
        state.internal.hasHeaders = showHeaders;

        // Sync this frame's section scroll into internal.bodyID before virtualizing, or the
        // row range lags a frame and fast scrollbar drags flash empty.
        MirrorTableScroll(&state);

        // Visible row range from this frame's scroll + last frame's viewport.
        int32_t scrollY   = 0;
        int32_t viewportH = state.internal.lastViewportH;
        if (state.internal.bodyID != interstice::ID::Invalid())
        {
            auto& scrollManagement = guiStateManager.GetScrollManagement();
            auto scrollHandle = scrollManagement.GetScrollOffsetHandle(state.internal.bodyID);
            if (scrollManagement.IsScrollOffsetHandleValid(scrollHandle))
                scrollY = -scrollManagement.GetScrollOffset(scrollHandle).y;
        }

        // Effective body-cell style: caller override, or the theme default.
        const bool     hasCustomCellStyle = (cellStyle.layoutStyleIndex != LayoutStyleIndex::Invalid);
        const StyleRef effectiveCellStyle = hasCustomCellStyle ? cellStyle : theme.tableCellStyleRef;

        // Row height must match the rendered cell so virtual scroll stays stable.
        // Default style: theme.tableRowHeight (measured at theme init); otherwise
        // measured here (cached by the text system).
        int32_t styleRowHeight = 1;
        if (!hasCustomCellStyle && theme.tableRowHeight > 0)
            styleRowHeight = theme.tableRowHeight;
        else if (textSystem)
        {
            const TextID probeID = textSystem->RegisterText(ID::Invalid(), "Mg", effectiveCellStyle, interstice::ElementState::None);
            int32_t h = textSystem->GetTextMeasurement(probeID).minHeight;
            if (const LayoutStyle* ls = styleManager.GetLayoutStyle(effectiveCellStyle.layoutStyleIndex))
                h += ls->padding.Vertical() + ls->borderWidths.Vertical();
            styleRowHeight = std::max(1, h);
        }
        const int32_t actualRowHeight = std::max(styleRowHeight, rowHeight);

        constexpr int kBufferRows = 5;
        const auto rowRange = TableHelpers::ComputeVirtualRowRange(
            scrollY, viewportH, actualRowHeight, totalRowCount, kBufferRows);
        const int firstVisible = rowRange.firstVisible;
        const int lastVisible  = rowRange.lastVisible;

        (void)idManager.PushParent(id);

        // One ranged ID for all rows and one for all sizers, scoped under the table's
        // id so two tables under the same parent don't collide.
        interstice::ID rowsRangeID = interstice::ID::Invalid();
        if (totalRowCount > 0 || columnCount > 0)
        {
            const uint64_t prevRowsHash = (state.internal.rowsRangeID != interstice::ID::Invalid())
                ? state.internal.rowsRangeID.value.rangedParts.hashValue : 0;
            if (totalRowCount > 0)
            {
                rowsRangeID = idManager.AllocateRangedID(static_cast<uint32_t>(totalRowCount));
                // Stored on TableState so keyboard nav can reach rows outside the window.
                state.internal.rowsRangeID     = rowsRangeID;
                state.internal.totalRowCount   = totalRowCount;
                state.internal.actualRowHeight = actualRowHeight;

                // ---- Sort permutation refresh ----
                // Active sort = sortColumn in range AND that column has a
                // sortAscending comparator. Otherwise the permutation is
                // dropped (identity mapping). Cached by (col, desc, count) +
                // explicit dirty flag so steady-state frames skip the sort.
                const int sc = state.sortColumn;
                const bool hasSort =
                    sc >= 0 && sc < int(columnCount) && columns[sc].sortAscending;
                if (!hasSort)
                {
                    state.internal.rowOrder.reset();
                    state.internal.sortedColumn     = -1;
                    state.internal.sortedRowCount   = totalRowCount;
                    state.internal.sortedDescending = state.sortDescending;
                    state.internal.rowOrderDirty    = false;
                }
                else
                {
                    const bool fingerprintChanged =
                        state.internal.sortedColumn   != sc
                        || state.internal.sortedDescending != state.sortDescending
                        || state.internal.sortedRowCount   != totalRowCount;
                    if (fingerprintChanged || state.internal.rowOrderDirty || !state.internal.rowOrder)
                    {
                        if (!state.internal.rowOrder)
                            state.internal.rowOrder = std::make_unique<std::vector<int>>();
                        auto& v = *state.internal.rowOrder;
                        v.resize(totalRowCount);
                        for (int i = 0; i < totalRowCount; ++i) v[i] = i;
                        const auto& cmp = columns[sc].sortAscending;
                        if (state.sortDescending)
                            std::stable_sort(v.begin(), v.end(),
                                [&cmp](int a, int b) { return cmp(b, a); });
                        else
                            std::stable_sort(v.begin(), v.end(), cmp);
                        state.internal.sortedColumn     = sc;
                        state.internal.sortedDescending = state.sortDescending;
                        state.internal.sortedRowCount   = totalRowCount;
                        state.internal.rowOrderDirty    = false;
                    }
                }
            }
            else
            {
                state.internal.rowsRangeID     = interstice::ID::Invalid();
                state.internal.totalRowCount   = 0;
                state.internal.actualRowHeight = 0;
                state.internal.rowOrder.reset();
                state.internal.sortedColumn     = -2;
                state.internal.sortedRowCount   = 0;
                state.internal.rowOrderDirty    = false;
            }
            // Rows past the new count are gone: drop them from the selection, or they'd
            // reappear on whatever data row takes that index when rows are added back.
            if (prevRowsHash != 0)
            {
                auto pastEnd = [&](interstice::ID r) {
                    return r.GetMode() == interstice::IDMode::SubRange &&
                           r.value.subRangeParts.parentHashValue == prevRowsHash &&
                           r.value.subRangeParts.index >= uint64_t(std::max(0, totalRowCount));
                };
                if (pastEnd(state.selectedRowID)) state.selectedRowID = interstice::ID::Invalid();
                if (state.multiSelect)
                {
                    std::erase_if(state.multiSelect->selectedIDs, pastEnd);
                    if (pastEnd(state.multiSelect->lastClickedID)) state.multiSelect->lastClickedID = interstice::ID::Invalid();
                }
            }
            if (columnCount > 0)
            {
                const interstice::ID bodyRange  = idManager.AllocateRangedID(static_cast<uint32_t>(columnCount), "_table_bodies");
                const interstice::ID sizerRange = idManager.AllocateRangedID(static_cast<uint32_t>(columnCount), "_table_sizers");
                const interstice::ID hdrRange   = idManager.AllocateRangedID(static_cast<uint32_t>(columnCount), "_table_headers");
                for (size_t c = 0; c < columnCount; ++c)
                {
                    columns[c].internal.bodyID       = idManager.GetSubID(bodyRange, static_cast<uint32_t>(c));
                    columns[c].internal.sizerID      = idManager.GetSubID(sizerRange, static_cast<uint32_t>(c));
                    columns[c].internal.headerCellID = idManager.GetSubID(hdrRange,   static_cast<uint32_t>(c));
                }
            }
        }
        else
        {
            // Nothing to show: drop the previous frame's row info so keyboard nav can't aim at vanished rows.
            state.internal.rowsRangeID     = interstice::ID::Invalid();
            state.internal.totalRowCount   = 0;
            state.internal.actualRowHeight = 0;
            state.internal.rowOrder.reset();
            state.internal.sortedColumn     = -2;
            state.internal.sortedRowCount   = 0;
            state.internal.rowOrderDirty    = false;
        }

        state.internal.horzScrollbarID = idManager.AllocateDynamicID("_table_horz_scrollbar");
        state.internal.vertScrollbarID = idManager.AllocateDynamicID("_table_vert_scrollbar");
        const interstice::ID rowID = idManager.AllocateDynamicID("_table_bodies_row");

        (void)idManager.PopParent();

        // Lazy-init persisted widths on a fresh state / when columnCount changed.
        if (state.columnWidths.size() != columnCount)
        {
            state.columnWidths.assign(columnCount, 0);
            for (size_t c = 0; c < columnCount; c++)
                state.columnWidths[c] = columns[c].column.initialWidth;
        }

        // Clamp persisted selectedColumn to the current column range.
        state.selectedColumn = (columnCount == 0) ? 0
            : std::clamp(state.selectedColumn, 0, int(columnCount) - 1);

        // Reset per-column scratch on the user-owned VirtualTableColumn array.
        // (Recording-mode BeginTable already did this once; harmless to repeat.)
        for (auto& col : columns)
        {
            col.internal.maxContentWidth = 0;
            col.internal.headerRef       = interstice::ElementRef::Invalid();
            col.internal.dragOffsetX     = 0;
        }
        // Tree row map is allocated lazily by TreeCell. clear() preserves
        // bucket capacity so no realloc in steady-state frames.
        if (state.internal.treeRowInfo) state.internal.treeRowInfo->clear();


        // Via EndTable the recording state is already in place (BeginTable stashed the outer one);
        // a direct call stashes whatever table it interrupts.
        if (!(m_tableEmit && m_tableEmit->isRecording))
            m_outerTableEmits.push_back(std::move(m_tableEmit));
        m_tableEmit.emplace();
        m_tableEmit->columns     = columns.data();
        m_tableEmit->columnCount = columnCount;
        m_tableEmit->userState   = &state;
        TableEmit& emit = *m_tableEmit;
        // A column drag can't finish if its column is gone (the column set shrank) or it
        // lost its capture (the table wasn't shown when the button went up): cancel it.
        if (m_columnDrag.active && m_columnDrag.tableID == id &&
            (m_columnDrag.fromCol >= int(columnCount) ||
             guiStateManager.Interaction().GetCaptureID() == interstice::ID::Invalid()))
            m_columnDrag = {};
        emit.dragActive       = m_columnDrag.active && m_columnDrag.tableID == id;
        emit.cellMode         = (state.selectionMode == TableSelectionMode::Cell);
        emit.draggedCol       = emit.dragActive ? m_columnDrag.fromCol : -1;
        emit.showHeaders      = showHeaders;
        emit.columnSeparators = columnSeparators;
        emit.cellStyleRef     = effectiveCellStyle;

        // Last frame's per-axis overflow, reset for this frame's sampling. Gates
        // scrollbar emission and the H-bar bottom pad.
        emit.anyHorzOverflow = state.internal.anyHorzOverflow;
        emit.anyVertOverflow = state.internal.anyVertOverflow;
        state.internal.anyHorzOverflow = false;
        state.internal.anyVertOverflow = false;

        // Sections (runs of consecutive columns sharing the same sectionId)
        // are detected inline by the row-scope walks below; per-section IDs
        // and the count get written into TableState as we go. Reset here so
        // a frame with zero columns leaves clean state.
        state.internal.sectionCount = 0;
        state.internal.bodyID       = interstice::ID::Invalid();

        // During a drag the last column drops its 1fr fill (else it paints under the
        // V scrollbar); use its pre-drag width minus the scrollbar so it doesn't snap smaller.
        if (emit.dragActive && columnCount > 0)
        {
            const size_t lastIdx = columnCount - 1;
            const int32_t lastW = styleManager.Px(state.columnWidths[lastIdx]);
            int32_t snapshotW = lastW;
            if (lastIdx < m_columnDrag.headerRects.size())
                snapshotW = std::max<int32_t>(snapshotW, m_columnDrag.headerRects[lastIdx].w);
            emit.lastColDragFixedWidth = std::max<int32_t>(
                lastW,
                snapshotW - theme.scrollbarThickness);
        }

        // Pre-compute per-column reorder-drag X offsets.
        if (emit.dragActive && columnCount > 0)
        {
            const auto& drag = m_columnDrag;
            const int fromCol = drag.fromCol;
            const int toCol   = std::clamp(drag.toCol, 0, int(columnCount) - 1);
            const int32_t draggedWidth = (size_t(fromCol) == columnCount - 1)
                ? emit.lastColDragFixedWidth
                : styleManager.Px(state.columnWidths[fromCol]);
            int32_t gap = 0;
            if (const interstice::LayoutStyle* ls = styleManager.GetLayoutStyle(theme.tableScrollableBodyStyleRef.layoutStyleIndex))
                gap = ls->layoutData.grid.column.gap;
            const int32_t shiftBy = draggedWidth + gap;
            interstice::IntRect draggedRect{};
            int32_t leftBound = 0, rightBound = 0;
            if (fromCol >= 0 && fromCol < int(drag.headerRects.size()))
            {
                draggedRect = drag.headerRects[fromCol];
                leftBound   = drag.headerRects[std::clamp(drag.minTarget, 0, int(drag.headerRects.size()) - 1)].x;
                const int maxIdx = std::clamp(drag.maxTarget, 0, int(drag.headerRects.size()) - 1);
                rightBound  = drag.headerRects[maxIdx].x + drag.headerRects[maxIdx].w;
            }
            for (int c = 0; c < int(columnCount); ++c)
            {
                int16_t ox = 0;
                if (c == fromCol)
                {
                    const int32_t rawX     = draggedRect.x + drag.offset.x;
                    const int32_t clampedX = std::clamp(rawX, leftBound, rightBound - draggedRect.w);
                    ox = static_cast<int16_t>(clampedX - draggedRect.x);
                }
                else if (fromCol < toCol && c > fromCol && c <= toCol)
                    ox = static_cast<int16_t>(-shiftBy);
                else if (fromCol > toCol && c >= toCol && c < fromCol)
                    ox = static_cast<int16_t>(shiftBy);
                columns[c].internal.dragOffsetX = ox;
            }
        }

        StyleRef containerStyle = (style != StyleRef{}) ? style : theme.tableStyleRef;
        TableState* statePtr = &state;

        interstice::Element container{
            .id           = id,
            .eventHandler = BuildTableContainerHandler(statePtr, id),
            .styleRef     = containerStyle,
            .state        = guiStateManager.GetCurrentState(id),
        };

        // Resolve the sort indicator. If any column is sortable, snap an
        // out-of-range sortColumn to the first sortable column so the
        // indicator is always visible. If no column is sortable the search
        // leaves effectiveSortCol == -1 and the assignment is skipped.
        int effectiveSortCol = -1;
        const int sc = statePtr->sortColumn;
        auto colIsSortable = [&](size_t i) {
            return columns[i].column.sortable
                || static_cast<bool>(columns[i].sortAscending);
        };
        if (sc >= 0 && sc < int(columnCount) && colIsSortable(sc))
            effectiveSortCol = sc;
        else
            for (size_t c = 0; c < columnCount; c++)
                if (colIsSortable(c)) { effectiveSortCol = int(c); break; }
        if (effectiveSortCol >= 0)
            statePtr->sortColumn = effectiveSortCol;

        // One shared cell click handler (cell mode), resolving cells via internal.cellLookup.
        if (statePtr->internal.cellLookup) statePtr->internal.cellLookup->clear();
        if (emit.cellMode)
        {
            statePtr->EnsureCellLookup();
            emit.sharedCellHandler =
                [this, statePtr, id](ElementEvent&& event) -> bool
            {
                if (event.type != interstice::EventType::MouseDown) return false;
                if (!event.state.LeftPressed()) return false;
                const auto* hit = statePtr->FindCell(event.id);
                if (!hit) return false;
                statePtr->selectedRowID  = hit->rowID;
                statePtr->selectedColumn = hit->columnIdx;
                guiStateManager.Interaction().SetFocusID(id);
                guiStateManager.TextEditorFocus().Clear();
                guiStateManager.Interaction().SetClicked(hit->rowID);
                return true;
            };
        }

        // The arrow points opposite to the sort direction: ascending (a→z)
        // shows a down arrow, descending (z→a) shows an up arrow.
        const IconID sortIcon = (effectiveSortCol < 0) ? IconID::Invalid
                                    : (statePtr->sortDescending ? theme.sortArrowUpIcon : theme.sortArrowDownIcon);

        // Build the shared row-backdrop handler once. The handler reads the
        // row's id from event.id, so a single instance covers every row.
        // (Empty in cell-selection mode — cell backdrops are NoInput.)
        if (!emit.cellMode)
            emit.sharedRowHandler = BuildRowBackdropHandler(statePtr, id);


        RegisterElement(std::move(container), std::move(constraints));
        tabChainManager.RegisterTabTarget(id);

        VERIFY(elementStorage.BeginChildren()); // outer wrapper's children
        {
            // One bodies row holding every section; each section's header (StickyY,
            // row 0) and body share one scrolling grid. The row's ID is state.internal.bodyID,
            // the canonical scroll-Y (kept in sync by MirrorTableScroll).
            state.internal.bodyID = rowID;
            auto res = elementStorage.Add(
                interstice::Element{
                    .id       = rowID,
                    .styleRef = theme.tableBodiesRowStyleRef,
                    .state    = guiStateManager.GetCurrentState(rowID),
                }, interstice::LayoutOverride{});
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));
            VERIFY(elementStorage.BeginChildren());

            int sectionIdx   = 0;
            int sectionStart = 0;
            for (int c = 1; c <= int(columnCount); ++c)
            {
                // Sections past kMaxTableSections fold into the last one instead of overflowing the ID arrays.
                const bool boundary = (c == int(columnCount))
                                   || (columns[c].sectionId != columns[c - 1].sectionId
                                       && sectionIdx + 1 < TableState::kMaxTableSections);
                if (!boundary) continue;

                if (sectionIdx > 0) EmitTableSectionSeparator();

                const int  colEnd     = c;
                const bool isLast     = (c == int(columnCount));
                const bool fitContent =
                    columns[sectionStart].sectionSizing == SectionSizing::FitContent;

                // Resolve column tracks once per section. Fit-content sections use only
                // Fixed tracks, so their width is the sum below.
                int sectionFixedWidth = 0;
                for (int cc = sectionStart; cc < colEnd; cc++)
                {
                    columns[cc].internal.columnTrack = ComputeSectionColumnTrack(
                        emit, cc, colEnd, /*isMiddle*/!fitContent, /*fitContent*/fitContent);
                    if (fitContent)
                        sectionFixedWidth += static_cast<int>(columns[cc].internal.columnTrack.value);
                }

                const ID bodyID = columns[sectionStart].internal.bodyID;
                state.SetSectionBodyID(sectionIdx, bodyID);
                state.internal.sectionCount = static_cast<int8_t>(sectionIdx + 1);

                // Scope IDs under the section body so sections can't collide via the dynamic counter.
                (void)idManager.PushParent(bodyID);

                // Body rows are emitted before headers so the sticky header paints on top
                // (grid rows are explicit, so only paint order changes). The rightmost
                // section samples the viewport.
                OpenTableBodySection(emit, bodyID,
                                     fitContent ? theme.tableFitContentBodyStyleRef
                                                : theme.tableScrollableBodyStyleRef,
                                     fitContent ? interstice::LayoutOverride{}.Width(sectionFixedWidth)
                                                : interstice::LayoutOverride{}.MinWidth(0),
                                     /*sampleViewport*/isLast);

                // Non-dragged content first; EmitDraggedColumnOnTop adds the dragged column last.
                EmitVirtualBodyRows(emit, sectionStart, colEnd,
                    totalRowCount, actualRowHeight,
                    firstVisible, lastVisible,
                    rowsRangeID, /*isLastSection*/isLast);
                if (emit.showHeaders)
                {
                    EmitTableHeaderSection(emit, id,
                                           sectionStart, colEnd, /*lastColIsStretch*/!fitContent,
                                           effectiveSortCol, sortIcon);
                    EmitColumnSizers(emit, sectionStart, colEnd, /*skipDragged*/true);
                }
                else
                {
                    // No header row → no sizers, but column boundaries should
                    // still read. Emit thin static separators at each internal
                    // boundary (unless the caller opted out).
                    if (emit.columnSeparators)
                        EmitColumnSeparators(emit, sectionStart, colEnd);
                }
                if (emit.dragActive
                    && emit.draggedCol >= sectionStart && emit.draggedCol < colEnd)
                {
                    EmitDraggedColumnOnTop(emit, id,
                        sectionStart, colEnd,
                        firstVisible, lastVisible, rowsRangeID,
                        /*lastColIsStretch*/!fitContent,
                        effectiveSortCol, sortIcon);
                }
                CloseTableBodySection(emit, bodyID,
                                      /*emitVerticalScrollbar*/isLast,
                                      /*suppressHorzScrollbar*/fitContent);

                (void)idManager.PopParent();

                sectionIdx++;
                sectionStart = c;
            }

            VERIFY(elementStorage.EndChildren());

            MirrorTableScroll(statePtr);
        }
        VERIFY(elementStorage.EndChildren()); // table container
        m_tableEmit = std::move(m_outerTableEmits.back());
        m_outerTableEmits.pop_back();
    }

}
