#include <gui_controls/subsystems/gui_reorderable_subsystem.h>

#include <cassert>
#include <gui_core/diagnostics/gui_logging.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/input/gui_input_manager.h>

namespace tachyon
{
    ReorderableSubsystem::ReorderableSubsystem(FramePipeline& frame)
    {
        frame.OnPreConstruct([this](FrameContext& ctx) { OnPreConstruct(ctx); });
        frame.OnEndFrame([this](FrameContext&) { OnEndFrame(); });
    }

    // A drag ends when its item sees the button release. If the item vanished first (or
    // the release was otherwise lost) nothing resets the drag, and the item would keep
    // being displaced. So a drag with no button held and no release still queued is dead.
    void ReorderableSubsystem::OnPreConstruct(FrameContext& ctx)
    {
        if (!m_drag.isDragging || ctx.inputManager == nullptr) return;
        if ((ctx.inputManager->GetCurrentButtons() & MouseButtons::Left) != MouseButtons::None) return;
        for (const auto& e : ctx.inputManager->GetRecordedEvents())
            if (e.type == EventType::MouseUp) return; // dispatched at the end of this frame
        m_drag = {};
    }

    void ReorderableSubsystem::OnEndFrame()
    {
        if (!IsBalanced())
        {
            Diagnostics::ReportMisuse(Diagnostics::MakeLogCategory("app"),
                "ReorderableSubsystem: unbalanced BeginReorderableGroup/End at end of frame (closed)");
            m_construction = {};
        }
    }
}
