#pragma once

#include <stdint.h>
#include <gui_core/gui_enum_operators.h>

namespace tachyon
{
    // Interaction distances in CSS px; scale them with StyleManager::Px.
    constexpr float   kWheelScrollStep   = 30.0f; ///< scrolled per wheel notch (CSS px)
    constexpr int32_t kDragStartDistance = 4;     ///< pointer travel before a press becomes a drag (CSS px)

    /// Modifier keys held, combined with |. The Alt key reports as Option on macOS; Command is
    /// the Cmd key there and the Windows or Super key elsewhere.
    enum class KeyModifier : uint8_t
    {
        None = 0,
        Shift = 1,
        Control = 2,
        Alt = 4,
        Option = 8,
        Command = 16,
    };
    TACHYON_ENUM_OPERATORS(KeyModifier)

    /// Mouse buttons, combined with |.
    enum class MouseButtons : uint8_t
    {
        None = 0,
        Left = 1,
        Middle = 2,
        Right = 4,
        X1 = 8,
        X2 = 16,
    };
    TACHYON_ENUM_OPERATORS(MouseButtons)

    /// A keyboard event: a key down or up, or a typed character (KeyChar).
    enum class KeyAction : uint8_t
    {
        KeyDown,
        KeyUp,
        KeyChar
    };

    /// Whether a mouse button went down or up.
    enum class MouseAction : uint8_t
    {
        MouseDown,
        MouseUp
    };

    /// The kind of input event the GUI dispatches to element handlers.
    enum class EventType : uint8_t
    {
        None = 0,
        MouseMove,
        MouseDown,       ///< which button: state.buttons & ~state.prevButtons
        MouseUp,         ///< which button: state.prevButtons & ~state.buttons
        MouseScroll,
        KeyDown,
        KeyUp,
        TextInput,
        DragEnter,
        DragOver,
        DragOut,
        Drop,

        LabelDragBegin,   ///< label -> control: drag starting; state.scroll.x = 0
        /// label -> control: drag in progress; state.scroll.x = pixels moved since the start
        LabelDrag,
        LabelDragEnd,     ///< label -> control: drag committed
        LabelDragCancel,  ///< label -> control: drag cancelled (Escape)
    };

    /// Keys by what the keyboard layout makes them, not by position. KeyAction::KeyChar
    /// events carry a Unicode codepoint in this type instead.
    enum class KeyCode : uint32_t
    {
        None = 0,

        Return,
        Escape,
        Backspace,
        Tab,
        Space,
        Exclaim,
        DoubleQuote,
        Hash,
        Percent,
        Dollar,
        Ampersand,
        Quote,
        LeftParen,
        RightParen,
        Asterisk,
        Plus,
        Comma,
        Minus,
        Period,
        Slash,
        Key0,
        Key1,
        Key2,
        Key3,
        Key4,
        Key5,
        Key6,
        Key7,
        Key8,
        Key9,
        Colon,
        Semicolon,
        Less,
        Equals,
        Greater,
        Question,
        At,

        LeftBracket,
        Backslash,
        RightBracket,
        Caret,
        Underscore,
        Backquote,
        KeyA,
        KeyB,
        KeyC,
        KeyD,
        KeyE,
        KeyF,
        KeyG,
        KeyH,
        KeyI,
        KeyJ,
        KeyK,
        KeyL,
        KeyM,
        KeyN,
        KeyO,
        KeyP,
        KeyQ,
        KeyR,
        KeyS,
        KeyT,
        KeyU,
        KeyV,
        KeyW,
        KeyX,
        KeyY,
        KeyZ,

        Capslock,

        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        Printscreen,
        Scrolllock,
        Pause,
        Insert,
        Home,
        PageUp,
        Delete,
        End,
        PageDown,
        Right,
        Left,
        Down,
        Up,

        NUMLOCKCLEAR,

        KeyPadDivide,
        KeyPadMultiply,
        KeyPadMinus,
        KeyPadPlus,
        KeyPadEnter,
        KeyPad1,
        KeyPad2,
        KeyPad3,
        KeyPad4,
        KeyPad5,
        KeyPad6,
        KeyPad7,
        KeyPad8,
        KeyPad9,
        KeyPad0,
        KeyPadPeriod,
        KeyPadEquals,
        KeyPadComma,
        KeyPadEqualsAs400,
        KeyPad00,
        KeyPad000,
        KeyPadLeftParen,
        KeyPadRightParen,
        KeyPadLeftBrace,
        KeyPadRightBrace,
        KeyPadTab,
        KeyPadBackspace,
        KeyPadA,
        KeyPadB,
        KeyPadC,
        KeyPadD,
        KeyPadE,
        KeyPadF,
        KeyPadXor,
        KeyPadPower,
        KeyPadPercent,
        KeyPadLess,
        KeyPadGreater,
        KeyPadAmpersand,
        KeyPadDoubleAmpersand,
        KeyPadVerticalbar,
        KeyPadDoubleVerticalbar,
        KeyPadColon,
        KeyPadHash,
        KeyPadSpace,
        KeyPadAt,
        KeyPadExclam,
        KeyPadMemstore,
        KeyPadMemrecall,
        KeyPadMemclear,
        KeyPadMemadd,
        KeyPadMemsubtract,
        KeyPadMemmultiply,
        KeyPadMemdivide,
        KeyPadPlusminus,
        KeyPadClear,
        KeyPadClearentry,
        KeyPadBinary,
        KeyPadOctal,
        KeyPadDecimal,
        KeyPadHexadecimal,

        Application,
        Power,
        F13,
        F14,
        F15,
        F16,
        F17,
        F18,
        F19,
        F20,
        F21,
        F22,
        F23,
        F24,
        Execute,
        Help,
        Menu,
        Select,
        Stop,
        Again,
        Undo,
        Cut,
        Copy,
        Paste,
        Find,
        Mute,
        Volumeup,
        Volumedown,

        Alterase,
        Sysreq,
        Cancel,
        Clear,
        Prior,
        Return2,
        Separator,
        Out,
        Oper,
        Clearagain,
        Crsel,
        Exsel,

        ThousandsSeparator,
        DecimalSeparator,
        CurrencyUnit,
        CurrencySubUnit,
        LeftControl,
        LeftShift,
        LeftAlt,
        LeftCommand,
        RightControl,
        RightShift,
        RightAlt,
        RightCommand,

        Mode,

        AudioNext,
        AudioPrev,
        AudioStop,
        AudioPlay,
        AudioMute,
        MediaSelect,
        WWW,
        Mail,
        Calculator,
        Computer,

        ACSearch,
        ACHome,
        ACBack,
        ACForward,
        ACStop,
        ACRefresh,
        ACBookmarks,

        BrightnessDown,
        BrightnessUp,
        DisplaySwitch,
        KeyboardIlluminationToggle,
        KeyboardIlluminationDown,
        KeyboardIlluminationUp,
        Eject,
        Sleep,
        App1,
        App2,

        AudioRewind,
        AudioFastforward,

        SoftLeft,
        SoftRight,
        Call,
        EndCall,
    };
}