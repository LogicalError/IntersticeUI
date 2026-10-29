#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <gui_core/input/gui_pointer_drag.h>
#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_gradient.h>

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    class FramePipeline;
    struct FrameContext;

    /// Gradient editor popup state: edits a working copy, written back each frame it's open.
    struct GradientEditorState
    {
        tachyon::ID       triggerID = tachyon::ID::Invalid(); ///< swatch that opened the editor
        tachyon::ID       windowID  = tachyon::ID::Invalid(); ///< popup window ID
        tachyon::Gradient workingCopy {};                  ///< the gradient being edited
        int           selectedStop          = 0;       ///< index into workingCopy.stops[]
        tachyon::color4f  selectedStopColor {};            ///< selected stop's color, for its ColorField
        float         selectedStopPosition  = 0.0f;    ///< selected stop's position, for its Field

        tachyon::Gradient originalGradient {};             ///< to restore on Cancel
        std::string          context;                         ///< the field's label, shown after the title

        /// OK keeps the gradient, Cancel and the close button restore the original one.
        /// Handled by the GradientField on its next frame.
        enum class CloseRequest { None, Ok, Cancel } closeRequest = CloseRequest::None;
        std::vector<tachyon::Gradient> savedPresets;       ///< "Save preset", for the session

        tachyon::PointerDrag press;                     ///< the press on a stop, a drag from the start
        int           dragStopIndex          = -1;      ///< the dragged stop, or -1
        float         dragGrab               = 0.0f;    ///< cursor minus the stop's offset at the press, so it doesn't jump
        bool          dragOff                = false;   ///< dragged well below the bar: removed on release
        tachyon::Gradient beforeDrag {};                ///< the gradient before the drag; put back by Escape
        int           selectedBeforeDrag     = 0;

        tachyon::VisualColorStyleIndex previewGradientStyle = tachyon::VisualColorStyleIndex::Invalid;
        tachyon::VisualColorStyleIndex stopEdgeColor        = tachyon::VisualColorStyleIndex::Invalid;
        tachyon::VisualColorStyleIndex stopOutlineColor     = tachyon::VisualColorStyleIndex::Invalid;
        tachyon::VisualColorStyleIndex stopHaloColor        = tachyon::VisualColorStyleIndex::Invalid;
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
        tachyon::VisualColorStyleIndex& ClaimSwatchStyle(tachyon::ID swatchID);

        /// Carries `swatchID`'s style over without rebuilding it: a cache-hit scope
        /// doesn't re-claim its swatches, but its blitted primitives still use them.
        void KeepSwatchAlive(tachyon::ID swatchID);

        /// Drops every held style index without destroying it; call after a theme reload
        /// (StyleManager::ClearAll), which already freed them.
        void ForgetStyles();

    private:
        GradientEditorState m_state{};
        std::unordered_map<tachyon::ID, tachyon::VisualColorStyleIndex> m_swatchStyles, m_prevSwatchStyles;
    };
    /// @}
}
