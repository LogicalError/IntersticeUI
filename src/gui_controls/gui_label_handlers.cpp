#include <gui_core/text/gui_text_system.h>

#include <gui_controls/gui_default_handlers.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/style/gui_style_manager.h>

#include <chrono>
#include <cmath>
#include <memory>

// Label drag / group-label event handlers.

namespace tachyon
{
    static bool DispatchToNextSibling(const ElementStorage& elementStorage, ID labelID, EventType type, const InputState& state)
    {
        const ElementRef labelRef = elementStorage.FindElementRefByID(labelID);
        if (!labelRef.IsValid()) return false;

        const ElementRef parentRef = elementStorage.GetParentRef(labelRef);
        if (!parentRef.IsValid()) return false;

        const IndexRange siblingRange = elementStorage.GetChildRange(parentRef);
        const uint32_t nextIndex = labelRef.index + 1;
        if (nextIndex >= siblingRange.end) return false;

        const ElementRef nextRef{ labelRef.depth, nextIndex };
        const Element& elem = elementStorage.GetElementUnsafe(nextRef);
        const ElementRect& rect = elementStorage.GetElementRectUnsafe(nextRef);
        if (!elem.eventHandler) return false;

        return elem.eventHandler(ElementEvent{ .id = elem.id, .type = type, .state = state, .rect = &rect, .elementRef = nextRef });
    }

    // ---- Dragging the value of the control after a label ----

    // Focuses the control a label is bound to (else the label), for typing.
    static void FocusLabelTarget(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, ID labelID)
    {
        ID focusTargetID = LabelBinding::ResolveControlForLabel(elementStorage, labelID);
        if (focusTargetID == ID::Invalid()) focusTargetID = labelID;
        guiStateManager.Interaction().SetFocusID(focusTargetID);
        guiStateManager.TextEditorFocus().Set(focusTargetID);
    }

    static void LabelDragPress(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, DragLabelState& state, const ElementEvent& event)
    {
        guiStateManager.Interaction().SetFocusID(event.id);
        guiStateManager.TextEditorFocus().Clear();
        state.drag.Press(event, elementStorage);
    }

    // Past the drag distance, each move sends LabelDrag (LabelDragBegin first) with scroll.x the
    // pixels moved since the press. True while the press is held.
    static bool LabelDragMove(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, DragLabelState& state, const ElementEvent& event)
    {
        if (!state.drag.IsPressed(event.id))
            return false;
        const bool wasDragging = state.drag.dragging;
        if (!state.drag.Move(event, elementStorage.GetStyleManager().Px(kDragStartDistance)))
            return true;
        if (!wasDragging)
        {
            guiStateManager.Interaction().SetCaptureID(event.id);
            InputState beginState = event.state;
            beginState.scroll = { 0, 0 };
            DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragBegin, beginState);
        }
        InputState dragState = event.state;
        dragState.scroll = { float(state.drag.ScreenDelta(event.state).x), 0.0f };
        DispatchToNextSibling(elementStorage, event.id, EventType::LabelDrag, dragState);
        return true;
    }

    // The release focuses the bound control for typing, after a drag as after a click.
    static bool LabelDragRelease(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, DragLabelState& state, const ElementEvent& event)
    {
        const PointerDrag::Ended ended = state.drag.Release(event);
        if (ended == PointerDrag::Ended::No)
            return false;
        FocusLabelTarget(guiStateManager, elementStorage, event.id);
        if (ended == PointerDrag::Ended::Click)
            return false;
        guiStateManager.Interaction().ReleaseCaptureID();
        DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragEnd, event.state);
        return true;
    }

    // Escape: a drag puts the control's value back; a press not yet dragged is dropped.
    static bool LabelDragCancel(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, DragLabelState& state, const ElementEvent& event)
    {
        const bool wasDragging = state.drag.IsDragging(event.id);
        if (!IsDragCancelKey(event.state) || !state.drag.Cancel(event.id))
            return false;
        if (wasDragging)
        {
            DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragCancel, event.state);
            guiStateManager.Interaction().ReleaseCaptureID();
        }
        return true;
    }

    EventHandler MakeDraggableLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage)
    {
        return [&guiStateManager, &elementStorage](ElementEvent&& event) -> bool
        {
            auto& store = guiStateManager.GetElementDataStore();
            switch (event.type)
            {
                case EventType::MouseDown:
                    if (!event.state.LeftPressed())
                        break;
                    LabelDragPress(guiStateManager, elementStorage, store.GetOrCreate<DragLabelState>(event.id), event);
                    return true;

                case EventType::MouseMove:
                    if (auto* state = store.Get<DragLabelState>(event.id))
                        return LabelDragMove(guiStateManager, elementStorage, *state, event);
                    break;

                case EventType::MouseUp:
                    if (auto* state = store.Get<DragLabelState>(event.id))
                        return LabelDragRelease(guiStateManager, elementStorage, *state, event);
                    break;

                case EventType::KeyDown:
                    if (auto* state = store.Get<DragLabelState>(event.id))
                        return LabelDragCancel(guiStateManager, elementStorage, *state, event);
                    break;

                default:
                    break;
            }
            return false;
        };
    }

    EventHandler MakeLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage)
    {
        return [&guiStateManager, &elementStorage](ElementEvent&& event)->bool
            {
                if (event.type == EventType::MouseDown && event.state.LeftPressed())
                {
                    FocusLabelTarget(guiStateManager, elementStorage, event.id);
                    return true;
                }
                return false;
            };
    }

    // ---- Resizing a label group from a label's right edge ----

    static constexpr int32_t kLabelResizeEdge = 5; // CSS pixels from the right edge that trigger resize

    // Pixels -> the group's stored CSS-pixel width. Measured widths round up so the
    // laid-out width still fits them.
    static int32_t ToGroupWidth(const LabelGroupVisualState& gv, int32_t pixels, bool roundUp = false)
    {
        const float scale = gv.styleManager ? gv.styleManager->GetUIScale() : 1.0f;
        const float units = float(pixels) / scale;
        return int32_t(roundUp ? std::ceil(units) : std::round(units));
    }

    struct LabelGroupResizeState
    {
        PointerDrag        drag;            // a drag from the press on
        int32_t            startWidth = 0;  // put back by Escape
        DoubleClickTracker clicks;          // a double-click fits the widest label
    };

    static int32_t ComputeMaxLabelTextWidth(const LabelGroupVisualState& gv)
    {
        if (!gv.labelTextIDs || !gv.textSystem) return 0;
        int32_t maxWidth = 0;
        for (TextID tid : *gv.labelTextIDs)
        {
            auto m = gv.textSystem->GetTextMeasurement(tid);
            maxWidth = std::max(maxWidth, m.maxWidth);
        }
        // Add padding + border so the text fits without ellipsis
        if (gv.styleManager)
        {
            if (const LayoutStyle* ls = gv.styleManager->GetLayoutStyle(gv.labelStyleRef.layoutStyleIndex))
                maxWidth += ls->padding.Horizontal() + ls->borderWidths.Horizontal();
        }
        return maxWidth;
    }

    static bool IsNearLabelRightEdge(const ElementStorage& storage, const ElementEvent& event)
    {
        if (!event.rect) return false;
        // event.rect is in layout space; LocalCursor() maps the cursor through any
        // ancestor contentTransform (e.g. a node view) into the same space.
        const int2 cursorInLabel = event.LocalCursor(storage);
        int32_t rightEdge = event.rect->rect.x + event.rect->rect.size.x;
        return cursorInLabel.x >= (rightEdge - storage.GetStyleManager().Px(kLabelResizeEdge));
    }

    // A press on the right edge: a double-click fits the widest label, else a resize starts.
    // False when the press isn't on the edge.
    static bool EdgeResizePress(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, const LabelGroupVisualState& gv,
                                LabelGroupResizeState& state, const ElementEvent& event)
    {
        if (!IsNearLabelRightEdge(elementStorage, event))
            return false;
        if (state.clicks.IsDoubleClick(event.id))
        {
            if (const int32_t maxTextWidth = ComputeMaxLabelTextWidth(gv); maxTextWidth > 0)
                *gv.sharedWidth = std::clamp(ToGroupWidth(gv, maxTextWidth, true), gv.minWidth, gv.maxWidth);
            return true;
        }
        state.drag.Press(event, elementStorage, MouseButtons::Left, /*atOnce*/ true);
        state.startWidth = *gv.sharedWidth;
        *gv.edgeDragging = true;
        guiStateManager.Interaction().SetCaptureID(event.id);
        return true;
    }

    static void EdgeResizeFollow(const ElementStorage& elementStorage, const LabelGroupVisualState& gv,
                                 const LabelGroupResizeState& state, const ElementEvent& event)
    {
        const int32_t delta = state.drag.LocalDelta(event, elementStorage).x;
        *gv.sharedWidth = std::clamp(state.startWidth + ToGroupWidth(gv, delta), gv.minWidth, gv.maxWidth);
    }

    static bool EdgeResizeMove(const ElementStorage& elementStorage, const LabelGroupVisualState& gv,
                               LabelGroupResizeState& state, const ElementEvent& event)
    {
        if (!state.drag.Move(event, 0))
            return false;
        EdgeResizeFollow(elementStorage, gv, state, event);
        return true;
    }

    static bool EdgeResizeRelease(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, const LabelGroupVisualState& gv,
                                  LabelGroupResizeState& state, const ElementEvent& event)
    {
        if (!state.drag.IsPressed(event.id))
            return false;
        EdgeResizeFollow(elementStorage, gv, state, event);
        if (state.drag.Release(event) == PointerDrag::Ended::No)
            return false;
        *gv.edgeDragging = false;
        guiStateManager.Interaction().ReleaseCaptureID();
        return true;
    }

    static bool EdgeResizeCancel(GUIStateManager& guiStateManager, const LabelGroupVisualState& gv,
                                 LabelGroupResizeState& state, const ElementEvent& event)
    {
        if (!IsDragCancelKey(event.state) || !state.drag.Cancel(event.id))
            return false;
        *gv.sharedWidth  = state.startWidth;
        *gv.edgeDragging = false;
        guiStateManager.Interaction().ReleaseCaptureID();
        return true;
    }

    // Hovering the right edge shows the resize cursor.
    static bool EdgeHover(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, const LabelGroupVisualState& gv,
                          const ElementEvent& event)
    {
        *gv.edgeHovered = IsNearLabelRightEdge(elementStorage, event);
        if (*gv.edgeHovered)
            guiStateManager.Cursor().Request(MouseCursor::ColResize);
        return *gv.edgeHovered;
    }

    EventHandler MakeGroupLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, LabelGroupVisualState gv)
    {
        return [&guiStateManager, &elementStorage, gv](ElementEvent&& event) -> bool
        {
            auto& store = guiStateManager.GetElementDataStore();
            switch (event.type)
            {
                case EventType::MouseMove:
                    if (auto* state = store.Get<LabelGroupResizeState>(event.id); state && EdgeResizeMove(elementStorage, gv, *state, event))
                        return true;
                    return EdgeHover(guiStateManager, elementStorage, gv, event);

                case EventType::MouseDown:
                    if (!event.state.LeftPressed())
                        break;
                    if (!EdgeResizePress(guiStateManager, elementStorage, gv, store.GetOrCreate<LabelGroupResizeState>(event.id), event))
                        FocusLabelTarget(guiStateManager, elementStorage, event.id);
                    return true;

                case EventType::MouseUp:
                    if (auto* state = store.Get<LabelGroupResizeState>(event.id))
                        return EdgeResizeRelease(guiStateManager, elementStorage, gv, *state, event);
                    break;

                case EventType::KeyDown:
                    if (auto* state = store.Get<LabelGroupResizeState>(event.id))
                        return EdgeResizeCancel(guiStateManager, gv, *state, event);
                    break;

                default:
                    break;
            }
            return false;
        };
    }

    // Resize + label-drag state in one entry: the data store holds one type per ID.
    struct GroupDraggableLabelState
    {
        LabelGroupResizeState resize;
        DragLabelState        drag;
    };

    EventHandler MakeGroupDraggableLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, LabelGroupVisualState gv)
    {
        return [&guiStateManager, &elementStorage, gv](ElementEvent&& event) -> bool
        {
            auto& store = guiStateManager.GetElementDataStore();
            switch (event.type)
            {
                case EventType::MouseMove:
                    if (auto* s = store.Get<GroupDraggableLabelState>(event.id))
                    {
                        if (EdgeResizeMove(elementStorage, gv, s->resize, event))
                            return true;
                        if (LabelDragMove(guiStateManager, elementStorage, s->drag, event))
                            return true;
                    }
                    return EdgeHover(guiStateManager, elementStorage, gv, event);

                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        auto& s = store.GetOrCreate<GroupDraggableLabelState>(event.id);
                        if (!EdgeResizePress(guiStateManager, elementStorage, gv, s.resize, event))
                            LabelDragPress(guiStateManager, elementStorage, s.drag, event);
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* s = store.Get<GroupDraggableLabelState>(event.id))
                        return EdgeResizeRelease(guiStateManager, elementStorage, gv, s->resize, event) ||
                               LabelDragRelease(guiStateManager, elementStorage, s->drag, event);
                    break;

                case EventType::KeyDown:
                    if (auto* s = store.Get<GroupDraggableLabelState>(event.id))
                        return EdgeResizeCancel(guiStateManager, gv, s->resize, event) ||
                               LabelDragCancel(guiStateManager, elementStorage, s->drag, event);
                    break;

                default:
                    break;
            }
            return false;
        };
    }

}
