#include <gui_core/text/gui_text_system.h>

#include <gui_controls/gui_default_handlers.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/style/gui_style_manager.h>

#include <chrono>
#include <cmath>
#include <memory>

// Sizer + slider drag-track event handlers.

namespace GUI
{
    EventHandler MakeHorizontalSizerHandler(GUIStateManager& guiStateManager, int& sizeValue, int minSize, int maxSize,
                                            std::function<void(int)> onChanged, std::function<int()> onAutoSize,
                                            bool invertMouseDelta, float pixelsPerUnit)
    {
        const int sign = invertMouseDelta ? -1 : 1;
        // Screen pixels → the stored size's units.
        const float unit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 1.0f;
        auto toUnits = [unit](int pixels) { return int(std::lround(float(pixels) / unit)); };
        auto fitUnits = [unit](int pixels) { return int(std::ceil(float(pixels) / unit)); };   // still fits the pixels
        return [&guiStateManager, &sizeValue, minSize, maxSize, onChanged, onAutoSize, sign, toUnits, fitUnits](ElementEvent&& event)->bool
        {
            auto& store = guiStateManager.GetElementDataStore();

            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        auto& state = store.GetOrCreate<HorizontalSizerState>(event.id);
                        // Double-click: auto-size
                        if (onAutoSize)
                        {
                            const int64_t now = SteadyClockNowNanos();
                            float msSinceLast = static_cast<float>(
                                now - state.lastClickTimeNanos) / 1'000'000.0f;
                            if (msSinceLast < 300.0f && state.lastClickID == event.id)
                            {
                                int autoSize = onAutoSize();
                                if (autoSize >= 0)
                                {
                                    int newSize = std::clamp(fitUnits(autoSize), minSize, maxSize);
                                    sizeValue = newSize;
                                    if (onChanged) onChanged(newSize);
                                }
                                state.lastClickID = ID::Invalid();
                                return true;
                            }
                            state.lastClickTimeNanos = now;
                            state.lastClickID = event.id;
                        }

                        state.previousFocusID = guiStateManager.Interaction().GetFocusedID();
                        state.previousTextFocusID = guiStateManager.TextEditorFocus().GetFocusedID();
                        // Scroll-into-view on the drag axis only: the handle is often taller than its parent.
                        guiStateManager.Interaction().SetFocusIDScrollAxis(event.id, ScrollAxis::X);
                        guiStateManager.TextEditorFocus().Clear();
                        state.draggingID = event.id;
                        state.dragStartLocal = event.state.screenPos;
                        state.startSize = sizeValue;
                        state.startScreenX = event.state.screenPos.x;
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                    if (auto* state = store.Get<HorizontalSizerState>(event.id); state && state->draggingID == event.id)
                    {
                        int delta = toUnits(event.state.screenPos.x - state->startScreenX) * sign;
                        int newSize = std::clamp(state->startSize + delta, minSize, maxSize);
                        if (newSize != sizeValue)
                        {
                            sizeValue = newSize;
                            if (onChanged) onChanged(newSize);
                        }
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* state = store.Get<HorizontalSizerState>(event.id); state && state->draggingID == event.id)
                    {
                        int delta = toUnits(event.state.screenPos.x - state->startScreenX) * sign;
                        int newSize = std::clamp(state->startSize + delta, minSize, maxSize);
                        if (newSize != sizeValue)
                        {
                            sizeValue = newSize;
                            if (onChanged) onChanged(newSize);
                        };
                        // Restore focus without triggering scroll-to-view
                        guiStateManager.Interaction().SetFocusIDNoScroll(state->previousFocusID);
                        guiStateManager.TextEditorFocus().Set(state->previousTextFocusID);
                        state->draggingID = ID::Invalid();
                        return true;
                    }
                    break;

                case EventType::KeyDown:
                    if (event.state.lastKey == GUI::KeyCode::Escape)
                    {
                        if (auto* state = store.Get<HorizontalSizerState>(event.id); state && state->draggingID == event.id)
                        {
                            int newSize = state->startSize;
                            if (newSize != sizeValue)
                            {
                                sizeValue = newSize;
                                if (onChanged) onChanged(newSize);
                            }
                            guiStateManager.Interaction().SetFocusIDNoScroll(state->previousFocusID);
                            guiStateManager.TextEditorFocus().Set(state->previousTextFocusID);
                            state->draggingID = ID::Invalid();
                            return true;
                        }
                    }
                    return false;

                default:
                    break;
            }

            return false;
        };
    }

    EventHandler MakeVerticalSizerHandler(GUIStateManager& guiStateManager, int& sizeValue, int minSize, int maxSize,
                                          std::function<void(int)> onChanged, std::function<int()> onAutoSize,
                                          bool invertMouseDelta, float pixelsPerUnit)
    {
        const int sign = invertMouseDelta ? -1 : 1;
        // Screen pixels → the stored size's units.
        const float unit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 1.0f;
        auto toUnits = [unit](int pixels) { return int(std::lround(float(pixels) / unit)); };
        auto fitUnits = [unit](int pixels) { return int(std::ceil(float(pixels) / unit)); };   // still fits the pixels
        return [&guiStateManager, &sizeValue, minSize, maxSize, onChanged, onAutoSize, sign, toUnits, fitUnits](ElementEvent&& event)->bool
        {
            auto& store = guiStateManager.GetElementDataStore();

            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        auto& state = store.GetOrCreate<VerticalSizerState>(event.id);
                        // Double-click: auto-size
                        if (onAutoSize)
                        {
                            const int64_t now = SteadyClockNowNanos();
                            float msSinceLast = static_cast<float>(
                                now - state.lastClickTimeNanos) / 1'000'000.0f;
                            if (msSinceLast < 300.0f && state.lastClickID == event.id)
                            {
                                int autoSize = onAutoSize();
                                if (autoSize >= 0)
                                {
                                    int newSize = std::clamp(fitUnits(autoSize), minSize, maxSize);
                                    sizeValue = newSize;
                                    if (onChanged) onChanged(newSize);
                                }
                                state.lastClickID = ID::Invalid();
                                return true;
                            }
                            state.lastClickTimeNanos = now;
                            state.lastClickID = event.id;
                        }

                        state.previousFocusID = guiStateManager.Interaction().GetFocusedID();
                        state.previousTextFocusID = guiStateManager.TextEditorFocus().GetFocusedID();
                        // Scroll-into-view on the drag axis only: the handle is often wider than its parent.
                        guiStateManager.Interaction().SetFocusIDScrollAxis(event.id, ScrollAxis::Y);
                        guiStateManager.TextEditorFocus().Clear();
                        state.draggingID = event.id;
                        state.dragStartLocal = event.state.screenPos;
                        state.startSize = sizeValue;
                        state.startScreenY = event.state.screenPos.y;
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                    if (auto* state = store.Get<VerticalSizerState>(event.id); state && state->draggingID == event.id)
                    {
                        int delta = toUnits(event.state.screenPos.y - state->startScreenY) * sign;
                        int newSize = std::clamp(state->startSize + delta, minSize, maxSize);
                        if (newSize != sizeValue)
                        {
                            sizeValue = newSize;
                            if (onChanged) onChanged(newSize);
                        }
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* state = store.Get<VerticalSizerState>(event.id); state && state->draggingID == event.id)
                    {
                        int delta = toUnits(event.state.screenPos.y - state->startScreenY) * sign;
                        int newSize = std::clamp(state->startSize + delta, minSize, maxSize);
                        if (newSize != sizeValue)
                        {
                            sizeValue = newSize;
                            if (onChanged) onChanged(newSize);
                        };
                        guiStateManager.Interaction().SetFocusIDNoScroll(state->previousFocusID);
                        guiStateManager.TextEditorFocus().Set(state->previousTextFocusID);
                        state->draggingID = ID::Invalid();
                        return true;
                    }
                    break;

                case EventType::KeyDown:
                    if (event.state.lastKey == GUI::KeyCode::Escape)
                    {
                        if (auto* state = store.Get<VerticalSizerState>(event.id); state && state->draggingID == event.id)
                        {
                            int newSize = state->startSize;
                            if (newSize != sizeValue)
                            {
                                sizeValue = newSize;
                                if (onChanged) onChanged(newSize);
                            }
                            guiStateManager.Interaction().SetFocusIDNoScroll(state->previousFocusID);
                            guiStateManager.TextEditorFocus().Set(state->previousTextFocusID);
                            state->draggingID = ID::Invalid();
                            return true;
                        }
                    }
                    return false;

                default:
                    break;
            }

            return false;
        };
    }

    EventHandler MakeSliderHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage,
                                   float* value, int elementWidth,
                                   std::function<void()> onChanged,
                                   float minVal, float maxVal)
    {
        // The clamping below assumes minVal <= maxVal.
        if (maxVal < minVal) std::swap(minVal, maxVal);

        return [value, &guiStateManager, &elementStorage, elementWidth, onChanged, minVal, maxVal](ElementEvent&& event)->bool
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
            auto setValueFromMouse = [value, minVal, maxVal, range, w, &event, &elementStorage, onChanged]() {
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

            // Sub-threshold press = click (opens text-edit on MouseUp); crossing latches drag.
            const int kClickThresholdPx = elementStorage.GetStyleManager().Px(kDragStartDistance);

            switch (event.type)
            {
                case EventType::MouseDown:
                    if (event.state.LeftPressed())
                    {
                        auto& state = store.GetOrCreate<SliderState>(event.id);
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        state.draggingID = event.id;
                        state.cancelDrag = false;
                        state.movedPastThreshold = false;
                        state.dragStartLocal = event.state.screenPos;
                        state.startValue = *value; // for Escape-to-cancel
                        return true;
                    }
                    break;

                case EventType::MouseMove:
                    if (auto* state = store.Get<SliderState>(event.id);
                        state && state->draggingID == event.id && !state->cancelDrag)
                    {
                        if (!state->movedPastThreshold)
                        {
                            const int dx = std::abs(event.state.screenPos.x - state->dragStartLocal.x);
                            const int dy = std::abs(event.state.screenPos.y - state->dragStartLocal.y);
                            if (std::max(dx, dy) >= kClickThresholdPx)
                                state->movedPastThreshold = true;
                        }
                        if (state->movedPastThreshold)
                            setValueFromMouse();
                        return true;
                    }
                    break;

                case EventType::MouseUp:
                    if (auto* state = store.Get<SliderState>(event.id);
                        state && state->draggingID == event.id)
                    {
                        if (state->cancelDrag)
                        {
                            // already restored by Escape
                        }
                        else if (state->movedPastThreshold)
                        {
                            setValueFromMouse();
                        }
                        else
                        {
                            // Clean click → SliderField swaps the bar for an in-place textbox next frame.
                            state->editMode = true;
                        }
                        state->draggingID = ID::Invalid();
                        return true;
                    }
                    break;

                case EventType::KeyDown:
                    if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) != ElementState::None)
                    {
                        float oldValue = *value;
                        float newValue = oldValue;

                        if (event.state.lastKey == GUI::KeyCode::Escape)
                        {
                            if (auto* state = store.Get<SliderState>(event.id);
                                state && state->draggingID == event.id)
                            {
                                *value = state->startValue;
                                state->cancelDrag = true;
                                if (onChanged) onChanged();
                                return true;
                            }
                        }

                        switch (event.state.lastKey)
                        {
                            case GUI::KeyCode::Left:     newValue = std::clamp(oldValue - smallStep, minVal, maxVal); break;
                            case GUI::KeyCode::Right:    newValue = std::clamp(oldValue + smallStep, minVal, maxVal); break;
                            case GUI::KeyCode::Home:     newValue = minVal; break;
                            case GUI::KeyCode::End:      newValue = maxVal; break;
                            case GUI::KeyCode::PageUp:   newValue = std::clamp(oldValue + bigStep, minVal, maxVal); break;
                            case GUI::KeyCode::PageDown: newValue = std::clamp(oldValue - bigStep, minVal, maxVal); break;
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
