#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_color_conversions.h>

namespace GUI
{
    class FramePipeline;
    struct FrameContext;

    // Persistent state for the color picker popup. The picker opens off a
    // ColorField swatch (triggerID), edits HSV/RGB/etc. in the working
    // copy, and writes the result back to the caller's color value. Lives
    // across frames so the popup keeps its tab / palette / drag state when
    // the dataflow re-enters BeginMenu each frame.
    struct ColorPickerState
    {
        GUI::ID triggerID = GUI::ID::Invalid(); // swatch that opened the picker
        GUI::ID windowID  = GUI::ID::Invalid(); // popup window ID
        float h = 0, s = 0, v = 0, a = 1;       // working HSV+A copy
        GUI::color4f originalColor {};           // color when picker was opened
        bool  allowHDR      = false;
        float hdrBrightness = 1.0f;
        std::string hexText;                     // hex input field text

        GUI::ColorSpace activeSpace = GUI::ColorSpace::HSV;

        // Components of the active space (H/S/V, R/G/B, L/a/b, ...). Members, since
        // GradientSlider handlers keep pointers to them until dispatch.
        float comp[3]     = { 0, 0, 0 };
        float snapComp[3] = { 0, 0, 0 };
        bool  slidersInitialized = false;

        GUI::VisualColorStyleIndex svQuadColor      = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex hueRing          = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex markerColor      = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex whiteColor       = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex svIndicatorColor = GUI::VisualColorStyleIndex::Invalid;

        enum class WheelDragTarget { None, Ring, Square } wheelDrag = WheelDragTarget::None;

        static constexpr int kNumGradientSliders = 4;
        GUI::VisualColorStyleIndex gradientStyles[kNumGradientSliders] = {};
        GUI::VisualColorStyleIndex triangleStyle = GUI::VisualColorStyleIndex::Invalid;
        GUI::VisualColorStyleIndex newColor      = GUI::VisualColorStyleIndex::Invalid;

        struct PaletteSwatch { GUI::color4f color {}; };
        std::vector<PaletteSwatch> palette = {
            {{ 0.8f, 0.8f, 0.8f, 1.0f }},
            {{ 0.0f, 0.8f, 0.8f, 1.0f }},
            {{ 0.8f, 0.0f, 0.8f, 1.0f }},
            {{ 0.8f, 0.8f, 0.0f, 1.0f }},
        };
    };

    // The color picker popup's state plus swatch ColorStyles keyed by swatch ID.
    // Claimed styles are updated in place across frames; unclaimed ones belong to
    // vanished swatches and are destroyed next frame. The popup body is
    // GUIApplication::EmitColorPicker.
    class ColorPickerSubsystem
    {
    public:
        explicit ColorPickerSubsystem(FramePipeline& frame);

        ColorPickerState& State() { return m_state; }

        // Invalid for a new swatch: the caller creates the style, reused from then on.
        GUI::VisualColorStyleIndex& ClaimSwatchStyle(GUI::ID swatchID);

        // Carry swatchID's style over without rebuilding it: a cache-hit scope
        // doesn't re-claim its swatches, but its blitted primitives still use them.
        void KeepSwatchAlive(GUI::ID swatchID);

    private:
        ColorPickerState m_state{};
        std::unordered_map<GUI::ID, GUI::VisualColorStyleIndex> m_swatchStyles, m_prevSwatchStyles;
    };
}
