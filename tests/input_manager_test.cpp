#include <gtest/gtest.h>
#include <gui_core/input/gui_input_manager.h>
#include <gui_core/input/gui_command_manager.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_scroll_management.h>
#include <gui_core/state/gui_tab_chain_manager.h>

namespace interstice
{

    // ============================================================================
    // Fixture for dispatch tests (needs GUIStateManager + ElementStorage + ScrollManagement)
    // ============================================================================

    class InputManagerTest : public ::testing::Test
    {
    protected:
        GradientManager  gradientManager;
        StyleManager     styleManager{ gradientManager };
        ElementStorage   elementStorage{ styleManager };
        GUIStateManager  guiStateManager;
        ScrollManagement scrollManagement;
        TabChainManager  tabChainManager;
        InputManager     manager;

        void Dispatch()
        {
            manager.Dispatch(guiStateManager, elementStorage);
        }
    };

    // ============================================================================
    // Recording tests
    // ============================================================================

    TEST_F(InputManagerTest, RecordsMouseMoveEvent)
    {
        manager.OnMouseMove(ID::Invalid(), interstice::int2{ 10, 20 }, MouseButtons::None, KeyModifier::None);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].type, EventType::MouseMove);
        EXPECT_EQ(events[0].state.screenPos.x, 10);
        EXPECT_EQ(events[0].state.screenPos.y, 20);
        EXPECT_FALSE(events[0].consumed);
    }

    TEST_F(InputManagerTest, RecordsMouseDownEvent)
    {
        manager.OnMouseButton(ID::Invalid(), interstice::int2{ 5, 8 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].type, EventType::MouseDown);
        EXPECT_TRUE(events[0].state.LeftPressed());
    }

    TEST_F(InputManagerTest, RecordsMouseUpEvent)
    {
        // Press first so prevButtons has Left set
        manager.OnMouseButton(ID::Invalid(), interstice::int2{ 5, 8 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
        manager.OnMouseButton(ID::Invalid(), interstice::int2{ 5, 8 }, MouseButtons::None, KeyModifier::None, MouseAction::MouseUp);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);
        EXPECT_EQ(events[1].type, EventType::MouseUp);
        EXPECT_TRUE(events[1].state.LeftReleased());
    }

    TEST_F(InputManagerTest, RecordsScrollEvent)
    {
        manager.OnMouseScroll(ID::Invalid(), interstice::int2{ 0, 0 }, interstice::float2{ 2, -2 }, KeyModifier::None);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].type, EventType::MouseScroll);
        EXPECT_EQ(events[0].state.scroll.x, 2);
        EXPECT_EQ(events[0].state.scroll.y, -2);
    }

    // Precision touchpads and hi-res wheels report sub-notch deltas.
    TEST_F(InputManagerTest, KeepsFractionalScrollDelta)
    {
        manager.OnMouseScroll(ID::Invalid(), interstice::int2{ 0, 0 }, interstice::float2{ 0.25f, -0.5f }, KeyModifier::None);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_FLOAT_EQ(events[0].state.scroll.x, 0.25f);
        EXPECT_FLOAT_EQ(events[0].state.scroll.y, -0.5f);
    }

    // A typed character is text, not a key release: its codepoint must not be
    // read as a KeyCode (it could end an unrelated held command).
    TEST_F(InputManagerTest, TypedCharacterIsRecordedAsTextInput)
    {
        manager.OnKeyboard(ID::Invalid(), static_cast<KeyCode>(U'a'), 0, KeyModifier::None, KeyAction::KeyChar);
        manager.OnKeyboard(ID::Invalid(), static_cast<KeyCode>(0xE9), 0, KeyModifier::None, KeyAction::KeyChar);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);
        EXPECT_EQ(events[0].type, EventType::TextInput);
        EXPECT_EQ(events[0].state.lastKey, KeyCode::None);
        EXPECT_EQ(events[0].state.textInput, "a");
        EXPECT_EQ(events[1].type, EventType::TextInput);
        EXPECT_EQ(events[1].state.textInput, "\xC3\xA9");
    }

    TEST_F(InputManagerTest, RecordsKeyDownEvent)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].type, EventType::KeyDown);
        EXPECT_EQ(events[0].state.lastKey, KeyCode::KeyA);
        EXPECT_EQ(events[0].state.lastKeyAction, KeyAction::KeyDown);
    }

    TEST_F(InputManagerTest, RecordsKeyUpEvent)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyUp);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].type, EventType::KeyUp);
    }

    TEST_F(InputManagerTest, MultipleKeyEventsRecordedSeparately)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyB, 0, KeyModifier::None, KeyAction::KeyDown);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);
        EXPECT_EQ(events[0].state.lastKey, KeyCode::KeyA);
        EXPECT_EQ(events[1].state.lastKey, KeyCode::KeyB);
    }

    TEST_F(InputManagerTest, PerEventMouseDelta)
    {
        manager.OnMouseMove(ID::Invalid(), interstice::int2{ 10, 10 }, MouseButtons::None, KeyModifier::None);
        manager.OnMouseMove(ID::Invalid(), interstice::int2{ 15, 12 }, MouseButtons::None, KeyModifier::None);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);

        // First move: prev was {0,0} (initial state)
        EXPECT_EQ(events[0].state.screenDelta.x, 10);
        EXPECT_EQ(events[0].state.screenDelta.y, 10);

        // Second move: prev was {10,10}
        EXPECT_EQ(events[1].state.screenDelta.x, 5);
        EXPECT_EQ(events[1].state.screenDelta.y, 2);
    }

    TEST_F(InputManagerTest, ButtonStateTrackedPerEvent)
    {
        manager.OnMouseButton(ID::Invalid(), interstice::int2{ 0, 0 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
        manager.OnMouseButton(ID::Invalid(), interstice::int2{ 0, 0 }, MouseButtons::None, KeyModifier::None, MouseAction::MouseUp);

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);

        // Down event: prevButtons = None, buttons = Left
        EXPECT_EQ(events[0].state.prevButtons, MouseButtons::None);
        EXPECT_EQ(events[0].state.buttons, MouseButtons::Left);

        // Up event: prevButtons = Left (from previous event), buttons = None
        EXPECT_EQ(events[1].state.prevButtons, MouseButtons::Left);
        EXPECT_EQ(events[1].state.buttons, MouseButtons::None);
    }

    // ============================================================================
    // Consume tests
    // ============================================================================

    TEST_F(InputManagerTest, ConsumeKeyMarksLastKeyboardEvent)
    {
        manager.OnMouseMove(ID::Invalid(), interstice::int2{ 0, 0 }, MouseButtons::None, KeyModifier::None);
        manager.OnKeyboard(ID::Invalid(), KeyCode::Tab, 0, KeyModifier::None, KeyAction::KeyDown);

        manager.ConsumeKey();

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);
        EXPECT_FALSE(events[0].consumed); // mouse event untouched
        EXPECT_TRUE(events[1].consumed);  // keyboard event consumed
    }

    TEST_F(InputManagerTest, ConsumeKeyDoesNotMarkMouseEvents)
    {
        manager.OnMouseMove(ID::Invalid(), interstice::int2{ 0, 0 }, MouseButtons::None, KeyModifier::None);

        manager.ConsumeKey(); // no keyboard event - should be a no-op

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_FALSE(events[0].consumed);
    }

    TEST_F(InputManagerTest, ConsumeKeyMarksLastAmongMultiple)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyB, 0, KeyModifier::None, KeyAction::KeyDown);

        manager.ConsumeKey(); // should mark KeyB (last keyboard event)

        const auto& events = manager.GetRecordedEvents();
        ASSERT_EQ(events.size(), 2u);
        EXPECT_FALSE(events[0].consumed);
        EXPECT_TRUE(events[1].consumed);
    }

    // ============================================================================
    // Dispatch tests
    // ============================================================================

    TEST_F(InputManagerTest, EventsClearedAfterDispatch)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);
        EXPECT_EQ(manager.GetRecordedEvents().size(), 1u);

        Dispatch();

        EXPECT_EQ(manager.GetRecordedEvents().size(), 0u);
    }

    TEST_F(InputManagerTest, GlobalHandlerReceivesKeyboardEvent)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);

        EventType receivedEventType = EventType::None;
        KeyCode   receivedKey = KeyCode::None;
        manager.PushGlobalHandler([&](ElementEvent&& event) -> bool
            {
                receivedEventType = event.type;
                receivedKey = event.state.lastKey;
                return true;
            });

        Dispatch();

        EXPECT_EQ(receivedEventType, EventType::KeyDown);
        EXPECT_EQ(receivedKey, KeyCode::KeyA);
    }

    // A command that opens or closes a mode (pushing/popping global handlers) from
    // inside a global handler must not break the dispatch loop it runs in.
    TEST_F(InputManagerTest, GlobalHandlerMayPushAndPopHandlers)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);

        int lowerCalls = 0;
        manager.PushGlobalHandler([&](ElementEvent&&) -> bool { ++lowerCalls; return false; });
        manager.PushGlobalHandler([&](ElementEvent&&) -> bool
            {
                for (int i = 0; i < 64; ++i) // forces the handler list to reallocate
                    manager.PushGlobalHandler([](ElementEvent&&) -> bool { return false; });
                for (int i = 0; i < 66; ++i) // ...and removes this handler itself
                    manager.PopGlobalHandler();
                return false;
            });

        Dispatch();

        EXPECT_EQ(lowerCalls, 1) << "handler changes take effect from the next event on";
    }

    TEST_F(InputManagerTest, ConsumedEventNotForwardedToGlobalHandler)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::Tab, 0, KeyModifier::None, KeyAction::KeyDown);
        manager.ConsumeKey();

        bool handlerCalled = false;
        manager.PushGlobalHandler([&](ElementEvent&& /*event*/) -> bool
            {
                handlerCalled = true;
                return true;
            });

        Dispatch();

        EXPECT_FALSE(handlerCalled);
    }

    TEST_F(InputManagerTest, GlobalHandlerReceivesMultipleKeyEvents)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyB, 0, KeyModifier::None, KeyAction::KeyDown);

        std::vector<KeyCode> receivedKeys;
        manager.PushGlobalHandler([&](ElementEvent&& event) -> bool
            {
                receivedKeys.push_back(event.state.lastKey);
                return false; // don't consume - let next event through
            });

        Dispatch();

        ASSERT_EQ(receivedKeys.size(), 2u);
        EXPECT_EQ(receivedKeys[0], KeyCode::KeyA);
        EXPECT_EQ(receivedKeys[1], KeyCode::KeyB);
    }

    // If the captured element vanishes mid-gesture, the stale capture is released
    // so mouse events fall through to normal routing instead of being swallowed.
    TEST_F(InputManagerTest, DanglingCaptureIsReleasedOnMouseEvent)
    {
        // Empty storage - the captured ID does not resolve to any element.
        ASSERT_EQ(elementStorage.BeginConstruction(), ElementStorageResult::Success);
        ASSERT_EQ(elementStorage.EndConstruction(), ElementStorageResult::Success);

        ID goneID{};
        goneID.value.dynamicParts.mode = IDMode::Dynamic;
        goneID.value.dynamicParts.hashValue = 12345;
        guiStateManager.Interaction().SetCaptureID(goneID);
        ASSERT_EQ(guiStateManager.Interaction().GetCaptureID(), goneID);

        manager.OnMouseButton(ID::Invalid(), { 0, 0 }, MouseButtons::Left,
                              KeyModifier::None, MouseAction::MouseDown);
        Dispatch();

        EXPECT_EQ(guiStateManager.Interaction().GetCaptureID(), ID::Invalid())
            << "a captured element that no longer exists must not hold input capture";
    }

    // ============================================================================
    // Process tests
    // ============================================================================

    TEST_F(InputManagerTest, ProcessClearsEventsAfterDispatch)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);
        EXPECT_EQ(manager.GetRecordedEvents().size(), 1u);

        manager.Process(guiStateManager, elementStorage, 
            tabChainManager, /*textSystem=*/nullptr,
            ID::Invalid());

        EXPECT_EQ(manager.GetRecordedEvents().size(), 0u);
    }

    // Tab navigation consumes the Tab press itself, not whatever keyboard event
    // happened to be recorded last in the same frame.
    TEST_F(InputManagerTest, TabNavigationConsumesTheTabEvent)
    {
        manager.OnKeyboard(ID::Invalid(), KeyCode::Tab,  0, KeyModifier::None, KeyAction::KeyDown);
        manager.OnKeyboard(ID::Invalid(), KeyCode::KeyA, 0, KeyModifier::None, KeyAction::KeyDown);

        std::vector<KeyCode> received;
        manager.PushGlobalHandler([&](ElementEvent&& event) -> bool
            {
                if (event.type == EventType::KeyDown) received.push_back(event.state.lastKey);
                return true;
            });

        manager.Process(guiStateManager, elementStorage,
            tabChainManager, /*textSystem=*/nullptr,
            ID::Invalid());

        ASSERT_EQ(received.size(), 1u);
        EXPECT_EQ(received[0], KeyCode::KeyA);
    }


    // ============================================================================
    // Click-outside-text-editor defocus tests.
    //
    // Before Dispatch, Process clears a focused text editor's focus (committing it)
    // when the click lands on empty space or another IsControl element. Clicks on
    // non-control affordances (drag thumbs, labels) and on the editor itself keep it.
    // ============================================================================

    namespace
    {
        ID DefocusTestMakeID(uint64_t value)
        {
            ID id{};
            id.value.dynamicParts.mode = IDMode::Dynamic;
            id.value.dynamicParts.hashValue = value;
            return id;
        }
    }

    class InputManagerDefocusTest : public ::testing::Test
    {
    protected:
        GradientManager  gradientManager{ 256 };
        StyleManager     styleManager{ gradientManager };
        StyleRef         defaultStyle{ styleManager.Register(Style{}) };
        ElementStorage   elementStorage{ styleManager };
        GUIStateManager  guiStateManager;
        TabChainManager  tabChainManager;
        InputManager     manager;

        // The text editor that's "currently focused" for every test in this fixture.
        ID textEditorID = DefocusTestMakeID(1001);

        Element MakeElement(ID id, ElementFlags flags = ElementFlags::None)
        {
            Element e{ .styleRef = defaultStyle, .flags = flags };
            e.id = id;
            return e;
        }

        // Build storage with [textEditor, click-target] siblings under a root.
        void BuildTreeWithClickTarget(ID clickTargetID, ElementFlags clickTargetFlags)
        {
            ASSERT_EQ(elementStorage.BeginConstruction(), ElementStorageResult::Success);
            ASSERT_NE(elementStorage.Add(MakeElement(DefocusTestMakeID(1000))).ref, ElementRef::Invalid());
            ASSERT_EQ(elementStorage.BeginChildren(), ElementStorageResult::Success);
            ASSERT_NE(elementStorage.Add(MakeElement(textEditorID, ElementFlags::IsControl)).ref, ElementRef::Invalid());
            ASSERT_NE(elementStorage.Add(MakeElement(clickTargetID, clickTargetFlags)).ref, ElementRef::Invalid());
            ASSERT_EQ(elementStorage.EndChildren(), ElementStorageResult::Success);
            ASSERT_EQ(elementStorage.EndConstruction(), ElementStorageResult::Success);
        }

        // Set the simulated "focused text editor" state and inject a MouseDown.
        void FocusTextEditorAndInjectClick()
        {
            guiStateManager.Interaction().SetFocusID(textEditorID);
            guiStateManager.TextEditorFocus().Set(textEditorID);
            manager.OnMouseButton(ID::Invalid(), { 0, 0 }, MouseButtons::Left,
                                  KeyModifier::None, MouseAction::MouseDown);
        }

        void RunProcess(ID overID)
        {
            manager.Process(guiStateManager, elementStorage, tabChainManager,
                            /*textSystem=*/nullptr, overID);
        }
    };

    // A focused element that swallows every key (text editors do) must not keep
    // a key release from the global handlers: a held command would never end.
    TEST_F(InputManagerDefocusTest, KeyUpReachesGlobalHandlersPastAFocusedElement)
    {
        ASSERT_EQ(elementStorage.BeginConstruction(), ElementStorageResult::Success);
        Element editor = MakeElement(textEditorID, ElementFlags::IsControl);
        editor.eventHandler = [](ElementEvent&&) { return true; };
        ASSERT_NE(elementStorage.Add(std::move(editor)).ref, ElementRef::Invalid());
        ASSERT_EQ(elementStorage.EndConstruction(), ElementStorageResult::Success);
        guiStateManager.Interaction().SetFocusID(textEditorID);

        CommandManager commands;
        int begins = 0, ends = 0;
        commands.RegisterHeldCommand(KeyCode::Space, KeyModifier::None,
                                     [&]() { begins++; }, [&]() { ends++; });
        manager.PushGlobalHandler(commands.MakeHandler());

        // Hold started while nothing was focused...
        guiStateManager.Interaction().SetFocusID(ID::Invalid());
        manager.OnKeyboard(ID::Invalid(), KeyCode::Space, 0, KeyModifier::None, KeyAction::KeyDown);
        RunProcess(ID::Invalid());
        ASSERT_EQ(begins, 1);

        // ...then focus moved into the editor while the key was still down.
        guiStateManager.Interaction().SetFocusID(textEditorID);
        manager.OnKeyboard(ID::Invalid(), KeyCode::Space, 0, KeyModifier::None, KeyAction::KeyUp);
        RunProcess(ID::Invalid());
        EXPECT_EQ(ends, 1);
    }

    TEST_F(InputManagerDefocusTest, ClickOnEmptySpaceDefocusesTextEditor)
    {
        // No tree needed - overID = Invalid is treated as empty space.
        ASSERT_EQ(elementStorage.BeginConstruction(), ElementStorageResult::Success);
        ASSERT_EQ(elementStorage.EndConstruction(), ElementStorageResult::Success);

        FocusTextEditorAndInjectClick();
        RunProcess(/*overID=*/ID::Invalid());

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), ID::Invalid());
        EXPECT_EQ(guiStateManager.TextEditorFocus().GetFocusedID(), ID::Invalid());
    }

    TEST_F(InputManagerDefocusTest, ClickOnAnotherControlDefocusesTextEditor)
    {
        const ID otherControlID = DefocusTestMakeID(2001);
        BuildTreeWithClickTarget(otherControlID, ElementFlags::IsControl);

        FocusTextEditorAndInjectClick();
        RunProcess(/*overID=*/otherControlID);

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), ID::Invalid())
            << "clicking another IsControl element must commit/defocus the prior editor";
        EXPECT_EQ(guiStateManager.TextEditorFocus().GetFocusedID(), ID::Invalid());
    }

    TEST_F(InputManagerDefocusTest, ClickOnNonControlPreservesTextEditorFocus)
    {
        // A drag thumb / decorative container without IsControl keeps the editor focused.
        const ID dragHandleID = DefocusTestMakeID(2002);
        BuildTreeWithClickTarget(dragHandleID, ElementFlags::None);

        FocusTextEditorAndInjectClick();
        RunProcess(/*overID=*/dragHandleID);

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), textEditorID)
            << "clicking a non-control element must NOT silently strip text-editor focus";
        EXPECT_EQ(guiStateManager.TextEditorFocus().GetFocusedID(), textEditorID);
    }

    TEST_F(InputManagerDefocusTest, ClickOnTheFocusedTextEditorPreservesFocus)
    {
        // Clicking the already-focused text editor must not defocus it.
        BuildTreeWithClickTarget(DefocusTestMakeID(2003), ElementFlags::IsControl);

        FocusTextEditorAndInjectClick();
        RunProcess(/*overID=*/textEditorID);

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), textEditorID);
        EXPECT_EQ(guiStateManager.TextEditorFocus().GetFocusedID(), textEditorID);
    }

    TEST_F(InputManagerDefocusTest, ClickOnLabelPreservesTextEditorFocus)
    {
        // Labels aren't IsControl; the defocus pass must not clear focus before a
        // label's own focus-redirect handler runs.
        const ID labelID = DefocusTestMakeID(2004);
        BuildTreeWithClickTarget(labelID, ElementFlags::IsLabel);

        FocusTextEditorAndInjectClick();
        RunProcess(/*overID=*/labelID);

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), textEditorID);
        EXPECT_EQ(guiStateManager.TextEditorFocus().GetFocusedID(), textEditorID);
    }

}