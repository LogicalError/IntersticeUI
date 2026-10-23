#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>     // int2
#include <gui_core/input/gui_input_enums.h>
#include <functional>

namespace interstice
{
    struct ElementRect;    // requires gui/gui_element_storage.h
    struct ElementRef;     // requires gui/gui_element_storage.h
    class  ElementStorage; // requires gui/gui_element_storage.h
    struct InputState;     // requires gui/input/gui_input_state.h

    struct ElementEvent
    {
        interstice::ID             id;
        EventType           type;
        const InputState&   state;
        const ElementRect*  rect;
        const ElementRef&   elementRef;

        // Cursor in the space `rect->rect` is stored in (inverse of ancestor
        // contentTransforms). Compare against the rect with this, not
        // `state.screenPos`, which differs under e.g. a pan/zoom canvas.
        int2 LocalCursor(const ElementStorage& storage) const;
    };

    // Element-level input handler used by the immediate-mode dispatcher.
    // id: element ID being targeted, event: what happened this dispatch, state: current input snapshot, rect: element bounds
    using EventHandler = std::function<bool(ElementEvent&& input)>;

    // Tries `first`, then `second` if first returns false.
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
