#include <gui_core/text/gui_utf8.h>
#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/state/gui_state_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/state/gui_tab_chain_manager.h>

#include <gui_core/input/gui_input_manager.h>

#include <gui_core/layout/gui_layout_helpers.h>

#include <gui_core/text/gui_text_system.h>

#include <optional>
#include <algorithm>

namespace tachyon
{
    // ---------------------------------------------------------------------------
    // Label/control resolution helpers
    // ---------------------------------------------------------------------------

    namespace
    {
        inline bool HasElementFlag(const Element& element, ElementFlags flag)
        {
            return (element.flags & flag) != ElementFlags::None;
        }

        void ResolveLabelForFocusedElement(GUIStateManager& guiStateManager, const ElementStorage& storage)
        {
            ID focusedID = guiStateManager.Interaction().GetFocusedID();
            guiStateManager.Labels().ClearFocusedAssociatedLabelIDs();
            if (focusedID == ID::Invalid())
                return;

            ElementRef currentRef = storage.FindElementRefByID(focusedID);
            if (!currentRef.IsValid())
                return;

            while (currentRef.IsValid() && !currentRef.IsRoot())
            {
                const ElementRef parentRef = storage.GetParentRef(currentRef);
                if (!parentRef.IsValid())
                    break;

                const IndexRange siblingRange = storage.GetChildRange(parentRef);
                if (currentRef.index > siblingRange.begin)
                {
                    const ElementRef previousSiblingRef { currentRef.depth, currentRef.index - 1 };
                    if (previousSiblingRef.IsValid())
                    {
                        const Element& previousSibling = storage.GetElementUnsafe(previousSiblingRef);
                        if (HasElementFlag(previousSibling, ElementFlags::IsLabel))
                        {
                            guiStateManager.Labels().AddFocusedAssociatedLabelID(previousSibling.id);
                        }
                    }
                }

                currentRef = parentRef;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    bool InputManager::IsMouseEvent(EventType e)
    {
        return e == EventType::MouseMove  ||
               e == EventType::MouseDown  ||
               e == EventType::MouseUp    ||
               e == EventType::MouseScroll;
    }

    bool InputManager::IsKeyboardEvent(EventType e)
    {
        return e == EventType::KeyDown   ||
               e == EventType::KeyUp     ||
               e == EventType::TextInput;
    }

    static bool TryDispatchHierarchicalScroll(const InputState& state,
                                              ElementStorage&   storage,
                                              GUIStateManager&  guiStateManager)
    {
        if (state.scroll.x == 0.0f && state.scroll.y == 0.0f)
            return false;

        const float step = storage.GetStyleManager().Px(kWheelScrollStep);
        const int2 scrollDelta
        {
            static_cast<int32_t>(state.scroll.x * step),
            static_cast<int32_t>(state.scroll.y * step)
        };

        if (scrollDelta.x == 0 && scrollDelta.y == 0)
            return false;

        const ID hovered = guiStateManager.Interaction().GetHoveredID();
        if (hovered == ID::Invalid())
            return false;

        // Find a scrollable ancestor. Scroll never crosses window boundaries: the
        // window element itself may scroll, but the walk stops after it so a popup
        // can't scroll the window behind it.
        ElementRef current = storage.FindElementRefByID(hovered);
        while (current.IsValid())
        {
            Element& element = storage.GetElementUnsafe(current);
            ElementRect& elementRect = storage.GetElementRectUnsafe(current);

            if (elementRect.layoutStyle != nullptr)
            {
                // Only absorb the axes the element scrolls on; others keep walking up.
                const bool scrollsX = (elementRect.layoutStyle->overflowX == Overflow::Auto);
                const bool scrollsY = (elementRect.layoutStyle->overflowY == Overflow::Auto);
                if (scrollsX || scrollsY)
                {
                    auto& scrollManagement = guiStateManager.GetScrollManagement();
                    auto scrollOffsetHandle = scrollManagement.GetScrollOffsetHandle(element.id);
                    if (scrollOffsetHandle != ScrollOffsetHandle::Invalid)
                    {
                        const int2 offset = scrollManagement.GetScrollOffset(scrollOffsetHandle);
                        const int2 requested = int2 {
                            offset.x + (scrollsX ? scrollDelta.x : 0),
                            offset.y + (scrollsY ? scrollDelta.y : 0)
                        };
                        const int2 clamped = LayoutHelpers::ClampScrollOffsetToContent(requested, elementRect);

                        if (clamped != offset)
                        {
                            scrollManagement.SetScrollOffset(scrollOffsetHandle, clamped);
                            return true;
                        }
                    }
                }
            }

            // Don't step past the window we started in.
            if ((element.flags & ElementFlags::IsWindow) != ElementFlags::None)
                break;

            current = storage.GetParentRef(current);
        }

        return false;
    }

    static bool DispatchDragEventToElement(GUIStateManager& guiStateManager,
                                           ElementStorage& storage,
                                           ID targetID,
                                           EventType type,
                                           int2 screenPos)
    {
        if (targetID == ID::Invalid() || type == EventType::None)
            return false;

        ElementRef ref = storage.FindElementRefByID(targetID);
        if (!ref.IsValid())
            return false;

        InputState state{};
        state.screenPos       = screenPos;
        state.prevScreenPos   = screenPos;
        state.dragSourceID    = guiStateManager.Drag().GetEventSourceID();
        state.dragPayloadType = guiStateManager.Drag().GetEventPayloadType();
        state.dragPayloadValue = guiStateManager.Drag().GetEventPayloadValue();

        // Bubble up like the hover chain, so a parent drop target (e.g. a tree row)
        // receives drag events over its inner children.
        while (ref.IsValid())
        {
            const Element&     elem = storage.GetElementUnsafe(ref);
            const ElementRect& rect = storage.GetElementRectUnsafe(ref);
            if (elem.eventHandler && elem.eventHandler(ElementEvent
                {
                    .id = elem.id,
                    .type = type,
                    .state = state,
                    .rect = &rect,
                    .elementRef = ref
                }))
            {
                return true;
            }
            ref = storage.GetParentRef(ref);
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // Recording - IPlatformInputHandler implementation
    // ---------------------------------------------------------------------------

    void InputManager::OnKeyboard(tachyon::ID /*windowId*/, KeyCode key, int /*repeat*/, KeyModifier modifiers, KeyAction action)
    {
        // KeyChar carries a Unicode codepoint in `key`, not a KeyCode.
        if (action == KeyAction::KeyChar)
        {
            InputState state;
            AppendUtf8(state.textInput, static_cast<uint32_t>(key));
            state.modifiers = modifiers;
            state.buttons   = m_currentButtons;
            m_recordedEvents.push_back({ EventType::TextInput, std::move(state) });
            return;
        }

        InputState state;
        state.lastKey       = key;
        state.lastKeyAction = action;
        state.modifiers     = modifiers;
        state.buttons       = m_currentButtons;

        const EventType type = (action == KeyAction::KeyDown) ? EventType::KeyDown : EventType::KeyUp;
        m_recordedEvents.push_back({ type, std::move(state) });
    }

    void InputManager::OnMouseMove(tachyon::ID /*windowId*/, tachyon::int2 screenPos, tachyon::MouseButtons /*buttons*/, KeyModifier modifiers)
    {
        InputState state;
        state.screenPos    = screenPos;
        state.prevScreenPos = m_prevScreenPos;
        state.screenDelta  = state.screenPos - state.prevScreenPos;
        state.buttons      = m_currentButtons;
        state.prevButtons  = m_currentButtons;
        state.modifiers    = modifiers;

        m_prevScreenPos = state.screenPos;
        m_recordedEvents.push_back({ EventType::MouseMove, std::move(state) });
    }

    void InputManager::OnMouseButton(tachyon::ID /*windowId*/, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers, MouseAction action)
    {
        InputState state;
        state.screenPos    = screenPos;
        state.prevScreenPos = m_prevScreenPos;
        state.screenDelta  = state.screenPos - state.prevScreenPos;
        state.prevButtons  = m_currentButtons;
        state.buttons      = buttons;
        state.modifiers    = modifiers;

        const EventType type = (action == MouseAction::MouseDown) ? EventType::MouseDown : EventType::MouseUp;
        m_prevScreenPos    = state.screenPos;
        m_currentButtons   = buttons;
        m_recordedEvents.push_back({ type, std::move(state) });
    }

    void InputManager::OnMouseScroll(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers)
    {
        InputState state;
        state.screenPos    = screenPos;
        state.prevScreenPos = m_prevScreenPos;
        state.screenDelta  = state.screenPos - state.prevScreenPos;
        state.buttons      = m_currentButtons;
        state.prevButtons  = m_currentButtons;
        state.scroll       = scrollOffset;
        state.modifiers    = modifiers;

        m_prevScreenPos = state.screenPos;
        m_recordedEvents.push_back({ EventType::MouseScroll, std::move(state), windowId });
    }

    void InputManager::SynthesizeMissingMouseEvents(int2 screenPos, MouseButtons /*unused*/, MouseButtons physicalButtons)
    {
        // A mismatch between physical and recorded buttons means SDL missed an event
        // (e.g. swallowed on Windows activation); synthesize it. Comparing against the
        // recorded state means this never double-fires when SDL delivers normally.
        auto check = [&](MouseButtons bit)
        {
            bool physPressed    = (physicalButtons & bit) != MouseButtons::None;
            bool currentPressed = (m_currentButtons & bit) != MouseButtons::None;

            if (physPressed && !currentPressed)
            {
                MouseButtons newButtons = m_currentButtons | bit;
                OnMouseButton(ID::Invalid(), screenPos, newButtons, KeyModifier::None, MouseAction::MouseDown);
            } else
            if (!physPressed && currentPressed)
            {
                MouseButtons newButtons = m_currentButtons & ~bit;
                OnMouseButton(ID::Invalid(), screenPos, newButtons, KeyModifier::None, MouseAction::MouseUp);
            }
        };

        check(MouseButtons::Left);
        check(MouseButtons::Right);
        check(MouseButtons::Middle);
    }

    void InputManager::ConsumeKey()
    {
        for (auto it = m_recordedEvents.rbegin(); it != m_recordedEvents.rend(); ++it)
        {
            if (IsKeyboardEvent(it->type))
            {
                it->consumed = true;
                return;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Pre-hover update
    // ---------------------------------------------------------------------------

    void InputManager::UpdatePreHover(GUIStateManager& guiStateManager)
    {
        // Update drag with the latest recorded mouse position.
        for (auto it = m_recordedEvents.rbegin(); it != m_recordedEvents.rend(); ++it)
        {
            if (it->type == EventType::MouseDown || it->type == EventType::MouseUp   ||
                it->type == EventType::MouseMove || it->type == EventType::MouseScroll)
            {
                guiStateManager.Drag().UpdateFromInput(it->state);
                break;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Process (tab nav, text editor management, dispatch, post-dispatch)
    // ---------------------------------------------------------------------------

    void InputManager::Process(GUIStateManager& guiStateManager,
                               ElementStorage& elementStorage,
                               TabChainManager& tabChainManager,
                               ITextSystem* textSystem,
                               ID overID)
    {
        guiStateManager.Drag().SetStartDistance(elementStorage.GetStyleManager().Px(kDragStartDistance));

        // A mouse press hides the focus ring; keyboard navigation (arrows, Home/End, Page
        // Up/Down outside a text editor, and Tab below) shows it.
        const bool inTextEditor = guiStateManager.TextEditorFocus().GetFocusedID() != ID::Invalid();
        for (const auto& recorded : m_recordedEvents)
        {
            if (recorded.type == EventType::MouseDown)
                guiStateManager.Interaction().SetFocusVisible(false);
            if (recorded.type == EventType::KeyDown && !inTextEditor)
                switch (recorded.state.lastKey)
                {
                    case KeyCode::Up:   case KeyCode::Down:   case KeyCode::Left:     case KeyCode::Right:
                    case KeyCode::Home: case KeyCode::End:    case KeyCode::PageUp:   case KeyCode::PageDown:
                        guiStateManager.Interaction().SetFocusVisible(true);
                        break;
                    default:
                        break;
                }
        }

        // Handle Tab navigation (before dispatching to elements)
        for (auto& recorded : m_recordedEvents)
        {
            if (recorded.type != EventType::KeyDown) continue;
            if (recorded.state.lastKey != KeyCode::Tab) continue;
            if (recorded.consumed) continue;

            bool isShiftHeld   = (recorded.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
            bool isControlHeld = (recorded.state.modifiers & KeyModifier::Control) != KeyModifier::None;

            // Check if current focused element is a textbox that accepts tabs
            bool shouldInsertTab = false;
            if (!isControlHeld) // Ctrl+Tab always navigates
            {
                ID focusedTextEditorID = guiStateManager.TextEditorFocus().GetFocusedID();
                if (focusedTextEditorID != ID::Invalid() &&
                    focusedTextEditorID == guiStateManager.Interaction().GetFocusedID())
                {
                    // Check if this text editor accepts tabs
                    shouldInsertTab = textSystem->DoesTextEditorAcceptTabs(focusedTextEditorID);
                }
            }
            // If we're inserting a tab, let the text editor handle it (don't consume)
            // Otherwise, try to navigate and always consume to prevent text editor from seeing it
            if (!shouldInsertTab)
            {
                // Navigate with tab chain
                ID nextTarget = isShiftHeld ?
                    tabChainManager.GetPrevTabTarget() :
                    tabChainManager.GetNextTabTarget();

                if (nextTarget != ID::Invalid())
                {
                    guiStateManager.Interaction().SetFocusID(nextTarget);
                    guiStateManager.Interaction().SetFocusVisible(true);
                }

                // Always consume Tab key when NOT inserting, even if navigation fails,
                // to prevent text editor from inserting tab when AcceptTabs is not set
                recorded.consumed = true;
            }
            break; // only handle the first Tab keydown per frame
        }

        // Update focused text editor state before dispatch so the defocus check below is accurate.
        if (textSystem)
        {
            const ID focusedID = guiStateManager.Interaction().GetFocusedID();
            if (focusedID != ID::Invalid() && textSystem->IsTextEditor(focusedID))
            {
                guiStateManager.TextEditorFocus().Set(focusedID);
            } else
            {
                guiStateManager.TextEditorFocus().Clear();
            }
        }

        // Defocus the text editor on a click outside it (before Dispatch, while events are still
        // recorded), but only on empty space or another control. Non-control affordances
        // (drag thumbs, static text, ...) must not strip editing focus.
        bool overIsControlOrEmpty = (overID == ID::Invalid());
        if (!overIsControlOrEmpty)
        {
            const ElementRef overRef = elementStorage.FindElementRefByID(overID);
            if (overRef.IsValid())
            {
                const Element& overElem = elementStorage.GetElementUnsafe(overRef);
                overIsControlOrEmpty =
                    (overElem.flags & ElementFlags::IsControl) != ElementFlags::None;
            }
            else
            {
                // ID resolved to nothing - treat as empty space so we still defocus.
                overIsControlOrEmpty = true;
            }
        }

        bool textEditorHovered = (overID == guiStateManager.TextEditorFocus().GetFocusedID());
        if (guiStateManager.Interaction().GetFocusedID() == guiStateManager.TextEditorFocus().GetFocusedID() &&
            !textEditorHovered &&
            overIsControlOrEmpty)
        {
            for (const auto& recorded : m_recordedEvents)
            {
                if (recorded.type == EventType::MouseDown && recorded.state.LeftPressed())
                {
                    guiStateManager.Interaction().SetFocusID(ID::Invalid());
                    guiStateManager.TextEditorFocus().Clear();
                    break;
                }
            }
        }

        // Dispatch input to element handlers (capture/focus/hover rules). Clears recorded events.
        Dispatch(guiStateManager, elementStorage);

        // Post-dispatch: a text field keeps the text focus only while it has the keyboard focus;
        // when the focused field changes, the text system moves its editor.
        auto& textFocus = guiStateManager.TextEditorFocus();
        if (textFocus.GetFocusedID() != guiStateManager.Interaction().GetFocusedID())
            textFocus.Clear();
        const bool textEditorFocused = textFocus.GetFocusedID() != ID::Invalid();
        if (textSystem && textFocus.GetPrevFocusedID() != textFocus.GetFocusedID())
        {
            textSystem->SwitchTextEditor(textFocus.GetPrevFocusedID(), textFocus.GetFocusedID());
            if (textEditorFocused)
                textSystem->RequestSelectAll(textFocus.GetFocusedID());
        }
        textFocus.Commit();

        // Scroll the active text editor's cursor into view if requested by the text system.
        if (textSystem && textEditorFocused)
        {
            auto focusedTextEditorId = guiStateManager.TextEditorFocus().GetFocusedID();
            if (auto cursorRect = textSystem->GetCursorRect(focusedTextEditorId))
            {
                auto& scrollManagement = guiStateManager.GetScrollManagement();
                const ElementRef textEditorRef = elementStorage.FindElementRefByID(focusedTextEditorId);
                // cursorRect is in editor-local space (origin = top-left of inner rect)
                scrollManagement.MakeTextCursorVisibleInTextBox(elementStorage, textEditorRef,
                                                                cursorRect->position, cursorRect->h);
            }
        }

        // Scroll the newly focused element into view (handles tab navigation and click-focus
        // for all control types, including non-text-editor controls).
        {
            const ID focusedID = guiStateManager.Interaction().GetFocusedID();
            if (focusedID != m_prevFocusedID && focusedID != ID::Invalid())
            {
                const ScrollAxis axis = guiStateManager.Interaction().ConsumeFocusScrollAxis();
                if (axis != ScrollAxis::None)
                {
                    const ElementRef focusedRef = elementStorage.FindElementRefByID(focusedID);
                    if (focusedRef.IsValid())
                    {
                        auto& scrollManagement = guiStateManager.GetScrollManagement();
                        scrollManagement.MakeElementVisibleHierarchically(elementStorage, focusedRef, axis);
                    }
                }
            }
            m_prevFocusedID = focusedID;
        }

        // Apply Focused state to the label associated with the currently focused control.
        ResolveLabelForFocusedElement(guiStateManager, elementStorage);
    }

    // ---------------------------------------------------------------------------
    // Dispatch
    // ---------------------------------------------------------------------------

    void InputManager::DispatchSingleEvent(RecordedInputEvent& recorded,
                                           GUIStateManager& guiStateManager,
                                           ElementStorage& storage)
    {
        if (recorded.consumed)
            return;

        const EventType   type  = recorded.type;
        const InputState& state = recorded.state;

        if (IsKeyboardEvent(type))
        {
            // Escape cancels an in-progress drag
            if (guiStateManager.Drag().IsDragging() && type == EventType::KeyDown && state.lastKey == KeyCode::Escape)
            {
                guiStateManager.Drag().Cancel();
                return;
            }

            // The capturing element (mid-gesture: sizer drag, reorder, ...) gets keys like
            // Escape first without taking focus; focus change on drag start would e.g.
            // auto-select a dragged tab's panel. Unhandled keys fall through to focus.
            bool handled = false;
            const ID captureID = guiStateManager.Interaction().GetCaptureID();
            if (captureID != ID::Invalid())
            {
                ElementRef ref = storage.FindElementRefByID(captureID);
                if (ref.IsValid())
                {
                    const Element&     elem = storage.GetElementUnsafe(ref);
                    const ElementRect& rect = storage.GetElementRectUnsafe(ref);
                    handled = elem.eventHandler && elem.eventHandler(ElementEvent{ .id = elem.id, .type = type, .state = state, .rect = &rect, .elementRef = ref });
                }
            }

            // Keyboard/text to focused element
            const ID focused = guiStateManager.Interaction().GetFocusedID();
            if (!handled && focused != ID::Invalid())
            {
                ElementRef ref = storage.FindElementRefByID(focused);
                if (ref.IsValid())
                {
                    const Element&     elem = storage.GetElementUnsafe(ref);
                    const ElementRect& rect = storage.GetElementRectUnsafe(ref);
                    handled = elem.eventHandler && elem.eventHandler(ElementEvent{ .id = elem.id, .type = type, .state = state, .rect = &rect, .elementRef = ref });
                }
            }

            // Global handlers (LIFO). A key release always reaches them, even when an
            // element handled it: it ends held commands that began before focus moved.
            if (handled && type != EventType::KeyUp)
                return;
            // A handler may push or pop handlers; iterate a copy so that can't
            // invalidate the loop or destroy the running handler.
            const std::vector<EventHandler> handlers = m_globalHandlers;
            for (auto it = handlers.rbegin(); it != handlers.rend(); ++it)
            {
                if ((*it)(ElementEvent{ .id = ID::Invalid(), .type = type, .state = state, .rect = nullptr, .elementRef = ElementRef::Invalid() }))
                    return;
            }
        }
        else if (IsMouseEvent(type))
        {
            // Explicit mouse capture: routes all events exclusively to the captured element.
            // Capture is set automatically when a handler returns true on a button press (see hover chain below).
            // It is released once the captured element has received the release of the last held button.
            const ID captureID = guiStateManager.Interaction().GetCaptureID();
            if (captureID != ID::Invalid())
            {
                ElementRef ref = storage.FindElementRefByID(captureID);
                if (ref.IsValid())
                {
                    const Element&     elem = storage.GetElementUnsafe(ref);
                    const ElementRect& rect = storage.GetElementRectUnsafe(ref);
                    if (elem.eventHandler)
                        elem.eventHandler(ElementEvent{ .id = elem.id, .type = type, .state = state, .rect = &rect, .elementRef = ref });
                    // The release that lets go of the last button ends the capture, even when
                    // press and release arrive in the same frame (no physical-state edge to see).
                    if (type == EventType::MouseUp && state.buttons == MouseButtons::None &&
                        guiStateManager.Interaction().GetCaptureID() == captureID)
                        guiStateManager.Interaction().ReleaseCaptureID();
                    return; // exclusive - no other element sees this event while capture is held
                }
                // The captured element vanished mid-gesture: release the stale capture and
                // route normally, or every mouse event is lost until all buttons are up.
                guiStateManager.Interaction().ReleaseCaptureID();
            }

            // Active capture
            const ID active = guiStateManager.Interaction().GetActiveID();
            if (active != ID::Invalid())
            {
                ElementRef ref = storage.FindElementRefByID(active);
                if (ref.IsValid())
                {
                    const Element&     elem = storage.GetElementUnsafe(ref);
                    const ElementRect& rect = storage.GetElementRectUnsafe(ref);
                    if (elem.eventHandler && elem.eventHandler(ElementEvent{ .id = elem.id, .type = type, .state = state, .rect = &rect, .elementRef = ref }))
                        return;
                }
            }

            const ID hovered = guiStateManager.Interaction().GetHoveredID();

            // Hover chain - input handlers run before hierarchical scroll so they can consume
            // wheel events (e.g. for zooming) and prevent the default scroll from also firing.
            // MouseScroll stops at the hovered element's window so it never reaches the window behind.
            if (hovered != ID::Invalid())
            {
                ElementRef cur = storage.FindElementRefByID(hovered);
                while (cur.IsValid())
                {
                    const Element&     elem = storage.GetElementUnsafe(cur);
                    const ElementRect& rect = storage.GetElementRectUnsafe(cur);

                    if (elem.eventHandler && elem.eventHandler(ElementEvent{ .id = elem.id, .type = type, .state = state, .rect = &rect, .elementRef = cur }))
                    {
                        // Auto-capture: if a button was just pressed, lock future events to this element
                        // until all buttons are released, even if the mouse moves outside the element.
                        if (type == EventType::MouseDown)
                            guiStateManager.Interaction().SetCaptureID(elem.id);
                        return;
                    }

                    if (type == EventType::MouseScroll &&
                        (elem.flags & ElementFlags::IsWindow) != ElementFlags::None)
                        break;

                    cur = storage.GetParentRef(cur);
                }
            }

            // Fallback: hierarchical scroll (fires only if no input handler consumed the event above)
            TryDispatchHierarchicalScroll(state, storage, guiStateManager);
        }
    }

    void InputManager::Dispatch(GUIStateManager& guiStateManager, ElementStorage& storage)
    {
        // Drag events are frame-level (computed from GUIStateManager), not tied to individual OS events.
        const ID dragOverTargetID  = guiStateManager.Drag().GetOverTargetID();
        const ID dragOutTargetID   = guiStateManager.Drag().GetOutTargetID();
        const ID dragEnterTargetID = guiStateManager.Drag().GetEnterTargetID();
        const ID dragDropTargetID  = guiStateManager.Drag().GetDropTargetID();

        if (dragOutTargetID != ID::Invalid())
        {
            DispatchDragEventToElement(guiStateManager, storage, dragOutTargetID, EventType::DragOut, m_prevScreenPos);
            guiStateManager.Drag().SetValidatedTargetID(ID::Invalid());
        }

        if (dragEnterTargetID != ID::Invalid())
        {
            const bool accepted = DispatchDragEventToElement(guiStateManager, storage, dragEnterTargetID, EventType::DragEnter, m_prevScreenPos);
            guiStateManager.Drag().SetValidatedTargetID(accepted ? dragOverTargetID : ID::Invalid());
        }

        if (dragOverTargetID != ID::Invalid())
            DispatchDragEventToElement(guiStateManager, storage, dragOverTargetID, EventType::DragOver, m_prevScreenPos);

        if (dragDropTargetID != ID::Invalid())
        {
            DispatchDragEventToElement(guiStateManager, storage, dragDropTargetID, EventType::Drop, m_prevScreenPos);
            guiStateManager.Drag().SetValidatedTargetID(ID::Invalid());
        }
        guiStateManager.Drag().ClearEvents();

        // Dispatch each recorded OS event individually.
        for (auto& recorded : m_recordedEvents)
            DispatchSingleEvent(recorded, guiStateManager, storage);

        m_recordedEvents.clear();
    }

} // namespace tachyon
