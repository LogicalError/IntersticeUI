#include <gui_controls/subsystems/gui_button_group_subsystem.h>

#include <cassert>
#include <gui_core/diagnostics/gui_logging.h>

namespace
{
    constexpr tachyon::Diagnostics::LogCategory kLogApp = tachyon::Diagnostics::MakeLogCategory("app");
}

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/style/gui_style_manager.h>

namespace tachyon
{
    ButtonGroupSubsystem::ButtonGroupSubsystem(FramePipeline& frame,
                                               ElementStorage& elementStorage,
                                               StyleManager& styleManager)
        : m_elementStorage(elementStorage), m_styleManager(styleManager)
    {
        frame.OnEndFrame([this](FrameContext&) { OnEndFrame(); });
    }

    void ButtonGroupSubsystem::OnEndFrame()
    {
        if (!IsBalanced())
        {
            Diagnostics::ReportMisuse(kLogApp, "ButtonGroupSubsystem: unbalanced Begin/End group at end of frame (closed)");
            m_buttonStack.clear();
            m_toggleStack.clear();
        }
    }

    static void UpdateElementStyleRef(tachyon::ElementStorage& storage,
                                      tachyon::StyleManager& styleManager,
                                      tachyon::ElementRef ref, tachyon::StyleRef newStyleRef)
    {
        if (auto* el = storage.GetElementPtr(ref))
            el->styleRef = newStyleRef;
        if (auto* er = storage.GetElementRectPtr(ref))
            er->layoutStyle = styleManager.GetLayoutStyle(newStyleRef.layoutStyleIndex);
    }

    void ButtonGroupSubsystem::BeginButtonGroup(ItemState state)
    {
        m_buttonStack.push_back({ state, m_elementStorage.GetCurrentNestingDepth() });
    }

    void ButtonGroupSubsystem::BeginToggleGroup(ItemState state)
    {
        m_toggleStack.push_back({ state, m_elementStorage.GetCurrentNestingDepth() });
    }

    void ButtonGroupSubsystem::EndButtonGroup()
    {
        if (m_buttonStack.empty())
        {
            Diagnostics::ReportMisuse(kLogApp, "ButtonGroupSubsystem: EndButtonGroup without BeginButtonGroup (ignored)");
            return;
        }
        auto& s = m_buttonStack.back().state;
        if (s.itemCount == 1)
            UpdateElementStyleRef(m_elementStorage, m_styleManager, s.firstRef, s.soloStyleRef);
        m_buttonStack.pop_back();
    }

    void ButtonGroupSubsystem::EndToggleGroup()
    {
        if (m_toggleStack.empty())
        {
            Diagnostics::ReportMisuse(kLogApp, "ButtonGroupSubsystem: EndToggleGroup without BeginToggleGroup (ignored)");
            return;
        }
        auto& s = m_toggleStack.back().state;
        if (s.itemCount == 1)
            UpdateElementStyleRef(m_elementStorage, m_styleManager, s.firstRef, s.soloStyleRef);
        m_toggleStack.pop_back();
    }

    ButtonGroupSubsystem::ItemState* ButtonGroupSubsystem::GetActiveButton()
    {
        if (m_buttonStack.empty()) return nullptr;
        auto& entry = m_buttonStack.back();
        if (m_elementStorage.GetCurrentNestingDepth() != entry.nestingDepth) return nullptr;
        return &entry.state;
    }

    ButtonGroupSubsystem::ItemState* ButtonGroupSubsystem::GetActiveToggle()
    {
        if (m_toggleStack.empty()) return nullptr;
        auto& entry = m_toggleStack.back();
        if (m_elementStorage.GetCurrentNestingDepth() != entry.nestingDepth) return nullptr;
        return &entry.state;
    }

    tachyon::StyleRef ButtonGroupSubsystem::NextItemStyleRef(ItemState& state)
    {
        const int currentCount = state.itemCount;
        state.itemCount++;
        if (currentCount == 0)
            return state.firstStyleRef;
        if (currentCount >= 2)
            UpdateElementStyleRef(m_elementStorage, m_styleManager, state.lastRef, state.midStyleRef);
        return state.lastStyleRef;
    }
}
