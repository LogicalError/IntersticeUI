// Unit tests for tachyon::CommandManager.

#include <gtest/gtest.h>

#include <gui_core/input/gui_command_manager.h>
#include <gui_core/input/gui_input_state.h>

using tachyon::CommandManager;
using tachyon::CommandID;
using tachyon::CommandMode;
using tachyon::IconID;
using tachyon::KeyCode;
using tachyon::KeyModifier;
using tachyon::InputState;

namespace
{
    // Single-key press; the command manager only reads lastKey/modifiers/consumed.
    InputState MakeKey(KeyCode key, KeyModifier mods = KeyModifier::None, bool consumed = false)
    {
        InputState s;
        s.lastKey   = key;
        s.modifiers = mods;
        s.consumed  = consumed;
        return s;
    }

    // Mirrors what MakeHandler does on EventType::KeyDown.
    bool Dispatch(CommandManager& m, KeyCode key, KeyModifier mods = KeyModifier::None, bool consumed = false)
    {
        return m.ProcessInput(MakeKey(key, mods, consumed));
    }

    // Keyup path (ends a held command); mirrors MakeHandler on EventType::KeyUp.
    bool DispatchUp(CommandManager& m, KeyCode key)
    {
        return m.ProcessKeyUp(MakeKey(key));
    }
}

TEST(CommandManager, SingleKeyDispatchFiresCallback)
{
    CommandManager m;
    int fired = 0;
    m.RegisterCommand(KeyCode::KeyD, KeyModifier::Control, [&]() { fired++; }, "Duplicate");

    EXPECT_TRUE(Dispatch(m, KeyCode::KeyD, KeyModifier::Control));
    EXPECT_EQ(fired, 1);
}

TEST(CommandManager, UnmatchedKeyIsNotConsumed)
{
    CommandManager m;
    m.RegisterCommand(KeyCode::KeyD, KeyModifier::Control, []() {});

    EXPECT_FALSE(Dispatch(m, KeyCode::KeyE, KeyModifier::Control));
}

TEST(CommandManager, ConsumedEventBypassesDispatch)
{
    CommandManager m;
    int fired = 0;
    m.RegisterCommand(KeyCode::KeyD, KeyModifier::Control, [&]() { fired++; });

    EXPECT_FALSE(Dispatch(m, KeyCode::KeyD, KeyModifier::Control, /*consumed=*/true));
    EXPECT_EQ(fired, 0);
}

TEST(CommandManager, ModeSpecificCommandFallsThroughToDefault)
{
    CommandManager m;
    CommandMode sculpt = m.RegisterMode("Sculpt");

    int defaultFired = 0, sculptFired = 0;
    m.RegisterCommand(KeyCode::KeyD, KeyModifier::Control,          [&]() { defaultFired++; });
    m.RegisterCommand(sculpt, KeyCode::KeyG, KeyModifier::None,     [&]() { sculptFired++; });

    m.SetMode(sculpt);
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyG));                                  // mode match
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyD, KeyModifier::Control));            // default fallback
    EXPECT_EQ(sculptFired, 1);
    EXPECT_EQ(defaultFired, 1);
}

TEST(CommandManager, ReregisteringSameBindingUpdatesInPlace)
{
    CommandManager m;
    int callsA = 0, callsB = 0;

    CommandID a = m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, [&]() { callsA++; }, "Save");
    CommandID b = m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, [&]() { callsB++; }, "Save Updated");

    EXPECT_EQ(a, b); // replaced in place - no dead command

    Dispatch(m, KeyCode::KeyS, KeyModifier::Control);
    EXPECT_EQ(callsA, 0); // original callback discarded
    EXPECT_EQ(callsB, 1);

    ASSERT_EQ(m.GetCommands().size(), 1u);
    EXPECT_EQ(m.GetCommands()[0].description, "Save Updated");
}

TEST(CommandManager, FindCommandIgnoresChordCommands)
{
    CommandManager m;
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    []() {});

    // A chord's prefix is not a single-key command; FindCommand returning it would
    // make conflict detection replace the chord.
    EXPECT_FALSE(m.FindCommand(CommandMode::Default, KeyCode::KeyK, KeyModifier::Control));
}

TEST(CommandManager, ChordDispatchFiresOnFullSequence)
{
    CommandManager m;
    int fired = 0;
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    [&]() { fired++; }, "Save All");

    // Prefix key consumed, but callback not yet fired
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyK, KeyModifier::Control));
    EXPECT_EQ(fired, 0);

    // Completing key fires callback
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyS, KeyModifier::Control));
    EXPECT_EQ(fired, 1);
}

// A chord registered without an action (e.g. a placeholder binding) completes
// without calling an empty std::function, which would throw.
TEST(CommandManager, ChordWithoutCallbackCompletesSafely)
{
    CommandManager m;
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    nullptr, "Placeholder");

    EXPECT_TRUE(Dispatch(m, KeyCode::KeyK, KeyModifier::Control));
    EXPECT_NO_THROW(EXPECT_TRUE(Dispatch(m, KeyCode::KeyS, KeyModifier::Control)));
}

TEST(CommandManager, ChordWrongSecondKeySwallowsButDoesNotFire)
{
    CommandManager m;
    int chord = 0, single = 0;
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    [&]() { chord++; });
    m.RegisterCommand(KeyCode::KeyX, KeyModifier::Control, [&]() { single++; });

    EXPECT_TRUE(Dispatch(m, KeyCode::KeyK, KeyModifier::Control)); // prefix pending
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyX, KeyModifier::Control)); // wrong second; swallowed
    EXPECT_EQ(chord, 0);
    EXPECT_EQ(single, 0); // single-key command must NOT fire while closing a chord

    // Pending is now cleared, so Ctrl+X fires normally on the next press.
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyX, KeyModifier::Control));
    EXPECT_EQ(single, 1);
}

TEST(CommandManager, EscapeCancelsPendingChord)
{
    CommandManager m;
    int fired = 0;
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    [&]() { fired++; });

    Dispatch(m, KeyCode::KeyK, KeyModifier::Control);
    EXPECT_TRUE(Dispatch(m, KeyCode::Escape));
    EXPECT_EQ(fired, 0);

    // After escape, chord's second key alone shouldn't fire
    EXPECT_FALSE(Dispatch(m, KeyCode::KeyS, KeyModifier::Control));
    EXPECT_EQ(fired, 0);
}

TEST(CommandManager, SetEditModeCancelsPendingChord)
{
    CommandManager m;
    CommandMode sculpt = m.RegisterMode("Sculpt");
    int fired = 0;
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    [&]() { fired++; });

    Dispatch(m, KeyCode::KeyK, KeyModifier::Control);
    m.SetMode(sculpt);
    // Pending was in Default; mode switch clears it.
    EXPECT_FALSE(Dispatch(m, KeyCode::KeyS, KeyModifier::Control));
    EXPECT_EQ(fired, 0);
}

TEST(CommandManager, SingleKeyShadowsChordWithSamePrefix)
{
    CommandManager m;
    int single = 0, chord = 0;
    m.RegisterCommand(KeyCode::KeyK, KeyModifier::Control,         [&]() { single++; });
    m.RegisterChord(CommandMode::Default,
                    KeyCode::KeyK, KeyModifier::Control,
                    KeyCode::KeyS, KeyModifier::Control,
                    [&]() { chord++; });

    // Single-key wins on prefix press.
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyK, KeyModifier::Control));
    EXPECT_EQ(single, 1);
    EXPECT_EQ(chord, 0);
}

TEST(CommandManager, RebindToConflictingBindingUnbindsOldOwner)
{
    CommandManager m;
    CommandID save = m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, []() {}, "Save");
    CommandID undo = m.RegisterCommand(KeyCode::KeyZ, KeyModifier::Control, []() {}, "Undo");

    // Rebind Undo -> Ctrl+S; old Save should become unbound (KeyCode::None).
    m.BeginRebind(undo);
    EXPECT_TRUE(Dispatch(m, KeyCode::KeyS, KeyModifier::Control));

    EXPECT_EQ(m.GetCommand(save)->key, KeyCode::None);
    EXPECT_EQ(m.GetCommand(save)->displayString, "(unbound)");
    EXPECT_EQ(m.GetCommand(undo)->key, KeyCode::KeyS);
    EXPECT_EQ(m.GetCommand(undo)->modifiers, KeyModifier::Control);
}

TEST(CommandManager, UnboundCommandNeverDispatches)
{
    CommandManager m;
    int fired = 0;
    CommandID idx = m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, [&]() { fired++; });

    // Rebind another command onto Ctrl+S to unbind idx, then confirm idx is dead.
    CommandID other = m.RegisterCommand(KeyCode::KeyX, KeyModifier::Control, []() {});
    m.BeginRebind(other);
    Dispatch(m, KeyCode::KeyS, KeyModifier::Control); // rebinds other to Ctrl+S, unbinds idx

    EXPECT_EQ(m.GetCommand(idx)->key, KeyCode::None);
    // Re-firing Ctrl+S now goes to 'other', not idx.
    EXPECT_FALSE(Dispatch(m, KeyCode::KeyX, KeyModifier::Control));
    EXPECT_EQ(fired, 0);
}

TEST(CommandManager, NoShortcutCommandNeverDispatches)
{
    CommandManager m;
    int fired = 0;
    // A command with no shortcut key (menu / toolbar only).
    CommandID id = m.RegisterCommand(IconID::Invalid, [&]() { fired++; }, "Menu Only");
    EXPECT_FALSE(m.GetCommand(id)->HasShortcut());

    // No real keypress ever matches a None-keyed command.
    EXPECT_FALSE(Dispatch(m, KeyCode::KeyA, KeyModifier::None));
    EXPECT_FALSE(Dispatch(m, KeyCode::None, KeyModifier::None));
    EXPECT_EQ(fired, 0);
}

TEST(CommandManager, MultipleNoShortcutCommandsGetDistinctHandles)
{
    CommandManager m;
    CommandID a = m.RegisterCommand(IconID::Invalid, []() {}, "A");
    CommandID b = m.RegisterCommand(IconID::Invalid, []() {}, "B");
    EXPECT_NE(a, b);
    EXPECT_EQ(m.GetCommands().size(), 2u);
}

TEST(CommandManager, IconRoundTripsThroughRegistration)
{
    CommandManager m;
    const IconID icon = static_cast<IconID>(7);
    CommandID withKey = m.RegisterCommand(icon, KeyCode::KeyS, KeyModifier::Control, []() {}, "Save");
    CommandID noKey   = m.RegisterCommand(icon, []() {}, "Refresh");
    EXPECT_EQ(m.GetCommand(withKey)->icon, icon);
    EXPECT_EQ(m.GetCommand(noKey)->icon, icon);
    EXPECT_EQ(m.GetCommand(noKey)->key, KeyCode::None);
}

TEST(CommandManager, FormatShortcutProducesReadableText)
{
    EXPECT_EQ(CommandManager::FormatShortcut(KeyCode::KeyD, KeyModifier::Control | KeyModifier::Shift),
              "Ctrl+Shift+D");
    EXPECT_EQ(CommandManager::FormatShortcut(KeyCode::F5,   KeyModifier::None), "F5");
    EXPECT_EQ(CommandManager::FormatShortcut(KeyCode::None, KeyModifier::Control), "(unbound)");
}

TEST(CommandManager, HeldCommandFiresBeginOnDownAndEndOnUp)
{
    CommandManager m;
    int begins = 0, ends = 0;
    m.RegisterHeldCommand(KeyCode::Space, KeyModifier::None,
                          [&]() { begins++; }, [&]() { ends++; }, "Pan");

    EXPECT_TRUE(Dispatch(m, KeyCode::Space));   // press -> begin
    EXPECT_EQ(begins, 1);
    EXPECT_EQ(ends, 0);

    EXPECT_TRUE(DispatchUp(m, KeyCode::Space)); // release -> end
    EXPECT_EQ(begins, 1);
    EXPECT_EQ(ends, 1);
}

TEST(CommandManager, HeldCommandDoesNotRefireBeginOnKeyRepeat)
{
    CommandManager m;
    int begins = 0, ends = 0;
    m.RegisterHeldCommand(KeyCode::Space, KeyModifier::None, [&]() { begins++; }, [&]() { ends++; });

    EXPECT_TRUE(Dispatch(m, KeyCode::Space)); // begin
    EXPECT_TRUE(Dispatch(m, KeyCode::Space)); // OS key-repeat - swallowed, no second begin
    EXPECT_EQ(begins, 1);

    DispatchUp(m, KeyCode::Space);
    EXPECT_EQ(ends, 1);

    // A fresh press after release begins again.
    EXPECT_TRUE(Dispatch(m, KeyCode::Space));
    EXPECT_EQ(begins, 2);
}

TEST(CommandManager, HeldCommandEndMatchesKeyIgnoringModifiers)
{
    CommandManager m;
    int ends = 0;
    m.RegisterHeldCommand(KeyCode::KeyZ, KeyModifier::Control, []() {}, [&]() { ends++; });

    EXPECT_TRUE(Dispatch(m, KeyCode::KeyZ, KeyModifier::Control)); // Ctrl+Z press -> begin
    // Releasing Ctrl before Z means the Z keyup carries no modifiers, but
    // letting go of the key still ends the hold.
    EXPECT_TRUE(DispatchUp(m, KeyCode::KeyZ));
    EXPECT_EQ(ends, 1);
}

TEST(CommandManager, InstantCommandIgnoresKeyUp)
{
    CommandManager m;
    int fired = 0;
    m.RegisterCommand(KeyCode::KeyD, KeyModifier::Control, [&]() { fired++; });

    EXPECT_TRUE(Dispatch(m, KeyCode::KeyD, KeyModifier::Control));
    EXPECT_FALSE(DispatchUp(m, KeyCode::KeyD)); // nothing held -> keyup not consumed
    EXPECT_EQ(fired, 1);
}

TEST(CommandManager, ReregisteringHeldAsInstantClearsEndDelegate)
{
    CommandManager m;
    int ends = 0;
    m.RegisterHeldCommand(KeyCode::Space, KeyModifier::None, []() {}, [&]() { ends++; });
    EXPECT_TRUE(m.GetCommands()[0].IsHeld());

    // Re-register the same binding as a plain instant command in place.
    m.RegisterCommand(KeyCode::Space, KeyModifier::None, []() {});
    EXPECT_FALSE(m.GetCommands()[0].IsHeld());

    Dispatch(m, KeyCode::Space);
    EXPECT_FALSE(DispatchUp(m, KeyCode::Space)); // no held delegate remains
    EXPECT_EQ(ends, 0);
}

TEST(CommandManager, FormatChordShortcutJoinsKeysWithSpace)
{
    EXPECT_EQ(CommandManager::FormatChordShortcut(
                  KeyCode::KeyK, KeyModifier::Control,
                  KeyCode::KeyS, KeyModifier::Control),
              "Ctrl+K Ctrl+S");
    // No second key -> degenerates to single-key format.
    EXPECT_EQ(CommandManager::FormatChordShortcut(
                  KeyCode::KeyK, KeyModifier::Control,
                  KeyCode::None, KeyModifier::None),
              "Ctrl+K");
}

// #53: the zero CommandID is invalid ("unset"), and real commands start at 1, so a
// zero-initialized struct of IDs holds no command.
TEST(CommandManager, TheZeroCommandIDIsInvalid)
{
    CommandManager m;
    const CommandID unset{};
    EXPECT_FALSE(unset.IsValid());
    EXPECT_EQ(m.GetCommand(unset), nullptr);

    const CommandID first = m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, []() {}, "Save");
    EXPECT_TRUE(first.IsValid());
    EXPECT_NE(first, unset);
    ASSERT_NE(m.GetCommand(first), nullptr);
    EXPECT_EQ(m.GetCommand(first)->description, "Save");
    EXPECT_EQ(CommandManager::IDAt(0), first);
    EXPECT_EQ(m.GetCommand(CommandID{ 99 }), nullptr) << "an unknown ID";

    m.BeginRebind(unset);
    EXPECT_FALSE(m.IsRebinding()) << "nothing to rebind";
}

// #53: modes are only what the app registers: each name gets its own value, none of them
// the Default fallback (the library has no fixed modes a registered one could collide with).
TEST(CommandManager, RegisteredModesAreDistinct)
{
    CommandManager m;
    const CommandMode nodeView = m.RegisterMode("NodeView");
    const CommandMode sculpt   = m.RegisterMode("Sculpt");
    EXPECT_NE(nodeView, CommandMode::Default);
    EXPECT_NE(sculpt, CommandMode::Default);
    EXPECT_NE(nodeView, sculpt);
    EXPECT_EQ(m.RegisterMode("NodeView"), nodeView) << "the same name, the same mode";
    EXPECT_EQ(m.GetModeName(sculpt), "Sculpt");
    EXPECT_EQ(m.GetModeCount(), 3u);

    int fired = 0;
    m.RegisterCommand(sculpt, KeyCode::KeyG, KeyModifier::None, [&]() { fired++; }, "Grab");
    m.SetMode(nodeView);
    m.ProcessInput(MakeKey(KeyCode::KeyG));
    EXPECT_EQ(fired, 0) << "another mode's shortcut";
    m.SetMode(sculpt);
    m.ProcessInput(MakeKey(KeyCode::KeyG));
    EXPECT_EQ(fired, 1);
}
