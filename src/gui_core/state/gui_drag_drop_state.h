// Drag & drop state, owned by GUIStateManager. A pending source is armed on mouse-down
// and becomes the active drag once the pointer passes the start distance. Per-frame
// events (enter/over/out/drop/validated) are cleared at the top of the next EndFrame.

#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/input/gui_input_enums.h>

#include <any>
#include <functional>
#include <stdint.h>

namespace interstice
{
    struct InputState;

    struct DragVisualContext
    {
        interstice::int2 screenPos{ 0, 0 };
        ID sourceID = ID::Invalid();
        ID targetID = ID::Invalid();
        bool isDropValid = false;
        bool leftMouseDown = false;

        uint32_t payloadType = 0;
        uintptr_t payloadValue = 0;
        const std::any* payload = nullptr;
    };

    using DragVisualizer = std::function<void(const DragVisualContext& context)>;

    class DragDropState
    {
        ID        m_pendingDragSourceID = ID::Invalid();
        ID        m_draggedID           = ID::Invalid();
        ID        m_dragOverID          = ID::Invalid();
        int2      m_dragStartScreenPos{ 0, 0 };
        uint32_t  m_dragPayloadType     = 0;
        uintptr_t m_dragPayloadValue    = 0;
        std::any  m_dragPayload;

        DragVisualizer m_sourceDragVisualizer;
        DragVisualizer m_frameVisualizerOverrideCurrent;
        DragVisualizer m_frameVisualizerOverrideNext;

        ID m_dragEnterTargetID     = ID::Invalid();
        ID m_dragOverTargetID      = ID::Invalid();
        ID m_dragOutTargetID       = ID::Invalid();
        ID m_dragDropTargetID      = ID::Invalid();
        ID m_dragValidatedTargetID = ID::Invalid();

        ID        m_dragEventSourceID       = ID::Invalid();
        uint32_t  m_dragEventPayloadType    = 0;
        uintptr_t m_dragEventPayloadValue   = 0;
        int32_t   m_startDistance           = kDragStartDistance;

        ID   ResolveDropTarget(ID hoveredID) const;
        void ClearDrag();
        void SetEventPayload(ID sourceID, uint32_t payloadType, uintptr_t payloadValue);

    public:
        // Query
        bool IsDragging()  const { return m_draggedID != ID::Invalid(); }
        ID   GetDraggedID()  const { return m_draggedID;  }
        ID   GetDragOverID() const { return m_dragOverID; }
        uintptr_t GetPayloadValue() const { return m_dragPayloadValue; }

        // acceptedPayloadType 0 = any type.
        bool IsDropTargetFor(ID id, uint32_t acceptedPayloadType = 0) const;

        ID GetEnterTargetID()     const { return m_dragEnterTargetID;     }
        ID GetOverTargetID()      const { return m_dragOverTargetID;      }
        ID GetOutTargetID()       const { return m_dragOutTargetID;       }
        ID GetDropTargetID()      const { return m_dragDropTargetID;      }
        ID GetValidatedTargetID() const { return m_dragValidatedTargetID; }

        ID        GetEventSourceID()       const { return m_dragEventSourceID;     }
        uint32_t  GetEventPayloadType()    const { return m_dragEventPayloadType;  }
        uintptr_t GetEventPayloadValue()   const { return m_dragEventPayloadValue; }
        const std::any& GetPayload()       const { return m_dragPayload; }

        // Mutation
        void Begin(ID sourceID, uint32_t payloadType, uintptr_t payloadValue, int2 startScreenPos,
                   std::any payload = {}, DragVisualizer sourceVisualizer = {});
        void SetSourceVisualizer(DragVisualizer sourceVisualizer);
        void UpdateFromInput(const InputState& in);
        // Pointer travel (pixels) before a pending drag starts.
        void SetStartDistance(int32_t pixels) { m_startDistance = pixels; }
        void Cancel();

        void SetValidatedTargetID(ID id) { m_dragValidatedTargetID = id; }

        void SetFrameVisualizerOverride(DragVisualizer visualizer) { m_frameVisualizerOverrideNext = std::move(visualizer); }
        void ClearFrameVisualizerOverride();
        bool InvokeActiveVisualizer(const DragVisualContext& context) const;

        // Clears per-frame events. GUIStateManager::EndFrame calls it; the input manager may too.
        void ClearEvents();

        // Frame hooks
        void BeginFrame();
        void EndFrameUpdate(ID hoveredForDrop, bool leftButtonDown);
    };
}
