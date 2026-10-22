#pragma once

#include <gui_core/gui_base_types.h>   // SteadyClockNowNanos, int2
#include <gui_core/gui_id.h>
#include <gui_core/state/gui_drag_drop_state.h>   // DragVisualizer

#include <functional>
#include <unordered_set>
#include <vector>

// Widget-state types shared by several control families (node view, table, tree/grid).

namespace GUI
{
    // ---- Multi-selection state (user-owned) ----
    struct MultiSelectState {
        std::unordered_set<GUI::ID> selectedIDs;
        GUI::ID lastClickedID = GUI::ID::Invalid();
        std::vector<GUI::ID> orderedItemIDs; // transient: rebuilt each frame for Shift+Click range selection
    };

    // ---- Drop position for tree reorder drops ----
    enum class TreeDropPosition : uint8_t {
        None   = 0,
        Before,     // drop as previous sibling of targetID                (line at row top)
        Inside,     // drop as FIRST child of targetID (open branch only) (line at row bottom, indented)
        InsideLast, // drop as LAST child of targetID                     (rectangle around row)
        After,      // drop as next sibling of targetID                   (line at row bottom)
    };

    // ---- Drag & drop config for item-based views (tree, list, grid) ----
    struct ItemDragDropConfig {
        uint32_t payloadType = 0;                                       // 0 = disabled
        std::function<uintptr_t(GUI::ID)> getPayload;                   // what to drag
        // Optional: returning false refuses the payload at that target (e.g. a folder
        // rejecting its own parent).
        std::function<bool(uintptr_t payload, GUI::ID targetID)> canDrop;
        std::function<void(uintptr_t, GUI::ID)> onDrop;                 // dropped payload value + target ID
        // Tree reorder: rows become drop targets with sibling / child zones.
        std::function<void(uintptr_t srcPayload, GUI::ID targetID,
                           TreeDropPosition pos)> onReorder;
        // Every drop on a row means InsideLast (folder-style targets, no sibling reordering).
        bool dropInsideOnly = false;
        GUI::DragVisualizer visualizer;                                 // optional visual feedback
    };

    // Click-gesture tracker, owned per widget instance so instances (and tests) don't share it.
    struct DoubleClickTracker
    {
        GUI::ID lastID = GUI::ID::Invalid();
        int64_t lastTimeNanos = 0;   // steady-clock ns; see SteadyClockNowNanos()

        struct Result { bool sameAsPrevious; float elapsedMs; };

        Result Update(GUI::ID id)
        {
            const int64_t now = SteadyClockNowNanos();
            Result r{
                .sameAsPrevious = (lastID == id),
                .elapsedMs = static_cast<float>(now - lastTimeNanos) / 1'000'000.0f,
            };
            lastID        = id;
            lastTimeNanos = now;
            return r;
        }

        void Reset() { lastID = GUI::ID::Invalid(); }
    };
}
