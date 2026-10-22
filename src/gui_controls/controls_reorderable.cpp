#include <gui_controls/gui_application.h>
#include <gui_controls/gui_label_binding.h>

#include <algorithm>

namespace GUI
{


    // ---------------------------------------------------------------------------
    // Reorderable Group
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped,
        std::function<void(int, int)> onReorder, StyleRef style, LayoutOverride&& constraints,
        std::function<void(int, GUI::int2)> onDragOut,
        std::function<void(int, GUI::int2)> dragVisualizer)
    {
        m_reorderable->Construction() = {};
        m_reorderable->Construction().groupID = id;
        m_reorderable->Construction().direction = direction;
        m_reorderable->Construction().isWrapped = isWrapped;
        m_reorderable->Construction().onReorder = std::move(onReorder);
        m_reorderable->Construction().onDragOut = std::move(onDragOut);
        m_reorderable->Construction().dragVisualizer = std::move(dragVisualizer);

        GUI::Element container
        {
            .id       = id,
            .styleRef = style,
        };
        container.state = guiStateManager.GetCurrentState(id);
        RegisterElement(std::move(container), std::move(constraints));
        m_reorderable->Construction().groupRef = elementStorage.FindElementRefByID(id);
        VERIFY(elementStorage.BeginChildren());
        return true;
    }

    void GUIApplication::BeginReorderableItem(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        int itemIndex = m_reorderable->Construction().nextItemIndex++;

        // Capture by value: construction state is reset in EndReorderableGroup, before input dispatch.
        GUI::ID groupID = m_reorderable->Construction().groupID;
        GUI::ElementRef groupRef = m_reorderable->Construction().groupRef;
        auto onReorder = m_reorderable->Construction().onReorder;
        auto onDragOut = m_reorderable->Construction().onDragOut;
        ReorderDirection direction = m_reorderable->Construction().direction;
        bool isWrapped = m_reorderable->Construction().isWrapped;

        GUI::Element item
        {
            .id       = id,
            .styleRef = style,
        };
        item.state = guiStateManager.GetCurrentState(id);
        item.eventHandler = [this, groupID, id, itemIndex, direction, isWrapped, groupRef, onReorder, onDragOut]
            (GUI::ElementEvent&& event) -> bool
        {
            int mainAxis = (direction == ReorderDirection::Horizontal) ? 0 : 1;
            int crossAxis = 1 - mainAxis;
            auto& drag = m_reorderable->Drag();

            switch (event.type)
            {
                case GUI::EventType::MouseDown:
                {
                    if (!event.state.LeftPressed()) break;

                    // Snapshot all sibling rects
                    auto childRange = elementStorage.GetChildRange(groupRef);
                    if (childRange.Empty()) break;

                    drag.isDragging = true;
                    drag.dragActivated = false;
                    drag.isWrapped = isWrapped;
                    drag.groupID = groupID;
                    drag.draggedIndex = itemIndex;
                    drag.insertionIndex = itemIndex;
                    drag.itemCount = static_cast<int>(childRange.Size());
                    drag.dragStartMousePos = event.state.screenPos;
                    drag.dragOffset = {0, 0};
                    drag.direction = direction;

                    // Snapshot group rect for drag-out detection
                    auto* groupRectPtr = elementStorage.GetElementRectPtr(groupRef);
                    if (groupRectPtr) drag.groupRect = groupRectPtr->rect;

                    drag.originalRects.clear();
                    drag.originalRects.reserve(childRange.Size());
                    drag.originalIDs.clear();
                    drag.originalIDs.reserve(childRange.Size());
                    uint32_t childDepth = groupRef.depth + 1;
                    for (uint32_t i = childRange.begin; i < childRange.end; i++)
                    {
                        GUI::ElementRef childRef{childDepth, i};
                        auto* rect = elementStorage.GetElementRectPtr(childRef);
                        drag.originalRects.push_back(rect ? rect->rect : GUI::IntRect{});
                        const auto& childElem = elementStorage.GetElementUnsafe(childRef);
                        drag.originalIDs.push_back(childElem.id);
                    }

                    guiStateManager.Interaction().SetCaptureID(id);
                    return true;
                }

                case GUI::EventType::MouseMove:
                {
                    if (!drag.isDragging || drag.draggedIndex != itemIndex) break;
                    if (!event.state.LeftDown()) break;

                    // Update drag offset
                    GUI::int2 delta = event.state.screenPos - drag.dragStartMousePos;

                    // Drag threshold: ignore tiny movements so an accidental
                    // cursor jitter during a click doesn't start reordering.
                    if (!drag.dragActivated)
                    {
                        const int threshold = styleManager.Px(kDragStartDistance);
                        if (delta.x * delta.x + delta.y * delta.y < threshold * threshold)
                            return true;
                        drag.dragActivated = true;
                    }

                    drag.dragOffset = delta;

                    // Cursor in layout space (like originalRects/groupRect), not screenPos,
                    // so an ancestor contentTransform doesn't skew the hit-test.
                    const GUI::int2 localMouse = event.LocalCursor(elementStorage);

                    // Outside the group with drag-out enabled: no in-place reorder.
                    if (onDragOut)
                    {
                        const GUI::IntRect& r = drag.groupRect;
                        if (localMouse.x < r.x || localMouse.x > r.x + r.w ||
                            localMouse.y < r.y || localMouse.y > r.y + r.h)
                        {
                            drag.insertionIndex = drag.draggedIndex;
                            return true;
                        }
                    }

                    int mouseMain = localMouse[mainAxis];
                    int count = static_cast<int>(drag.originalRects.size());
                    int from = drag.draggedIndex;
                    int newInsertionIndex = from;

                    if (isWrapped)
                    {
                        // Wrapped mode: find which row/column the mouse is in,
                        // then find insertion position within that row/column
                        int mouseCross = localMouse[crossAxis];

                        // Find the target row/column: the one whose cross-axis range contains the mouse
                        // Items on the same row share the same cross-axis position
                        int targetRowStart = -1;
                        int targetRowEnd = -1;
                        int prevCross = -1;
                        for (int i = 0; i < count; i++)
                        {
                            int itemCross = drag.originalRects[i].position[crossAxis];
                            int itemCrossSize = drag.originalRects[i].size[crossAxis];
                            if (itemCross != prevCross)
                            {
                                // New row/column starts
                                if (targetRowStart >= 0 && mouseCross < itemCross)
                                    break; // Mouse is in the previous row
                                targetRowStart = i;
                                prevCross = itemCross;
                            }
                            targetRowEnd = i;

                            // Check if mouse is within this row's cross-axis bounds
                            if (mouseCross >= itemCross && mouseCross < itemCross + itemCrossSize)
                            {
                                // Mouse is in this row — scan to end of row
                                for (int j = i + 1; j < count; j++)
                                {
                                    if (drag.originalRects[j].position[crossAxis] != prevCross)
                                        break;
                                    targetRowEnd = j;
                                }
                                break;
                            }
                        }

                        if (targetRowStart < 0) { targetRowStart = 0; targetRowEnd = count - 1; }

                        // The dragged item's new index = how many OTHER items end up before
                        // it: every item in earlier rows, plus the target row's items whose
                        // midpoint the cursor has passed.
                        newInsertionIndex = targetRowStart - (from < targetRowStart ? 1 : 0);
                        for (int i = targetRowStart; i <= targetRowEnd; i++)
                        {
                            if (i == from) continue;
                            int mid = drag.originalRects[i].position[mainAxis] + drag.originalRects[i].size[mainAxis] / 2;
                            if (mouseMain >= mid)
                                newInsertionIndex++;
                        }
                    }
                    else
                    {
                        // Single-line mode: edge-crossing logic
                        // Check items after dragged: did mouse cross their left edge?
                        for (int i = from + 1; i < count; i++)
                        {
                            int leftEdge = drag.originalRects[i].position[mainAxis];
                            if (mouseMain >= leftEdge)
                                newInsertionIndex = i;
                            else
                                break;
                        }
                        // Check items before dragged: did mouse cross their right edge?
                        for (int i = from - 1; i >= 0; i--)
                        {
                            int rightEdge = drag.originalRects[i].position[mainAxis]
                                          + drag.originalRects[i].size[mainAxis];
                            if (mouseMain < rightEdge)
                                newInsertionIndex = i;
                            else
                                break;
                        }
                    }

                    newInsertionIndex = std::clamp(newInsertionIndex, 0, count - 1);
                    drag.insertionIndex = newInsertionIndex;
                    return true;
                }

                case GUI::EventType::MouseUp:
                {
                    if (!drag.isDragging || drag.draggedIndex != itemIndex) break;

                    // Focus the first control of the row at slot `targetIndex`. Resolved via the
                    // MouseDown ID snapshot, not the current slot, since EndReorderableItem may
                    // have moved the dragged row; dynamic IDs are slot-stable across frames.
                    auto focusFirstControlAt = [&](int targetIndex)
                    {
                        if (targetIndex < 0 ||
                            static_cast<size_t>(targetIndex) >= drag.originalIDs.size())
                            return;
                        const GUI::ID rowID = drag.originalIDs[targetIndex];
                        const GUI::ElementRef rowRef = elementStorage.FindElementRefByID(rowID);
                        if (!rowRef.IsValid()) return;
                        const ID firstControl =
                            LabelBinding::FindFirstControlInSubtree(elementStorage, rowRef);
                        if (firstControl != ID::Invalid())
                        {
                            guiStateManager.Interaction().SetFocusID(firstControl);
                            guiStateManager.TextEditorFocus().Clear();
                        }
                    };

                    // Threshold never crossed → treat as a click, not a drag.
                    if (!drag.dragActivated)
                    {
                        guiStateManager.Interaction().ReleaseCaptureID();
                        // Plain click: focus this row's first control (before originalIDs is cleared).
                        focusFirstControlAt(itemIndex);
                        drag.isDragging = false;
                        drag.dragActivated = false;
                        drag.draggedIndex = -1;
                        drag.insertionIndex = -1;
                        drag.originalRects.clear();
                        drag.originalIDs.clear();
                        drag.dragOffset = {0, 0};
                        return true;
                    }

                    int from = drag.draggedIndex;
                    int to = drag.insertionIndex;

                    // Drag-out: released outside the group (plus a margin) on either axis.
                    // The release position lets the caller hit-test other containers.
                    if (onDragOut)
                    {
                        const GUI::int2 mouse  = event.state.screenPos;
                        const GUI::IntRect& r  = drag.groupRect;
                        const int dragOutThreshold = styleManager.Px(8);
                        const bool outsideX = mouse.x < r.x - dragOutThreshold
                                           || mouse.x > r.x + r.w + dragOutThreshold;
                        const bool outsideY = mouse.y < r.y - dragOutThreshold
                                           || mouse.y > r.y + r.h + dragOutThreshold;

                        if (outsideX || outsideY)
                        {
                            drag.isDragging = false;
                            drag.dragActivated = false;
                            drag.draggedIndex = -1;
                            drag.insertionIndex = -1;
                            drag.originalRects.clear();
                            drag.originalIDs.clear();
                            drag.dragOffset = {0, 0};
                            guiStateManager.Interaction().ReleaseCaptureID();
                            onDragOut(from, mouse);
                            return true;
                        }
                    }

                    guiStateManager.Interaction().ReleaseCaptureID();

                    // Focus must run before the cleanup below clears originalIDs.
                    if (from != to && onReorder)
                    {
                        onReorder(from, to);

                        // The callback reorders user data; next frame's dynamic IDs
                        // map to different content. Clear hover/transition state so
                        // visuals don't flash on the wrong slot.
                        transitionManager.Clear();

                        // Focus the first control of the slot the item landed in
                        // (slot-stable IDs; next frame it holds the moved content).
                        focusFirstControlAt(to);
                    }
                    else
                    {
                        // Dropped on its own slot: focus like a plain click.
                        focusFirstControlAt(itemIndex);
                    }

                    drag.isDragging = false;
                    drag.dragActivated = false;
                    drag.draggedIndex = -1;
                    drag.insertionIndex = -1;
                    drag.originalRects.clear();
                    drag.originalIDs.clear();
                    drag.dragOffset = {0, 0};

                    return true;
                }

                case GUI::EventType::KeyDown:
                {
                    if (drag.isDragging && drag.draggedIndex == itemIndex
                        && event.state.lastKey == GUI::KeyCode::Escape)
                    {
                        drag.isDragging = false;
                        drag.dragActivated = false;
                        drag.draggedIndex = -1;
                        drag.insertionIndex = -1;
                        drag.originalRects.clear();
                        drag.originalIDs.clear();
                        drag.dragOffset = {0, 0};
                        guiStateManager.Interaction().ReleaseCaptureID();
                        return true;
                    }
                    break;
                }

                default: break;
            }
            return false;
        };

        RegisterElement(std::move(item), std::move(constraints));

        // Track item ref for offset application in EndReorderableGroup
        auto itemRef = elementStorage.FindElementRefByID(id);
        m_reorderable->Construction().itemRefs.push_back(itemRef);

        VERIFY(elementStorage.BeginChildren());
    }

    void GUIApplication::EndReorderableItem()
    {
        VERIFY(elementStorage.EndChildren());

        auto& drag = m_reorderable->Drag();
        int itemIndex = m_reorderable->Construction().nextItemIndex - 1;

        if (drag.isDragging && drag.dragActivated && drag.groupID == m_reorderable->Construction().groupID)
        {
            int from = drag.draggedIndex;
            int to = drag.insertionIndex;

            if (from != to && itemIndex == std::max(from, to))
            {
                uint32_t childDepth = m_reorderable->Construction().groupRef.depth + 1;
                uint32_t rangeBegin = m_reorderable->Construction().itemRefs[0].index;
                ElementMove move;
                VERIFY(elementStorage.MoveChild(childDepth, rangeBegin,
                    static_cast<uint32_t>(from), static_cast<uint32_t>(to), &move));
                windowRegistry.ApplyElementMove(move);
                m_reorderable->Construction().reorderApplied = true;
            }
        }
    }

    void GUIApplication::EndReorderableGroup()
    {
        VERIFY(elementStorage.EndChildren());

        auto& drag = m_reorderable->Drag();
        // The item list can change under a drag (data edited mid-drag): only touch slots that still exist.
        const size_t itemCount = m_reorderable->Construction().itemRefs.size();
        if (drag.isDragging && drag.dragActivated && drag.groupID == m_reorderable->Construction().groupID
            && !drag.originalRects.empty() && itemCount > 0
            && size_t(drag.draggedIndex) < itemCount && size_t(drag.insertionIndex) < itemCount
            && size_t(drag.draggedIndex) < drag.originalRects.size() && size_t(drag.insertionIndex) < drag.originalRects.size())
        {
            int mainAxis = (drag.direction == ReorderDirection::Horizontal) ? 0 : 1;
            int from = drag.draggedIndex;
            int to = drag.insertionIndex;

            // Determine the dragged item's ref (at 'to' if reordered, 'from' if same position)
            uint32_t childDepth = m_reorderable->Construction().groupRef.depth + 1;
            uint32_t rangeBegin = m_reorderable->Construction().itemRefs[0].index;
            uint32_t draggedLocalIdx = m_reorderable->Construction().reorderApplied
                ? static_cast<uint32_t>(to) : static_cast<uint32_t>(from);
            GUI::ElementRef draggedRef{childDepth, rangeBegin + draggedLocalIdx};

            // Ghost hand-off checks both axes; the in-place cross-axis pull-out only needs the
            // cross axis (the main axis stays constrained to the group's siblings).
            int crossAxis = 1 - mainAxis;
            const GUI::int2 mouse = drag.dragStartMousePos + drag.dragOffset;
            const bool outsideCross = mouse[crossAxis] < drag.groupRect.position[crossAxis]
                                   || mouse[crossAxis] > drag.groupRect.position[crossAxis] + drag.groupRect.size[crossAxis];
            const bool outsideMain  = mouse[mainAxis]  < drag.groupRect.position[mainAxis]
                                   || mouse[mainAxis]  > drag.groupRect.position[mainAxis]  + drag.groupRect.size[mainAxis];
            const bool fullyOutside = outsideCross || outsideMain;
            const bool outsideGroup = outsideCross;

            // Outside the group with a visualizer: the ghost draws the drag and the
            // in-place item keeps its natural position (no offsets applied).
            const bool useGhost = fullyOutside && m_reorderable->Construction().dragVisualizer != nullptr;
            if (useGhost)
            {
                m_reorderable->Construction().dragVisualizer(from, mouse);
            }

            if (!useGhost)
            {
                // For groups without drag-out, clamp the main-axis offset so the
                // dragged item stays within the group bounds.
                int mainDragOffset = drag.dragOffset[mainAxis];
                if (!m_reorderable->Construction().onDragOut)
                {
                    int origPos  = drag.originalRects[from].position[mainAxis];
                    int origSize = drag.originalRects[from].size[mainAxis];
                    int groupMin = drag.groupRect.position[mainAxis];
                    int groupMax = drag.groupRect.position[mainAxis] + drag.groupRect.size[mainAxis];
                    mainDragOffset = std::clamp(mainDragOffset, groupMin - origPos, groupMax - origSize - origPos);
                }

                // Cross-axis movement only when the group wraps or supports drag-out;
                // plain lists (e.g. list/dictionary Field()) stay locked to their axis.
                const bool canLeaveAxis = m_reorderable->Construction().isWrapped
                                       || m_reorderable->Construction().onDragOut     != nullptr
                                       || m_reorderable->Construction().dragVisualizer != nullptr;

                if (m_reorderable->Construction().reorderApplied)
                {
                    int draggedSize = drag.originalRects[from].size[mainAxis];

                    // Compute where the layout will place the dragged item at position 'to'
                    int flowPos;
                    if (from > to)
                        flowPos = drag.originalRects[to].position[mainAxis];
                    else
                        flowPos = drag.originalRects[to].position[mainAxis]
                                + drag.originalRects[to].size[mainAxis] - draggedSize;

                    // Offset = desired position (original + mouse delta) - layout flow position
                    int16_t mainOffset = static_cast<int16_t>(
                        drag.originalRects[from].position[mainAxis] + mainDragOffset - flowPos);
                    int16_t crossOffset = (outsideGroup && canLeaveAxis) ? static_cast<int16_t>(drag.dragOffset[crossAxis]) : static_cast<int16_t>(0);
                    int16_t offsetX = (mainAxis == 0) ? mainOffset : crossOffset;
                    int16_t offsetY = (mainAxis == 1) ? mainOffset : crossOffset;

                    auto& elemRect = elementStorage.GetElementRectUnsafe(draggedRef);
                    auto pc = GUI::LayoutOverride{}.Relative(offsetX, offsetY);
                    elemRect.constraints.x = pc.x;
                    elemRect.constraints.y = pc.y;
                }
                else
                {
                    // from == to: just apply drag offset directly
                    int16_t mainOffset = static_cast<int16_t>(mainDragOffset);
                    int16_t crossOffset = (outsideGroup && canLeaveAxis) ? static_cast<int16_t>(drag.dragOffset[crossAxis]) : static_cast<int16_t>(0);
                    int16_t offsetX = (mainAxis == 0) ? mainOffset : crossOffset;
                    int16_t offsetY = (mainAxis == 1) ? mainOffset : crossOffset;

                    auto& elemRect = elementStorage.GetElementRectUnsafe(draggedRef);
                    auto pc = GUI::LayoutOverride{}.Relative(offsetX, offsetY);
                    elemRect.constraints.x = pc.x;
                    elemRect.constraints.y = pc.y;
                }

                // Render dragged item on top
                windowRegistry.MoveSubtreeToEnd(draggedRef);
            }
        }

        m_reorderable->Construction() = {};
    }

    // ---------------------------------------------------------------------------
    // Reorder drag handle
    // ---------------------------------------------------------------------------

    // Drag-handle grip with no handler of its own, so input bubbles to the enclosing
    // reorderable item. Icon size and cursor come from the -horz/-vert knob style.
    void GUIApplication::EmitDragHandle()
    {
        GUI::ID handleID = idManager.AllocateDynamicID("reorder_drag_handle");
        GUI::Element handle{};
        handle.id       = handleID;
        handle.state    = guiStateManager.GetCurrentState(handleID);
        handle.styleRef = (m_reorderable->Construction().direction == ReorderDirection::Horizontal)
                              ? theme.listItemDragKnobHorzStyleRef
                              : theme.listItemDragKnobVertStyleRef;
        handle.textID   = textSystem->RegisterText(handleID, theme.dragHandleIcon, "", handle.styleRef, handle.state);
        auto res = elementStorage.Add(std::move(handle), LayoutOverride{});
        VERIFY(res.status);
        VERIFY(windowRegistry.Add(res.ref));
    }

}