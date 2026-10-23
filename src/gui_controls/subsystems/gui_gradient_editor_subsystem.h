#pragma once

#include <unordered_map>

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_gradient.h>

namespace interstice
{
    class FramePipeline;
    struct FrameContext;

    // Gradient editor popup state: edits a working copy, written back each frame it's open.
    struct GradientEditorState
    {
        interstice::ID       triggerID = interstice::ID::Invalid(); // swatch that opened the editor
        interstice::ID       windowID  = interstice::ID::Invalid(); // popup window ID
        interstice::Gradient workingCopy {};                  // mutable copy while editor is open
        int           selectedStop          = 0;       // index into workingCopy.stops[]
        interstice::color4f  selectedStopColor {};            // bridge to ColorField
        float         selectedStopPosition  = 0.0f;    // bridge to numeric Field

        enum class DragTarget { None, Stop } drag = DragTarget::None;
        int           dragStopIndex          = -1;

        interstice::VisualColorStyleIndex previewGradientStyle = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex stopMarkerColor      = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex stopMarkerSelected   = interstice::VisualColorStyleIndex::Invalid;
    };

    // The gradient editor's state plus swatch styles keyed by swatch ID, with the same
    // lifecycle as ColorPickerSubsystem's. The popup body is GUIApplication::EmitGradientEditor.
    class GradientEditorSubsystem
    {
    public:
        explicit GradientEditorSubsystem(FramePipeline& frame);

        GradientEditorState& State() { return m_state; }

        // Invalid for a new swatch: the caller creates the style, reused from then on.
        interstice::VisualColorStyleIndex& ClaimSwatchStyle(interstice::ID swatchID);

        // Carry swatchID's style over without rebuilding it: a cache-hit scope
        // doesn't re-claim its swatches, but its blitted primitives still use them.
        void KeepSwatchAlive(interstice::ID swatchID);

    private:
        GradientEditorState m_state{};
        std::unordered_map<interstice::ID, interstice::VisualColorStyleIndex> m_swatchStyles, m_prevSwatchStyles;
    };
}
