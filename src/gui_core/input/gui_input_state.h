#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_input_enums.h>

#include <string>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    /// Snapshot of input state for a single recorded OS event.
    struct InputState
    {
        tachyon::int2 screenPos{0,0};
        tachyon::int2 prevScreenPos{0,0};
        tachyon::int2 screenDelta{0,0};

        tachyon::MouseButtons buttons = tachyon::MouseButtons::None;
        tachyon::MouseButtons prevButtons = tachyon::MouseButtons::None;

        tachyon::KeyModifier modifiers = tachyon::KeyModifier::None;

        tachyon::float2 scroll{}; ///< wheel notches (fractional on hi-res devices); label drags: pixels

        tachyon::KeyCode lastKey = tachyon::KeyCode::None;
        tachyon::KeyAction lastKeyAction = tachyon::KeyAction::KeyDown;

        /// Drag-and-drop source and payload; set for drag events.
        ///@{
        tachyon::ID dragSourceID = tachyon::ID::Invalid();
        uint32_t dragPayloadType = 0;
        uintptr_t dragPayloadValue = 0;
        ///@}

        /// Text typed with this event (UTF-8).
        std::string textInput;

        /// True when the event was already handled (e.g. Tab by tab navigation).
        bool consumed = false;

        bool LeftDown() const { return (buttons & tachyon::MouseButtons::Left) != tachyon::MouseButtons::None; }
        bool LeftPressed() const { return LeftDown() && ((prevButtons & tachyon::MouseButtons::Left) == tachyon::MouseButtons::None); }
        bool LeftReleased() const { return !LeftDown() && ((prevButtons & tachyon::MouseButtons::Left) != tachyon::MouseButtons::None); }

        bool RightDown() const { return (buttons & tachyon::MouseButtons::Right) != tachyon::MouseButtons::None; }
        bool RightPressed() const { return RightDown() && ((prevButtons & tachyon::MouseButtons::Right) == tachyon::MouseButtons::None); }
        bool RightReleased() const { return !RightDown() && ((prevButtons & tachyon::MouseButtons::Right) != tachyon::MouseButtons::None); }

        bool MiddleDown() const { return (buttons & tachyon::MouseButtons::Middle) != tachyon::MouseButtons::None; }
        bool MiddlePressed() const { return MiddleDown() && ((prevButtons & tachyon::MouseButtons::Middle) == tachyon::MouseButtons::None); }
        bool MiddleReleased() const { return !MiddleDown() && ((prevButtons & tachyon::MouseButtons::Middle) != tachyon::MouseButtons::None); }
    };
    /// @}
}
