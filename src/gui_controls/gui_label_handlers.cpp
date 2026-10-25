#include <gui_core/text/gui_text_system.h>

#include <gui_controls/gui_default_handlers.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/style/gui_style_manager.h>

#include <chrono>
#include <cmath>
#include <memory>

// Label drag / group-label event handlers.

namespace interstice
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

    EventHandler MakeDraggableLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage)
    {
        return [&guiStateManager, &elementStorage](ElementEvent&& event) -> bool
        {
            auto& store = guiStateManager.GetElementDataStore();
            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();

                        auto& state = store.GetOrCreate<DragLabelState>(event.id);
                        state.activeID     = event.id;
                        state.mode         = DragScalarMode::Pressing;
                        state.dragStartPos = event.state.screenPos;
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                {
                    if (!event.state.LeftDown())
                        return false;

                    auto* state = store.Get<DragLabelState>(event.id);
                    if (!state || state->activeID != event.id) return false;
                    if (state->mode == DragScalarMode::Pressing)
                    {
                        state->mode = DragScalarMode::Dragging;
                        guiStateManager.Interaction().SetCaptureID(event.id);
                        // LabelDragBegin with scroll.x = 0
                        InputState beginState = event.state;
                        beginState.scroll = { 0, 0 };
                        DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragBegin, beginState);
                    }
                    if (state->mode == DragScalarMode::Dragging)
                    {
                        // LabelDrag with scroll.x = cumulative pixel delta
                        InputState dragState = event.state;
                        dragState.scroll = { float(event.state.screenPos.x - state->dragStartPos.x), 0.0f };
                        DispatchToNextSibling(elementStorage, event.id, EventType::LabelDrag, dragState);
                        return true;
                    }
                    break;
                }

                case EventType::KeyUp:
                    if (auto* state = store.Get<DragLabelState>(event.id);
                        state && state->activeID == event.id &&
                        event.state.lastKey == interstice::KeyCode::Escape)
                    {
                        DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragCancel, event.state);
                        state->mode = DragScalarMode::Idle;
                        guiStateManager.Interaction().ReleaseCaptureID();
                        return true;
                    }
                    return false;

                case EventType::MouseUp:
                {
                    auto* state = store.Get<DragLabelState>(event.id);
                    if (!state || state->activeID != event.id)
                        return false;
                    if (event.state.LeftReleased())
                    {
                        ID focusTargetID = LabelBinding::ResolveControlForLabel(elementStorage, event.id);
                        if (focusTargetID == ID::Invalid()) focusTargetID = event.id;
                        guiStateManager.Interaction().SetFocusID(focusTargetID);
                        guiStateManager.TextEditorFocus().Set(focusTargetID);
                    }
                    state->activeID = ID::Invalid();
                    if (state->mode == DragScalarMode::Dragging)
                    {
                        state->mode = DragScalarMode::Idle;
                        guiStateManager.Interaction().ReleaseCaptureID();
                        DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragEnd, event.state);
                        return true;
                    }
                    state->mode = DragScalarMode::Idle;
                    break;
                }

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
                    ID focusTargetID = LabelBinding::ResolveControlForLabel(elementStorage, event.id);
                    if (focusTargetID == ID::Invalid()) focusTargetID = event.id;
                    guiStateManager.Interaction().SetFocusID(focusTargetID);
                    guiStateManager.TextEditorFocus().Set(focusTargetID);
                    return true;
                }
                return false;
            };
    }

    // ---- Label group resize helpers ----

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
        ID draggingID = ID::Invalid();
        int32_t startWidth = 0;
        int32_t startScreenX = 0;
        std::chrono::steady_clock::time_point lastEdgeClickTime{};
        ID lastEdgeClickID = ID::Invalid();
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

    EventHandler MakeGroupLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, LabelGroupVisualState gv)
    {
        return [&guiStateManager, &elementStorage, gv](ElementEvent&& event) -> bool
        {
            auto& store = guiStateManager.GetElementDataStore();
            switch (event.type)
            {
                case EventType::MouseMove:
                    if (auto* resizeState = store.Get<LabelGroupResizeState>(event.id);
                        resizeState && resizeState->draggingID == event.id)
                    {
                        int32_t delta = event.state.screenPos.x - resizeState->startScreenX;
                        *gv.sharedWidth = std::clamp(resizeState->startWidth + ToGroupWidth(gv, delta), gv.minWidth, gv.maxWidth);
                        return true;
                    }
                    if (IsNearLabelRightEdge(elementStorage, event))
                    {
                        *gv.edgeHovered = true;
                        guiStateManager.Cursor().Request(MouseCursor::ColResize);
                        return true;
                    }
                    *gv.edgeHovered = false;
                    return false;

                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        if (IsNearLabelRightEdge(elementStorage, event))
                        {
                            auto& resizeState = store.GetOrCreate<LabelGroupResizeState>(event.id);
                            // Double-click on edge: auto-size to max label text width
                            auto now = std::chrono::steady_clock::now();
                            float msSinceLast = std::chrono::duration<float, std::milli>(
                                now - resizeState.lastEdgeClickTime).count();
                            if (msSinceLast < 300.0f && resizeState.lastEdgeClickID != ID::Invalid())
                            {
                                int32_t maxTextWidth = ComputeMaxLabelTextWidth(gv);
                                if (maxTextWidth > 0)
                                    *gv.sharedWidth = std::clamp(ToGroupWidth(gv, maxTextWidth, true), gv.minWidth, gv.maxWidth);
                                resizeState.lastEdgeClickID = ID::Invalid();
                                return true;
                            }
                            resizeState.lastEdgeClickTime = now;
                            resizeState.lastEdgeClickID = event.id;

                            resizeState.draggingID = event.id;
                            resizeState.startWidth = *gv.sharedWidth;
                            resizeState.startScreenX = event.state.screenPos.x;
                            *gv.edgeDragging = true;
                            guiStateManager.Interaction().SetCaptureID(event.id);
                            return true;
                        }
                        ID focusTargetID = LabelBinding::ResolveControlForLabel(elementStorage, event.id);
                        if (focusTargetID == ID::Invalid()) focusTargetID = event.id;
                        guiStateManager.Interaction().SetFocusID(focusTargetID);
                        guiStateManager.TextEditorFocus().Set(focusTargetID);
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* resizeState = store.Get<LabelGroupResizeState>(event.id);
                        resizeState && resizeState->draggingID == event.id)
                    {
                        int32_t delta = event.state.screenPos.x - resizeState->startScreenX;
                        *gv.sharedWidth = std::clamp(resizeState->startWidth + ToGroupWidth(gv, delta), gv.minWidth, gv.maxWidth);
                        resizeState->draggingID = ID::Invalid();
                        *gv.edgeDragging = false;
                        guiStateManager.Interaction().ReleaseCaptureID();
                        return true;
                    }
                    break;

                case EventType::KeyDown:
                    if (event.state.lastKey == interstice::KeyCode::Escape)
                    {
                        if (auto* resizeState = store.Get<LabelGroupResizeState>(event.id);
                            resizeState && resizeState->draggingID == event.id)
                        {
                            *gv.sharedWidth = resizeState->startWidth;
                            resizeState->draggingID = ID::Invalid();
                            *gv.edgeDragging = false;
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        }
                    }
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
                {
                    if (auto* s = store.Get<GroupDraggableLabelState>(event.id))
                    {
                        if (s->resize.draggingID == event.id)
                        {
                            int32_t delta = event.state.screenPos.x - s->resize.startScreenX;
                            *gv.sharedWidth = std::clamp(s->resize.startWidth + ToGroupWidth(gv, delta), gv.minWidth, gv.maxWidth);
                            return true;
                        }

                        if (s->drag.activeID == event.id && event.state.LeftDown())
                        {
                            if (s->drag.mode == DragScalarMode::Pressing)
                            {
                                s->drag.mode = DragScalarMode::Dragging;
                                guiStateManager.Interaction().SetCaptureID(event.id);
                                InputState beginState = event.state;
                                beginState.scroll = { 0, 0 };
                                DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragBegin, beginState);
                            }
                            if (s->drag.mode == DragScalarMode::Dragging)
                            {
                                InputState ds = event.state;
                                ds.scroll = { float(event.state.screenPos.x - s->drag.dragStartPos.x), 0.0f };
                                DispatchToNextSibling(elementStorage, event.id, EventType::LabelDrag, ds);
                                return true;
                            }
                        }
                    }

                    if (IsNearLabelRightEdge(elementStorage, event))
                    {
                        *gv.edgeHovered = true;
                        guiStateManager.Cursor().Request(MouseCursor::ColResize);
                        return true;
                    }
                    *gv.edgeHovered = false;
                    return false;
                }

                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        auto& s = store.GetOrCreate<GroupDraggableLabelState>(event.id);
                        if (IsNearLabelRightEdge(elementStorage, event))
                        {
                            // Double-click on edge: auto-size to max label text width
                            auto now = std::chrono::steady_clock::now();
                            float msSinceLast = std::chrono::duration<float, std::milli>(
                                now - s.resize.lastEdgeClickTime).count();
                            if (msSinceLast < 300.0f && s.resize.lastEdgeClickID != ID::Invalid())
                            {
                                int32_t maxTextWidth = ComputeMaxLabelTextWidth(gv);
                                if (maxTextWidth > 0)
                                    *gv.sharedWidth = std::clamp(ToGroupWidth(gv, maxTextWidth, true), gv.minWidth, gv.maxWidth);
                                s.resize.lastEdgeClickID = ID::Invalid();
                                return true;
                            }
                            s.resize.lastEdgeClickTime = now;
                            s.resize.lastEdgeClickID = event.id;

                            s.resize.draggingID = event.id;
                            s.resize.startWidth = *gv.sharedWidth;
                            s.resize.startScreenX = event.state.screenPos.x;
                            *gv.edgeDragging = true;
                            guiStateManager.Interaction().SetCaptureID(event.id);
                            return true;
                        }
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        s.drag.activeID     = event.id;
                        s.drag.mode         = DragScalarMode::Pressing;
                        s.drag.dragStartPos = event.state.screenPos;
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* s = store.Get<GroupDraggableLabelState>(event.id))
                    {
                        if (s->resize.draggingID == event.id)
                        {
                            int32_t delta = event.state.screenPos.x - s->resize.startScreenX;
                            *gv.sharedWidth = std::clamp(s->resize.startWidth + ToGroupWidth(gv, delta), gv.minWidth, gv.maxWidth);
                            s->resize.draggingID = ID::Invalid();
                            *gv.edgeDragging = false;
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        }
                        if (s->drag.activeID == event.id)
                        {
                            if (event.state.LeftReleased())
                            {
                                ID focusTargetID = LabelBinding::ResolveControlForLabel(elementStorage, event.id);
                                if (focusTargetID == ID::Invalid()) focusTargetID = event.id;
                                guiStateManager.Interaction().SetFocusID(focusTargetID);
                                guiStateManager.TextEditorFocus().Set(focusTargetID);
                            }
                            s->drag.activeID = ID::Invalid();
                            if (s->drag.mode == DragScalarMode::Dragging)
                            {
                                s->drag.mode = DragScalarMode::Idle;
                                guiStateManager.Interaction().ReleaseCaptureID();
                                DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragEnd, event.state);
                                return true;
                            }
                            s->drag.mode = DragScalarMode::Idle;
                        }
                    }
                    break;

                case EventType::KeyUp:
                    if (event.state.lastKey == interstice::KeyCode::Escape)
                    {
                        if (auto* s = store.Get<GroupDraggableLabelState>(event.id))
                        {
                            if (s->resize.draggingID == event.id)
                            {
                                *gv.sharedWidth = s->resize.startWidth;
                                s->resize.draggingID = ID::Invalid();
                                *gv.edgeDragging = false;
                                guiStateManager.Interaction().ReleaseCaptureID();
                                return true;
                            }
                            if (s->drag.activeID == event.id)
                            {
                                DispatchToNextSibling(elementStorage, event.id, EventType::LabelDragCancel, event.state);
                                s->drag.mode = DragScalarMode::Idle;
                                guiStateManager.Interaction().ReleaseCaptureID();
                                return true;
                            }
                        }
                    }
                    return false;

                default:
                    break;
            }
            return false;
        };
    }

}
