#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_input_enums.h>

#include <string>

namespace interstice
{
    // Snapshot of input state for a single recorded OS event.
    struct InputState
    {
        interstice::int2 screenPos{0,0};
        interstice::int2 prevScreenPos{0,0};
        interstice::int2 screenDelta{0,0};

        interstice::MouseButtons buttons = interstice::MouseButtons::None;
        interstice::MouseButtons prevButtons = interstice::MouseButtons::None;

        interstice::KeyModifier modifiers = interstice::KeyModifier::None;

        interstice::float2 scroll{}; // wheel notches (fractional on hi-res devices); label drags: pixels

        interstice::KeyCode lastKey = interstice::KeyCode::None;
        interstice::KeyAction lastKeyAction = interstice::KeyAction::KeyDown;

        // Drag/drop payload metadata (set by dispatcher when dispatching drag events)
        interstice::ID dragSourceID = interstice::ID::Invalid();
        uint32_t dragPayloadType = 0;
        uintptr_t dragPayloadValue = 0;

        // Accumulated text input for this frame (UTF-8)
        std::string textInput;

        // Indicates that this event has already been consumed (e.g., Tab handled by tab navigation)
        bool consumed = false;

        bool LeftDown() const { return (buttons & interstice::MouseButtons::Left) != interstice::MouseButtons::None; }
        bool LeftPressed() const { return LeftDown() && ((prevButtons & interstice::MouseButtons::Left) == interstice::MouseButtons::None); }
        bool LeftReleased() const { return !LeftDown() && ((prevButtons & interstice::MouseButtons::Left) != interstice::MouseButtons::None); }

        bool RightDown() const { return (buttons & interstice::MouseButtons::Right) != interstice::MouseButtons::None; }
        bool RightPressed() const { return RightDown() && ((prevButtons & interstice::MouseButtons::Right) == interstice::MouseButtons::None); }
        bool RightReleased() const { return !RightDown() && ((prevButtons & interstice::MouseButtons::Right) != interstice::MouseButtons::None); }

        bool MiddleDown() const { return (buttons & interstice::MouseButtons::Middle) != interstice::MouseButtons::None; }
        bool MiddlePressed() const { return MiddleDown() && ((prevButtons & interstice::MouseButtons::Middle) == interstice::MouseButtons::None); }
        bool MiddleReleased() const { return !MiddleDown() && ((prevButtons & interstice::MouseButtons::Middle) != interstice::MouseButtons::None); }
    };
}
