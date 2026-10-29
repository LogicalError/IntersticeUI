#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_input_handler_func.h>
#include <gui_core/input/gui_input_state.h>

#include <stdint.h>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    class ElementStorage;

    /// Whether the pointer moving `delta` pixels from a press makes it a drag: `distance` pixels
    /// or more in a straight line. Every press that can be a click or a drag decides with this,
    /// at Px(kDragStartDistance).
    constexpr bool IsDragTravel(int2 delta, int32_t distance) noexcept
    {
        const int64_t dx = delta.x, dy = delta.y;
        return dx * dx + dy * dy >= int64_t(distance) * distance;
    }

    /// Escape going down: cancels the drag of the element that has the event.
    constexpr bool IsDragCancelKey(const InputState& s) noexcept
    {
        return s.lastKey == KeyCode::Escape && s.lastKeyAction == KeyAction::KeyDown;
    }

    /// One press on an element that may become a drag. A control keeps it in its own state and
    /// gives it that element's events: Press when a button goes down, Move on each move, Release
    /// when the button comes up and Cancel on Escape. What the drag changes, and the value to
    /// put back on a cancel, stay with the control.
    ///
    /// The element that returns true for the press gets the mouse capture, so the moves and the
    /// release come to it wherever the pointer goes.
    struct PointerDrag
    {
        ID           id     = ID::Invalid();      ///< the element pressed; Invalid when there is no press
        MouseButtons button = MouseButtons::None; ///< the button held
        int2         pressScreen{ 0, 0 };         ///< pointer at the press, screen pixels
        int2         pressLocal{ 0, 0 };          ///< pointer at the press, in the space of the element's rect
        bool         dragging = false;            ///< moved the drag distance (or a drag from the press on)

        /// A press is held, on any element.
        bool IsHeld() const noexcept { return id != ID::Invalid(); }
        /// A press of `element` is held.
        bool IsPressed(ID element) const noexcept { return id != ID::Invalid() && id == element; }
        /// A press of `element` is held and has become a drag.
        bool IsDragging(ID element) const noexcept { return dragging && IsPressed(element); }

        /// Starts tracking `e` when it is `button` going down: true then. With `atOnce` the press is
        /// a drag from the start (a sizer, a scrollbar thumb) and never a click.
        bool Press(const ElementEvent& e, const ElementStorage& storage,
                   MouseButtons button = MouseButtons::Left, bool atOnce = false);

        /// For a move of the held press: true when it is a drag move, the one that goes past
        /// `distance` pixels (see IsDragTravel) included. False for another element, without a
        /// press, or while the press is still under the distance.
        bool Move(const ElementEvent& e, int32_t distance);

        /// How a press ended.
        enum class Ended : uint8_t
        {
            No,     ///< not the release of the held press
            Click,  ///< released before it became a drag
            Drag    ///< released after dragging
        };
        /// For the held button coming up on `e.id`: how the press ended. There is no press after.
        Ended Release(const ElementEvent& e);

        /// Drops the press of `element` (Escape, a lost capture): true when there was one.
        bool Cancel(ID element) noexcept;
        /// Drops any press.
        void Reset() noexcept { *this = PointerDrag{}; }

        /// Pointer travel since the press, in screen pixels.
        int2 ScreenDelta(const InputState& s) const noexcept { return s.screenPos - pressScreen; }
        /// Pointer travel since the press, in the space of the element's rect (follows zoom).
        int2 LocalDelta(const ElementEvent& e, const ElementStorage& storage) const;
    };
    /// @}
}
