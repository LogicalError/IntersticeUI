#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_color_conversions.h>

namespace interstice
{
    class FramePipeline;
    struct FrameContext;

    /// Persistent state of the color picker popup. The picker opens off a ColorField
    /// swatch (triggerID), edits a working copy, and writes the result back to the
    /// caller's color. It lives across frames so the popup keeps its tab, palette and
    /// drag state while the popup is rebuilt each frame.
    struct ColorPickerState
    {
        interstice::ID triggerID = interstice::ID::Invalid(); ///< swatch that opened the picker
        interstice::ID windowID  = interstice::ID::Invalid(); ///< popup window ID
        float h = 0, s = 0, v = 0, a = 1;       ///< working HSV+A copy
        interstice::color4f originalColor {};           ///< color when picker was opened
        bool  allowHDR      = false;                 ///< the opening field accepts HDR colors
        float hdrBrightness = 1.0f;                  ///< HDR multiplier; > 1 brightens past 1
        std::string hexText;                     ///< hex input field text

        interstice::ColorSpace activeSpace = interstice::ColorSpace::HSV;

        /// Components of the active space (H/S/V, R/G/B, L/a/b, ...). Members, since
        /// GradientSlider handlers keep pointers to them until dispatch.
        float comp[3]     = { 0, 0, 0 };
        /// comp as last built; a difference next frame means a slider edited it.
        float snapComp[3] = { 0, 0, 0 };
        bool  slidersInitialized = false;

        /// Channel values shown 0-255 (and hue in degrees, S/V/L in percent), or 0-1.
        bool byteRange = true;
        /// The eyedropper is held: the color follows the screen under the cursor.
        bool picking   = false;
        std::optional<bool> canPick;     ///< whether the platform reads the screen (asked once)
        std::string context;   ///< what's being edited (the field's label), after the title

        /// OK keeps the color (and adds it to the recent ones), Cancel and the close button
        /// restore the one the picker opened with. Handled by the ColorField on its next frame.
        enum class CloseRequest { None, Ok, Cancel } closeRequest = CloseRequest::None;
        std::vector<interstice::color4f> recent;   ///< newest first

        interstice::VisualColorStyleIndex svQuadColor   = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex hueStrip      = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex alphaStrip    = interstice::VisualColorStyleIndex::Invalid;
        /// The pure hue, shown in the hue handle.
        interstice::VisualColorStyleIndex hueColor      = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex whiteColor    = interstice::VisualColorStyleIndex::Invalid;
        /// Outline around the cursors and handles.
        interstice::VisualColorStyleIndex outlineColor  = interstice::VisualColorStyleIndex::Invalid;
        /// The new color, with its alpha.
        interstice::VisualColorStyleIndex newColor      = interstice::VisualColorStyleIndex::Invalid;
        interstice::VisualColorStyleIndex originalStyle = interstice::VisualColorStyleIndex::Invalid;
        /// The new color without its alpha, for the preview's top half.
        interstice::VisualColorStyleIndex newOpaque      = interstice::VisualColorStyleIndex::Invalid;
        /// The original color without its alpha, for the preview's top half.
        interstice::VisualColorStyleIndex originalOpaque = interstice::VisualColorStyleIndex::Invalid;

        enum class DragTarget { None, Square, Hue, Alpha } drag = DragTarget::None;

        static constexpr int kNumGradientSliders = 4;
        interstice::VisualColorStyleIndex gradientStyles[kNumGradientSliders] = {};

        struct PaletteSwatch { interstice::color4f color {}; };
        std::vector<PaletteSwatch> palette = {
            {{ 0.8f, 0.8f, 0.8f, 1.0f }},
            {{ 0.0f, 0.8f, 0.8f, 1.0f }},
            {{ 0.8f, 0.0f, 0.8f, 1.0f }},
            {{ 0.8f, 0.8f, 0.0f, 1.0f }},
        };
    };

    /// The color picker popup's state plus swatch ColorStyles keyed by swatch ID.
    /// Claimed styles are updated in place across frames; unclaimed ones belong to
    /// vanished swatches and are destroyed next frame. The popup body is
    /// GUIApplication::EmitColorPicker.
    class ColorPickerSubsystem
    {
    public:
        explicit ColorPickerSubsystem(FramePipeline& frame);

        /// The picker popup's state.
        ColorPickerState& State() { return m_state; }

        /// The color style slot of swatch `swatchID`, kept alive this frame. Invalid for a new
        /// swatch: the caller creates the style and stores it there, reused from then on.
        interstice::VisualColorStyleIndex& ClaimSwatchStyle(interstice::ID swatchID);

        /// Carries `swatchID`'s style over without rebuilding it: a cache-hit scope
        /// doesn't re-claim its swatches, but its blitted primitives still use them.
        void KeepSwatchAlive(interstice::ID swatchID);

        /// Drops every held style index without destroying it; call after a theme reload
        /// (StyleManager::ClearAll), which already freed them.
        void ForgetStyles();

    private:
        ColorPickerState m_state{};
        std::unordered_map<interstice::ID, interstice::VisualColorStyleIndex> m_swatchStyles, m_prevSwatchStyles;
    };
}
