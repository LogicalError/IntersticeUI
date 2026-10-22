#include <gui_controls/gui_application.h>
#include <gui_controls/gui_tree_utils.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace GUI
{


    // ---------------------------------------------------------------------------
    // Tree/List shared helpers
    // ---------------------------------------------------------------------------

    bool GUIApplication::IsTreeItemSelected(GUI::ID id, int itemIndex) const
    {
        if (m_treeState.multiSelect)
            return m_treeState.multiSelect->selectedIDs.count(id) > 0;
        if (m_treeState.selectedID)
            return *m_treeState.selectedID == id;
        if (m_treeState.selectedIndex)
            return *m_treeState.selectedIndex == itemIndex;
        return false;
    }


    GUI::TextID GUIApplication::TreeItemRegisterText(GUI::ID id, std::string_view text, IconID icon, StyleRef style, GUI::ElementState state)
    {
        if (icon != IconID::Invalid)
        {
            GUI::ContentRun runs[] = {
                { .type = GUI::ContentRun::Icon, .iconID = icon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                { .type = GUI::ContentRun::Text, .text = text }
            };
            return textSystem->RegisterRichText(id, runs, style, state);
        }
        return textSystem->RegisterText(id, text, style, state);
    }



    // ---- Shared helpers for tree rows ----

    // Row drag/drop handler: advances the drag on MouseMove, tracks the drop zone on
    // DragEnter/DragOver, fires dd->onReorder on Drop. MouseDown is left to the caller,
    // which primes the drag alongside selection (Chain stops at its `return true`).
    //
    // Zones: branches use thirds (Before/Inside/After), leaves halves (Before/After).
    GUI::EventHandler GUIApplication::MakeTreeRowDragDropHandler(GUI::ID rowID, bool rowHasChildren, ItemDragDropConfig* dd, bool* isOpen)
    {
        return [this, rowID, rowHasChildren, dd, isOpen](GUI::ElementEvent&& event) -> bool
        {
            if (!dd || dd->payloadType == 0) return false;

            auto computeZone = [&](const GUI::IntRect& rect) -> TreeDropPosition
            {
                if (dd->dropInsideOnly) return TreeDropPosition::InsideLast;
                // rect is layout-space — convert cursor through any ancestor
                // contentTransform first (e.g. a node-view canvas).
                const int32_t cursorY = event.LocalCursor(elementStorage).y;
                int relY = cursorY - rect.position.y;
                int h    = rect.size.y;
                if (h <= 0) return TreeDropPosition::None;
                if (rowHasChildren)
                {
                    // Branch zones (thirds):
                    //   top    → Before
                    //   middle → InsideLast (append child)
                    //   bottom → Inside (prepend child) when open, After when closed
                    const bool openBranch = isOpen && *isOpen;
                    if (relY <  h / 3)       return TreeDropPosition::Before;
                    if (relY < (2 * h) / 3)  return TreeDropPosition::InsideLast;
                    return openBranch ? TreeDropPosition::Inside
                                      : TreeDropPosition::After;
                }
                else
                {
                    if (relY < h / 2)        return TreeDropPosition::Before;
                    return TreeDropPosition::After;
                }
            };

            switch (event.type)
            {
                case GUI::EventType::MouseMove:
                {
                    if ((event.state.buttons & GUI::MouseButtons::Left) == GUI::MouseButtons::None) break;
                    guiStateManager.Drag().UpdateFromInput(event.state);
                    return guiStateManager.Drag().GetDraggedID() == rowID;
                }
                case GUI::EventType::DragEnter:
                case GUI::EventType::DragOver:
                {
                    if (event.state.dragPayloadType != dd->payloadType) return false;
                    if (!event.rect) return true;
                    // No indicator on the node being dragged itself, nor on
                    // rows that refuse this payload (canDrop).
                    if (event.state.dragSourceID == rowID
                        || (dd->canDrop && !dd->canDrop(event.state.dragPayloadValue, rowID))) {
                        if (m_treeDropState.targetRowID == rowID) m_treeDropState = {};
                        return true;
                    }
                    m_treeDropState.targetRowID  = rowID;
                    m_treeDropState.position     = computeZone(event.rect->rect);
                    return true;
                }
                case GUI::EventType::DragOut:
                {
                    if (event.state.dragPayloadType != dd->payloadType) return false;
                    if (m_treeDropState.targetRowID == rowID) m_treeDropState = {};
                    return true;
                }
                case GUI::EventType::Drop:
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
        };
    }

    // One AddCustomDraw for both the guide line and the drop indicator, so both get the
    // row's overflow:visible clip (AddCustomDrawAfter's tighter clip cuts off edge lines).
    void GUIApplication::TreeItemRowCustomDraw(const GUI::ElementRef& rowRef, bool hasArrow, GUI::ID rowID)
    {
        bool drawGuideLine = (m_treeState.depth > 0);

        [[maybe_unused]]
        auto result =
        elementStorage.AddCustomDraw(rowRef,
            [this, hasArrow, rowID, drawGuideLine,
             lineColor      = theme.treeGuideLineStyle,
             indicatorColor = theme.treeDropIndicatorStyle]
            (GUI::CustomDrawContext& ctx)
            {
                // Span the outer rect so lines are continuous between rows. Coords are
                // inner-rect-local, so the outer top is outer.y - inner.y.
                const auto outer = ctx.ElementRect();
                const auto inner = ctx.ElementInnerRect();
                const int32_t w    = outer.w;
                const int32_t h    = outer.h;
                const int32_t topY = outer.y - inner.y;
                const int32_t left = outer.x - inner.x;

                // -------- Tree guide line (indent gutter) --------
                if (drawGuideLine)
                {
                    // The tree indent sits halfway into the PARENT's left padding, which isn't
                    // derivable from this element's own rect — look up the parent.
                    GUI::ElementRef parentRef       = ctx.GetParentRef(ctx.Ref());
                    GUI::IntRect    parentRect      = ctx.GetElementRect(parentRef);
                    GUI::IntRect    parentInnerRect = ctx.GetInnerRect(parentRef);
                    if (parentRect.w > 0)
                    {
                        int32_t leftPadding = parentInnerRect.x - parentRect.x;
                        int32_t lineX   = left - leftPadding / 2;  // left of row's left edge
                        int32_t centerY = topY + h / 2;

                        // Last sibling: vertical line stops at center. Otherwise extends to bottom.
                        GUI::ElementRef nextSibling = ctx.GetNextSiblingRef(ctx.Ref());
                        int32_t bottom = nextSibling.IsValid() ? topY + h : centerY;

                        int32_t tickRight = left + (hasArrow ? 0 : styleManager.Px(10));

                        GUI::LineStyle style { .colorStyleIndex = lineColor, .lineWidth = styleManager.Px(1.f) };
                        ctx.RenderLine({ lineX, topY },    { lineX,     bottom  }, style);
                        ctx.RenderLine({ lineX, centerY }, { tickRight, centerY }, style);
                    }
                }

                // -------- Drop indicator (when this is the current drop target) --------
                // A cancelled drag fires neither Drop nor DragOut; gate on the live drag.
                if (guiStateManager.Drag().IsDragging()
                    && m_treeDropState.targetRowID == rowID
                    && m_treeDropState.position != TreeDropPosition::None)
                {
                    const int32_t kIndent = styleManager.Px(16);  // one visual indent level
                    const int32_t thick   = styleManager.Px(3);
                    const int32_t half    = thick / 2;         // insets that keep the line inside the row
                    GUI::LineStyle indStyle { .colorStyleIndex = indicatorColor, .lineWidth = float(thick) };

                    // InsideLast → rectangle around the row (drop as last child).
                    if (m_treeDropState.position == TreeDropPosition::InsideLast)
                    {
                        const int32_t leftX  = left;
                        const int32_t rightX = left + w - 1;
                        const int32_t top    = topY + half;
                        const int32_t bot    = topY + h - 1 - half;
                        ctx.RenderLine({ leftX,  top }, { rightX, top }, indStyle);
                        ctx.RenderLine({ leftX,  bot }, { rightX, bot }, indStyle);
                        ctx.RenderLine({ leftX,  top }, { leftX,  bot }, indStyle);
                        ctx.RenderLine({ rightX, top }, { rightX, bot }, indStyle);
                        return;
                    }

                    int32_t lineLeft  = left;
                    int32_t lineRight = left + w;
                    int32_t y;

                    switch (m_treeDropState.position)
                    {
                        case TreeDropPosition::Before:
                            y = topY + half;
                            break;
                        case TreeDropPosition::After:
                            y = topY + h - 1 - half;
                            break;
                        case TreeDropPosition::Inside:
                            // Indent to signal "becomes first child" of this row.
                            lineLeft += kIndent;
                            y = topY + h - 1 - half;
                            break;
                        default: return;
                    }

                    ctx.RenderLine({ lineLeft, y }, { lineRight, y }, indStyle);
                }
            });
    }

    bool GUIApplication::TreeItemRenameCheck(GUI::ID bodyID, std::string* renameBuffer, StyleRef textStyle)
    {
        bool modified = false;
        guiStateManager.Interaction().SetFocusID(bodyID);
        guiStateManager.TextEditorFocus().Set(bodyID);

        // Escape cancels the rename; everything else goes to the text box handler.
        GUI::EventHandler textHandler = MakeTextBoxHandler(guiStateManager);
        GUI::EventHandler editHandler =
            [this, inner = std::move(textHandler)](GUI::ElementEvent&& event) -> bool
            {
                if (event.type == GUI::EventType::KeyDown &&
                    event.state.lastKey == GUI::KeyCode::Escape)
                {
                    (*m_renameSub).Cancel();
                    return true;
                }
                return inner ? inner(std::move(event)) : false;
            };

        RegisterControl(GUI::Element{ .id = bodyID, .eventHandler = std::move(editHandler), .styleRef = textStyle },
            [this, &modified, renameBuffer](GUI::Element& element)
            {
                return textSystem->RegisterEditableText(element.id, *renameBuffer, GUI::TextEditBehavior::None, element.styleRef, element.state, modified);
            }, GUI::LayoutOverride::Auto());

        // Skip commit check on first frame — SetActiveTextEditor(Invalid) from a
        // previous deactivation leaves a stale pending_text_committed.
        if ((*m_renameSub).firstFrame)
            (*m_renameSub).firstFrame = false;
        else if (modified)
            (*m_renameSub).Cancel();

        return modified;
    }

    // ---------------------------------------------------------------------------
    // ListView
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginListView(ID id, int* selectedIndex, int itemCount,
                                    ItemDragDropConfig* dragDrop,
                                    StyleRef style, LayoutOverride&& constraints)
    {
        m_treeState = {};
        m_treeState.containerID    = id;
        m_treeState.selectedIndex  = selectedIndex;
        m_treeState.totalItemCount = itemCount;
        m_treeState.dragDrop       = dragDrop;

        GUI::Element container{};
        container.id       = id;
        container.styleRef = style;
        container.state    = guiStateManager.GetCurrentState(id);
        container.eventHandler = [this, selectedIndex, itemCount](ElementEvent&& event) -> bool
        {
            switch (event.type)
            {
                case GUI::EventType::MouseDown:
                    if (!event.state.LeftPressed()) break;
                    guiStateManager.Interaction().SetFocusID(event.id);
                    guiStateManager.TextEditorFocus().Clear();
                    return true;
                case GUI::EventType::KeyDown:
                {
                    if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) == ElementState::None)
                        break;
                    if (!selectedIndex) break;
                    switch (event.state.lastKey)
                    {
                        case GUI::KeyCode::Up:   *selectedIndex = std::max(0, *selectedIndex - 1); return true;
                        case GUI::KeyCode::Down: *selectedIndex = std::min(itemCount - 1, *selectedIndex + 1); return true;
                        default: break;
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

    void GUIApplication::EndListView()
    {
        VERIFY(elementStorage.EndChildren());
        m_treeState = {};
    }

    bool GUIApplication::ListItem(ID id, std::string_view text, IconID icon, StyleRef style, LayoutOverride&& constraints)
    {
        int itemIndex = m_treeState.nextItemIndex++;
        bool selected = IsTreeItemSelected(id, itemIndex);

        // Capture user-owned pointers; m_treeState is gone by the time handlers run.
        GUI::ID containerID    = m_treeState.containerID;
        int* selectedIndex     = m_treeState.selectedIndex;
        ItemDragDropConfig* dd = m_treeState.dragDrop;
        const bool dragEnabled = (dd && dd->payloadType != 0);

        // Record this item's index so end-of-frame onReorder lambdas can resolve target IDs.
        // This map is on the Application (not m_treeState) so it survives EndListView.
        if (dd)
            m_listItemIndexByID[id] = itemIndex;

        if (selected) guiStateManager.Construction().PushSelected();

        // ---------------------------------------------------------------------
        // Non-draggable path: single leaf element.
        // ---------------------------------------------------------------------
        if (!dragEnabled)
        {
            if (style == StyleRef{}) style = theme.listItemStyleRef;

            GUI::Element item{};
            item.id       = id;
            item.state    = guiStateManager.GetCurrentState(id);
            item.styleRef = style;
            item.textID   = TreeItemRegisterText(id, text, icon, style, item.state);
            item.eventHandler = [this, containerID, id, itemIndex, selectedIndex](ElementEvent&& event) -> bool
            {
                if (event.type == GUI::EventType::MouseDown && event.state.LeftPressed())
                {
                    if (selectedIndex) *selectedIndex = itemIndex;
                    auto& scrollManagement = guiStateManager.GetScrollManagement(); 
                    scrollManagement.MakeElementVisibleHierarchically(elementStorage, event.elementRef);
                    guiStateManager.Interaction().SetFocusID(containerID);
                    guiStateManager.TextEditorFocus().Clear();
                    guiStateManager.Interaction().SetClicked(id);
                    return true;
                }
                return false;
            };

            auto addRes = elementStorage.Add(std::move(item), std::move(constraints));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));

            if (selected) guiStateManager.Construction().PopSelected();
            return guiStateManager.Interaction().HasBeenClicked(id);
        }

        // ---------------------------------------------------------------------
        // Drag-enabled path: row container + drag knob + body layout.
        //
        // Structure:
        //   [row  listItemRowStyleRef, handles drop-target + selection fallback]
        //     ├── [knob listItemDragKnobStyleRef, MouseDown primes drag, MouseMove promotes drag]
        //     └── [body listItemBodyStyleRef, textID, handles selection click]
        //
        // Selection background and drop target are on the row; child drag events bubble up to it.
        // ---------------------------------------------------------------------

        // 1) Outer row container.
        GUI::Element row{};
        row.id       = id;
        row.state    = guiStateManager.GetCurrentState(id);
        row.styleRef = theme.listItemRowStyleRef;
        // Row handler: only DragDrop events matter here (from children bubbling up).
        // MouseDown on the row itself (margin area) also just selects.
        row.eventHandler = [this, containerID, id, itemIndex, selectedIndex](ElementEvent&& event) -> bool
        {
            if (event.type == GUI::EventType::MouseDown && event.state.LeftPressed())
            {
                if (selectedIndex) *selectedIndex = itemIndex;
                auto& scrollManagement = guiStateManager.GetScrollManagement(); 
                scrollManagement.MakeElementVisibleHierarchically(elementStorage, event.elementRef);
                guiStateManager.Interaction().SetFocusID(containerID);
                guiStateManager.TextEditorFocus().Clear();
                guiStateManager.Interaction().SetClicked(id);
                return true;
            }
            return false;
        };
        row.eventHandler = GUI::Chain(std::move(row.eventHandler),
            MakeTreeRowDragDropHandler(id, /*rowHasChildren=*/false, dd));

        auto rowRes = elementStorage.Add(std::move(row), std::move(constraints));
        VERIFY(rowRes.status);
        VERIFY(windowRegistry.Add(rowRes.ref));

        // Drop indicator line for this row when it's the current drop target.
        TreeItemRowCustomDraw(rowRes.ref, /*hasArrow=*/false, id);

        VERIFY(elementStorage.BeginChildren());

        // 2) Drag knob child (leftmost). It captures the mouse on MouseDown, so the drag
        //    must be advanced from its own MouseMove.
        {
            GUI::ID knobID = idManager.AllocateDynamicID("list_drag_knob");
            GUI::Element knob{};
            knob.id       = knobID;
            knob.state    = guiStateManager.GetCurrentState(knobID);
            knob.styleRef = theme.listItemDragKnobVertStyleRef;
            knob.eventHandler = [this, rowID = id, itemIndex, selectedIndex, dd](ElementEvent&& event) -> bool
            {
                switch (event.type)
                {
                    case GUI::EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed()) break;
                        // Selection follows the click so the correct item is highlighted.
                        if (selectedIndex) *selectedIndex = itemIndex;
                        // Focus moves on MouseUp, not here, so grabbing the thumb doesn't
                        // interrupt editing another control.
                        guiStateManager.Interaction().SetClicked(rowID);
                        // Prime the drag. Payload = source index (via FindListItemIndex).
                        uintptr_t payloadValue = dd->getPayload ? dd->getPayload(rowID) : 0;
                        guiStateManager.Drag().Begin(rowID, dd->payloadType, payloadValue,
                                                  event.state.screenPos, std::any(),
                                                  dd->visualizer);
                        return true;
                    }
                    case GUI::EventType::MouseMove:
                    {
                        if ((event.state.buttons & GUI::MouseButtons::Left) == GUI::MouseButtons::None) break;
                        guiStateManager.Drag().UpdateFromInput(event.state);
                        return guiStateManager.Drag().GetDraggedID() == rowID;
                    }
                    case GUI::EventType::MouseUp:
                    {
                        if (!event.state.LeftReleased()) break;
                        // Focus the row's first control, if any; otherwise leave focus alone.
                        const ElementRef rowRef = elementStorage.GetParentRef(event.elementRef);
                        if (rowRef.IsValid())
                        {
                            const ID firstControl =
                                LabelBinding::FindFirstControlInSubtree(elementStorage, rowRef);
                            // Skip the knob itself in case it is ever flagged as a control.
                            if (firstControl != ID::Invalid() && firstControl != event.id)
                            {
                                guiStateManager.Interaction().SetFocusID(firstControl);
                                guiStateManager.TextEditorFocus().Clear();
                            }
                        }
                        // Don't consume — the drag system also wants to see MouseUp.
                        break;
                    }
                    default: break;
                }
                return false;
            };

            // Grip icon, sized by the knob style's -interstice-icon-size.
            knob.textID  = textSystem->RegisterText(knobID, theme.dragHandleIcon, "", knob.styleRef, knob.state);
            auto knobRes = elementStorage.Add(std::move(knob), LayoutOverride{});
            VERIFY(knobRes.status);
            VERIFY(windowRegistry.Add(knobRes.ref));
        }

        // 3) Body child (text + icon, flex-grows to fill the remaining width).
        {
            GUI::ID bodyID = idManager.AllocateDynamicID("list_item_body");
            GUI::Element body{};
            body.id       = bodyID;
            body.state    = guiStateManager.GetCurrentState(bodyID);
            body.styleRef = theme.listItemBodyStyleRef;
            body.textID   = TreeItemRegisterText(bodyID, text, icon, theme.listItemBodyStyleRef, body.state);
            body.eventHandler = [this, rowID = id, itemIndex, selectedIndex, containerID](ElementEvent&& event) -> bool
            {
                if (event.type == GUI::EventType::MouseDown && event.state.LeftPressed())
                {
                    if (selectedIndex) *selectedIndex = itemIndex;
                    auto& scrollManagement = guiStateManager.GetScrollManagement(); 
                    scrollManagement.MakeElementVisibleHierarchically(elementStorage, event.elementRef);
                    guiStateManager.Interaction().SetFocusID(containerID);
                    guiStateManager.TextEditorFocus().Clear();
                    guiStateManager.Interaction().SetClicked(rowID);
                    return true;
                }
                return false;
            };

            auto bodyRes = elementStorage.Add(std::move(body), LayoutOverride::Auto());
            VERIFY(bodyRes.status);
            VERIFY(windowRegistry.Add(bodyRes.ref));
        }

        VERIFY(elementStorage.EndChildren());

        if (selected) guiStateManager.Construction().PopSelected();
        return guiStateManager.Interaction().HasBeenClicked(id);
    }

    int GUIApplication::FindListItemIndex(GUI::ID id) const
    {
        auto it = m_listItemIndexByID.find(id);
        return (it != m_listItemIndexByID.end()) ? it->second : -1;
    }


    // -----------------------------------------------------------------------
    // VirtualTreeView — flat-data, virtualized hierarchical view backed by a
    // single-column VirtualTable. The caller owns the row vector; mutations
    // call RebuildTreeViewIndex to refresh derived fields. Selection,
    // scroll, and drag-and-drop live on the supplied TableState.
    // -----------------------------------------------------------------------

    void GUIApplication::RebuildTreeViewIndex(std::vector<TreeViewRow>& rows)
    {
        TreeUtils::RebuildTreeIndex(rows);
    }

    void GUIApplication::WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows,
                                                std::vector<int>& visibleIdx)
    {
        visibleIdx.clear();
        for (size_t i = 0; i < rows.size(); )
        {
            visibleIdx.push_back(int(i));
            if (rows[i].HasChildren() && !rows[i].isOpen)
                i += 1 + rows[i].skipCount;
            else
                i += 1;
        }
    }

    // Row IDs are visible positions, so when visibility changes the selection
    // is carried over to the same flat rows. A single selection hidden by a
    // collapse moves to its nearest visible ancestor; hidden multi-selection
    // entries are dropped.
    static void RemapTreeSelection(const std::vector<TreeViewRow>& rows,
                                   const std::vector<int>& visibleIdx,
                                   TableState& state)
    {
        const std::vector<int>& prevVisible = state._treeVisibleIdx;
        std::vector<int> flatToVisible(rows.size(), -1);
        for (size_t v = 0; v < visibleIdx.size(); ++v)
            flatToVisible[size_t(visibleIdx[v])] = int(v);

        auto visibleOrAncestor = [&](int flat) -> int
        {
            int depth = rows[size_t(flat)].depth;
            for (int j = flat; j >= 0; --j)
            {
                if (j != flat && rows[size_t(j)].depth >= depth) continue;
                if (flatToVisible[size_t(j)] >= 0) return flatToVisible[size_t(j)];
                depth = rows[size_t(j)].depth;
            }
            return -1;
        };

        auto remap = [&](GUI::ID id, bool toAncestor) -> GUI::ID
        {
            if (id.GetMode() != GUI::IDMode::SubRange ||
                id.value.subRangeParts.parentHashValue != state._rowsRangeID.value.rangedParts.hashValue)
                return id;
            const size_t prevVis = size_t(id.value.subRangeParts.index);
            if (prevVis >= prevVisible.size()) return GUI::ID::Invalid();
            const int flat = prevVisible[prevVis];
            if (flat < 0 || flat >= int(rows.size())) return GUI::ID::Invalid();
            const int vis = toAncestor ? visibleOrAncestor(flat) : flatToVisible[size_t(flat)];
            return vis >= 0 ? state.RowIDInRange(vis) : GUI::ID::Invalid();
        };

        state.selectedRowID = remap(state.selectedRowID, true);
        if (state.multiSelect)
        {
            std::unordered_set<GUI::ID> remapped;
            for (GUI::ID sel : state.multiSelect->selectedIDs)
                if (GUI::ID moved = remap(sel, false); moved != GUI::ID::Invalid())
                    remapped.insert(moved);
            state.multiSelect->selectedIDs = std::move(remapped);
            state.multiSelect->lastClickedID = remap(state.multiSelect->lastClickedID, true);
        }
    }

    void GUIApplication::VirtualTreeView(GUI::ID id,
                                         std::vector<TreeViewRow>& rows,
                                         std::vector<int>& visibleIdx,
                                         TableState& state,
                                         StyleRef style,
                                         LayoutOverride&& constraints,
                                         std::string_view header)
    {
        // Rebuild the visible-row list each frame from the expand/collapse state.
        WalkTreeViewVisibility(rows, visibleIdx);
        if (state._rowsRangeID != GUI::ID::Invalid() && visibleIdx != state._treeVisibleIdx)
            RemapTreeSelection(rows, visibleIdx, state);
        state._treeVisibleIdx = visibleIdx;

        // Reserve the arrow column only when some row can expand (avoids a stray indent on flat trees).
        bool anyExpandable = false;
        for (const TreeViewRow& r : rows)
            if (r.HasChildren()) { anyExpandable = true; break; }

        VirtualTableColumn cols[] = {
            {
                { .header = header },
                [this, &rows, &visibleIdx, anyExpandable](int rowIdx) {
                    if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                    TreeViewRow& r = rows[visibleIdx[rowIdx]];
                    TreeCell(r.name, r.depth,
                             r.HasChildren() ? &r.isOpen : nullptr,
                             r.icon,
                             r.ancestorLineMask,
                             r.isLastSibling,
                             r.renameBuffer,
                             /*reserveExpander*/anyExpandable);
                }
            }
        };

        const StyleRef tableStyle = (style != StyleRef{}) ? style : theme.treeViewStyleRef;

        VirtualTable(id, state,
                     std::span<VirtualTableColumn>(cols, 1),
                     int(visibleIdx.size()), 0, tableStyle,
                     std::move(constraints), /*showHeaders*/!header.empty());
    }

}
