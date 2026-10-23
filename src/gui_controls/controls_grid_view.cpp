#include <gui_controls/gui_application.h>
#include <gui_controls/gui_table_helpers.h>

#include <algorithm>
#include <cstdint>

namespace interstice
{


    // ---------------------------------------------------------------------------
    // GridView
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginGridView(ID id, int* selectedIndex, StyleRef style,
                                    MultiSelectState* multiSelect,
                                    ItemDragDropConfig* dragDrop,
                                    LayoutOverride&& constraints)
    {
        m_gridViewState = {};
        m_gridViewState.containerID   = id;
        m_gridViewState.selectedIndex = selectedIndex;
        m_gridViewState.multiSelect   = multiSelect;
        m_gridViewState.dragDrop      = dragDrop;

        if (multiSelect)
            multiSelect->orderedItemIDs.clear();

        // Capture pointers for the container keyboard handler
        int* selIdx = selectedIndex;

        interstice::Element container{};
        container.id       = id;
        container.styleRef = style;
        container.state    = guiStateManager.GetCurrentState(id);
        container.eventHandler = [this, id, selIdx, multiSelect](ElementEvent&& event) -> bool
        {
            switch (event.type)
            {
                case interstice::EventType::MouseDown:
                    if (!event.state.LeftPressed()) break;
                    guiStateManager.Interaction().SetFocusID(event.id);
                    guiStateManager.TextEditorFocus().Clear();
                    return true;

                case interstice::EventType::KeyDown:
                {
                    // F2 — rename the last-clicked item (multi-select grids).
                    if (event.state.lastKey == interstice::KeyCode::F2 && multiSelect)
                    {
                        auto it = m_gridItemRenameByID.find(multiSelect->lastClickedID);
                        if (it != m_gridItemRenameByID.end() && it->second.renameBuf)
                        {
                            (*m_renameSub).Start(multiSelect->lastClickedID,
                                                 it->second.renameBuf, it->second.renameSeed);
                            return true;
                        }
                        break;
                    }
                    if (!selIdx) break;

                    // Compute grid dimensions from laid-out child rects
                    auto containerRef = elementStorage.FindElementRefByID(id);
                    auto childRange = elementStorage.GetChildRange(containerRef);
                    int total = static_cast<int>(childRange.Size());
                    if (total <= 0) break;

                    // Columns per row = items laid out on the first row (gaps and
                    // wrapping included), not innerWidth / itemWidth.
                    uint32_t childDepth = containerRef.depth + 1;
                    int cols = 1;
                    if (const auto* firstRect = elementStorage.GetElementRectPtr(ElementRef{childDepth, childRange.begin}))
                    {
                        while (cols < total)
                        {
                            const auto* r = elementStorage.GetElementRectPtr(
                                ElementRef{ childDepth, childRange.begin + static_cast<uint32_t>(cols) });
                            if (!r || r->rect.y != firstRect->rect.y) break;
                            ++cols;
                        }
                    }

                    int cur = std::clamp(*selIdx, 0, total - 1);
                    int next = cur;

                    switch (event.state.lastKey)
                    {
                        case interstice::KeyCode::Right: next = std::min(cur + 1, total - 1); break;
                        case interstice::KeyCode::Left:  next = std::max(cur - 1, 0); break;
                        case interstice::KeyCode::Down:  next = std::min(cur + cols, total - 1); break;
                        case interstice::KeyCode::Up:    next = std::max(cur - cols, 0); break;
                        default: return false;
                    }

                    if (next != cur)
                    {
                        *selIdx = next;
                        return true;
                    }
                    break;
                }

                default: break;
            }
            return false;
        };

        RegisterElement(std::move(container), std::move(constraints));
        tabChainManager.RegisterTabTarget(id);
        VERIFY(elementStorage.BeginChildren());
        return true;
    }

    void GUIApplication::EndGridView()
    {
        VERIFY(elementStorage.EndChildren());
        m_gridViewState = {};
    }

    bool GUIApplication::GridViewItem(ID id, std::string_view text, IconID icon,
                                   interstice::ElementState extraState, StyleRef itemStyle,
                                   LayoutOverride&& constraints,
                                   std::string* renameBuffer)
    {
        int itemIndex = m_gridViewState.nextItemIndex++;
        int* selectedIndex        = m_gridViewState.selectedIndex;
        MultiSelectState* multi   = m_gridViewState.multiSelect;
        interstice::ID containerID       = m_gridViewState.containerID;
        ItemDragDropConfig* dd    = m_gridViewState.dragDrop;
        DoubleClickTracker* clickTracker = m_gridViewState.clickTracker;
        interstice::ID* pendingRename    = m_gridViewState.pendingRenameClick;

        bool selected = false;
        if (multi)
            selected = multi->selectedIDs.count(id) > 0;
        else if (selectedIndex)
            selected = *selectedIndex == itemIndex;

        StyleRef style = (itemStyle != StyleRef{}) ? itemStyle : theme.gridViewItemStyleRef;

        // Record the rename binding for the F2 / Ctrl+Shift+Click / slow re-click triggers.
        if (renameBuffer)
        {
            auto& info = m_gridItemRenameByID[id];
            info.renameBuf = renameBuffer;
            info.renameSeed.assign(text);
        }

        // Drop the rename when its editor lost focus (click-away cancel).
        if (renameBuffer && (*m_renameSub).isActive &&
            (*m_renameSub).targetID == id &&
            !(*m_renameSub).firstFrame &&
            guiStateManager.Interaction().GetFocusedID() != id)
        {
            (*m_renameSub).Cancel();
        }

        // Renaming: the cell becomes [icon, one-line inline editor], laid out like the
        // item's icon placement. Not selectable / draggable while editing.
        if (renameBuffer && (*m_renameSub).isActive && (*m_renameSub).targetID == id)
        {
            if (multi)
                multi->orderedItemIDs.push_back(id);

            const VisualTextStyle* vts =
                styleManager.GetVisualTextStyle(style.visualTextStyleIndex, interstice::ElementState::None);
            const bool iconBeside = vts && (vts->iconSide == IconSide::Left
                                         || vts->iconSide == IconSide::Right);
            const int32_t iconSize = (vts && vts->iconSize > 0) ? vts->iconSize : styleManager.Px(16);

            interstice::ID boxID = idManager.AllocateDynamicID();
            interstice::Element box{};
            box.id       = boxID;
            box.styleRef = iconBeside ? theme.gridViewRenameListItemStyleRef
                                      : theme.gridViewRenameItemStyleRef;
            box.state    = guiStateManager.GetCurrentState(boxID);
            auto boxRes = elementStorage.Add(std::move(box), std::move(constraints));
            VERIFY(boxRes.status);
            VERIFY(windowRegistry.Add(boxRes.ref));
            VERIFY(elementStorage.BeginChildren());

            if (icon != IconID::Invalid)
            {
                interstice::ID iconElemID = idManager.AllocateDynamicID();
                interstice::Element icn{};
                icn.id       = iconElemID;
                icn.iconID   = icon;
                icn.flags    = interstice::ElementFlags::NoInput;
                icn.styleRef = theme.gridViewRenameIconStyleRef;
                icn.state    = guiStateManager.GetCurrentState(iconElemID);
                auto icnRes = elementStorage.Add(std::move(icn),
                    LayoutOverride{}.Size(iconSize, iconSize).NoGrow().NoShrink());
                VERIFY(icnRes.status);
                VERIFY(windowRegistry.Add(icnRes.ref));
            }

            TreeItemRenameCheck(id, renameBuffer,
                                iconBeside ? theme.treeTableTextEditStyleRef
                                           : theme.gridViewRenameEditStyleRef);

            VERIFY(elementStorage.EndChildren());
            return false;
        }

        if (selected) guiStateManager.Construction().PushSelected();

        interstice::Element item{};
        item.id       = id;
        item.state    = guiStateManager.GetCurrentState(id) | extraState;
        item.styleRef = style;
        item.iconID   = icon;
        item.textID   = textSystem->RegisterText(id, icon, text, style, item.state);

        item.eventHandler = [this, containerID, id, itemIndex, selectedIndex, multi, dd,
                             clickTracker, pendingRename,
                             wasSelected = selected, canRename = (renameBuffer != nullptr)]
            (ElementEvent&& event) -> bool
        {
            switch (event.type)
            {
                case interstice::EventType::MouseDown:
                {
                    if (!event.state.LeftPressed()) break;

                    bool ctrl  = (event.state.modifiers & interstice::KeyModifier::Control) != interstice::KeyModifier::None;
                    bool shift = (event.state.modifiers & interstice::KeyModifier::Shift) != interstice::KeyModifier::None;

                    // Ctrl+Shift+Click — start rename (same as table rows).
                    if (canRename && ctrl && shift)
                    {
                        auto it = m_gridItemRenameByID.find(id);
                        if (it != m_gridItemRenameByID.end() && it->second.renameBuf)
                        {
                            (*m_renameSub).Start(id, it->second.renameBuf, it->second.renameSeed);
                            if (clickTracker) clickTracker->Reset();
                            return true;
                        }
                    }

                    // Slow re-click on a selected item arms a rename, started on MouseUp
                    // so dragging a selected item never opens an editor.
                    if (clickTracker && pendingRename)
                    {
                        const auto clickInfo = clickTracker->Update(id);
                        *pendingRename = (canRename && wasSelected && !ctrl && !shift
                                          && clickInfo.sameAsPrevious
                                          && clickInfo.elapsedMs >= 500.0f
                                          && clickInfo.elapsedMs < 3000.0f)
                                       ? id : interstice::ID::Invalid();
                    }

                    if (multi)
                    {
                        if (shift && multi->lastClickedID != interstice::ID::Invalid())
                        {
                            const interstice::ID anchor = multi->lastClickedID;
                            const bool sameRange = anchor.GetMode() == interstice::IDMode::SubRange && id.GetMode() == interstice::IDMode::SubRange &&
                                anchor.value.subRangeParts.parentHashValue == id.value.subRangeParts.parentHashValue;
                            const auto& ordered = multi->orderedItemIDs;
                            auto itAnchor  = std::find(ordered.begin(), ordered.end(), anchor);
                            auto itCurrent = std::find(ordered.begin(), ordered.end(), id);

                            if (sameRange)
                            {
                                // Virtual grid: every item is a sub-ID of one range, so the range
                                // comes from the indices, even when the anchor isn't emitted.
                                if (!ctrl)
                                    multi->selectedIDs.clear();
                                const uint64_t a  = anchor.value.subRangeParts.index;
                                const uint64_t b  = id.value.subRangeParts.index;
                                for (uint64_t i = std::min(a, b); i <= std::max(a, b); ++i)
                                {
                                    interstice::ID item = id;
                                    item.value.subRangeParts.index = i;
                                    multi->selectedIDs.insert(item);
                                }
                            }
                            else if (itAnchor != ordered.end() && itCurrent != ordered.end())
                            {
                                if (!ctrl)
                                    multi->selectedIDs.clear();
                                auto first = std::min(itAnchor, itCurrent);
                                auto last  = std::max(itAnchor, itCurrent);
                                for (auto it = first; it <= last; ++it)
                                    multi->selectedIDs.insert(*it);
                            }
                            // Don't update lastClickedID on Shift+Click
                        }
                        else if (ctrl)
                        {
                            if (multi->selectedIDs.count(id)) multi->selectedIDs.erase(id);
                            else multi->selectedIDs.insert(id);
                            multi->lastClickedID = id;
                        }
                        else
                        {
                            multi->selectedIDs.clear();
                            multi->selectedIDs.insert(id);
                            multi->lastClickedID = id;
                        }
                    }
                    else if (selectedIndex)
                    {
                        *selectedIndex = itemIndex;
                    }

                    guiStateManager.Interaction().SetFocusID(containerID);
                    guiStateManager.TextEditorFocus().Clear();
                    guiStateManager.Interaction().SetClicked(id);

                    // Prime a pending drag (promoted once past the threshold). Inlined rather
                    // than chaining MakeDragSourceHandler: Chain stops at the `return true` below.
                    if (dd && dd->payloadType != 0)
                    {
                        uintptr_t payloadValue = dd->getPayload ? dd->getPayload(id) : 0;
                        guiStateManager.Drag().Begin(id, dd->payloadType, payloadValue,
                                                  event.state.screenPos, std::any(),
                                                  dd->visualizer);
                    }
                    return true;
                }

                case interstice::EventType::MouseMove:
                {
                    if (!dd || dd->payloadType == 0) break;
                    if ((event.state.buttons & interstice::MouseButtons::Left) == interstice::MouseButtons::None) break;
                    guiStateManager.Drag().UpdateFromInput(event.state);
                    return guiStateManager.Drag().GetDraggedID() == id;
                }

                case interstice::EventType::MouseUp:
                {
                    if (!event.state.LeftReleased()) break;
                    if (pendingRename && *pendingRename == id)
                    {
                        *pendingRename = interstice::ID::Invalid();
                        // A promoted drag supersedes the armed rename.
                        if (!guiStateManager.Drag().IsDragging())
                        {
                            auto it = m_gridItemRenameByID.find(id);
                            if (it != m_gridItemRenameByID.end() && it->second.renameBuf)
                            {
                                (*m_renameSub).Start(id, it->second.renameBuf, it->second.renameSeed);
                                return true;
                            }
                        }
                    }
                    break;
                }

                default: break;
            }
            return false;
        };

        // Track item ID for Shift+Click range selection
        if (multi)
            multi->orderedItemIDs.push_back(id);

        // The drop target only handles drag events, so chaining it after MouseDown is safe.
        if (dd && dd->payloadType != 0)
        {
            auto capturedID = id;
            item.eventHandler = interstice::Chain(std::move(item.eventHandler),
                interstice::MakeDropTargetHandler(dd->payloadType,
                    [dd, capturedID](uintptr_t payloadValue, interstice::ID /*sourceID*/) {
                        if (dd->onDrop) dd->onDrop(payloadValue, capturedID);
                    },
                    [dd, capturedID](uintptr_t payloadValue) {
                        return !dd->canDrop || dd->canDrop(payloadValue, capturedID);
                    }));
        }

        auto addRes = elementStorage.Add(std::move(item), std::move(constraints));
        VERIFY(addRes.status);
        VERIFY(windowRegistry.Add(addRes.ref));

        if (selected) guiStateManager.Construction().PopSelected();
        return guiStateManager.Interaction().HasBeenClicked(id);
    }

    // ---------------------------------------------------------------------------
    // VirtualGridView — GridViewItems in a virtualized, self-scrolling viewport.
    // Only visible rows are emitted; spacers stand in for the rest so the
    // content height (and scrollbar) spans the whole grid.
    // ---------------------------------------------------------------------------
    void GUIApplication::VirtualGridView(ID id, GridViewVirtualState& vstate,
                                         int itemCount, int2 cellSize,
                                         const std::function<void(int index, ID itemID)>& generateItem,
                                         int* selectedIndex,
                                         MultiSelectState* multiSelect,
                                         ItemDragDropConfig* dragDrop,
                                         StyleRef scrollStyle,
                                         StyleRef rowStyle,
                                         LayoutOverride&& constraints)
    {
        if (scrollStyle == StyleRef{}) scrollStyle = theme.gridViewScrollStyleRef;
        if (rowStyle    == StyleRef{}) rowStyle    = theme.gridViewRowStyleRef;

        // Per-frame state read by GridViewItem (selection / drag-drop / id).
        m_gridViewState = {};
        m_gridViewState.containerID        = id;
        m_gridViewState.selectedIndex      = selectedIndex;
        m_gridViewState.multiSelect        = multiSelect;
        m_gridViewState.dragDrop           = dragDrop;
        m_gridViewState.clickTracker       = &vstate.internal.itemClickTracker;
        m_gridViewState.pendingRenameClick = &vstate.internal.pendingRenameClick;
        if (multiSelect)
            multiSelect->orderedItemIDs.clear();

        // Wheel-scroll and the scrollbar both need the scroll-offset handle to exist.
        auto& scrollManagement = guiStateManager.GetScrollManagement();
        (void)scrollManagement.GetOrCreateScrollOffsetHandle(id);

        const LayoutStyle* rowLayout = styleManager.GetLayoutStyle(rowStyle.layoutStyleIndex);
        const int kGap                = rowLayout ? rowLayout->layoutData.flex.gap : 0;
        constexpr int kBufferRows     = 2;

        const int cellW     = std::max(1, cellSize.x);
        const int cellH     = std::max(1, cellSize.y);
        const int colStride = cellW + kGap;
        const int rowStride = cellH + kGap;

        // Column/row counts from last frame's viewport, minus the scrollbar gutter when overflowing.
        const int viewW = vstate.lastViewportW;
        const int viewH = vstate.lastViewportH;
        auto colsForWidth = [&](int w) { return std::max(1, (w + kGap) / colStride); };
        int availW    = viewW;
        int cols      = colsForWidth(availW);
        int totalRows = (itemCount + cols - 1) / cols;
        const int scrollbarThickness = theme.scrollbarThickness;
        if (static_cast<int64_t>(totalRows) * rowStride > viewH && viewW > scrollbarThickness)
        {
            availW    = viewW - scrollbarThickness;
            cols      = colsForWidth(availW);
            totalRows = (itemCount + cols - 1) / cols;
        }

        // Cell widths that exactly fill the row: leftover pixels are spread by floor-boundary
        // rounding, so resizing grows one cell at a time. colBound(k) is column k's left edge;
        // the per-column width is published right before each generateItem call.
        const int contentW = std::max(0, availW - (cols - 1) * kGap);
        auto colBound = [contentW, cols](int k)
        { return static_cast<int>((int64_t)k * contentW / cols); };
        vstate.cellStretchedW = std::max(cellW, contentW / cols);
        vstate.internal.cols          = cols;
        vstate.internal.rowStride     = rowStride;
        vstate.internal.cellH         = cellH;

        int scrollY = 0;
        {
            const auto handle = scrollManagement.GetScrollOffsetHandle(id);
            if (scrollManagement.IsScrollOffsetHandleValid(handle))
                scrollY = -scrollManagement.GetScrollOffset(handle).y; // negative offset = scrolled down
        }
        const auto range = interstice::TableHelpers::ComputeVirtualRowRange(
            scrollY, viewH, rowStride, totalRows, kBufferRows);
        const int firstRow = range.firstVisible;
        const int lastRow  = range.lastVisible;

        // ---- Scroll container ------------------------------------------------
        interstice::Element container{};
        container.id       = id;
        container.styleRef = scrollStyle;
        container.state    = guiStateManager.GetCurrentState(id);
        container.eventHandler =
            [this, selectedIndex, multiSelect, itemCount, vsPtr = &vstate](ElementEvent&& event) -> bool
            {
                switch (event.type)
                {
                    case interstice::EventType::MouseDown:
                        if (!event.state.LeftPressed()) break;
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        return true;

                    case interstice::EventType::KeyDown:
                    {
                        // F2 — rename the selected item (only while it's within the emitted window).
                        if (event.state.lastKey == interstice::KeyCode::F2)
                        {
                            interstice::ID target = interstice::ID::Invalid();
                            if (multiSelect)
                                target = multiSelect->lastClickedID;
                            else if (selectedIndex && *selectedIndex >= 0
                                     && vsPtr->internal.itemsRangeID != interstice::ID::Invalid())
                                target = idManager.GetSubID(vsPtr->internal.itemsRangeID,
                                                            static_cast<uint32_t>(*selectedIndex));
                            auto it = m_gridItemRenameByID.find(target);
                            if (it != m_gridItemRenameByID.end() && it->second.renameBuf)
                            {
                                (*m_renameSub).Start(target, it->second.renameBuf, it->second.renameSeed);
                                return true;
                            }
                            break;
                        }
                        if (!selectedIndex || itemCount <= 0) break;
                        const int navCols = std::max(1, vsPtr->internal.cols);
                        const int cur  = std::clamp(*selectedIndex, 0, itemCount - 1);
                        int       next = cur;
                        switch (event.state.lastKey)
                        {
                            case interstice::KeyCode::Right: next = std::min(cur + 1, itemCount - 1); break;
                            case interstice::KeyCode::Left:  next = std::max(cur - 1, 0); break;
                            case interstice::KeyCode::Down:  next = std::min(cur + navCols, itemCount - 1); break;
                            case interstice::KeyCode::Up:    next = std::max(cur - navCols, 0); break;
                            default: return false;
                        }
                        if (next == cur) break;
                        *selectedIndex = next;

                        // Scroll the new item's row into view; it may lie outside the
                        // virtualization window, so work from the row pitch, not an element.
                        auto& sm = guiStateManager.GetScrollManagement();
                        const auto handle = sm.GetOrCreateScrollOffsetHandle(event.id);
                        const int2 offset = sm.GetScrollOffset(handle);
                        const int  top    = (next / navCols) * vsPtr->internal.rowStride;
                        const int  bottom = top + vsPtr->internal.cellH;
                        int scrollY = -offset.y;
                        if (top < scrollY)                                 scrollY = top;
                        else if (bottom > scrollY + vsPtr->lastViewportH)  scrollY = bottom - vsPtr->lastViewportH;
                        if (scrollY != -offset.y) sm.SetScrollOffset(handle, int2{ offset.x, -scrollY });
                        return true;
                    }
                    default: break;
                }
                return false;
            };

        // Background drop target: onDrop fires with the container's ID ("dropped into the view").
        if (dragDrop && dragDrop->payloadType != 0)
        {
            container.eventHandler = interstice::Chain(std::move(container.eventHandler),
                interstice::MakeDropTargetHandler(dragDrop->payloadType,
                    [dd = dragDrop, id](uintptr_t payloadValue, interstice::ID /*sourceID*/) {
                        if (dd->onDrop) dd->onDrop(payloadValue, id);
                    },
                    [dd = dragDrop, id](uintptr_t payloadValue) {
                        return !dd->canDrop || dd->canDrop(payloadValue, id);
                    }));
        }
        RegisterElement(std::move(container), std::move(constraints));
        tabChainManager.RegisterTabTarget(id);

        // Capture this frame's viewport for next frame's column / row-window math.
        const auto containerRef = elementStorage.FindElementRefByID(id);
        if (containerRef.IsValid())
        {
            GridViewVirtualState* vs = &vstate;
            (void)elementStorage.AddPostLayout(containerRef,
                [vs](interstice::PostLayoutContext& plctx)
                {
                    const auto inner = plctx.ElementInnerRect();
                    vs->lastViewportW = inner.w;
                    vs->lastViewportH = inner.h;
                });
        }

        // Scope internal dynamic IDs under `id`: the emitted row count varies with scroll and
        // would otherwise shift the IDs of later siblings. The scrollbar ID comes first so it's stable.
        (void)idManager.PushParent(id);
        const interstice::ID scrollbarID = idManager.AllocateDynamicID("_grid_vscroll");

        // One ranged ID spans all items; an item's ID is the sub-ID at its dataset index,
        // so it's stable across frames and the index is recoverable from it.
        const interstice::ID itemsRangeID = (itemCount > 0)
            ? idManager.AllocateRangedID(static_cast<uint32_t>(itemCount), "_grid_items")
            : interstice::ID::Invalid();
        vstate.internal.itemsRangeID = itemsRangeID; // read by the container's F2 handler

        VERIFY(elementStorage.BeginChildren());

        // Transparent, non-interactive block reserving the height of the rows
        // scrolled off above / below the visible window.
        auto emitSpacer = [this, id](int height)
        {
            if (height <= 0) return;
            const interstice::ID spacerID = idManager.AllocateDynamicID("_grid_spacer");
            interstice::Element spacer{};
            spacer.id       = spacerID;
            spacer.styleRef = theme.emptyHorzStyleRef;
            spacer.flags    = interstice::ElementFlags::NoInput;
            RegisterElement(std::move(spacer),
                            interstice::LayoutOverride{}.Height(height).NoGrow().NoShrink());
        };

        emitSpacer(firstRow * rowStride);

        for (int r = firstRow; r < lastRow; ++r)
        {
            const interstice::ID rowID = idManager.AllocateDynamicID("_grid_row");
            interstice::Element row{};
            row.id       = rowID;
            row.styleRef = rowStyle;
            RegisterElement(std::move(row),
                            interstice::LayoutOverride{}.Height(rowStride).NoGrow().NoShrink());
            VERIFY(elementStorage.BeginChildren());
            const int base = r * cols;
            for (int c = 0; c < cols; ++c)
            {
                const int idx = base + c;
                if (idx >= itemCount) break;
                // This column's share of the row's leftover pixels.
                vstate.cellStretchedW = std::max(cellW, colBound(c + 1) - colBound(c));
                // GridViewItem reads nextItemIndex as the dataset index, so
                // per-item selection and drag payloads map to the real index.
                m_gridViewState.nextItemIndex = idx;
                const interstice::ID itemID = idManager.GetSubID(itemsRangeID, static_cast<uint32_t>(idx));
                generateItem(idx, itemID);
            }
            VERIFY(elementStorage.EndChildren());
        }

        emitSpacer((totalRows - lastRow) * rowStride);

        // Vertical scrollbar overlay — self-hides when there's no overflow.
        VerticalScrollbar(scrollbarID, id);

        VERIFY(elementStorage.EndChildren());
        (void)idManager.PopParent();
        m_gridViewState = {};
    }
}