#pragma once

#include <stdint.h>
#include <span>
#include <vector>

namespace tachyon
{
    struct int2;
    struct IntRect;
    struct ElementRect;
    struct GridProperties;
    struct GridPlacement;
    struct GridTrack;
    struct GridLayout;

    struct LayoutGrid
    {
        /// Returns the content width required by the grid.
        static int32_t HorizontalSizing(const ElementRect&           parentRect,
                                        std::span<const ElementRect> children);

        /// Returns the content height required by the grid.
        static int32_t VerticalSizing(const ElementRect&           parentRect,
                                      std::span<const ElementRect> children);

        static void HorizontalPositioning(const ElementRect&     parentRect,
                                          std::span<ElementRect> childRects);

        /// Returns the content bounds.
        static IntRect VerticalPositioning(const ElementRect&     parentRect,
                                           const IntRect&         clipRect,
                                           std::span<ElementRect> childRects);

      private:
        struct GridLayout
        {
            std::vector<int32_t> columnSizes;
            std::vector<int32_t> rowSizes;
            std::vector<int32_t> columnOffsets;
            std::vector<int32_t> rowOffsets;
            size_t effectiveColumnCount = 0;
            size_t effectiveRowCount = 0;
        };

        /// Grid cell per child.
        struct GridPlacement
        {
            std::vector<int2> cellPositions; ///< (col, row)
            size_t effectiveColumnCount = 0;
            size_t effectiveRowCount = 0;
        };
        
        static void BuildTrackDefinitions(std::span<const ElementRect> childRects,
                                          const GridPlacement& placement,
                                          std::vector<GridTrack>& outColumnTracks,
                                          std::vector<GridTrack>& outRowTracks);

        /// Stage 1: auto-placement and effective grid dimensions
        static void DeterminePlacement(std::span<const ElementRect> childRects,
                                       GridPlacement& outPlacement);

        /// Stage 2: intrinsic (max-content) track sizes
        static void ComputeIntrinsicTrackSizes(std::span<const ElementRect> childRects,
                                               const GridPlacement& placement,
                                               std::vector<int32_t>& outColumnIntrinsics,
                                               std::vector<int32_t>& outRowIntrinsics);

        /// Stage 3: final track sizes and offsets
        static void ResolveFinalLayout(const GridProperties& grid,
                                      std::span<const ElementRect> childRects,
                                      const GridPlacement& placement,
                                      const std::span<int32_t> intrinsicColumnSizes,
                                      const std::span<int32_t> intrinsicRowSizes,
                                      int32_t availableWidth,
                                      int32_t availableHeight,
                                      GridLayout& outLayout);

        static void ResolveTrackSizes(std::span<const GridTrack> tracks,
                                      std::span<const int32_t> intrinsicSizes,
                                      int32_t availableSpace,
                                      std::span<int32_t> outSizes);

        static void ComputeOffsets(std::span<const int32_t> sizes, int16_t gap,
                                   std::span<int32_t> outOffsets);

        /// outPlacement lets callers reuse the placement.
        static void ComputeGridLayout(const GridProperties& grid,
                                     std::span<const ElementRect> childRects,
                                     int32_t availableWidth,
                                     int32_t availableHeight,
                                     GridLayout& outLayout,
                                     GridPlacement* outPlacement = nullptr);
    };

} // namespace tachyon