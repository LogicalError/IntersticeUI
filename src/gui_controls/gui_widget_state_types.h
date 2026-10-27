#pragma once

#include <gui_core/gui_base_types.h>   // SteadyClockNowNanos, int2
#include <gui_core/gui_id.h>
#include <gui_core/state/gui_drag_drop_state.h>   // DragVisualizer

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

// Widget-state types shared by several control families (node view, table, tree/grid).

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    // ---- Multi-selection state (user-owned) ----
    /// Selected item IDs of a multi-select view; user-owned, keep it alive across frames.
    struct MultiSelectState {
        std::unordered_set<tachyon::ID> selectedIDs;
        tachyon::ID lastClickedID = tachyon::ID::Invalid();  ///< anchor for Shift+Click ranges
        /// Transient: rebuilt each frame for Shift+Click range selection.
        std::vector<tachyon::ID> orderedItemIDs;
    };

    // ---- Command search (user-owned; see GUIApplication::CommandSearch) ----
    /// State of a CommandSearch box; user-owned, keep it alive across frames.
    struct CommandSearchState {
        std::string query;                            ///< the typed text
        std::string hint = "Search or run a command"; ///< shown while the query is empty
        /// Keycap at the right, e.g. the key that focuses the search.
        std::string shortcut;
        int         current = 0;                      ///< highlighted match, moved by Up / Down
        bool        focusRequested = false;           ///< set by FocusCommandSearch, taken next frame
        bool        ranCommand = false;               ///< a command ran since the last CommandSearch call
        tachyon::ID listWindowID = tachyon::ID::Invalid();  ///< internal: the results popup
    };

    // ---- Drop position for tree reorder drops ----
    /// Where a tree reorder drop lands relative to the target row.
    enum class TreeDropPosition : uint8_t {
        None   = 0,
        Before,     ///< previous sibling of the target (line at the row top)
        Inside,     ///< first child of an open target branch (indented line at the row bottom)
        InsideLast, ///< last child of the target (rectangle around the row)
        After,      ///< next sibling of the target (line at the row bottom)
    };

    // ---- Drag & drop config for item-based views (tree, list, grid) ----
    /// Drag & drop setup for item-based views (tree, list, grid): items drag a payload and
    /// accept drops of the same payloadType.
    struct ItemDragDropConfig {
        uint32_t payloadType = 0;                                       ///< 0 = disabled
        /// What to drag: the payload value for an item ID.
        std::function<uintptr_t(tachyon::ID)> getPayload;
        /// Optional: returning false refuses the payload at that target (e.g. a folder
        /// rejecting its own parent).
        std::function<bool(uintptr_t payload, tachyon::ID targetID)> canDrop;
        /// Called with the dropped payload and the target ID (in a grid, the view's ID for a
        /// drop on the background).
        std::function<void(uintptr_t, tachyon::ID)> onDrop;
        /// Tree reorder; when set, rows become drop targets with sibling and child zones.
        std::function<void(uintptr_t srcPayload, tachyon::ID targetID,
                           TreeDropPosition pos)> onReorder;
        /// Every drop on a row means InsideLast (folder-style targets, no sibling reordering).
        bool dropInsideOnly = false;
        tachyon::DragVisualizer visualizer;                                 ///< optional visual feedback
    };

    /// Click-gesture tracker, owned per widget instance so instances (and tests) don't share it.
    struct DoubleClickTracker
    {
        tachyon::ID lastID = tachyon::ID::Invalid();  ///< item of the previous click
        int64_t lastTimeNanos = 0;   ///< steady-clock ns; see SteadyClockNowNanos()

        /// Whether this click hit the same item as the previous one, and the time since it.
        struct Result { bool sameAsPrevious; float elapsedMs; };

        /// Records a click on `id` and compares it with the previous click.
        Result Update(tachyon::ID id)
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

        /// Forgets the previous click, so the next one never counts as a repeat.
        void Reset() { lastID = tachyon::ID::Invalid(); }
    };
    /// @}
}
