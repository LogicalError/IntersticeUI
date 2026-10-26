#pragma once

#include <stdint.h>
#include <string_view>
#include <gui_core/gui_base_types.h>

namespace interstice
{
    struct ID;
    enum class KeyCode : uint32_t;
    enum class KeyModifier : uint8_t;
    enum class KeyAction : uint8_t;

    enum class MouseButtons : uint8_t;
    enum class MouseAction : uint8_t;

    /// Platform-level input interface: receives raw OS events (keyboard, mouse)
    /// and forwards them into the GUI input pipeline.
    class IPlatformInputHandler
    {
    public:
        virtual ~IPlatformInputHandler() = default;
        /// A key went down or up (`key` is its KeyCode, `repeat` nonzero for an auto-repeat), or
        /// a character was typed (KeyAction::KeyChar: `key` holds its Unicode codepoint).
        virtual void OnKeyboard(interstice::ID id, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) = 0;
        /// The mouse moved over window `id`; screenPos in screen pixels, `buttons` those held down.
        virtual void OnMouseMove(interstice::ID id, interstice::int2 screenPos, interstice::MouseButtons buttons, KeyModifier modifiers) = 0;
        /// A mouse button went down or up; `buttons` are those held down after the change.
        virtual void OnMouseButton(interstice::ID id, interstice::int2 screenPos, interstice::MouseButtons buttons, KeyModifier modifiers, interstice::MouseAction action) = 0;
        /// The wheel turned, by scrollOffset notches; positive y is away from the user (scrolls up).
        virtual void OnMouseScroll(interstice::ID id, interstice::int2 screenPos, interstice::float2 scrollOffset, KeyModifier modifiers) = 0;
        /// IME composition (pre-edit) text, UTF-8, shown but not yet committed; empty
        /// when composing ends. `cursor` is the caret's codepoint offset within it.
        /// Committed text still arrives as KeyChar.
        virtual void OnTextComposition(interstice::ID id, std::string_view text, int32_t cursor) = 0;
    };
};
