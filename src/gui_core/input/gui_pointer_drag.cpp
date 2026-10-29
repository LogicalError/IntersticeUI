#include <gui_core/input/gui_pointer_drag.h>

#include <gui_core/elements/gui_element_storage.h>

namespace tachyon
{
    namespace
    {
        bool Held(MouseButtons buttons, MouseButtons button) noexcept
        {
            return (buttons & button) != MouseButtons::None;
        }
    }

    bool PointerDrag::Press(const ElementEvent& e, const ElementStorage& storage, MouseButtons pressed, bool atOnce)
    {
        if (e.type != EventType::MouseDown || !Held(e.state.buttons, pressed) || Held(e.state.prevButtons, pressed))
            return false;
        id          = e.id;
        button      = pressed;
        pressScreen = e.state.screenPos;
        pressLocal  = e.LocalCursor(storage);
        dragging    = atOnce;
        return true;
    }

    bool PointerDrag::Move(const ElementEvent& e, int32_t distance)
    {
        if (e.type != EventType::MouseMove || !IsPressed(e.id) || !Held(e.state.buttons, button))
            return false;
        if (!dragging && IsDragTravel(ScreenDelta(e.state), distance))
            dragging = true;
        return dragging;
    }

    PointerDrag::Ended PointerDrag::Release(const ElementEvent& e)
    {
        if (e.type != EventType::MouseUp || !IsPressed(e.id) || Held(e.state.buttons, button))
            return Ended::No;
        const Ended ended = dragging ? Ended::Drag : Ended::Click;
        Reset();
        return ended;
    }

    bool PointerDrag::Cancel(ID element) noexcept
    {
        if (!IsPressed(element))
            return false;
        Reset();
        return true;
    }

    int2 PointerDrag::LocalDelta(const ElementEvent& e, const ElementStorage& storage) const
    {
        return e.LocalCursor(storage) - pressLocal;
    }
}
