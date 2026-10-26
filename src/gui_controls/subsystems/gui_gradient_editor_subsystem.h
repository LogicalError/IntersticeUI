#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_gradient.h>

namespace interstice
{
    class FramePipeline;
    struct FrameContext;

    /// Gradient editor popup state: edits a working copy, written back each frame it's open.
    struct GradientEditorState
    {
        interstice::ID       triggerID = interstice::ID::Invalid(); ///< swatch that opened the editor
        interstice::ID       windowID  = interstice::ID::Invalid(); ///< popup window ID
        interstice::Gradient workingCopy {};                  ///< the gradient being edited
        int           selectedStop          = 0;       ///< index into workingCopy.stops[]
        interstice::color4f  selectedStopColor {};            ///< selected stop's color, for its ColorField
        float         selectedStopPosition  = 0.0f;    ///< selected stop's position, for its Field

        interstice::Gradient originalGradient {};             ///< to restore on Cancel
        std::string          context;                         ///< the field's label, shown after the title

        /// OK keeps the gradient, Cancel and the close button restore the original one.
        /// Handled by the GradientField on its next frame.
        enum class CloseRequest { None, Ok, Cancel } closeRequest = CloseRequest::None;
        std::vector<interstice::Gradient> savedPresets;       ///< "Save preset", for the session

        enum class DragTarget { None, Stop } drag = DragTarget::None;   ///< what is being dragged
        int           dragStopIndex          = -1;      ///< the dragged stop, or -1
        bool          dragOff                = false;   ///< dragged well below the bar: removed on release

        interstice::VisualColorStyleIndex previewGradientStyle = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex stopEdgeColor        = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex stopOutlineColor     = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex stopHaloColor        = interstice::VisualColorStyleIndex::Invalid;
    };

    /// The gradient editor's state plus swatch styles keyed by swatch ID, with the same
    /// lifecycle as ColorPickerSubsystem's. The popup body is GUIApplication::EmitGradientEditor.
    class GradientEditorSubsystem
    {
    public:
        explicit GradientEditorSubsystem(FramePipeline& frame);

        /// The editor popup's state.
        GradientEditorState& State() { return m_state; }

        /// The style slot of swatch `swatchID`, kept alive this frame. Invalid for a new
        /// swatch: the caller creates the style and stores it there, reused from then on.
        interstice::VisualColorStyleIndex& ClaimSwatchStyle(interstice::ID swatchID);

        /// Carries `swatchID`'s style over without rebuilding it: a cache-hit scope
        /// doesn't re-claim its swatches, but its blitted primitives still use them.
        void KeepSwatchAlive(interstice::ID swatchID);

        /// Drops every held style index without destroying it; call after a theme reload
        /// (StyleManager::ClearAll), which already freed them.
        void ForgetStyles();

    private:
        GradientEditorState m_state{};
        std::unordered_map<interstice::ID, interstice::VisualColorStyleIndex> m_swatchStyles, m_prevSwatchStyles;
    };
}
