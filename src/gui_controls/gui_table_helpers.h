#pragma once

#include <gui_core/gui_base_types.h>

#include <span>

namespace interstice
{
    struct LayoutStyle;
    struct VisualTextStyle;
}

namespace interstice::TableHelpers
{
    // Half-open [firstVisible, lastVisible) rows to emit, plus bufferRows of over-draw.
    // A 0 viewportH (first frame) still yields a small slice so layout can measure.
    struct VirtualRowRange
    {
        int firstVisible = 0;
        int lastVisible  = 0;
    };

    VirtualRowRange ComputeVirtualRowRange(int32_t scrollY,
                                           int32_t viewportH,
                                           int32_t rowHeight,
                                           int     totalRowCount,
                                           int     bufferRows);

    // Drop column for a column drag: the dragged column passes a neighbour once the
    // mouse crosses that neighbour's far edge. headerRects in display order.
    int ComputeColumnInsertionIndex(int32_t                  mouseX,
                                    int                      fromCol,
                                    std::span<const IntRect> headerRects);

}
