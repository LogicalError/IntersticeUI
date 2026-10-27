#pragma once

#include <stdint.h>
#include <string_view>
#include <gui_core/gui_base_types.h>

namespace tachyon
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
        virtual void OnKeyboard(tachyon::ID id, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) = 0;
        /// The mouse moved over window `id`; screenPos in screen pixels, `buttons` those held down.
        virtual void OnMouseMove(tachyon::ID id, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers) = 0;
        /// A mouse button went down or up; `buttons` are those held down after the change.
        virtual void OnMouseButton(tachyon::ID id, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers, tachyon::MouseAction action) = 0;
        /// The wheel turned, by scrollOffset notches; positive y is away from the user (scrolls up).
        virtual void OnMouseScroll(tachyon::ID id, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers) = 0;
        /// IME composition (pre-edit) text, UTF-8, shown but not yet committed; empty
        /// when composing ends. `cursor` is the caret's codepoint offset within it.
        /// Committed text still arrives as KeyChar.
        virtual void OnTextComposition(tachyon::ID id, std::string_view text, int32_t cursor) = 0;
    };
};
