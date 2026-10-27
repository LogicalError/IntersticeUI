#pragma once

#include <vector>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/input/gui_input_handler_func.h>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    class ScrollManagement;
    class ElementStorage;
    class GUIStateManager;
    class TabChainManager;
    class ITextSystem;

    /// A single OS input event recorded for dispatch this frame.
    struct RecordedInputEvent
    {
        EventType  type     = EventType::None;
        InputState state    = {};
        tachyon::ID    windowId = tachyon::ID::Invalid(); ///< OS window that received the event
        bool       consumed = false; ///< already handled; Dispatch skips it
    };

    /// Receives raw OS events, records them, and runs the frame's input pipeline (tab
    /// navigation, text editor focus, dispatch, post-dispatch).
    class InputManager : public IPlatformInputHandler
    {
    public:
        InputManager() = default;
        ~InputManager() = default;

        /// IPlatformInputHandler: record OS events for this frame's dispatch.
        ///@{
        void OnKeyboard(tachyon::ID windowId, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) override;
        void OnMouseMove(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers) override;
        void OnMouseButton(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers, MouseAction action) override;
        void OnMouseScroll(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers) override;
        void OnTextComposition(tachyon::ID, std::string_view, int32_t) override {} // the text editor shows it
        ///@}

        /// Pushes a global handler. Global handlers are tried last-pushed first, after
        /// element-level handlers had a chance to consume the input.
        void PushGlobalHandler(EventHandler h)
        {
            if (h) m_globalHandlers.push_back(std::move(h));
        }

        /// Pops the last pushed global handler; false when there is none.
        bool PopGlobalHandler()
        {
            if (m_globalHandlers.empty()) return false;
            m_globalHandlers.pop_back();
            return true;
        }

        /// Marks the most recently recorded keyboard event as consumed, so Dispatch skips it.
        void ConsumeKey();

        /// This frame's recorded events, e.g. to read them before Process.
        const std::vector<RecordedInputEvent>& GetRecordedEvents() const { return m_recordedEvents; }

        /// Mouse buttons held as of the last recorded button event.
        MouseButtons GetCurrentButtons() const { return m_currentButtons; }

        /// Updates the drag position from the latest recorded mouse event; call before hover
        /// resolution and GUIStateManager::EndFrame.
        void UpdatePreHover(GUIStateManager& guiStateManager);

        /// Detects mouse button presses the OS swallowed (e.g. clicking behind a parented popup
        /// window on Windows) and injects synthetic MouseDown/MouseUp events for them. Call after
        /// the window manager's Update, before Process.
        void SynthesizeMissingMouseEvents(int2 screenPos,MouseButtons lastPhysicalButtons, MouseButtons physicalButtons);

        /// Per-frame input processing: tab navigation, text editor focus, dispatch to element
        /// handlers, and post-dispatch state updates. Call after guiStateManager.EndFrame().
        void Process(GUIStateManager& guiStateManager,
                     ElementStorage& elementStorage,
                     TabChainManager& tabChainManager,
                     ITextSystem* textSystem,
                     ID overID);

        /// Dispatches each recorded event to elements by capture / focus / hover rules, then
        /// clears the events. Process calls it.
        void Dispatch(GUIStateManager& guiStateManager, ElementStorage& storage);

    private:
        static bool IsMouseEvent(EventType type);
        static bool IsKeyboardEvent(EventType type);

        void DispatchSingleEvent(RecordedInputEvent& recorded, GUIStateManager& guiStateManager, ElementStorage& storage);

        /// Persists across frames for computing per-event position deltas and button states.
        int2          m_prevScreenPos  {};
        MouseButtons  m_currentButtons { MouseButtons::None };
        ID            m_prevFocusedID  = ID::Invalid();

        std::vector<RecordedInputEvent> m_recordedEvents;
        std::vector<EventHandler>       m_globalHandlers;
    };
    /// @}
}
