#include <gui_controls/subsystems/gui_label_group_subsystem.h>
#include <gui_core/style/gui_style_manager.h>

#include <cassert>
#include <cmath>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_controls/gui_theme.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/state/gui_state_manager.h>

namespace interstice
{
    LabelGroupSubsystem::LabelGroupSubsystem(FramePipeline& frame,
                                             ElementStorage& elementStorage,
                                             GUIStateManager& guiStateManager,
                                             GUITheme& theme)
        : m_elementStorage(elementStorage)
        , m_guiStateManager(guiStateManager)
        , m_theme(theme)
    {
        frame.OnPreConstruct ([this](FrameContext&) { OnPreConstruct();  });
        frame.OnPostConstruct([this](FrameContext&) { OnPostConstruct(); });
        frame.OnEndFrame     ([this](FrameContext&) { OnEndFrame();      });
    }

    LabelGroupSubsystem::State* LabelGroupSubsystem::GetActiveGroup()
    {
        if (m_stack.empty()) return nullptr;
        const auto& entry = m_stack.back();
        // Only apply the group at the same nesting depth it was begun at.
        if (m_elementStorage.GetCurrentNestingDepth() != entry.nestingDepth) return nullptr;
        return entry.state;
    }

    void LabelGroupSubsystem::BeginGroup(interstice::ID id, int32_t defaultWidth, int32_t minWidth, int32_t maxWidth, bool resizable)
    {
        // No per-frame reset here (it's in OnPreConstruct): a group may be entered several
        // times per frame (e.g. once per row), and a reset would drop an earlier hover.
        auto [it, inserted] = m_states.try_emplace(id, State{ .labelWidth = defaultWidth, .minWidth = minWidth, .maxWidth = maxWidth, .resizable = resizable });
        it->second.resizable = resizable;
        m_stack.push_back({ &it->second, m_elementStorage.GetCurrentNestingDepth() });
    }

    void LabelGroupSubsystem::EndGroup()
    {
        assert(!m_stack.empty());
        m_stack.pop_back();
    }

    void LabelGroupSubsystem::OnPreConstruct()
    {
        for (auto& [id, gs] : m_states)
        {
            gs.anyLabelHovered = false;
            gs.labelTextIDs.clear();
            gs.labelRefs.clear();
            gs.maxLabelWidthThisFrame = 0;
        }
    }

    void LabelGroupSubsystem::OnPostConstruct()
    {
        for (auto& [id, gs] : m_states)
        {
            // If no label in the group was hovered this frame and we're not
            // dragging the edge, the mouse has left the group - clear edge state.
            if (!gs.anyLabelHovered && !gs.edgeDragging)
                gs.edgeHovered = false;

            // Non-resizable groups auto-fit to the widest label, picked up next frame.
            if (!gs.resizable && gs.maxLabelWidthThisFrame > 0)
                gs.labelWidth = int32_t(std::ceil(float(gs.maxLabelWidthThisFrame) / m_elementStorage.GetStyleManager().GetUIScale()));
        }
    }

    void LabelGroupSubsystem::OnEndFrame()
    {
        if (!IsBalanced())
        {
            assert(false && "LabelGroupSubsystem: unbalanced BeginGroup/EndGroup at end of frame");
            m_stack.clear();
        }
    }

    void LabelGroupSubsystem::AddDragger(interstice::ElementRef labelRef)
    {
        auto* groupState = m_stack.back().state;

        // Hover from last frame's hit-test, so edge state persists without a MouseMove.
        interstice::ID labelID = m_elementStorage.GetElementUnsafe(labelRef).id;
        interstice::ElementState labelState = m_guiStateManager.GetCurrentState(labelID);
        if ((labelState & interstice::ElementState::Hover) != interstice::ElementState::None)
        {
            groupState->anyLabelHovered = true;
            if (groupState->edgeHovered || groupState->edgeDragging)
                m_guiStateManager.Cursor().Request(interstice::MouseCursor::ColResize);
        }

        auto* state = groupState;
        const size_t labelIndex = state->labelRefs.size();
        state->labelRefs.push_back(labelRef);
        auto lineColor = m_theme.dividerHoverLineStyle;   // like the other dividers' hover
        [[maybe_unused]] auto cdRes = m_elementStorage.AddCustomDraw(labelRef,
            [state, labelIndex, storage = &m_elementStorage, lineColor,
             edgeWidth = m_elementStorage.GetStyleManager().Px(4)](interstice::CustomDrawContext& ctx)
            {
                if (!state->edgeHovered && !state->edgeDragging)
                    return;
                const auto outer = ctx.ElementRect();
                const auto inner = ctx.ElementInnerRect();
                const int32_t edgeOffset = (int32_t)floor(edgeWidth / 2.0f);
                const int32_t xRight = (outer.x + outer.w) - inner.x - edgeOffset;
                const int32_t yTop   = outer.y - inner.y;
                int32_t       yBot   = (outer.y + outer.h) - inner.y;

                // One straight line down the group: run on to the next label in the same
                // container, across the gap between the rows.
                if (labelIndex + 1 < state->labelRefs.size())
                {
                    const ElementRef self = state->labelRefs[labelIndex];
                    const ElementRef next = state->labelRefs[labelIndex + 1];
                    const ElementRect* selfRect = storage->GetElementRectPtr(self);
                    const ElementRect* nextRect = storage->GetElementRectPtr(next);
                    if (selfRect && nextRect && storage->GetParentRef(self) == storage->GetParentRef(next)
                        && nextRect->rect.y >= selfRect->rect.y + selfRect->rect.h)
                        yBot = (outer.y + (nextRect->rect.y - selfRect->rect.y)) - inner.y;
                }

                interstice::LineStyle style{ .colorStyleIndex = lineColor, .lineWidth = float(edgeWidth) };
                ctx.RenderLine({ xRight, yTop }, { xRight, yBot }, style);
            });
        assert(cdRes == ElementStorageResult::Success);
    }
}
