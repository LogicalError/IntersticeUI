#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>     // int2
#include <gui_core/input/gui_input_enums.h>
#include <functional>

namespace tachyon
{
    struct ElementRect;    // gui_core/elements/gui_element.h
    struct ElementRef;     // gui_core/elements/gui_element.h
    class  ElementStorage; // gui_core/elements/gui_element_storage.h
    struct InputState;     // gui_core/input/gui_input_state.h

    /// An input event delivered to an element's EventHandler.
    struct ElementEvent
    {
        tachyon::ID             id;          ///< the targeted element
        EventType           type;
        const InputState&   state;       ///< the input snapshot of this event
        const ElementRect*  rect;        ///< the element's layout rects
        const ElementRef&   elementRef;

        /// Cursor in the space `rect->rect` is stored in (inverse of ancestor
        /// contentTransforms). Compare against the rect with this, not
        /// `state.screenPos`, which differs under e.g. a pan/zoom canvas.
        int2 LocalCursor(const ElementStorage& storage) const;
    };

    /// Element-level input handler used by the immediate-mode dispatcher; returns true when it
    /// consumed the event.
    using EventHandler = std::function<bool(ElementEvent&& input)>;

    /// A handler that tries `first`, then `second` when `first` returns false.
    inline EventHandler Chain(EventHandler first, EventHandler second)
    {
        return [f = std::move(first), s = std::move(second)](ElementEvent&& event) -> bool
        {
            if (f && f(ElementEvent{ event })) return true;
            if (s && s(std::move(event))) return true;
            return false;
        };
    }
}
