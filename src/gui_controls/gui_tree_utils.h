#pragma once

// Flat-list tree helpers shared by VirtualTreeView / tree-table views: rows
// are a flat vector in depth-first order, each row carrying its depth.
// RebuildTreeIndex derives skipCount / ancestorLineMask / isLastSibling;
// ReorderTreeRow moves a subtree (with reparenting) for drag-drop.

#include "gui_application.h"

#include <concepts>
#include <iterator>
#include <stdint.h>
#include <vector>

namespace interstice::TreeUtils
{
    template <typename Row>
    concept TreeRowLike = requires(Row r)
    {
        { r.depth }            -> std::convertible_to<int>;
        { r.skipCount }        -> std::convertible_to<int>;
        { r.isOpen }           -> std::convertible_to<bool>;
        { r.ancestorLineMask } -> std::convertible_to<uint32_t>;
        { r.isLastSibling }    -> std::convertible_to<bool>;
    };

    // Recompute derived index fields (skipCount, ancestorLineMask,
    // isLastSibling) from each row's depth. Call once after structural
    // edits; toggling isOpen does not need a rebuild. O(n^2) worst case;
    // fine for the hundreds-of-rows datasets these views target.
    template <TreeRowLike Row>
    void RebuildTreeIndex(std::vector<Row>& rows)
    {
        const int n = static_cast<int>(rows.size());
        for (int i = 0; i < n; i++)
        {
            const int D = rows[i].depth;

            int j = i + 1;
            while (j < n && rows[j].depth > D) j++;
            rows[i].skipCount = j - i - 1;

            uint32_t mask = 0;
            for (int d = 0; d <= D - 2 && d < 32; d++) // the mask is 32 bits wide
            {
                const int target = d + 1;
                for (int k = i + 1; k < n; k++)
                {
                    if (rows[k].depth < target) break;
                    if (rows[k].depth == target) { mask |= (1u << d); break; }
                }
            }
            rows[i].ancestorLineMask = mask;

            bool last = true;
            for (int k = i + 1; k < n; k++)
            {
                if (rows[k].depth < D) break;
                if (rows[k].depth == D) { last = false; break; }
            }
            rows[i].isLastSibling = last;
        }
    }

    // Move the subtree rooted at srcDs to a position determined by
    // (dstDs, pos). Reparents (depth shifts) when the target depth differs.
    // Rejects self-drop and drops into the source's own descendants. Caller
    // must invoke RebuildTreeIndex afterwards. Returns the moved row's new
    // flat index, or -1 on rejection.
    template <TreeRowLike Row>
    int ReorderTreeRow(std::vector<Row>& rows, int srcDs, int dstDs,
                       interstice::TreeDropPosition pos)
    {
        const int n = static_cast<int>(rows.size());
        if (srcDs < 0 || srcDs >= n) return -1;
        if (dstDs < 0 || dstDs >= n) return -1;
        if (srcDs == dstDs) return -1;

        const int srcStart      = srcDs;
        const int srcSliceCount = 1 + rows[srcStart].skipCount;
        const int srcEnd        = srcStart + srcSliceCount;

        // Cycle guard: dropping into the source's own subtree is invalid.
        if (dstDs >= srcStart && dstDs < srcEnd) return -1;

        int targetBaseDepth = rows[dstDs].depth;
        int insertAt        = dstDs;
        switch (pos)
        {
            case interstice::TreeDropPosition::Before:
                insertAt        = dstDs;
                targetBaseDepth = rows[dstDs].depth;
                break;
            case interstice::TreeDropPosition::Inside:
                // First child: splice in right after the parent row.
                insertAt        = dstDs + 1;
                targetBaseDepth = rows[dstDs].depth + 1;
                rows[dstDs].isOpen = true;
                break;
            case interstice::TreeDropPosition::InsideLast:
                // Last child: splice in past the parent's whole subtree.
                insertAt        = dstDs + 1 + rows[dstDs].skipCount;
                targetBaseDepth = rows[dstDs].depth + 1;
                rows[dstDs].isOpen = true;
                break;
            case interstice::TreeDropPosition::After:
                insertAt        = dstDs + 1 + rows[dstDs].skipCount;
                targetBaseDepth = rows[dstDs].depth;
                break;
            default: return -1;
        }

        // Lift the source slice into a temp, adjust depth on each lifted row.
        const int srcBaseDepth = rows[srcStart].depth;
        const int deltaDepth   = targetBaseDepth - srcBaseDepth;
        std::vector<Row> lifted;
        lifted.reserve(srcSliceCount);
        lifted.insert(lifted.end(),
                      std::make_move_iterator(rows.begin() + srcStart),
                      std::make_move_iterator(rows.begin() + srcEnd));
        if (deltaDepth != 0)
            for (auto& r : lifted) r.depth += deltaDepth;

        rows.erase(rows.begin() + srcStart, rows.begin() + srcEnd);
        if (insertAt > srcStart) insertAt -= srcSliceCount;
        rows.insert(rows.begin() + insertAt,
                    std::make_move_iterator(lifted.begin()),
                    std::make_move_iterator(lifted.end()));
        return insertAt;   // new flat index of the moved row's root
    }
}
