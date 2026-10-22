#include <gui_controls/gui_table_helpers.h>

#include <gui_core/style/gui_style.h>

#include <algorithm>
#include <cmath>

namespace GUI::TableHelpers
{
    VirtualRowRange ComputeVirtualRowRange(int32_t scrollY,
                                           int32_t viewportH,
                                           int32_t rowHeight,
                                           int     totalRowCount,
                                           int     bufferRows)
    {
        VirtualRowRange range{};
        if (totalRowCount <= 0 || rowHeight <= 0)
            return range;

        const int32_t safeRowHeight = std::max<int32_t>(1, rowHeight);
        int first = std::max(0, scrollY / safeRowHeight - bufferRows);
        int last  = std::min(totalRowCount,
                             (scrollY + viewportH) / safeRowHeight + 1 + bufferRows);
        first = std::clamp(first, 0, totalRowCount);
        last  = std::clamp(last,  first, totalRowCount);
        range.firstVisible = first;
        range.lastVisible  = last;
        return range;
    }

    int ComputeColumnInsertionIndex(int32_t                  mouseX,
                                    int                      fromCol,
                                    std::span<const IntRect> headerRects)
    {
        const int count = static_cast<int>(headerRects.size());
        if (count <= 0) return 0;
        if (fromCol < 0 || fromCol >= count) return std::clamp(fromCol, 0, count - 1);

        int target = fromCol;
        // Mouse to the right of the dragged column: step past each right-of
        // column once the mouse has crossed its LEFT edge.
        for (int i = fromCol + 1; i < count; i++)
        {
            const int leftEdge = headerRects[i].x;
            if (mouseX >= leftEdge) target = i;
            else break;
        }
        // Mouse to the left of the dragged column: step past each left-of
        // column once the mouse has crossed its RIGHT edge.
        for (int i = fromCol - 1; i >= 0; i--)
        {
            const int rightEdge = headerRects[i].x + headerRects[i].w;
            if (mouseX < rightEdge) target = i;
            else break;
        }
        return std::clamp(target, 0, count - 1);
    }

}
