#include <gui_core/state/gui_drag_drop_state.h>
#include <gui_core/input/gui_input_state.h>

#include <cstdlib>

namespace tachyon
{
    ID DragDropState::ResolveDropTarget(ID hoveredID) const
    {
        if (m_dragPayloadType == 0 || hoveredID == ID::Invalid())
            return ID::Invalid();

        return hoveredID;
    }

    void DragDropState::ClearDrag()
    {
        m_pendingDragSourceID = ID::Invalid();
        m_draggedID           = ID::Invalid();
        m_dragOverID          = ID::Invalid();
        m_dragPayloadType     = 0;
        m_dragPayloadValue    = 0;
        m_dragPayload.reset();
        m_dragStartScreenPos  = int2{ 0, 0 };
        m_sourceDragVisualizer           = {};
        m_frameVisualizerOverrideCurrent = {};
        m_frameVisualizerOverrideNext    = {};
        m_dragValidatedTargetID = ID::Invalid();
    }

    void DragDropState::ClearEvents()
    {
        m_dragEnterTargetID       = ID::Invalid();
        m_dragOverTargetID        = ID::Invalid();
        m_dragOutTargetID         = ID::Invalid();
        m_dragDropTargetID        = ID::Invalid();
        m_dragEventSourceID       = ID::Invalid();
        m_dragEventPayloadType    = 0;
        m_dragEventPayloadValue   = 0;
    }

    void DragDropState::SetEventPayload(ID sourceID, uint32_t payloadType, uintptr_t payloadValue)
    {
        m_dragEventSourceID     = sourceID;
        m_dragEventPayloadType  = payloadType;
        m_dragEventPayloadValue = payloadValue;
    }

    bool DragDropState::IsDropTargetFor(ID id, uint32_t acceptedPayloadType) const
    {
        if (m_draggedID == ID::Invalid()) return false;
        if (m_dragOverID != id) return false;
        if (acceptedPayloadType != 0 && m_dragPayloadType != acceptedPayloadType) return false;
        return true;
    }

    void DragDropState::ClearFrameVisualizerOverride()
    {
        m_frameVisualizerOverrideCurrent = {};
        m_frameVisualizerOverrideNext    = {};
    }

    bool DragDropState::InvokeActiveVisualizer(const DragVisualContext& context) const
    {
        if (!IsDragging()) return false;

        if (m_frameVisualizerOverrideCurrent)
        {
            m_frameVisualizerOverrideCurrent(context);
            return true;
        }

        if (m_sourceDragVisualizer)
        {
            m_sourceDragVisualizer(context);
            return true;
        }

        return false;
    }

    void DragDropState::Begin(ID sourceID, uint32_t payloadType, uintptr_t payloadValue, int2 startScreenPos,
                              std::any payload, DragVisualizer sourceVisualizer)
    {
        if (sourceID == ID::Invalid() || payloadType == 0)
            return;

        m_pendingDragSourceID  = sourceID;
        m_draggedID            = ID::Invalid();
        m_dragOverID           = ID::Invalid();
        m_dragPayloadType      = payloadType;
        m_dragPayloadValue     = payloadValue;
        m_dragPayload          = payload.has_value() ? std::move(payload) : std::any(payloadValue);
        m_dragStartScreenPos   = startScreenPos;
        m_sourceDragVisualizer = std::move(sourceVisualizer);
    }

    void DragDropState::SetSourceVisualizer(DragVisualizer sourceVisualizer)
    {
        m_sourceDragVisualizer = std::move(sourceVisualizer);
    }

    void DragDropState::UpdateFromInput(const InputState& in)
    {
        if (m_pendingDragSourceID == ID::Invalid() || m_draggedID != ID::Invalid())
            return;

        if (!in.LeftDown())
        {
            ClearDrag();
            return;
        }

        const int32_t dx = std::abs(in.screenPos.x - m_dragStartScreenPos.x);
        const int32_t dy = std::abs(in.screenPos.y - m_dragStartScreenPos.y);
        if (dx >= m_startDistance || dy >= m_startDistance)
        {
            m_draggedID           = m_pendingDragSourceID;
            m_pendingDragSourceID = ID::Invalid();
        }
    }

    void DragDropState::Cancel()
    {
        ClearDrag();
    }

    void DragDropState::BeginFrame()
    {
        m_frameVisualizerOverrideCurrent = std::move(m_frameVisualizerOverrideNext);
        m_frameVisualizerOverrideNext    = {};
    }

    void DragDropState::EndFrameUpdate(ID hoveredForDrop, bool leftButtonDown)
    {
        if (m_draggedID != ID::Invalid())
        {
            SetEventPayload(m_draggedID, m_dragPayloadType, m_dragPayloadValue);
        }

        if (m_draggedID != ID::Invalid() && leftButtonDown)
        {
            const ID previousDragOver = m_dragOverID;
            const ID nextDragOver     = ResolveDropTarget(hoveredForDrop);

            if (previousDragOver != nextDragOver)
            {
                if (previousDragOver != ID::Invalid()) m_dragOutTargetID   = previousDragOver;
                if (nextDragOver     != ID::Invalid()) m_dragEnterTargetID = nextDragOver;
            }

            m_dragOverID = nextDragOver;
            if (m_dragOverID != ID::Invalid())
            {
                m_dragOverTargetID = m_dragOverID;
            }
        }

        if (!leftButtonDown)
        {
            if (m_draggedID != ID::Invalid())
            {
                const ID releaseTarget = ResolveDropTarget(hoveredForDrop);
                if (releaseTarget != ID::Invalid())
                {
                    m_dragDropTargetID = releaseTarget;
                }
                else if (m_dragOverID != ID::Invalid())
                {
                    m_dragOutTargetID = m_dragOverID;
                }
            }

            ClearDrag();
        }
    }
}
