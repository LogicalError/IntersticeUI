#include <gui_core/text/gui_text_system.h>

#include <gui_controls/gui_default_handlers.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/style/gui_style_manager.h>

#include <chrono>
#include <memory>

namespace interstice
{
    bool TextEditorUsesKey(const InputState& state, bool readOnly)
    {
        const KeyCode key = state.lastKey;
        switch (key)
        {
            case KeyCode::Left: case KeyCode::Right: case KeyCode::Up: case KeyCode::Down:
            case KeyCode::Home: case KeyCode::End:
                return true;   // caret movement and selection, with any modifier
            default:
                break;
        }

        if ((state.modifiers & (KeyModifier::Control | KeyModifier::Command)) != KeyModifier::None)
        {
            switch (key)
            {
                case KeyCode::KeyA: case KeyCode::KeyC:
                    return true;
                case KeyCode::KeyV: case KeyCode::KeyX: case KeyCode::KeyZ:
                case KeyCode::Backspace: case KeyCode::Delete:
                    return !readOnly;
                default:
                    return false;
            }
        }
        if (readOnly || (state.modifiers & (KeyModifier::Alt | KeyModifier::Option)) != KeyModifier::None)
            return false;

        // Unmodified keys type text, except the ones that never do.
        const bool functionKey = (key >= KeyCode::F1 && key <= KeyCode::F12) || (key >= KeyCode::F13 && key <= KeyCode::F24);
        return key != KeyCode::None && !functionKey && key != KeyCode::Escape && key != KeyCode::Tab;
    }

    EventHandler MakeTextBoxHandler(GUIStateManager& guiStateManager)
    {
        return [&guiStateManager](ElementEvent&& event)->bool
        {
            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Set(event.id);
                        return true;
                    }
                    break;

                case EventType::KeyDown:
                case EventType::KeyUp:
                case EventType::TextInput:
                    if (guiStateManager.Interaction().GetFocusedID() == event.id && !event.state.consumed)
                        return event.type == EventType::TextInput ? !event.state.textInput.empty()
                                                                  : TextEditorUsesKey(event.state, /*readOnly*/ false);
                    break;

                default:
                    break;
            }

            return false;
        };
    }



    EventHandler MakeButtonHandler(GUIStateManager& guiStateManager, std::function<void()> onClick)
    {
        return [&guiStateManager, onClick](ElementEvent&& event)->bool
        {
            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        guiStateManager.Interaction().SetClicked(event.id);
                        if (onClick) onClick();
                        return true;
                    }
                    break;

                case EventType::KeyDown:
                    if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) != ElementState::None &&
                        IsActivationKey(event))
                    {
                        guiStateManager.Interaction().SetClicked(event.id);
                        // No held button drives the Active state on keyboard
                        // activation - flash it for a frame so the press shows.
                        guiStateManager.Interaction().FlashActive(event.id);
                        if (onClick) onClick();
                        return true;
                    }
                    break;

                default:
                    break;
            }

            return false;
        };
    }


    namespace
    {
        // Rounded division for POSITIVE inputs. Truncating both ways can leave the thumb a
        // pixel behind when content >> viewport, making 1px drags stick.
        inline int32_t DivRound(int64_t num, int64_t den)
        {
            if (den <= 0) return 0;
            return static_cast<int32_t>((num + den / 2) / den);
        }

        struct ScrollbarGeometry
        {
            bool    scrollable = false;
            int32_t trackPos   = 0;       // layout-space near edge of the visible track
            int32_t trackSize  = 0;       // visible track size on the scroll axis
            int32_t thumbSize  = 0;
            int32_t scrollRange = 0;      // contentSize - visibleSize, always >= 0
            int32_t maxThumbOffset = 0;   // trackSize - thumbSize, always >= 0
        };

        // parentRect gives the content bounds and viewport. The track comes from selfRect
        // when given (explicit-size bars), else from the parent's innerRect (span-and-clip).
        inline ScrollbarGeometry ComputeScrollbarGeometry(
            const ElementRect* parentRect, const ElementRect* selfRect, bool vertical, int32_t minThumbLength)
        {
            ScrollbarGeometry g{};
            if (!parentRect) return g;

            const int32_t contentSize = vertical ? parentRect->contentBounds.h : parentRect->contentBounds.w;
            const int32_t visibleSize = vertical ? parentRect->innerRect.h    : parentRect->innerRect.w;
            if (visibleSize <= 0) return g;

            if (selfRect)
            {
                g.trackPos  = vertical ? selfRect->rect.y : selfRect->rect.x;
                g.trackSize = vertical ? selfRect->rect.h : selfRect->rect.w;
                if (g.trackSize <= 0) return g;
            }
            else
            {
                g.trackPos  = vertical ? parentRect->innerRect.y : parentRect->innerRect.x;
                g.trackSize = visibleSize;
            }

            if (contentSize <= visibleSize) return g; // no scrolling needed

            int32_t thumbSize = static_cast<int32_t>(
                static_cast<int64_t>(g.trackSize) * visibleSize / contentSize);
            thumbSize = std::clamp(thumbSize, std::min(minThumbLength, g.trackSize), g.trackSize);

            g.scrollable = true;
            g.thumbSize  = thumbSize;
            g.scrollRange = contentSize - visibleSize;
            g.maxThumbOffset = g.trackSize - thumbSize;
            return g;
        }
    } // anonymous

    EventHandler MakeScrollbarHandler(GUIStateManager& guiStateManager,
                                      ElementStorage& elementStorage,
                                      ID containerID, bool vertical,
                                      int32_t trackLength)
    {
        const bool measureFromSelf = (trackLength > 0);
        const int32_t minThumbLength = elementStorage.GetStyleManager().Px(kScrollbarMinThumbLength);
        return [&guiStateManager, &elementStorage, containerID, vertical, measureFromSelf, minThumbLength]
               (ElementEvent&& event)->bool
        {
            if (containerID == ID::Invalid()) return false;

            auto& scrollManagement = guiStateManager.GetScrollManagement();
            auto& store = guiStateManager.GetElementDataStore();

            auto getParentRect = [&elementStorage, &event]() -> const ElementRect*
            {
                const auto parentRef = elementStorage.GetParentRef(event.elementRef);
                return elementStorage.GetElementRectPtr(parentRef);
            };
            auto getSelfRect = [&elementStorage, &event]() -> const ElementRect*
            {
                return elementStorage.GetElementRectPtr(event.elementRef);
            };

            auto writeScroll = [&scrollManagement, containerID, vertical](int newScrollPos)
            {
                auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(containerID);
                int2 offset = scrollManagement.GetScrollOffset(handle);
                if (vertical) offset.y = -newScrollPos;
                else          offset.x = -newScrollPos;
                scrollManagement.SetScrollOffset(handle, offset);
            };

            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        const auto* parentRect = getParentRect();
                        const auto* selfRect   = measureFromSelf ? getSelfRect() : nullptr;
                        const auto geom = ComputeScrollbarGeometry(parentRect, selfRect, vertical, minThumbLength);
                        if (!geom.scrollable) return false;

                        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(containerID);
                        const int2 offset = scrollManagement.GetScrollOffset(handle);
                        const int32_t currentScroll = vertical ? -offset.y : -offset.x;
                        const int32_t currentThumbOffset = (geom.scrollRange > 0)
                            ? DivRound(static_cast<int64_t>(currentScroll) * geom.maxThumbOffset, geom.scrollRange)
                            : 0;

                        // trackPos is in layout space (taken from rect.position) - convert
                        // the cursor through any ancestor contentTransform first.
                        const int2 lc = event.LocalCursor(elementStorage);
                        const int32_t mousePos = vertical ? lc.y : lc.x;
                        const int32_t clickOffset = mousePos - geom.trackPos;

                        auto& state = store.GetOrCreate<ScrollbarDragState>(event.id);
                        state.draggingID = event.id;

                        if (clickOffset >= currentThumbOffset &&
                            clickOffset <  currentThumbOffset + geom.thumbSize)
                        {
                            // Started on the thumb: drag relative to grab point.
                            state.thumbGrabOffset = clickOffset - currentThumbOffset;
                        }
                        else
                        {
                            // Started on the empty track: jump so the thumb centers under the mouse.
                            state.thumbGrabOffset = geom.thumbSize / 2;
                            const int32_t newThumbOffset = std::clamp(
                                clickOffset - state.thumbGrabOffset, 0, geom.maxThumbOffset);
                            const int32_t newScroll = (geom.maxThumbOffset > 0)
                                ? DivRound(static_cast<int64_t>(newThumbOffset) * geom.scrollRange, geom.maxThumbOffset)
                                : 0;
                            writeScroll(newScroll);
                        }

                        guiStateManager.Interaction().SetCaptureID(event.id);
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                {
                    auto* state = store.Get<ScrollbarDragState>(event.id);
                    if (!state || state->draggingID != event.id) return false;
                    const auto* parentRect = getParentRect();
                    const auto* selfRect   = measureFromSelf ? getSelfRect() : nullptr;
                    const auto geom = ComputeScrollbarGeometry(parentRect, selfRect, vertical, minThumbLength);
                    if (!geom.scrollable) return false;

                    // trackPos is layout-space; cursor must be in the same space.
                    const int2 lc = event.LocalCursor(elementStorage);
                    const int32_t mousePos = vertical ? lc.y : lc.x;
                    const int32_t newThumbOffset = std::clamp(
                        mousePos - geom.trackPos - state->thumbGrabOffset, 0, geom.maxThumbOffset);
                    const int32_t newScroll = (geom.maxThumbOffset > 0)
                        ? DivRound(static_cast<int64_t>(newThumbOffset) * geom.scrollRange, geom.maxThumbOffset)
                        : 0;
                    writeScroll(newScroll);
                    return true;
                }

                case EventType::MouseUp:
                {
                    auto* state = store.Get<ScrollbarDragState>(event.id);
                    if (!state || state->draggingID != event.id) return false;
                    state->draggingID = ID::Invalid();
                    guiStateManager.Interaction().ReleaseCaptureID();
                    return true;
                }

                default:
                    break;
            }

            return false;
        };
    }

    EventHandler MakeMouseWheelScrollHandler(ScrollManagement& scrollManagement, float wheelStep)
    {
        return [&scrollManagement, wheelStep](ElementEvent&& event)->bool
        {
            if (event.type != EventType::MouseScroll)
                return false;

            if (event.id == ID::Invalid())
                return false;

            ScrollOffsetHandle handle = scrollManagement.GetOrCreateScrollOffsetHandle(event.id);
            int2 offset = scrollManagement.GetScrollOffset(handle);

            // Wheel up (positive Y) increases the offset.
            int2 nextOffset = offset;
            nextOffset.x += static_cast<int32_t>(event.state.scroll.x * wheelStep);
            nextOffset.y += static_cast<int32_t>(event.state.scroll.y * wheelStep);

            if (nextOffset == offset)
                return false;

            scrollManagement.SetScrollOffset(handle, nextOffset);
            return true;
        };
    }

    EventHandler MakeDragSourceHandler(GUIStateManager& guiStateManager,
                                       uint32_t payloadType,
                                       std::function<uintptr_t()> getPayloadValue,
                                       std::function<std::any()> getPayload,
                                       DragVisualizer sourceVisualizer)
    {
        return [&guiStateManager, payloadType, getPayloadValue, getPayload, sourceVisualizer = std::move(sourceVisualizer)](ElementEvent&& event)->bool
        {
            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        const uintptr_t payloadValue = getPayloadValue ? getPayloadValue() : 0;
                        std::any payload = getPayload ? getPayload() : std::any(payloadValue);
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        guiStateManager.Drag().Begin(event.id, payloadType, payloadValue, event.state.screenPos, std::move(payload), sourceVisualizer);
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                    if ((event.state.buttons & interstice::MouseButtons::Left) != interstice::MouseButtons::None)
                    {
                        guiStateManager.Drag().UpdateFromInput(event.state);
                        return guiStateManager.Drag().GetDraggedID() == event.id;
                    }
                    break;

                default:
                    break;
            }

            return false;
        };
    }

    EventHandler MakeDropTargetHandler(uint32_t acceptedPayloadType,
                                       std::function<void(uintptr_t payloadValue, ID sourceID)> onDrop,
                                       std::function<bool(uintptr_t payloadValue)> canDrop)
    {
        return [acceptedPayloadType, onDrop, canDrop](ElementEvent&& event)->bool
        {
            auto accepts = [&] {
                return event.state.dragPayloadType == acceptedPayloadType
                    && (!canDrop || canDrop(event.state.dragPayloadValue));
            };
            switch (event.type)
            {
                case EventType::Drop:
                    if (accepts())
                    {
                        if (onDrop) onDrop(event.state.dragPayloadValue, event.state.dragSourceID);
                        return true;
                    }
                    break;

                case EventType::DragEnter:
                case EventType::DragOver:
                case EventType::DragOut:
                    return accepts();

                default:
                    break;
            }

            return false;
        };
    }
}
