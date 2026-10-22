#pragma once

#include <vector>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/input/gui_input_handler_func.h>

namespace GUI
{
    class ScrollManagement;
    class ElementStorage;
    class GUIStateManager;
    class TabChainManager;
    class ITextSystem;

    // A single OS input event recorded for dispatch this frame.
    struct RecordedInputEvent
    {
        EventType  type     = EventType::None;
        InputState state    = {};
        GUI::ID    windowId = GUI::ID::Invalid(); // OS window that received the event
        bool       consumed = false; // true if already handled (event will be skipped during Dispatch)
    };

    // Manages all frame input: receives raw OS events, records them, and processes
    // the full input pipeline each frame (tab navigation, text editor focus, dispatch, post-dispatch).
    class InputManager : public IPlatformInputHandler
    {
    public:
        InputManager() = default;
        ~InputManager() = default;

        // IPlatformInputHandler — called by the platform backend when OS events arrive.
        void OnKeyboard(GUI::ID windowId, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) override;
        void OnMouseMove(GUI::ID windowId, GUI::int2 screenPos, GUI::MouseButtons buttons, KeyModifier modifiers) override;
        void OnMouseButton(GUI::ID windowId, GUI::int2 screenPos, GUI::MouseButtons buttons, KeyModifier modifiers, MouseAction action) override;
        void OnMouseScroll(GUI::ID windowId, GUI::int2 screenPos, GUI::float2 scrollOffset, KeyModifier modifiers) override;
        void OnTextComposition(GUI::ID, std::string_view, int32_t) override {} // the text editor shows it

        // Push a global handler onto the stack. Handlers are tried in LIFO order
        // after element-level handlers have had a chance to consume the input.
        void PushGlobalHandler(EventHandler h)
        {
            if (h) m_globalHandlers.push_back(std::move(h));
        }

        // Pop the top-most global handler. Returns true if a handler was popped.
        bool PopGlobalHandler()
        {
            if (m_globalHandlers.empty()) return false;
            m_globalHandlers.pop_back();
            return true;
        }

        // Mark the most recently recorded keyboard event as consumed.
        // Consumed events are skipped during Dispatch.
        void ConsumeKey();

        // Inspect recorded events (e.g., read them before calling Process).
        const std::vector<RecordedInputEvent>& GetRecordedEvents() const { return m_recordedEvents; }

        // Mouse buttons held as of the last recorded button event.
        MouseButtons GetCurrentButtons() const { return m_currentButtons; }

        // Pre-hover step: must be called before hover/EndFrame. Updates the drag position.
        void UpdatePreHover(GUIStateManager& guiStateManager);

        // Detect mouse button presses that the OS swallowed (e.g. clicking behind
        // a parented popup window on Windows) and inject synthetic MouseDown/MouseUp
        // events so the GUI layer sees them. Call after WindowManager::Update(), before Process.
        void SynthesizeMissingMouseEvents(int2 screenPos,MouseButtons lastPhysicalButtons, MouseButtons physicalButtons);

        // Main per-frame input processing: tab navigation, text editor focus management,
        // dispatch to element handlers, and post-dispatch state updates.
        // Must be called after guiStateManager.EndFrame().
        void Process(GUIStateManager& guiStateManager,
                     ElementStorage& elementStorage,
                     TabChainManager& tabChainManager,
                     ITextSystem* textSystem,
                     ID overID);

        // Dispatch each recorded event to elements using capture/focus/hover rules.
        // Clears the event list at the end. (Also called internally by Process.)
        void Dispatch(GUIStateManager& guiStateManager, ElementStorage& storage);

    private:
        static bool IsMouseEvent(EventType type);
        static bool IsKeyboardEvent(EventType type);

        void DispatchSingleEvent(RecordedInputEvent& recorded, GUIStateManager& guiStateManager, ElementStorage& storage);

        // Persists across frames for computing per-event position deltas and button states.
        int2          m_prevScreenPos  {};
        MouseButtons  m_currentButtons { MouseButtons::None };
        ID            m_prevFocusedID  = ID::Invalid();

        std::vector<RecordedInputEvent> m_recordedEvents;
        std::vector<EventHandler>       m_globalHandlers;
    };
}
