#pragma once

#include <unordered_map>

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_gradient.h>

namespace GUI
{
    class FramePipeline;
    struct FrameContext;

    // Gradient editor popup state: edits a working copy, written back each frame it's open.
    struct GradientEditorState
    {
        GUI::ID       triggerID = GUI::ID::Invalid(); // swatch that opened the editor
        GUI::ID       windowID  = GUI::ID::Invalid(); // popup window ID
        GUI::Gradient workingCopy {};                  // mutable copy while editor is open
        int           selectedStop          = 0;       // index into workingCopy.stops[]
        GUI::color4f  selectedStopColor {};            // bridge to ColorField
        float         selectedStopPosition  = 0.0f;    // bridge to numeric Field

        enum class DragTarget { None, Stop } drag = DragTarget::None;
        int           dragStopIndex          = -1;

        GUI::VisualColorStyleIndex previewGradientStyle = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex stopMarkerColor      = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex stopMarkerSelected   = GUI::VisualColorStyleIndex::Invalid;
    };

    // The gradient editor's state plus swatch styles keyed by swatch ID, with the same
    // lifecycle as ColorPickerSubsystem's. The popup body is GUIApplication::EmitGradientEditor.
    class GradientEditorSubsystem
    {
    public:
        explicit GradientEditorSubsystem(FramePipeline& frame);

        GradientEditorState& State() { return m_state; }

        // Invalid for a new swatch: the caller creates the style, reused from then on.
        GUI::VisualColorStyleIndex& ClaimSwatchStyle(GUI::ID swatchID);

        // Carry swatchID's style over without rebuilding it: a cache-hit scope
        // doesn't re-claim its swatches, but its blitted primitives still use them.
        void KeepSwatchAlive(GUI::ID swatchID);

    private:
        GradientEditorState m_state{};
        std::unordered_map<GUI::ID, GUI::VisualColorStyleIndex> m_swatchStyles, m_prevSwatchStyles;
    };
}
