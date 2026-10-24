#pragma once

#include <gui_core/gui_base_types.h>   // SteadyClockNowNanos, int2
#include <gui_core/gui_id.h>
#include <gui_core/state/gui_drag_drop_state.h>   // DragVisualizer

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

// Widget-state types shared by several control families (node view, table, tree/grid).

namespace interstice
{
    // ---- Multi-selection state (user-owned) ----
    struct MultiSelectState {
        std::unordered_set<interstice::ID> selectedIDs;
        interstice::ID lastClickedID = interstice::ID::Invalid();
        std::vector<interstice::ID> orderedItemIDs; // transient: rebuilt each frame for Shift+Click range selection
    };

    // ---- Command search (user-owned; see GUIApplication::CommandSearch) ----
    struct CommandSearchState {
        std::string query;
        std::string hint = "Search or run a command"; // shown while the query is empty
        std::string shortcut;                         // keycap at the right, e.g. the key that focuses the search
        int         current = 0;                      // highlighted match, moved by Up / Down
        bool        focusRequested = false;           // set by FocusCommandSearch, taken next frame
        bool        ranCommand = false;               // a command ran since the last CommandSearch call
        interstice::ID listWindowID = interstice::ID::Invalid();
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
        std::function<uintptr_t(interstice::ID)> getPayload;                   // what to drag
        // Optional: returning false refuses the payload at that target (e.g. a folder
        // rejecting its own parent).
        std::function<bool(uintptr_t payload, interstice::ID targetID)> canDrop;
        std::function<void(uintptr_t, interstice::ID)> onDrop;                 // dropped payload value + target ID
        // Tree reorder: rows become drop targets with sibling / child zones.
        std::function<void(uintptr_t srcPayload, interstice::ID targetID,
                           TreeDropPosition pos)> onReorder;
        // Every drop on a row means InsideLast (folder-style targets, no sibling reordering).
        bool dropInsideOnly = false;
        interstice::DragVisualizer visualizer;                                 // optional visual feedback
    };

    // Click-gesture tracker, owned per widget instance so instances (and tests) don't share it.
    struct DoubleClickTracker
    {
        interstice::ID lastID = interstice::ID::Invalid();
        int64_t lastTimeNanos = 0;   // steady-clock ns; see SteadyClockNowNanos()

        struct Result { bool sameAsPrevious; float elapsedMs; };

        Result Update(interstice::ID id)
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

        void Reset() { lastID = interstice::ID::Invalid(); }
    };
}
