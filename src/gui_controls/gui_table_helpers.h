#pragma once

#include <gui_core/gui_base_types.h>

#include <span>

namespace tachyon
{
    struct LayoutStyle;
    struct VisualTextStyle;
}

namespace tachyon::TableHelpers
{
    /// @addtogroup controls_data
    /// @{

    /// Half-open [firstVisible, lastVisible) range of rows to emit.
    struct VirtualRowRange
    {
        int firstVisible = 0;
        int lastVisible  = 0;
    };

    /// The rows a virtualized list of `totalRowCount` fixed-height rows emits, plus
    /// `bufferRows` of over-draw on each side; all lengths in pixels. A 0 `viewportH`
    /// (first frame) still yields a small slice so layout can measure. Empty when
    /// `rowHeight` <= 0.
    VirtualRowRange ComputeVirtualRowRange(int32_t scrollY,
                                           int32_t viewportH,
                                           int32_t rowHeight,
                                           int     totalRowCount,
                                           int     bufferRows);

    /// Drop column for a column drag: the dragged column passes a neighbour once the
    /// mouse crosses that neighbour's far edge. `headerRects` are in display order.
    int ComputeColumnInsertionIndex(int32_t                  mouseX,
                                    int                      fromCol,
                                    std::span<const IntRect> headerRects);

    /// @}
}
