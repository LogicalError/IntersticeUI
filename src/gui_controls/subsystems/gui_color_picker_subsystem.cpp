#include <gui_controls/subsystems/gui_color_picker_subsystem.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/style/gui_style_manager.h>

namespace interstice
{
    ColorPickerSubsystem::ColorPickerSubsystem(FramePipeline& frame)
    {
        // Entries left in `previous` weren't claimed last frame: their swatches are gone.
        frame.OnPreFrame([this](FrameContext& ctx) {
            for (auto& [id, idx] : m_prevSwatchStyles)
                ctx.styleManager.DestroyColorStyle(idx);
            m_prevSwatchStyles = std::move(m_swatchStyles);
            m_swatchStyles.clear();
        });
    }

    VisualColorStyleIndex& ColorPickerSubsystem::ClaimSwatchStyle(interstice::ID swatchID)
    {
        auto [it, inserted] = m_swatchStyles.try_emplace(swatchID, VisualColorStyleIndex::Invalid);
        if (inserted)
        {
            auto prev = m_prevSwatchStyles.find(swatchID);
            if (prev != m_prevSwatchStyles.end())
            {
                it->second = prev->second;
                m_prevSwatchStyles.erase(prev);
            }
        }
        return it->second;
    }

    void ColorPickerSubsystem::KeepSwatchAlive(interstice::ID swatchID)
    {
        auto prev = m_prevSwatchStyles.find(swatchID);
        if (prev != m_prevSwatchStyles.end())
        {
            m_swatchStyles.try_emplace(swatchID, prev->second);
            m_prevSwatchStyles.erase(prev);
        }
    }

    void ColorPickerSubsystem::ForgetStyles()
    {
        // ClearAll freed every style: drop the indices (without destroying them) so
        // they're created anew.
        m_swatchStyles.clear();
        m_prevSwatchStyles.clear();
        ColorPickerState& s = m_state;
        for (auto* idx : { &s.svQuadColor, &s.hueStrip, &s.alphaStrip, &s.hueColor, &s.whiteColor,
                           &s.outlineColor, &s.newColor, &s.originalStyle, &s.newOpaque, &s.originalOpaque })
            *idx = VisualColorStyleIndex::Invalid;
        for (auto& idx : s.gradientStyles)
            idx = VisualColorStyleIndex::Invalid;
    }
}
