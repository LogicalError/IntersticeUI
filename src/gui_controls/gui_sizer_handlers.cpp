#include <gui_core/text/gui_text_system.h>

#include <gui_controls/gui_default_handlers.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/style/gui_style_manager.h>

#include <chrono>
#include <cmath>
#include <memory>

// Sizer + slider drag-track event handlers.

namespace tachyon
{
    namespace
    {
        EventHandler MakeSizerHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage,
                                      int& sizeValue, int minSize, int maxSize,
                                      std::function<void(int)> onChanged, std::function<int()> onAutoSize,
                                      bool invertMouseDelta, float pixelsPerUnit, bool vertical)
        {
            const int sign = invertMouseDelta ? -1 : 1;
            // Layout pixels -> the stored size's units.
            const float unit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 1.0f;
            auto toUnits = [unit](int pixels) { return int(std::lround(float(pixels) / unit)); };
            auto fitUnits = [unit](int pixels) { return int(std::ceil(float(pixels) / unit)); };   // still fits the pixels
            return [&guiStateManager, &elementStorage, &sizeValue, minSize, maxSize, onChanged, onAutoSize, sign, toUnits, fitUnits, vertical]
                   (ElementEvent&& event) -> bool
            {
                auto& store = guiStateManager.GetElementDataStore();
                auto setSize = [&](int size)
                {
                    size = std::clamp(size, minSize, maxSize);
                    if (size == sizeValue)
                        return;
                    sizeValue = size;
                    if (onChanged) onChanged(size);
                };
                auto followPointer = [&](const SizerState& state)
                {
                    const int2 delta = state.drag.LocalDelta(event, elementStorage);
                    setSize(state.startSize + toUnits(vertical ? delta.y : delta.x) * sign);
                };
                auto giveFocusBack = [&](const SizerState& state)
                {
                    // Without scrolling it into view.
                    guiStateManager.Interaction().SetFocusIDNoScroll(state.previousFocusID);
                    guiStateManager.TextEditorFocus().Set(state.previousTextFocusID);
                };

                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed())
                            break;
                        auto& state = store.GetOrCreate<SizerState>(event.id);
                        if (onAutoSize && state.clicks.IsDoubleClick(event.id))
                        {
                            if (const int autoSize = onAutoSize(); autoSize >= 0)
                                setSize(fitUnits(autoSize));
                            return true;
                        }
                        state.drag.Press(event, elementStorage, MouseButtons::Left, /*atOnce*/ true);
                        state.previousFocusID     = guiStateManager.Interaction().GetFocusedID();
                        state.previousTextFocusID = guiStateManager.TextEditorFocus().GetFocusedID();
                        // Scroll-into-view on the drag axis only: the handle is often longer than its parent.
                        guiStateManager.Interaction().SetFocusIDScrollAxis(event.id, vertical ? ScrollAxis::Y : ScrollAxis::X);
                        guiStateManager.TextEditorFocus().Clear();
                        state.startSize = sizeValue;
                        return true;
                    }

                    case EventType::MouseMove:
                        if (auto* state = store.Get<SizerState>(event.id); state && state->drag.Move(event, 0))
                        {
                            followPointer(*state);
                            return true;
                        }
                        break;

                    case EventType::MouseUp:
                        if (auto* state = store.Get<SizerState>(event.id); state && state->drag.IsPressed(event.id))
                        {
                            followPointer(*state);
                            state->drag.Release(event);
                            giveFocusBack(*state);
                            return true;
                        }
                        break;

                    case EventType::KeyDown:
                        if (auto* state = store.Get<SizerState>(event.id); state && IsDragCancelKey(event.state) && state->drag.Cancel(event.id))
                        {
                            setSize(state->startSize);
                            giveFocusBack(*state);
                            return true;
                        }
                        break;

                    default:
                        break;
                }
                return false;
            };
        }
    }

    EventHandler MakeHorizontalSizerHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage,
                                            int& sizeValue, int minSize, int maxSize,
                                            std::function<void(int)> onChanged, std::function<int()> onAutoSize,
                                            bool invertMouseDelta, float pixelsPerUnit)
    {
        return MakeSizerHandler(guiStateManager, elementStorage, sizeValue, minSize, maxSize, std::move(onChanged), std::move(onAutoSize),
                                invertMouseDelta, pixelsPerUnit, /*vertical*/ false);
    }

    EventHandler MakeVerticalSizerHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage,
                                          int& sizeValue, int minSize, int maxSize,
                                          std::function<void(int)> onChanged, std::function<int()> onAutoSize,
                                          bool invertMouseDelta, float pixelsPerUnit)
    {
        return MakeSizerHandler(guiStateManager, elementStorage, sizeValue, minSize, maxSize, std::move(onChanged), std::move(onAutoSize),
                                invertMouseDelta, pixelsPerUnit, /*vertical*/ true);
    }

    EventHandler MakeSliderHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage,
                                   float* value, int elementWidth,
                                   std::function<void()> onChanged,
                                   float minVal, float maxVal, SliderPress press)
    {
        // The clamping below assumes minVal <= maxVal.
        if (maxVal < minVal) std::swap(minVal, maxVal);

        return [value, &guiStateManager, &elementStorage, elementWidth, onChanged, minVal, maxVal, press](ElementEvent&& event)->bool
        {
            if (value == nullptr) return false;
            int w = std::max(1, elementWidth);
            // Arrow keys step 1% of the range, PageUp/Down 10%.
            const float range = maxVal - minVal;
            const float smallStep = range * 0.01f;
            const float bigStep   = range * 0.10f;

            auto& store = guiStateManager.GetElementDataStore();

            // The value follows the absolute mouse X across the element (no accumulated deltas).
            // Uses the live rect when available, since elementWidth may be last frame's;
            // tests dispatch without a rect and fall back to elementWidth.
            auto setValueFromMouse = [value, minVal, range, w, &event, &elementStorage, onChanged]() {
                int32_t elementLeftX = event.rect ? event.rect->rect.position.x : 0;
                int32_t elementW     = event.rect ? event.rect->rect.w           : w;
                if (elementW <= 0) elementW = 1;
                // LocalCursor maps through ancestor content transforms into event.rect's space.
                int32_t cursorX = event.rect ? event.LocalCursor(elementStorage).x : event.state.screenPos.x;
                int32_t localX = cursorX - elementLeftX;
                float t = std::clamp(float(localX) / float(elementW), 0.0f, 1.0f);
                float newValue = minVal + t * range;
                if (newValue != *value)
                {
                    *value = newValue;
                    if (onChanged) onChanged();
                }
            };

            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        auto& state = store.GetOrCreate<SliderState>(event.id);
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        // A press that isn't dragged is a click, which opens the value for typing.
                        state.drag.Press(event, elementStorage, MouseButtons::Left, /*atOnce*/ press == SliderPress::Jump);
                        state.startValue = *value;
                        if (press == SliderPress::Jump)
                            setValueFromMouse();
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                    if (auto* state = store.Get<SliderState>(event.id); state && state->drag.IsPressed(event.id))
                    {
                        if (state->drag.Move(event, elementStorage.GetStyleManager().Px(kDragStartDistance)))
                            setValueFromMouse();
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* state = store.Get<SliderState>(event.id); state && state->drag.IsPressed(event.id))
                    {
                        if (state->drag.Release(event) == PointerDrag::Ended::Drag)
                            setValueFromMouse();
                        else
                            state->editMode = true;   // SliderField swaps the bar for an in-place text box next frame
                        return true;
                    }
                    break;

                // Dragging the field's label: the distance maps across the track, as a drag on it.
                case EventType::LabelDragBegin:
                    store.GetOrCreate<SliderState>(event.id).startValue = *value;
                    return true;

                case EventType::LabelDrag:
                    if (auto* state = store.Get<SliderState>(event.id))
                    {
                        const float delta = float(event.state.scroll.x) * float(LabelDragSpeedScale(event.state)) * range / float(w);
                        const float newValue = std::clamp(state->startValue + delta, minVal, maxVal);
                        if (newValue != *value)
                        {
                            *value = newValue;
                            if (onChanged) onChanged();
                        }
                        return true;
                    }
                    break;

                case EventType::LabelDragEnd:
                    return true;

                case EventType::LabelDragCancel:
                    if (auto* state = store.Get<SliderState>(event.id))
                    {
                        *value = state->startValue;
                        if (onChanged) onChanged();
                    }
                    return true;

                case EventType::KeyDown:
                    if (auto* state = store.Get<SliderState>(event.id); state && IsDragCancelKey(event.state) && state->drag.Cancel(event.id))
                    {
                        *value = state->startValue;
                        if (onChanged) onChanged();
                        return true;
                    }
                    if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) != ElementState::None)
                    {
                        float oldValue = *value;
                        float newValue = oldValue;

                        switch (event.state.lastKey)
                        {
                            case tachyon::KeyCode::Left:     newValue = std::clamp(oldValue - smallStep, minVal, maxVal); break;
                            case tachyon::KeyCode::Right:    newValue = std::clamp(oldValue + smallStep, minVal, maxVal); break;
                            case tachyon::KeyCode::Home:     newValue = minVal; break;
                            case tachyon::KeyCode::End:      newValue = maxVal; break;
                            case tachyon::KeyCode::PageUp:   newValue = std::clamp(oldValue + bigStep, minVal, maxVal); break;
                            case tachyon::KeyCode::PageDown: newValue = std::clamp(oldValue - bigStep, minVal, maxVal); break;
                            default: return false;
                        }

                        if (newValue != oldValue)
                        {
                            *value = newValue;
                            if (onChanged) onChanged();
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
}
