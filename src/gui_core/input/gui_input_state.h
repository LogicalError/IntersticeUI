#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_input_enums.h>

#include <string>

namespace GUI
{
    // Snapshot of input state for a single recorded OS event.
    struct InputState
    {
        GUI::int2 screenPos{0,0};
        GUI::int2 prevScreenPos{0,0};
        GUI::int2 screenDelta{0,0};

        GUI::MouseButtons buttons = GUI::MouseButtons::None;
        GUI::MouseButtons prevButtons = GUI::MouseButtons::None;

        GUI::KeyModifier modifiers = GUI::KeyModifier::None;

        GUI::float2 scroll{}; // wheel notches (fractional on hi-res devices); label drags: pixels

        GUI::KeyCode lastKey = GUI::KeyCode::None;
        GUI::KeyAction lastKeyAction = GUI::KeyAction::KeyDown;

        // Drag/drop payload metadata (set by dispatcher when dispatching drag events)
        GUI::ID dragSourceID = GUI::ID::Invalid();
        uint32_t dragPayloadType = 0;
        uintptr_t dragPayloadValue = 0;

        // Accumulated text input for this frame (UTF-8)
        std::string textInput;

        // Indicates that this event has already been consumed (e.g., Tab handled by tab navigation)
        bool consumed = false;

        bool LeftDown() const { return (buttons & GUI::MouseButtons::Left) != GUI::MouseButtons::None; }
        bool LeftPressed() const { return LeftDown() && ((prevButtons & GUI::MouseButtons::Left) == GUI::MouseButtons::None); }
        bool LeftReleased() const { return !LeftDown() && ((prevButtons & GUI::MouseButtons::Left) != GUI::MouseButtons::None); }

        bool RightDown() const { return (buttons & GUI::MouseButtons::Right) != GUI::MouseButtons::None; }
        bool RightPressed() const { return RightDown() && ((prevButtons & GUI::MouseButtons::Right) == GUI::MouseButtons::None); }
        bool RightReleased() const { return !RightDown() && ((prevButtons & GUI::MouseButtons::Right) != GUI::MouseButtons::None); }

        bool MiddleDown() const { return (buttons & GUI::MouseButtons::Middle) != GUI::MouseButtons::None; }
        bool MiddlePressed() const { return MiddleDown() && ((prevButtons & GUI::MouseButtons::Middle) == GUI::MouseButtons::None); }
        bool MiddleReleased() const { return !MiddleDown() && ((prevButtons & GUI::MouseButtons::Middle) != GUI::MouseButtons::None); }
    };
}
