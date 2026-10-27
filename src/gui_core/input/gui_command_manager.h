#pragma once

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_handler_func.h>
#include <gui_core/input/gui_undo_history.h>
#include <gui_core/style/gui_style_text.h> // IconID

#include <stdint.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tachyon
{
    /// A set of shortcuts that is active while the app says so (e.g. a node view's while it has
    /// the focus). Opaque: other values come from CommandManager::RegisterMode. Default is
    /// always registered and is the fallback when the active mode has no matching command.
    enum class CommandMode : uint32_t
    {
        Default = 0
    };

    /// Handle to a registered command, stable for the manager's lifetime. The zero value is
    /// invalid, so structs holding one can be zero-initialized; real IDs start at 1.
    struct CommandID
    {
        uint32_t value = 0;

        constexpr bool IsValid() const { return value != 0; }
        constexpr explicit operator bool() const { return value != 0; }
        constexpr bool operator==(const CommandID&) const = default;
        static constexpr CommandID Invalid() { return {}; }
    };

    /// Registry of commands: named actions with an optional icon and an optional shortcut key,
    /// each firing one callback when invoked (clicked from a menu / toolbar, or via its key).
    /// Commands are organized by named modes.
    ///
    /// Modes are registered by name and return an opaque CommandMode:
    ///
    ///     CommandMode sculpt = commandManager.RegisterMode("Sculpt");
    ///     commandManager.RegisterCommand(sculpt, KeyCode::KeyG, KeyModifier::None, []{ ... }, "Grab");
    ///
    /// A command may have no shortcut key (KeyCode::None): it never dispatches from
    /// the keyboard but is still listed in the panel, where the user can bind a key.
    ///
    /// Runs as a global InputManager handler (MakeHandler), so focused controls, a focused
    /// text editor and state.consumed all take keys first.
    ///
    /// Shortcut lookup order: active mode first, then Default as fallback.
    class CommandManager
    {
    public:
        /// A registered command.
        struct Command
        {
            CommandMode           mode;
            IconID                icon = IconID::Invalid; ///< optional
            KeyCode               key;
            KeyModifier           modifiers;
            /// Second key of a 2-key chord (e.g. Ctrl+K then Ctrl+S); None = single-key.
            KeyCode               chord2Key  = KeyCode::None;
            KeyModifier           chord2Mods = KeyModifier::None;
            std::string           description;
            std::string           displayString; ///< cached "Ctrl+Shift+D" or "Ctrl+K Ctrl+S"
            /// Instant: `callback` fires on press, `onEnd` is empty.
            /// Held (RegisterHeldCommand): `callback` on press, `onEnd` on release.
            ///@{
            std::function<void()> callback;
            std::function<void()> onEnd;
            ///@}

            bool IsChord()     const { return chord2Key != KeyCode::None; }
            bool HasShortcut() const { return key != KeyCode::None; }
            bool IsHeld()      const { return static_cast<bool>(onEnd); }
        };

        /// The app's undo history (text fields push their edits to it).
        UndoHistory&       History()       { return m_history; }
        const UndoHistory& History() const { return m_history; }

        CommandManager()
        {
            m_modeNames.push_back("Default");
            m_byMode.emplace_back(); // bucket for Default
        }

        /// Registers a named mode and returns the CommandMode to use when registering commands
        /// or calling SetMode. Registering the same name twice returns the existing value.
        CommandMode RegisterMode(std::string_view name)
        {
            for (uint32_t i = 0; i < uint32_t(m_modeNames.size()); i++)
                if (m_modeNames[i] == name) return CommandMode(i);
            m_modeNames.emplace_back(name);
            m_byMode.emplace_back();
            return CommandMode(m_modeNames.size() - 1);
        }

        /// The name `mode` was registered with; empty for an unknown mode.
        std::string_view GetModeName(CommandMode mode) const
        {
            uint32_t index = uint32_t(mode);
            return (index < m_modeNames.size()) ? std::string_view(m_modeNames[index]) : std::string_view{};
        }

        uint32_t GetModeCount() const { return uint32_t(m_modeNames.size()); }

        /// Makes `mode` the active mode, whose shortcuts are tried before Default. Cancels any
        /// pending chord prefix.
        void        SetMode(CommandMode mode)
        {
            m_currentMode       = mode;
            m_pendingChord.key  = KeyCode::None;
            m_pendingChord.mods = KeyModifier::None;
        }
        CommandMode GetMode() const { return m_currentMode; }

        /// Registers `callback` in `mode`; `description` names it in menus and shortcut lists.
        /// `key` may be KeyCode::None (menu / toolbar only). Re-registering an existing
        /// (mode, key, modifiers) updates that command in place instead of adding one that
        /// could never dispatch. No-shortcut commands never collide.
        CommandID RegisterCommand(CommandMode mode, IconID icon, KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            if (const CommandID existing = FindCommand(mode, key, modifiers))
            {
                TACHYON_LOG_DEBUG(::tachyon::Diagnostics::MakeLogCategory("input"), "updating existing command: {}", description);
                Command& s    = At(existing);
                s.icon        = icon;
                s.description = std::string(description);
                s.callback    = std::move(callback);
                s.onEnd       = {}; // re-registering as instant drops any prior held delegate
                return existing;
            }

            const CommandID id{ uint32_t(m_commands.size()) + 1 };
            m_commands.push_back({
                mode, icon, key, modifiers,
                KeyCode::None, KeyModifier::None, // single-key: no chord2
                std::string(description),
                FormatShortcut(key, modifiers),
                std::move(callback)
            });
            if (uint32_t(mode) < m_byMode.size())
                m_byMode[uint32_t(mode)].push_back(id);
            return id;
        }

        /// RegisterCommand in the Default mode.
        CommandID RegisterCommand(IconID icon, KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(CommandMode::Default, icon, key, modifiers, std::move(callback), description);
        }

        /// RegisterCommand without an icon.
        CommandID RegisterCommand(CommandMode mode, KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(mode, IconID::Invalid, key, modifiers, std::move(callback), description);
        }

        /// RegisterCommand in the Default mode, without an icon.
        CommandID RegisterCommand(KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(CommandMode::Default, IconID::Invalid, key, modifiers, std::move(callback), description);
        }

        /// RegisterCommand without a shortcut key.
        CommandID RegisterCommand(CommandMode mode, IconID icon,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(mode, icon, KeyCode::None, KeyModifier::None, std::move(callback), description);
        }

        /// RegisterCommand in the Default mode, without a shortcut key.
        CommandID RegisterCommand(IconID icon,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(CommandMode::Default, icon, KeyCode::None, KeyModifier::None, std::move(callback), description);
        }

        /// Registers a hold-to-act command (pan, orbit): `onBegin` on press, `onEnd` on release.
        /// Single-key only; OS key-repeat doesn't re-fire `onBegin`. Otherwise as RegisterCommand.
        CommandID RegisterHeldCommand(CommandMode mode, IconID icon, KeyCode key, KeyModifier modifiers,
                                      std::function<void()> onBegin, std::function<void()> onEnd,
                                      std::string_view description = {})
        {
            const CommandID id = RegisterCommand(mode, icon, key, modifiers, std::move(onBegin), description);
            At(id).onEnd = std::move(onEnd);
            return id;
        }

        /// RegisterHeldCommand in the Default mode, without an icon.
        CommandID RegisterHeldCommand(KeyCode key, KeyModifier modifiers,
                                      std::function<void()> onBegin, std::function<void()> onEnd,
                                      std::string_view description = {})
        {
            return RegisterHeldCommand(CommandMode::Default, IconID::Invalid, key, modifiers,
                                       std::move(onBegin), std::move(onEnd), description);
        }

        /// RegisterHeldCommand without an icon.
        CommandID RegisterHeldCommand(CommandMode mode, KeyCode key, KeyModifier modifiers,
                                      std::function<void()> onBegin, std::function<void()> onEnd,
                                      std::string_view description = {})
        {
            return RegisterHeldCommand(mode, IconID::Invalid, key, modifiers,
                                       std::move(onBegin), std::move(onEnd), description);
        }

        /// The single-key command bound to (`mode`, `key`, `modifiers`); Invalid if none. Does not
        /// fall back to Default and does not match chords.
        CommandID FindCommand(CommandMode mode, KeyCode key, KeyModifier modifiers) const
        {
            if (key == KeyCode::None) return {};
            const uint32_t modeIdx = uint32_t(mode);
            if (modeIdx >= m_byMode.size()) return {};
            for (const CommandID id : m_byMode[modeIdx])
            {
                const Command& s = At(id);
                if (!s.IsChord() && s.key == key && s.modifiers == modifiers) return id;
            }
            return {};
        }

        /// The chord command in `mode` bound to exactly `k1`+`m1` then `k2`+`m2`; Invalid if none.
        CommandID FindChord(CommandMode mode, KeyCode k1, KeyModifier m1,
                            KeyCode k2, KeyModifier m2) const
        {
            if (k1 == KeyCode::None || k2 == KeyCode::None) return {};
            const uint32_t modeIdx = uint32_t(mode);
            if (modeIdx >= m_byMode.size()) return {};
            for (const CommandID id : m_byMode[modeIdx])
            {
                const Command& s = At(id);
                if (s.IsChord()
                    && s.key == k1 && s.modifiers  == m1
                    && s.chord2Key == k2 && s.chord2Mods == m2)
                    return id;
            }
            return {};
        }

        /// Registers a 2-key chord command (e.g. Ctrl+K then Ctrl+S). Re-registering the same
        /// sequence updates it in place. A single-key command sharing the first key always
        /// wins at dispatch, so treat them as mutually exclusive.
        CommandID RegisterChord(CommandMode mode,
                                KeyCode key1, KeyModifier mods1,
                                KeyCode key2, KeyModifier mods2,
                                std::function<void()> callback, std::string_view description = {})
        {
            if (const CommandID existing = FindChord(mode, key1, mods1, key2, mods2))
            {
                Command& s    = At(existing);
                s.description = std::string(description);
                s.callback    = std::move(callback);
                return existing;
            }

            const CommandID id{ uint32_t(m_commands.size()) + 1 };
            m_commands.push_back({
                mode, IconID::Invalid, key1, mods1,
                key2, mods2,
                std::string(description),
                FormatChordShortcut(key1, mods1, key2, mods2),
                std::move(callback)
            });
            if (uint32_t(mode) < m_byMode.size())
                m_byMode[uint32_t(mode)].push_back(id);
            return id;
        }

        /// An EventHandler to push onto the InputManager as a global handler.
        /// KeyDown drives single-key / chord / rebind dispatch (and the begin of a
        /// held command); KeyUp drives the end of held commands.
        EventHandler MakeHandler()
        {
            return [this](ElementEvent&& event) -> bool
            {
                if (event.type == EventType::KeyDown) return ProcessInput(event.state);
                if (event.type == EventType::KeyUp)   return ProcessKeyUp(event.state);
                return false;
            };
        }

        // --- Inspection API (for command/shortcut viewer UI) ---

        /// Every command, in registration order: the one at index i has CommandID{ i + 1 }.
        const std::vector<Command>& GetCommands() const { return m_commands; }
        static CommandID IDAt(size_t index) { return CommandID{ uint32_t(index) + 1 }; }
        static size_t    IndexOf(CommandID id) { return size_t(id.value) - 1; }   // a valid ID's index

        /// The command, or null for an invalid or unknown ID.
        const Command* GetCommand(CommandID id) const
        {
            return (id.value >= 1 && id.value <= m_commands.size()) ? &m_commands[id.value - 1] : nullptr;
        }

        /// Handles for a given mode, in registration order.
        const std::vector<CommandID>& GetCommandsForMode(CommandMode mode) const
        {
            static const std::vector<CommandID> empty;
            uint32_t index = uint32_t(mode);
            return (index < m_byMode.size()) ? m_byMode[index] : empty;
        }

        // --- Rebind mode ---

        /// Makes the next non-modifier keydown the shortcut of `id` instead of firing anything.
        /// Escape cancels.
        void      BeginRebind(CommandID id) { m_rebinding = GetCommand(id) ? id : CommandID{}; }
        void      CancelRebind()            { m_rebinding = {}; }
        bool      IsRebinding() const       { return m_rebinding.IsValid(); }
        CommandID RebindTarget() const      { return m_rebinding; }

        /// True while a chord prefix was pressed and awaits its second key.
        bool IsChordPending() const { return m_pendingChord.key != KeyCode::None; }

        /// Formats a chord shortcut as "Ctrl+K Ctrl+S"; the single-key form when `k2` is None,
        /// "(unbound)" when `k1` is None.
        static std::string FormatChordShortcut(KeyCode k1, KeyModifier m1,
                                               KeyCode k2, KeyModifier m2)
        {
            if (k1 == KeyCode::None) return "(unbound)";
            std::string out = FormatShortcut(k1, m1);
            if (k2 != KeyCode::None)
            {
                out += ' ';
                out += FormatShortcut(k2, m2);
            }
            return out;
        }

        /// Formats a (key, modifiers) combo as a human-readable string like "Ctrl+Shift+D".
        /// An unbound shortcut (KeyCode::None) renders as "(unbound)" regardless of modifiers.
        static std::string FormatShortcut(KeyCode key, KeyModifier modifiers)
        {
            if (key == KeyCode::None) return "(unbound)";
            std::string out;
            auto addMod = [&](KeyModifier bit, const char* name)
            {
                if ((modifiers & bit) != KeyModifier::None)
                {
                    if (!out.empty()) out += '+';
                    out += name;
                }
            };
            addMod(KeyModifier::Control, "Ctrl");
            addMod(KeyModifier::Shift,   "Shift");
            addMod(KeyModifier::Alt,     "Alt");
            addMod(KeyModifier::Command, "Cmd");
            addMod(KeyModifier::Option,  "Opt");

            if (!out.empty()) out += '+';
            out += KeyCodeName(key);
            return out;
        }

        /// A short human-readable key name, e.g. "A", "F5", "Enter", "["; "?" for unknown keys.
        static std::string KeyCodeName(KeyCode k)
        {
            switch (k)
            {
                case KeyCode::None:        return "(none)";
                case KeyCode::Return:      return "Enter";
                case KeyCode::Escape:      return "Esc";
                case KeyCode::Backspace:   return "Backspace";
                case KeyCode::Tab:         return "Tab";
                case KeyCode::Space:       return "Space";
                case KeyCode::Comma:       return ",";
                case KeyCode::Minus:       return "-";
                case KeyCode::Period:      return ".";
                case KeyCode::Slash:       return "/";
                case KeyCode::Semicolon:   return ";";
                case KeyCode::Equals:      return "=";
                case KeyCode::LeftBracket: return "[";
                case KeyCode::Backslash:   return "\\";
                case KeyCode::RightBracket:return "]";
                case KeyCode::Backquote:   return "`";
                case KeyCode::Quote:       return "'";
                case KeyCode::Insert:      return "Insert";
                case KeyCode::Home:        return "Home";
                case KeyCode::PageUp:      return "PageUp";
                case KeyCode::Delete:      return "Delete";
                case KeyCode::End:         return "End";
                case KeyCode::PageDown:    return "PageDown";
                case KeyCode::Right:       return "Right";
                case KeyCode::Left:        return "Left";
                case KeyCode::Down:        return "Down";
                case KeyCode::Up:          return "Up";
                default: break;
            }
            if (k >= KeyCode::KeyA && k <= KeyCode::KeyZ)
                return std::string(1, char('A' + (uint32_t(k) - uint32_t(KeyCode::KeyA))));
            if (k >= KeyCode::Key0 && k <= KeyCode::Key9)
                return std::string(1, char('0' + (uint32_t(k) - uint32_t(KeyCode::Key0))));
            if (k >= KeyCode::F1 && k <= KeyCode::F12)
                return "F" + std::to_string(1 + int(uint32_t(k) - uint32_t(KeyCode::F1)));
            return "?";
        }

    private:
        struct KeyChord
        {
            KeyCode     key  = KeyCode::None;
            KeyModifier mods = KeyModifier::None;
        };

        static bool IsModifierKey(KeyCode k)
        {
            return k == KeyCode::LeftControl  || k == KeyCode::RightControl
                || k == KeyCode::LeftShift    || k == KeyCode::RightShift
                || k == KeyCode::LeftAlt      || k == KeyCode::RightAlt
                || k == KeyCode::LeftCommand  || k == KeyCode::RightCommand;
        }

        Command&       At(CommandID id)       { return m_commands[id.value - 1]; }
        const Command& At(CommandID id) const { return m_commands[id.value - 1]; }

        void RebindCommand(CommandID id, KeyCode key, KeyModifier modifiers)
        {
            if (!GetCommand(id)) return;
            Command& s = At(id);

            // Most recent wins: another command in this mode owning (key, mods) becomes unbound.
            if (key != KeyCode::None)
            {
                const CommandID existing = FindCommand(s.mode, key, modifiers);
                if (existing && existing != id)
                {
                    Command& other      = At(existing);
                    other.key           = KeyCode::None;
                    other.modifiers     = KeyModifier::None;
                    other.displayString = FormatShortcut(KeyCode::None, KeyModifier::None);
                }
            }

            s.key           = key;
            s.modifiers     = modifiers;
            s.displayString = FormatShortcut(key, modifiers);
        }

    public:
        /// Runs a keydown InputState through rebind / chord / shortcut dispatch; true when it
        /// handled the key.
        /// Public so tests and synthesized events can bypass MakeHandler.
        bool ProcessInput(const InputState& state)
        {
            if (state.lastKey == KeyCode::None)
                return false;

            // Before the state.consumed check so a focused textbox can't swallow the rebind key.
            if (m_rebinding)
            {
                if (IsModifierKey(state.lastKey)) return false;
                if (state.lastKey != KeyCode::Escape)
                    RebindCommand(m_rebinding, state.lastKey, state.modifiers);
                m_rebinding = {};
                return true;
            }

            if (state.consumed)
                return false;

            // The next non-modifier key completes the chord or cancels it.
            if (m_pendingChord.key != KeyCode::None)
            {
                if (IsModifierKey(state.lastKey)) return false;

                KeyChord pending = m_pendingChord;
                m_pendingChord   = {};

                if (state.lastKey == KeyCode::Escape)
                    return true;

                if (TryCompleteChord(m_currentMode, pending, state)) return true;
                if (m_currentMode != CommandMode::Default
                    && TryCompleteChord(CommandMode::Default, pending, state)) return true;

                // Swallow a non-completing key so it doesn't fire an unrelated command.
                return true;
            }

            if (TryDispatchMode(m_currentMode, state)) return true;
            if (m_currentMode != CommandMode::Default
                && TryDispatchMode(CommandMode::Default, state)) return true;

            // A chord prefix enters pending state.
            if (HasChordPrefix(m_currentMode, state.lastKey, state.modifiers)
                || (m_currentMode != CommandMode::Default
                    && HasChordPrefix(CommandMode::Default, state.lastKey, state.modifiers)))
            {
                m_pendingChord = { state.lastKey, state.modifiers };
                return true;
            }

            return false;
        }

        /// Fires onEnd of every active held command on the released key (modifiers
        /// are ignored); true if any fired. Public like ProcessInput.
        bool ProcessKeyUp(const InputState& state)
        {
            if (state.lastKey == KeyCode::None) return false;
            bool fired = false;
            for (size_t j = 0; j < m_activeHeld.size(); )
            {
                if (m_activeHeld[j].key != state.lastKey) { ++j; continue; }
                const CommandID cmd = m_activeHeld[j].cmd;
                m_activeHeld.erase(m_activeHeld.begin() + j);
                if (const Command* c = GetCommand(cmd); c && c->onEnd)
                    c->onEnd();
                fired = true;
            }
            return fired;
        }

    private:
        bool TryDispatchMode(CommandMode mode, const InputState& state)
        {
            uint32_t index = uint32_t(mode);
            if (index >= m_byMode.size()) return false;
            for (const CommandID i : m_byMode[index])
            {
                const Command& s = At(i);
                if (s.IsChord()) continue; // chord commands only fire via the pending-prefix path
                if (s.key == state.lastKey && s.modifiers == state.modifiers)
                {
                    BeginCommand(i);
                    return true;
                }
            }
            return false;
        }

        /// A held command stays active until key release; OS key-repeat KeyDowns
        /// are swallowed while active so onBegin fires once per press.
        void BeginCommand(CommandID i)
        {
            const Command& s = At(i);
            if (s.IsHeld())
            {
                if (IsHeldActive(i)) return;
                m_activeHeld.push_back({ s.key, i });
            }
            if (s.callback) s.callback();
        }

        bool IsHeldActive(CommandID cmd) const
        {
            for (const HeldActive& h : m_activeHeld)
                if (h.cmd == cmd) return true;
            return false;
        }

        bool TryCompleteChord(CommandMode mode, KeyChord prefix, const InputState& state)
        {
            uint32_t index = uint32_t(mode);
            if (index >= m_byMode.size()) return false;
            for (const CommandID i : m_byMode[index])
            {
                const Command& s = At(i);
                if (!s.IsChord()) continue;
                if (s.key == prefix.key && s.modifiers == prefix.mods
                    && s.chord2Key == state.lastKey && s.chord2Mods == state.modifiers)
                {
                    if (s.callback) s.callback();
                    return true;
                }
            }
            return false;
        }

        bool HasChordPrefix(CommandMode mode, KeyCode key, KeyModifier mods) const
        {
            uint32_t index = uint32_t(mode);
            if (index >= m_byMode.size()) return false;
            for (const CommandID i : m_byMode[index])
            {
                const Command& s = At(i);
                if (s.IsChord() && s.key == key && s.modifiers == mods)
                    return true;
            }
            return false;
        }

        UndoHistory m_history;
        struct HeldActive { KeyCode key; CommandID cmd; };

        std::vector<std::string>           m_modeNames;
        std::vector<Command>               m_commands;
        std::vector<std::vector<CommandID>> m_byMode;      ///< m_byMode[mode] = the mode's commands
        CommandMode                        m_currentMode = CommandMode::Default;
        CommandID                          m_rebinding;       ///< the command taking the next key, if any
        KeyChord                           m_pendingChord {}; ///< prefix awaiting its second key; key None when none
        std::vector<HeldActive>            m_activeHeld;      ///< held commands with key currently down
    };

} // namespace tachyon
