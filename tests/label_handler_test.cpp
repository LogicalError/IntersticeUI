#include <gtest/gtest.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>

#include <gui_core/elements/gui_element.h>
#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/input/gui_input_manager.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>

#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_tab_chain_manager.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <gui_controls/gui_default_handlers.h>

namespace tachyon
{
    namespace
    {
        ID MakeID(uint64_t value)
        {
            ID id{};
            id.value.dynamicParts.mode = IDMode::Dynamic;
            id.value.dynamicParts.hashValue = value;
            return id;
        }

        ElementEvent MakeEvent(ID id, EventType type, InputState& state)
        {
            return ElementEvent{
                /*id*/         id,
                /*type*/       type,
                /*state*/      state,
                /*rect*/       nullptr,
                /*elementRef*/ ElementRef::Invalid(),
            };
        }

        InputState MakeMouseDownState()
        {
            InputState s{};
            s.buttons     = MouseButtons::Left;
            s.prevButtons = MouseButtons::None; // LeftPressed() == true
            return s;
        }
    }

    // =========================================================================
    // DragLabel MouseDown must not set TextEditorFocus to the label: labels are
    // not text editors, and it would make the label render another editor's content.
    // =========================================================================

    class LabelHandlerTextEditorFocusTest : public ::testing::Test
    {
    protected:
        GradientManager  gradientManager{ 256 };
        StyleManager     styleManager{ gradientManager };
        StyleRef         defaultStyle{ styleManager.Register(Style{}) };
        ElementStorage   elementStorage{ styleManager };
        GUIStateManager  guiStateManager;

        ID labelID = MakeID(101);
    };

    TEST_F(LabelHandlerTextEditorFocusTest, DraggableLabelMouseDownDoesNotPointTextEditorFocusAtLabel)
    {
        auto handler = MakeDraggableLabelHandler(guiStateManager, elementStorage);

        // Another field already holds text-editor focus.
        const ID priorEditor = MakeID(999);
        guiStateManager.TextEditorFocus().Set(priorEditor);

        InputState down = MakeMouseDownState();
        EXPECT_TRUE(handler(MakeEvent(labelID, EventType::MouseDown, down)));

        // Interaction focus moves to the label so it can capture drag motion.
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), labelID);

        EXPECT_NE(guiStateManager.TextEditorFocus().GetFocusedID(), labelID);
    }

    TEST_F(LabelHandlerTextEditorFocusTest, GroupDraggableLabelMouseDownDoesNotPointTextEditorFocusAtLabel)
    {
        // Minimal visual state: textSystem/styleManager are only dereferenced
        // when the press is near the right edge.
        int32_t sharedWidth = 100;
        bool edgeHovered = false;
        bool edgeDragging = false;
        std::vector<TextID> labelTextIDs;
        LabelGroupVisualState gv{
            .sharedWidth   = &sharedWidth,
            .minWidth      = 40,
            .maxWidth      = 300,
            .edgeHovered   = &edgeHovered,
            .edgeDragging  = &edgeDragging,
            .labelTextIDs  = &labelTextIDs,
            .textSystem    = nullptr,
            .styleManager  = nullptr,
            .labelStyleRef = StyleRef{},
        };

        auto handler = MakeGroupDraggableLabelHandler(guiStateManager, elementStorage, gv);

        const ID priorEditor = MakeID(999);
        guiStateManager.TextEditorFocus().Set(priorEditor);

        InputState down = MakeMouseDownState();
        EXPECT_TRUE(handler(MakeEvent(labelID, EventType::MouseDown, down)));

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), labelID);
        EXPECT_NE(guiStateManager.TextEditorFocus().GetFocusedID(), labelID);
    }

    // =========================================================================
    // Label :focus propagation: m_focusedAssociatedLabelIDs, filled at the end of
    // frame N, must survive BeginFrame so frame N+1's GetCurrentState sees them.
    // =========================================================================

    class LabelFocusPropagationTest : public ::testing::Test
    {
    protected:
        GradientManager  gradientManager{ 256 };
        StyleManager     styleManager{ gradientManager };
        StyleRef         defaultStyle{ styleManager.Register(Style{}) };
        ElementStorage   elementStorage{ styleManager };
        GUIStateManager  guiStateManager;
        TabChainManager  tabChainManager;
        InputManager     inputManager;

        Element MakeLabel(ID id)
        {
            Element e{ .styleRef = defaultStyle, .flags = ElementFlags::IsLabel };
            e.id = id;
            return e;
        }

        Element MakeControl(ID id)
        {
            Element e{ .styleRef = defaultStyle, .flags = ElementFlags::IsControl };
            e.id = id;
            return e;
        }

        Element MakeRoot(ID id)
        {
            Element e{ .styleRef = defaultStyle };
            e.id = id;
            return e;
        }
    };

    // Focusing a Field bound to a Label applies the label's :focus style next frame.
    TEST_F(LabelFocusPropagationTest, FocusedControlPropagatesToPreviousSiblingLabelOnNextFrame)
    {
        const ID labelID   = MakeID(201);
        const ID controlID = MakeID(202);

        // Frame N: build a tree with [label, control] siblings under a root.
        guiStateManager.BeginFrame();
        ASSERT_EQ(elementStorage.BeginConstruction(), ElementStorageResult::Success);
        {
            ASSERT_NE(elementStorage.Add(MakeRoot(MakeID(200))).ref, ElementRef::Invalid());
            ASSERT_EQ(elementStorage.BeginChildren(), ElementStorageResult::Success);
            ASSERT_NE(elementStorage.Add(MakeLabel(labelID)).ref,    ElementRef::Invalid());
            ASSERT_NE(elementStorage.Add(MakeControl(controlID)).ref, ElementRef::Invalid());
            ASSERT_EQ(elementStorage.EndChildren(), ElementStorageResult::Success);
        }
        ASSERT_EQ(elementStorage.EndConstruction(), ElementStorageResult::Success);

        // Focus moves to the control (simulates a click handler firing).
        guiStateManager.Interaction().SetFocusID(controlID);

        // End-of-frame input processing populates m_focusedAssociatedLabelIDs.
        inputManager.Process(guiStateManager, elementStorage,
            tabChainManager, /*textSystem=*/nullptr,
            /*overID=*/ID::Invalid());

        // Frame N+1: assoc list must survive across BeginFrame so OnGUI's
        // GetCurrentState sees the label as focused.
        guiStateManager.BeginFrame();

        const ElementState labelState = guiStateManager.GetCurrentState(labelID);
        EXPECT_NE((labelState & ElementState::Focused), ElementState::None)
            << "Label lost Focused state across BeginFrame — focus propagation broken";
    }
}
