#pragma once

#include <stdint.h>
#include <string_view>
#include <gui_core/gui_base_types.h>

namespace GUI
{
    struct ID;
    enum class KeyCode : uint32_t;
    enum class KeyModifier : uint8_t;
    enum class KeyAction : uint8_t;

    enum class MouseButtons : uint8_t;
    enum class MouseAction : uint8_t;

    // Platform-level input interface: receives raw OS events (keyboard, mouse)
    // and forwards them into the GUI input pipeline.
    class IPlatformInputHandler
    {
    public:
        virtual ~IPlatformInputHandler() = default;
        virtual void OnKeyboard(GUI::ID id, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) = 0;
        virtual void OnMouseMove(GUI::ID id, GUI::int2 screenPos, GUI::MouseButtons buttons, KeyModifier modifiers) = 0;
        virtual void OnMouseButton(GUI::ID id, GUI::int2 screenPos, GUI::MouseButtons buttons, KeyModifier modifiers, GUI::MouseAction action) = 0;
        virtual void OnMouseScroll(GUI::ID id, GUI::int2 screenPos, GUI::float2 scrollOffset, KeyModifier modifiers) = 0;
        // IME composition (pre-edit) text, UTF-8, shown but not yet committed; empty
        // when composing ends. `cursor` is the caret's codepoint offset within it.
        // Committed text still arrives as KeyChar.
        virtual void OnTextComposition(GUI::ID id, std::string_view text, int32_t cursor) = 0;
    };
};
