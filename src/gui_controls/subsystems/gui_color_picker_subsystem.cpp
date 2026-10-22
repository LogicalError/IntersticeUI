#include <gui_controls/subsystems/gui_color_picker_subsystem.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/style/gui_style_manager.h>

namespace GUI
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

    VisualColorStyleIndex& ColorPickerSubsystem::ClaimSwatchStyle(GUI::ID swatchID)
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

    void ColorPickerSubsystem::KeepSwatchAlive(GUI::ID swatchID)
    {
        auto prev = m_prevSwatchStyles.find(swatchID);
        if (prev != m_prevSwatchStyles.end())
        {
            m_swatchStyles.try_emplace(swatchID, prev->second);
            m_prevSwatchStyles.erase(prev);
        }
    }
}
