#pragma once

#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_handler_func.h>

#include <gui_core/state/gui_state_manager.h>

#include <gui_core/style/gui_style_ref.h>
#include <gui_core/style/gui_style_text.h>           // TextID, IconID (used by value)

#include <gui_core/text/gui_numeric_field_config.h>

#include <gui_controls/gui_type_dimensions.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <stdint.h>
#include <type_traits>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    class ElementStorage;
    class ITextSystem;
    class StyleManager;

    /// True for a KeyDown of Return, keypad Enter or Space.
    inline bool IsActivationKey(const ElementEvent& event)
    {
        if (event.type != EventType::KeyDown) return false;
        return event.state.lastKey == tachyon::KeyCode::Return ||
               event.state.lastKey == tachyon::KeyCode::Return2 ||
               event.state.lastKey == tachyon::KeyCode::KeyPadEnter ||
               event.state.lastKey == tachyon::KeyCode::Space;
    }
    
    
    /// Whether a focused text editor acts on this key (KeyDown/KeyUp), so its handler
    /// takes it; the rest reach the app's shortcuts. Read-only text only selects and copies.
    bool TextEditorUsesKey(const InputState& state, bool readOnly);

    /// Text box handler: a left click focuses it for editing; while focused it takes text
    /// input and the keys its editor uses.
    EventHandler MakeTextBoxHandler(GUIStateManager& guiStateManager);


    /// Per-element state of MakeHorizontalSizerHandler (element data store); internal.
    struct HorizontalSizerState
    {
		ID previousFocusID = ID::Invalid();
        ID previousTextFocusID = ID::Invalid();
        ID draggingID = ID::Invalid();
        tachyon::int2 dragStartLocal{0,0};
        int startSize = 0;
        int startScreenX = 0;
        int64_t lastClickTimeNanos = 0;   ///< steady-clock ns; see SteadyClockNowNanos()
        ID lastClickID = ID::Invalid();
    };

    /// Sizer handler that drags `sizeValue` within [`minSize`, `maxSize`], calling `onChanged`
    /// with each new size. Sizes are in units of `pixelsPerUnit` screen pixels (for CSS px pass
    /// StyleManager::GetUIScale()). `onAutoSize` runs on double-click and returns the desired
    /// size in screen pixels, or -1 to skip. `invertMouseDelta` grows the size when dragging left.
    EventHandler MakeHorizontalSizerHandler(GUIStateManager& guiStateManager, int& sizeValue, int minSize, int maxSize,
                                            std::function<void(int)> onChanged = nullptr,
                                            std::function<int()> onAutoSize = nullptr,
                                            bool invertMouseDelta = false, float pixelsPerUnit = 1.0f);

    /// Per-element state of MakeVerticalSizerHandler (element data store); internal.
    struct VerticalSizerState
    {
        ID previousFocusID = ID::Invalid();
        ID previousTextFocusID = ID::Invalid();
        ID draggingID = ID::Invalid();
        tachyon::int2 dragStartLocal{0,0};
        int startSize = 0;
        int startScreenY = 0;
        int64_t lastClickTimeNanos = 0;   ///< steady-clock ns; see SteadyClockNowNanos()
        ID lastClickID = ID::Invalid();
    };

    /// Y-axis counterpart of MakeHorizontalSizerHandler; `invertMouseDelta` grows the size when
    /// dragging up.
    EventHandler MakeVerticalSizerHandler(GUIStateManager& guiStateManager, int& sizeValue, int minSize, int maxSize,
                                          std::function<void(int)> onChanged = nullptr,
                                          std::function<int()> onAutoSize = nullptr,
                                          bool invertMouseDelta = false, float pixelsPerUnit = 1.0f);

    /// Per-element state of MakeSliderHandler (element data store); internal.
    struct SliderState
    {
        ID draggingID = ID::Invalid();
        tachyon::int2 dragStartLocal{ 0,0 }; ///< mouse screen pos at MouseDown (click-vs-drag origin)
        float startValue = 0.0f;
        bool cancelDrag = false;
        bool movedPastThreshold = false;
        bool editMode = false;           ///< set by a clean click: in-place text editing
    };

    /// What pressing a slider does: Edit - a click (no drag) opens its value for typing;
    /// Jump - the value moves to the cursor at once and follows it while held.
    enum class SliderPress : uint8_t { Edit, Jump };

    /// Slider handler for `*value` in [`minVal`, `maxVal`]; `elementWidth` (pixels, <= 0 is
    /// treated as 1) maps drag distance onto that range. Arrow keys step 1% of the range,
    /// PageUp/Down 10%.
    EventHandler MakeSliderHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage,
                                   float* value, int elementWidth,
                                   std::function<void()> onChanged = nullptr,
                                   float minVal = 0.0f, float maxVal = 1.0f,
                                   SliderPress press = SliderPress::Edit);

    /// Phase of a label drag: idle, pressed but under the drag threshold, or dragging.
    enum class DragScalarMode { Idle, Pressing, Dragging };

    /// Per-element state of a draggable label (element data store); internal.
    struct DragLabelState
    {
        ID             activeID;
        DragScalarMode mode = DragScalarMode::Idle;
        tachyon::int2      dragStartPos{ 0, 0 };
    };

    /// Turns mouse drags on a label into LabelDrag* events sent to the next sibling element.
    EventHandler MakeDraggableLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage);

    /// Label handler: a left click focuses the control bound to the label (else the label).
    EventHandler MakeLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage);

    /// Shared by all labels in a group, so the edge highlight is synchronized.
    struct LabelGroupVisualState {
        int32_t* sharedWidth;              ///< the group's label width, CSS px
        int32_t  minWidth;                 ///< CSS px
        int32_t  maxWidth;                 ///< CSS px
        bool* edgeHovered;                 ///< a label's resize edge is hovered
        bool* edgeDragging;                ///< a label's resize edge is being dragged
        std::vector<TextID>* labelTextIDs; ///< for double-click auto-size
        ITextSystem* textSystem;           ///< for measuring label text widths
        const StyleManager* styleManager;  ///< for resolving label padding
        StyleRef labelStyleRef;
    };

    /// Label group variants: the right edge resizes the shared label width.
    EventHandler MakeGroupLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, LabelGroupVisualState groupVisual);
    EventHandler MakeGroupDraggableLabelHandler(GUIStateManager& guiStateManager, const ElementStorage& elementStorage, LabelGroupVisualState groupVisual);

    /// Button handler: a left click, or an activation key while focused, calls `onClick`.
    EventHandler MakeButtonHandler(GUIStateManager& guiStateManager, std::function<void()> onClick);

    /// Wheel handler that scrolls the element's own scroll offset by `wheelStep` pixels per
    /// notch (StyleManager::Px(kWheelScrollStep)).
    EventHandler MakeMouseWheelScrollHandler(ScrollManagement& scrollManagement, float wheelStep);

    /// Per-element state of MakeScrollbarHandler (element data store); internal.
    struct ScrollbarDragState
    {
        ID  draggingID = ID::Invalid();
        int thumbGrabOffset = 0; ///< cursor offset from the thumb's near edge at drag start, pixels
    };

    /// Minimum thumb length in CSS pixels (scale with StyleManager::Px), so the thumb
    /// stays clickable over huge content. The thickness is GUITheme::scrollbarThickness.
    constexpr int32_t kScrollbarMinThumbLength = 20;

    /// Scrollbar track/thumb handler; writes the scroll offset of `containerID` (the scrollable
    /// parent). `trackLength` = 0: span-and-clip, the bar spans kScrollbarAxisSpan and the track
    /// is the parent's innerRect. `trackLength` > 0: the bar is sized explicitly (to leave room
    /// for a corner cap, say) and the track is its own rect.
    EventHandler MakeScrollbarHandler(GUIStateManager& guiStateManager,
                                      ElementStorage& elementStorage,
                                      ID containerID, bool vertical,
                                      int32_t trackLength = 0);

    /// Drag source handler: a left press starts a drag of `payloadType`, with the value from
    /// `getPayloadValue` (else 0) and the payload from `getPayload` (else that value).
    EventHandler MakeDragSourceHandler(GUIStateManager& guiStateManager,
                                       uint32_t payloadType,
                                       std::function<uintptr_t()> getPayloadValue = nullptr,
                                       std::function<std::any()> getPayload = nullptr,
                                       DragVisualizer sourceVisualizer = {});

    /// Drop target handler for `acceptedPayloadType`; `onDrop` gets the payload value and the
    /// source ID. `canDrop` (optional) returning false makes the element a non-target for that
    /// payload.
    EventHandler MakeDropTargetHandler(uint32_t acceptedPayloadType,
                                       std::function<void(uintptr_t payloadValue, ID sourceID)> onDrop,
                                       std::function<bool(uintptr_t payloadValue)> canDrop = nullptr);
    

    
    /// Label-drag speed multiplier: 0.1 with Ctrl, 10 with Shift (which wins), else 1.
    inline double LabelDragSpeedScale(const InputState& state)
    {
        double speed = 1.0;
        if ((state.modifiers & KeyModifier::Control) != KeyModifier::None) speed = 0.1;
        if ((state.modifiers & KeyModifier::Shift)   != KeyModifier::None) speed = 10.0;
        return speed;
    }

    /// Numeric field handler: a left click focuses it for typing, and LabelDrag* events from
    /// its label drag `value` (a cancelled drag restores the start value).
    template<typename T>
    inline EventHandler MakeNumericalBoxHandler(GUIStateManager& guiStateManager, T& value, NumericFieldConfig<T> config = {})
    {
        struct LabelDragSnapshot { T startValue{}; };

        return [&guiStateManager, &value, config](ElementEvent&& event)->bool
        {
            auto& store = guiStateManager.GetElementDataStore();
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

                case EventType::LabelDragBegin:
                {
                    auto& snapshot = store.GetOrCreate<LabelDragSnapshot>(event.id);
                    snapshot.startValue = value;
                    if constexpr (std::is_floating_point_v<T>)
                        if (!std::isfinite(snapshot.startValue)) { snapshot.startValue = T(0); value = T(0); }
                    return true;
                }

                case EventType::LabelDrag:
                {
                    auto* snapshot = store.Get<LabelDragSnapshot>(event.id);
                    if (!snapshot) return false;
                    T newVal = config.Calculate(static_cast<int32_t>(event.state.scroll.x), LabelDragSpeedScale(event.state), snapshot->startValue);
                    value = newVal;
                    return true;
                }

                case EventType::LabelDragEnd:
                    return true;

                case EventType::LabelDragCancel:
                    if (auto* snapshot = store.Get<LabelDragSnapshot>(event.id))
                        value = snapshot->startValue;
                    return true;

                default:
                    break;
            }

            return false;
        };
    }



    /// Applies LabelDrag* events to all components (multi-dimensional Field groups).
    template<typename T>
    inline EventHandler MakeMultiDimDragHandler(GUIStateManager& guiStateManager, T& value,
        NumericFieldConfig<typename TypeDimensions<T>::ComponentType> config = {})
    {
        using ComponentT = std::remove_reference_t<decltype(TypeDimensions<T>::GetComponent(value, 0))>;

        // Value at drag start; in the element data store since handlers are rebuilt every frame.
        struct Snapshot { T startValue{}; };

        return [&guiStateManager, &value, config](ElementEvent&& event) -> bool
        {
            auto& store = guiStateManager.GetElementDataStore();
            switch (event.type)
            {
                case EventType::LabelDragBegin:
                {
                    auto& snapshot = store.GetOrCreate<Snapshot>(event.id);
                    snapshot.startValue = value;
                    if constexpr (std::is_floating_point_v<ComponentT>)
                        for (int i = 0; i < TypeDimensions<T>::dimensions; i++)
                        {
                            ComponentT& comp = TypeDimensions<T>::GetComponent(value, i);
                            ComponentT& startComp = TypeDimensions<T>::GetComponent(snapshot.startValue, i);
                            if (!std::isfinite(startComp)) { startComp = ComponentT(0); comp = ComponentT(0); }
                        }
                    return true;
                }

                case EventType::LabelDrag:
                {
                    auto* snapshot = store.Get<Snapshot>(event.id);
                    if (!snapshot) return false;
                    double speed = LabelDragSpeedScale(event.state);
                    for (int i = 0; i < TypeDimensions<T>::dimensions; i++)
                    {
                        ComponentT& comp = TypeDimensions<T>::GetComponent(value, i);
                        ComponentT startComp = TypeDimensions<T>::GetComponent(snapshot->startValue, i);
                        comp = config.Calculate(static_cast<int32_t>(event.state.scroll.x), speed, startComp);
                    }
                    return true;
                }

                case EventType::LabelDragEnd:
                    return true;

                case EventType::LabelDragCancel:
                    if (auto* snapshot = store.Get<Snapshot>(event.id))
                        value = snapshot->startValue;
                    return true;

                default:
                    break;
            }
            return false;
        };
    }
    /// @}
}
