#pragma once

#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/gui_id.h>                  // ID is passed by value to inline forwarding methods
#include <gui_core/input/gui_input_enums.h>   // KeyCode/KeyModifier/MouseButtons/KeyAction/MouseAction enums

#include <initializer_list>
#include <vector>

namespace GUI
{
    // Sends every platform input event to all handlers (e.g. InputManager + ITextSystem);
    // none can block the others.
    class InputHandlerBroadcaster final : public IPlatformInputHandler
    {
    public:
        InputHandlerBroadcaster() = default;

        explicit InputHandlerBroadcaster(std::initializer_list<IPlatformInputHandler*> handlers)
            : m_handlers(handlers) {}

        void OnKeyboard(GUI::ID id, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnKeyboard(id, key, repeat, modifiers, action);
        }

        void OnMouseMove(GUI::ID id, GUI::int2 screenPos, GUI::MouseButtons buttons, KeyModifier modifiers) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnMouseMove(id, screenPos, buttons, modifiers);
        }

        void OnMouseButton(GUI::ID id, GUI::int2 screenPos, GUI::MouseButtons buttons, KeyModifier modifiers, GUI::MouseAction action) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnMouseButton(id, screenPos, buttons, modifiers, action);
        }

        void OnMouseScroll(GUI::ID id, GUI::int2 screenPos, GUI::float2 scrollOffset, KeyModifier modifiers) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnMouseScroll(id, screenPos, scrollOffset, modifiers);
        }

        void OnTextComposition(GUI::ID id, std::string_view text, int32_t cursor) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnTextComposition(id, text, cursor);
        }

    private:
        std::vector<IPlatformInputHandler*> m_handlers;
    };
} // namespace GUI
