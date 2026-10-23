#pragma once

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_handler_func.h>
#include <gui_core/style/gui_style_text.h> // IconID

#include <stdint.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace interstice
{
    // Opaque edit-mode identifier. Values are indices vended by CommandManager::RegisterEditMode.
    // EditMode::Default (0) is always pre-registered and acts as a fallback when no
    // mode-specific command matches the active mode.
    enum class EditMode : uint32_t
    {
        Select, Move, Rotate, Scale,
        Default = Select
    };

    // Handle to a registered command, stable for the manager's lifetime.
    using CommandID = uint32_t;

    // A named action with an optional icon and an optional shortcut key, firing
    // one callback when invoked (clicked from a menu / toolbar, or via its key).
    // Commands are organized by named edit modes.
    //
    // Modes are registered by name and return an opaque EditMode index:
    //   EditMode sculpt = commandManager.RegisterEditMode("Sculpt");
    //   commandManager.RegisterCommand(sculpt, KeyCode::KeyG, KeyModifier::None, []{ ... }, "Grab");
    //
    // A command may have no shortcut key (KeyCode::None): it never dispatches from
    // the keyboard but is still listed in the panel, where the user can bind a key.
    //
    // Runs as a global InputManager handler, so focused controls, a focused text
    // editor and state.consumed all take keys first.
    //
    // Shortcut lookup order: active mode first, then Default as fallback.
    class CommandManager
    {
    public:
        struct Command
        {
            EditMode              mode;
            IconID                icon = IconID::Invalid; // optional
            KeyCode               key;
            KeyModifier           modifiers;
            // Second key of a 2-key chord (e.g. Ctrl+K then Ctrl+S); None = single-key.
            KeyCode               chord2Key  = KeyCode::None;
            KeyModifier           chord2Mods = KeyModifier::None;
            std::string           description;
            std::string           displayString; // cached "Ctrl+Shift+D" or "Ctrl+K Ctrl+S"
            // Instant: `callback` fires on press, `onEnd` is empty.
            // Held (RegisterHeldCommand): `callback` on press, `onEnd` on release.
            std::function<void()> callback;
            std::function<void()> onEnd;

            bool IsChord()     const { return chord2Key != KeyCode::None; }
            bool HasShortcut() const { return key != KeyCode::None; }
            bool IsHeld()      const { return static_cast<bool>(onEnd); }
        };

        CommandManager()
        {
            m_modeNames.push_back("Default");
            m_byMode.emplace_back(); // bucket for Default
        }

        // Register a named edit mode. Returns the EditMode value to use when registering
        // commands or calling SetEditMode. Registering the same name twice returns the
        // existing value without adding a duplicate.
        EditMode RegisterEditMode(std::string_view name)
        {
            for (uint32_t i = 0; i < uint32_t(m_modeNames.size()); i++)
                if (m_modeNames[i] == name) return EditMode(i);
            m_modeNames.emplace_back(name);
            m_byMode.emplace_back();
            return EditMode(m_modeNames.size() - 1);
        }

        std::string_view GetEditModeName(EditMode mode) const
        {
            uint32_t index = uint32_t(mode);
            return (index < m_modeNames.size()) ? std::string_view(m_modeNames[index]) : std::string_view{};
        }

        uint32_t GetEditModeCount() const { return uint32_t(m_modeNames.size()); }

        // Cancels any pending chord prefix.
        void     SetEditMode(EditMode mode)
        {
            m_currentMode       = mode;
            m_pendingChord.key  = KeyCode::None;
            m_pendingChord.mods = KeyModifier::None;
        }
        EditMode GetEditMode() const        { return m_currentMode; }

        // key may be KeyCode::None (menu / toolbar only). Re-registering an existing
        // (mode, key, modifiers) updates that slot in place instead of adding a
        // command that could never dispatch. No-shortcut commands never collide.
        CommandID RegisterCommand(EditMode mode, IconID icon, KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            if (int32_t existing = FindCommand(mode, key, modifiers); existing >= 0)
            {
                INTERSTICE_LOG_DEBUG(::interstice::Diagnostics::MakeLogCategory("input"), "updating existing command: {}", description);
                Command& s    = m_commands[uint32_t(existing)];
                s.icon        = icon;
                s.description = std::string(description);
                s.callback    = std::move(callback);
                s.onEnd       = {}; // re-registering as instant drops any prior held delegate
                return uint32_t(existing);
            }

            CommandID index = CommandID(m_commands.size());
            m_commands.push_back({
                mode, icon, key, modifiers,
                KeyCode::None, KeyModifier::None, // single-key: no chord2
                std::string(description),
                FormatShortcut(key, modifiers),
                std::move(callback)
            });
            if (uint32_t(mode) < m_byMode.size())
                m_byMode[uint32_t(mode)].push_back(index);
            return index;
        }

        // Convenience overloads forwarding to the master registration above.

        // Default (fallback) mode, with icon.
        CommandID RegisterCommand(IconID icon, KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(EditMode::Default, icon, key, modifiers, std::move(callback), description);
        }

        // Specific mode, no icon.
        CommandID RegisterCommand(EditMode mode, KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(mode, IconID::Invalid, key, modifiers, std::move(callback), description);
        }

        // Default (fallback) mode, no icon.
        CommandID RegisterCommand(KeyCode key, KeyModifier modifiers,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(EditMode::Default, IconID::Invalid, key, modifiers, std::move(callback), description);
        }

        // Specific mode, with icon, no shortcut key.
        CommandID RegisterCommand(EditMode mode, IconID icon,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(mode, icon, KeyCode::None, KeyModifier::None, std::move(callback), description);
        }

        // Default (fallback) mode, with icon, no shortcut key.
        CommandID RegisterCommand(IconID icon,
                                  std::function<void()> callback, std::string_view description = {})
        {
            return RegisterCommand(EditMode::Default, icon, KeyCode::None, KeyModifier::None, std::move(callback), description);
        }

        // Hold-to-act command (pan, orbit, ...): onBegin on press, onEnd on release.
        // Single-key only; OS key-repeat doesn't re-fire onBegin. Otherwise as RegisterCommand.
        CommandID RegisterHeldCommand(EditMode mode, IconID icon, KeyCode key, KeyModifier modifiers,
                                      std::function<void()> onBegin, std::function<void()> onEnd,
                                      std::string_view description = {})
        {
            CommandID id = RegisterCommand(mode, icon, key, modifiers, std::move(onBegin), description);
            m_commands[id].onEnd = std::move(onEnd);
            return id;
        }

        // Default (fallback) mode, no icon.
        CommandID RegisterHeldCommand(KeyCode key, KeyModifier modifiers,
                                      std::function<void()> onBegin, std::function<void()> onEnd,
                                      std::string_view description = {})
        {
            return RegisterHeldCommand(EditMode::Default, IconID::Invalid, key, modifiers,
                                       std::move(onBegin), std::move(onEnd), description);
        }

        // Specific mode, no icon.
        CommandID RegisterHeldCommand(EditMode mode, KeyCode key, KeyModifier modifiers,
                                      std::function<void()> onBegin, std::function<void()> onEnd,
                                      std::string_view description = {})
        {
            return RegisterHeldCommand(mode, IconID::Invalid, key, modifiers,
                                       std::move(onBegin), std::move(onEnd), description);
        }

        // Find a single-key command by (mode, key, modifiers). Does NOT fall through
        // to Default and does NOT match chord commands. Returns -1 if none.
        int32_t FindCommand(EditMode mode, KeyCode key, KeyModifier modifiers) const
        {
            if (key == KeyCode::None) return -1;
            uint32_t modeIdx = uint32_t(mode);
            if (modeIdx >= m_byMode.size()) return -1;
            for (uint32_t i : m_byMode[modeIdx])
            {
                const Command& s = m_commands[i];
                if (!s.IsChord() && s.key == key && s.modifiers == modifiers) return int32_t(i);
            }
            return -1;
        }

        // Find a chord command whose full (key1,mods1,key2,mods2) matches.
        // Returns -1 if none.
        int32_t FindChord(EditMode mode, KeyCode k1, KeyModifier m1,
                          KeyCode k2, KeyModifier m2) const
        {
            if (k1 == KeyCode::None || k2 == KeyCode::None) return -1;
            uint32_t modeIdx = uint32_t(mode);
            if (modeIdx >= m_byMode.size()) return -1;
            for (uint32_t i : m_byMode[modeIdx])
            {
                const Command& s = m_commands[i];
                if (s.IsChord()
                    && s.key == k1 && s.modifiers  == m1
                    && s.chord2Key == k2 && s.chord2Mods == m2)
                    return int32_t(i);
            }
            return -1;
        }

        // 2-key chord command (e.g. Ctrl+K then Ctrl+S). Re-registering the same
        // sequence updates it in place. A single-key command sharing the first key
        // always wins at dispatch, so treat them as mutually exclusive.
        CommandID RegisterChord(EditMode mode,
                                KeyCode key1, KeyModifier mods1,
                                KeyCode key2, KeyModifier mods2,
                                std::function<void()> callback, std::string_view description = {})
        {
            if (int32_t existing = FindChord(mode, key1, mods1, key2, mods2); existing >= 0)
            {
                Command& s    = m_commands[uint32_t(existing)];
                s.description = std::string(description);
                s.callback    = std::move(callback);
                return uint32_t(existing);
            }

            CommandID index = CommandID(m_commands.size());
            m_commands.push_back({
                mode, IconID::Invalid, key1, mods1,
                key2, mods2,
                std::string(description),
                FormatChordShortcut(key1, mods1, key2, mods2),
                std::move(callback)
            });
            if (uint32_t(mode) < m_byMode.size())
                m_byMode[uint32_t(mode)].push_back(index);
            return index;
        }

        // Returns an EventHandler to push onto the InputManager as a global handler.
        // KeyDown drives single-key / chord / rebind dispatch (and the begin of a
        // held command); KeyUp drives the end of held commands.
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

        const std::vector<Command>& GetCommands() const { return m_commands; }

        // Handles for a given mode, in registration order.
        const std::vector<CommandID>& GetCommandsForMode(EditMode mode) const
        {
            static const std::vector<CommandID> empty;
            uint32_t index = uint32_t(mode);
            return (index < m_byMode.size()) ? m_byMode[index] : empty;
        }

        // --- Rebind mode ---
        //
        // While active, the next non-modifier keydown becomes the target command's
        // shortcut instead of firing anything. Escape cancels.
        void    BeginRebind(CommandID index) { m_rebindIndex = (index < m_commands.size()) ? int32_t(index) : -1; }
        void    CancelRebind()                { m_rebindIndex = -1; }
        bool    IsRebinding() const           { return m_rebindIndex >= 0; }
        int32_t RebindIndex() const           { return m_rebindIndex; }

        // A chord prefix was pressed and awaits its second key.
        bool IsChordPending() const { return m_pendingChord.key != KeyCode::None; }

        // Format a chord shortcut as "Ctrl+K Ctrl+S". If k2 is None the single-key
        // representation is returned. If k1 is None returns "(unbound)".
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

        // Format a (key, modifiers) combo as a human-readable string like "Ctrl+Shift+D".
        // An unbound shortcut (KeyCode::None) renders as "(unbound)" regardless of modifiers.
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

        // Name of a KeyCode as a short human-readable string. Returns e.g. "A",
        // "F5", "Enter", "[". Unknown keys return "?".
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

        void RebindCommand(CommandID index, KeyCode key, KeyModifier modifiers)
        {
            if (index >= m_commands.size()) return;
            Command& s = m_commands[index];

            // Most recent wins: another command in this mode owning (key, mods) becomes unbound.
            if (key != KeyCode::None)
            {
                int32_t existing = FindCommand(s.mode, key, modifiers);
                if (existing >= 0 && uint32_t(existing) != index)
                {
                    Command& other      = m_commands[uint32_t(existing)];
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
        // Runs a keydown InputState through dispatch; true if consumed.
        // Public so tests and synthesized events can bypass MakeHandler.
        bool ProcessInput(const InputState& state)
        {
            if (state.lastKey == KeyCode::None)
                return false;

            // Before the state.consumed check so a focused textbox can't swallow the rebind key.
            if (m_rebindIndex >= 0)
            {
                if (IsModifierKey(state.lastKey)) return false;
                if (state.lastKey != KeyCode::Escape)
                    RebindCommand(CommandID(m_rebindIndex), state.lastKey, state.modifiers);
                m_rebindIndex = -1;
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
                if (m_currentMode != EditMode::Default
                    && TryCompleteChord(EditMode::Default, pending, state)) return true;

                // Swallow a non-completing key so it doesn't fire an unrelated command.
                return true;
            }

            if (TryDispatchMode(m_currentMode, state)) return true;
            if (m_currentMode != EditMode::Default
                && TryDispatchMode(EditMode::Default, state)) return true;

            // A chord prefix enters pending state.
            if (HasChordPrefix(m_currentMode, state.lastKey, state.modifiers)
                || (m_currentMode != EditMode::Default
                    && HasChordPrefix(EditMode::Default, state.lastKey, state.modifiers)))
            {
                m_pendingChord = { state.lastKey, state.modifiers };
                return true;
            }

            return false;
        }

        // Fires onEnd of every active held command on the released key (modifiers
        // are ignored); true if any fired. Public like ProcessInput.
        bool ProcessKeyUp(const InputState& state)
        {
            if (state.lastKey == KeyCode::None) return false;
            bool fired = false;
            for (size_t j = 0; j < m_activeHeld.size(); )
            {
                if (m_activeHeld[j].key != state.lastKey) { ++j; continue; }
                CommandID cmd = m_activeHeld[j].cmd;
                m_activeHeld.erase(m_activeHeld.begin() + j);
                if (cmd < m_commands.size() && m_commands[cmd].onEnd)
                    m_commands[cmd].onEnd();
                fired = true;
            }
            return fired;
        }

    private:
        bool TryDispatchMode(EditMode mode, const InputState& state)
        {
            uint32_t index = uint32_t(mode);
            if (index >= m_byMode.size()) return false;
            for (uint32_t i : m_byMode[index])
            {
                const Command& s = m_commands[i];
                if (s.IsChord()) continue; // chord commands only fire via the pending-prefix path
                if (s.key == state.lastKey && s.modifiers == state.modifiers)
                {
                    BeginCommand(i);
                    return true;
                }
            }
            return false;
        }

        // A held command stays active until key release; OS key-repeat KeyDowns
        // are swallowed while active so onBegin fires once per press.
        void BeginCommand(CommandID i)
        {
            const Command& s = m_commands[i];
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

        bool TryCompleteChord(EditMode mode, KeyChord prefix, const InputState& state)
        {
            uint32_t index = uint32_t(mode);
            if (index >= m_byMode.size()) return false;
            for (uint32_t i : m_byMode[index])
            {
                const Command& s = m_commands[i];
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

        bool HasChordPrefix(EditMode mode, KeyCode key, KeyModifier mods) const
        {
            uint32_t index = uint32_t(mode);
            if (index >= m_byMode.size()) return false;
            for (uint32_t i : m_byMode[index])
            {
                const Command& s = m_commands[i];
                if (s.IsChord() && s.key == key && s.modifiers == mods)
                    return true;
            }
            return false;
        }

        // One per held command whose key is currently down.
        struct HeldActive { KeyCode key; CommandID cmd; };

        std::vector<std::string>           m_modeNames;
        std::vector<Command>               m_commands;
        std::vector<std::vector<CommandID>> m_byMode;      // m_byMode[mode] = handles into m_commands
        EditMode                           m_currentMode = EditMode::Default;
        int32_t                            m_rebindIndex = -1; // index into m_commands, or -1
        KeyChord                           m_pendingChord {}; // prefix waiting for completion (key=None = no pending)
        std::vector<HeldActive>            m_activeHeld;      // held commands with key currently down
    };

} // namespace interstice
