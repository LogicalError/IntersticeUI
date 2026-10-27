#pragma once

#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/gui_id.h>                  // ID is passed by value to inline forwarding methods
#include <gui_core/input/gui_input_enums.h>   // KeyCode/KeyModifier/MouseButtons/KeyAction/MouseAction enums

#include <initializer_list>
#include <vector>

namespace tachyon
{
    /// Sends every platform input event to all handlers (e.g. InputManager + ITextSystem);
    /// none can block the others.
    class InputHandlerBroadcaster final : public IPlatformInputHandler
    {
    public:
        InputHandlerBroadcaster() = default;

        explicit InputHandlerBroadcaster(std::initializer_list<IPlatformInputHandler*> handlers)
            : m_handlers(handlers) {}

        void OnKeyboard(tachyon::ID id, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnKeyboard(id, key, repeat, modifiers, action);
        }

        void OnMouseMove(tachyon::ID id, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnMouseMove(id, screenPos, buttons, modifiers);
        }

        void OnMouseButton(tachyon::ID id, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers, tachyon::MouseAction action) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnMouseButton(id, screenPos, buttons, modifiers, action);
        }

        void OnMouseScroll(tachyon::ID id, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnMouseScroll(id, screenPos, scrollOffset, modifiers);
        }

        void OnTextComposition(tachyon::ID id, std::string_view text, int32_t cursor) override
        {
            for (IPlatformInputHandler* h : m_handlers)
                if (h) h->OnTextComposition(id, text, cursor);
        }

    private:
        std::vector<IPlatformInputHandler*> m_handlers;
    };
} // namespace tachyon
