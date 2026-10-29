#include <gui_core/layout/gui_layout_grid.h>
#include <gui_core/layout/gui_layout_helpers.h>

#include <gui_core/diagnostics/gui_profiler.h>

#include <algorithm>
#include <assert.h>
#include <numeric>

namespace tachyon
{
    // Stage 1: Determine placement and effective grid dimensions
    void LayoutGrid::DeterminePlacement(std::span<const ElementRect> childRects,
                                        GridPlacement&               outPlacement)
    {
        if (childRects.empty())
        {
            outPlacement = {};
            return;
        }

        // First pass: find explicit bounds and required minimum span dimensions.
        int32_t maxExplicitCol = 0;
        int32_t maxExplicitRow = 0;
        int32_t maxColumnSpan = 1;
        int32_t maxRowSpan = 1;

        for (const auto& childElem : childRects)
        {
            if (!childElem.constraints.IsInFlow())
                continue;

            const GridProperties grid = childElem.constraints.GetGridItem(childElem.layoutStyle);
            maxColumnSpan = std::max(maxColumnSpan, std::max(1, static_cast<int32_t>(grid.column.span)));
            maxRowSpan = std::max(maxRowSpan, std::max(1, static_cast<int32_t>(grid.row.span)));

            if (grid.column.start >= 0)
            {
                maxExplicitCol = std::max(maxExplicitCol,
                    grid.column.start + grid.column.span);
            }
            if (grid.row.start >= 0)
            {
                maxExplicitRow = std::max(maxExplicitRow,
                    grid.row.start + grid.row.span);
            }
        }

        // Initialize grid dimensions.
        // Auto-placed items with spans must still have enough tracks to fit.
        size_t effectiveCols = std::max<size_t>(1, std::max(maxExplicitCol, maxColumnSpan));
        size_t effectiveRows = std::max<size_t>(1, std::max(maxExplicitRow, maxRowSpan));

        // Account for auto-placed items.
        size_t explicitlyPlacedCount = 0;
        size_t layoutedCount = 0;
        for (const auto& childElem : childRects)
        {
            if (!childElem.constraints.IsInFlow())
                continue;
            
            layoutedCount++;
            const GridProperties grid = childElem.constraints.GetGridItem(childElem.layoutStyle);
            if (grid.column.start >= 0 && grid.row.start >= 0)
            {
                explicitlyPlacedCount++;
            }
        }

        const size_t autoPlacedCount = layoutedCount - explicitlyPlacedCount;
        if (autoPlacedCount > 0)
        {
            const size_t minCellsNeeded = explicitlyPlacedCount + autoPlacedCount;
            const size_t currentCells = effectiveCols * effectiveRows;

            if (currentCells < minCellsNeeded)
            {
                // Expand rows to accommodate; keep inferred column count stable.
                effectiveRows = (minCellsNeeded + effectiveCols - 1) / effectiveCols;
            }
        }

        outPlacement.effectiveColumnCount = effectiveCols;
        outPlacement.effectiveRowCount = effectiveRows;
        outPlacement.cellPositions.resize(childRects.size());

        // Second pass: place items.
        std::vector<bool> occupied(effectiveCols * effectiveRows, false);

        // Place explicitly positioned items first.
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            if (grid.column.start >= 0 && grid.row.start >= 0)
            {
                outPlacement.cellPositions[i] = int2{ grid.column.start, grid.row.start };

                // Mark cells as occupied (handle spans).
                for (int32_t r = 0; r < grid.row.span; ++r)
                {
                    for (int32_t c = 0; c < grid.column.span; ++c)
                    {
                        const int32_t col = grid.column.start + c;
                        const int32_t row = grid.row.start + r;

                        while (col >= static_cast<int32_t>(effectiveCols))
                            effectiveCols++;
                        while (row >= static_cast<int32_t>(effectiveRows))
                            effectiveRows++;

                        if (occupied.size() < effectiveCols * effectiveRows)
                            occupied.resize(effectiveCols * effectiveRows, false);

                        if (col < static_cast<int32_t>(effectiveCols) && row < static_cast<int32_t>(effectiveRows))
                            occupied[row * effectiveCols + col] = true;
                    }
                }
            }
        }

        // Auto-place remaining items (dense packing).
        size_t nextCol = 0;
        size_t nextRow = 0;

        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            if (grid.column.start < 0 || grid.row.start < 0)
            {
                bool placed = false;

                while (nextRow < effectiveRows && !placed)
                {
                    while (nextCol < effectiveCols && !placed)
                    {
                        if (!occupied[nextRow * effectiveCols + nextCol])
                        {
                            outPlacement.cellPositions[i] = int2{
                                static_cast<int32_t>(nextCol),
                                static_cast<int32_t>(nextRow)
                            };

                            for (int32_t r = 0; r < grid.row.span; ++r)
                            {
                                for (int32_t c = 0; c < grid.column.span; ++c)
                                {
                                    const int32_t col = static_cast<int32_t>(nextCol) + c;
                                    const int32_t row = static_cast<int32_t>(nextRow) + r;

                                    if (col < static_cast<int32_t>(effectiveCols) && row < static_cast<int32_t>(effectiveRows))
                                        occupied[row * effectiveCols + col] = true;
                                }
                            }

                            placed = true;
                        }
                        if (!placed) nextCol++;
                    }
                    if (!placed)
                    {
                        nextCol = 0;
                        nextRow++;
                    }
                }

                if (!placed)
                {
                    effectiveRows++;
                    occupied.resize(effectiveCols * effectiveRows, false);

                    outPlacement.cellPositions[i] = int2{
                        static_cast<int32_t>(nextCol),
                        static_cast<int32_t>(nextRow)
                    };
                    occupied[nextRow * effectiveCols + nextCol] = true;
                }
            }
        }

        outPlacement.effectiveColumnCount = effectiveCols;
        outPlacement.effectiveRowCount = effectiveRows;
    }

    void LayoutGrid::BuildTrackDefinitions(std::span<const ElementRect> childRects,
                                           const GridPlacement&         placement,
                                           std::vector<GridTrack>&      outColumnTracks,
                                           std::vector<GridTrack>&      outRowTracks)
    {
        outColumnTracks.assign(placement.effectiveColumnCount, GridTrack::Auto());
        outRowTracks.assign(placement.effectiveRowCount, GridTrack::Auto());
        
        // Children define tracks; Fixed > Fractional > Auto.
        auto trackPriority = [](GridTrack::Type type) -> int {
            switch (type)
            {
                case GridTrack::Type::Fixed: return 3;
                case GridTrack::Type::Fractional: return 2;
                case GridTrack::Type::Auto: return 1;
                default: return 0;
            }
        };
        
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const int2 cell = placement.cellPositions[i];
            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);
            
            // Column track
            if (cell.x >= 0 && cell.x < static_cast<int32_t>(placement.effectiveColumnCount))
            {
                GridTrack& existingCol = outColumnTracks[cell.x];
                const GridTrack& childCol = grid.column.track;
                
                if (trackPriority(childCol.type) > trackPriority(existingCol.type))
                {
                    existingCol = childCol;
                }
                else if (childCol.type == existingCol.type)
                {
                    // Same type - use larger value
                    if (childCol.type == GridTrack::Type::Fixed)
                    {
                        existingCol.value = std::max(existingCol.value, childCol.value);
                    }
                    else if (childCol.type == GridTrack::Type::Fractional)
                    {
                        existingCol.value = std::max(existingCol.value, childCol.value);
                    }
                }
            }
            
            // Row track
            if (cell.y >= 0 && cell.y < static_cast<int32_t>(placement.effectiveRowCount))
            {
                GridTrack& existingRow = outRowTracks[cell.y];
                const GridTrack& childRow = grid.row.track;
                
                if (trackPriority(childRow.type) > trackPriority(existingRow.type))
                {
                    existingRow = childRow;
                }
                else if (childRow.type == existingRow.type)
                {
                    if (childRow.type == GridTrack::Type::Fixed)
                    {
                        existingRow.value = std::max(existingRow.value, childRow.value);
                    }
                    else if (childRow.type == GridTrack::Type::Fractional)
                    {
                        existingRow.value = std::max(existingRow.value, childRow.value);
                    }
                }
            }
        }
    }

    // Stage 2: Compute intrinsic track sizes
    void LayoutGrid::ComputeIntrinsicTrackSizes(std::span<const ElementRect> childRects,
                                                const GridPlacement&         placement,
                                                std::vector<int32_t>&        outColumnIntrinsics,
                                                std::vector<int32_t>&        outRowIntrinsics)
    {
        outColumnIntrinsics.assign(placement.effectiveColumnCount, 0);
        outRowIntrinsics.assign(placement.effectiveRowCount, 0);

        // First pass: single-span items directly define intrinsic track sizes.
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const int2 cell = placement.cellPositions[i];
            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            if (grid.column.span == 1 &&
                cell.x >= 0 && cell.x < static_cast<int32_t>(outColumnIntrinsics.size()))
            {
                outColumnIntrinsics[cell.x] = std::max(outColumnIntrinsics[cell.x], childRects[i].rect.w);
            }

            if (grid.row.span == 1 &&
                cell.y >= 0 && cell.y < static_cast<int32_t>(outRowIntrinsics.size()))
            {
                outRowIntrinsics[cell.y] = std::max(outRowIntrinsics[cell.y], childRects[i].rect.h);
            }
        }

        // Second pass: multi-span items must fit across the already-established track sizes.
        // If current sum is too small, distribute the deficit evenly across spanned tracks.
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const int2 cell = placement.cellPositions[i];
            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            const int32_t colStart = std::max(0, cell.x);
            const int32_t rowStart = std::max(0, cell.y);
            const int32_t colEnd = std::min<int32_t>(
                static_cast<int32_t>(outColumnIntrinsics.size()),
                colStart + std::max<int32_t>(1, grid.column.span));
            const int32_t rowEnd = std::min<int32_t>(
                static_cast<int32_t>(outRowIntrinsics.size()),
                rowStart + std::max<int32_t>(1, grid.row.span));

            if (colStart < colEnd && grid.column.span > 1)
            {
                int32_t current = 0;
                for (int32_t c = colStart; c < colEnd; ++c)
                    current += outColumnIntrinsics[c];

                const int32_t needed = childRects[i].rect.w;
                if (needed > current)
                {
                    const int32_t deficit = needed - current;
                    const int32_t count = colEnd - colStart;
                    const int32_t addEach = deficit / count;
                    int32_t remainder = deficit - (addEach * count);
                    for (int32_t c = colStart; c < colEnd; ++c)
                    {
                        outColumnIntrinsics[c] += addEach;
                        if (remainder > 0)
                        {
                            outColumnIntrinsics[c] += 1;
                            remainder--;
                        }
                    }
                }
            }

            if (rowStart < rowEnd && grid.row.span > 1)
            {
                int32_t current = 0;
                for (int32_t r = rowStart; r < rowEnd; ++r)
                    current += outRowIntrinsics[r];

                const int32_t needed = childRects[i].rect.h;
                if (needed > current)
                {
                    const int32_t deficit = needed - current;
                    const int32_t count = rowEnd - rowStart;
                    const int32_t addEach = deficit / count;
                    int32_t remainder = deficit - (addEach * count);
                    for (int32_t r = rowStart; r < rowEnd; ++r)
                    {
                        outRowIntrinsics[r] += addEach;
                        if (remainder > 0)
                        {
                            outRowIntrinsics[r] += 1;
                            remainder--;
                        }
                    }
                }
            }
        }
    }

    // Stage 3: Resolve final sizes and offsets
    void LayoutGrid::ResolveFinalLayout(const GridProperties&        gridLayout,
                                        std::span<const ElementRect> childRects,
                                        const GridPlacement&         placement,
                                        const std::span<int32_t>     intrinsicColumnSizes,
                                        const std::span<int32_t>     intrinsicRowSizes,
                                        int32_t                      availableWidth,
                                        int32_t                      availableHeight,
                                        GridLayout&                  outLayout)
    {
        outLayout.effectiveColumnCount = intrinsicColumnSizes.size();
        outLayout.effectiveRowCount = intrinsicRowSizes.size();

        outLayout.columnSizes.resize(outLayout.effectiveColumnCount);
        outLayout.rowSizes.resize(outLayout.effectiveRowCount);

        std::vector<GridTrack> columnTracks, rowTracks;
        BuildTrackDefinitions(childRects, placement, columnTracks, rowTracks);

        // Available space is the content box; gaps are added later by ComputeOffsets.
        const int32_t totalColumnGaps =
            static_cast<int32_t>(std::max<size_t>(1, outLayout.effectiveColumnCount) - 1) * gridLayout.column.gap;
        const int32_t totalRowGaps =
            static_cast<int32_t>(std::max<size_t>(1, outLayout.effectiveRowCount) - 1) * gridLayout.row.gap;

        const int32_t availableTrackWidth = (availableWidth == INT32_MAX)
            ? INT32_MAX
            : std::max(0, availableWidth - totalColumnGaps);
        const int32_t availableTrackHeight = (availableHeight == INT32_MAX)
            ? INT32_MAX
            : std::max(0, availableHeight - totalRowGaps);

        ResolveTrackSizes(columnTracks, intrinsicColumnSizes, availableTrackWidth,
                         outLayout.columnSizes);
        ResolveTrackSizes(rowTracks, intrinsicRowSizes, availableTrackHeight,
                         outLayout.rowSizes);

        outLayout.columnOffsets.resize(outLayout.effectiveColumnCount + 1);
        outLayout.rowOffsets.resize(outLayout.effectiveRowCount + 1);

        ComputeOffsets(outLayout.columnSizes, gridLayout.column.gap, outLayout.columnOffsets);
        ComputeOffsets(outLayout.rowSizes, gridLayout.row.gap, outLayout.rowOffsets);
    }

    void LayoutGrid::ComputeGridLayout(const GridProperties&        gridLayout,
                                       std::span<const ElementRect> childRects,
                                       int32_t                      availableWidth,
                                       int32_t                      availableHeight,
                                       GridLayout&                  outLayout,
                                       GridPlacement*               outPlacement)
    {
        GridPlacement localPlacement;
        GridPlacement& placement = outPlacement ? *outPlacement : localPlacement;
        DeterminePlacement(childRects, placement);

        std::vector<int32_t> intrinsicCols, intrinsicRows;
        ComputeIntrinsicTrackSizes(childRects, placement, intrinsicCols, intrinsicRows);

        ResolveFinalLayout(gridLayout, childRects, placement, intrinsicCols, intrinsicRows,
                          availableWidth, availableHeight, outLayout);
    }

    void LayoutGrid::ResolveTrackSizes(std::span<const GridTrack> tracks,
                                       std::span<const int32_t> intrinsicSizes,
                                       int32_t availableSpace,
                                       std::span<int32_t> outSizes)
    {
        assert(outSizes.size() == intrinsicSizes.size());
        assert(tracks.size() == intrinsicSizes.size());

        const size_t trackCount = outSizes.size();
        const bool hasBoundedAvailableSpace = (availableSpace != INT32_MAX);

        int64_t fixedTotal = 0;
        double  frTotal = 0.0;
        int32_t frCount = 0;
        // A track's share of free space (0.5fr = half of 1fr); non-positive counts as 1fr.
        auto FrWeight = [](const GridTrack& track) { return track.value > 0.0f ? double(track.value) : 1.0; };

        for (size_t i = 0; i < trackCount; ++i)
        {
            const GridTrack& track = tracks[i];

            switch (track.type)
            {
                case GridTrack::Type::Fixed:
                {
                    outSizes[i] = static_cast<int32_t>(track.value);
                    fixedTotal += outSizes[i];
                    break;
                }
                case GridTrack::Type::Fractional:
                {
                    // minmax(max(minPx, auto), Nfr): floor here, free space added below.
                    const int32_t floorPx = std::max<int32_t>(
                        static_cast<int32_t>(track.minPx), intrinsicSizes[i]);
                    if (hasBoundedAvailableSpace)
                    {
                        outSizes[i] = floorPx;
                        fixedTotal += outSizes[i];
                        frTotal += FrWeight(track);
                        ++frCount;
                    }
                    else
                    {
                        // Unbounded (intrinsic) pass: fr tracks don't expand.
                        outSizes[i] = floorPx;
                        fixedTotal += outSizes[i];
                    }
                    break;
                }
                case GridTrack::Type::Auto:
                default:
                {
                    outSizes[i] = intrinsicSizes[i];
                    fixedTotal += intrinsicSizes[i];
                    break;
                }
            }
        }

        if (hasBoundedAvailableSpace && frCount > 0 && availableSpace > fixedTotal)
        {
            const int64_t freeSpace = static_cast<int64_t>(availableSpace) - fixedTotal;
            int64_t distributed = 0;

            for (size_t i = 0; i < trackCount; ++i)
            {
                const GridTrack& track = tracks[i];
                if (track.type == GridTrack::Type::Fractional)
                {
                    const int64_t size64 = static_cast<int64_t>(static_cast<double>(freeSpace) * FrWeight(track) / frTotal);
                    const int32_t extra = static_cast<int32_t>(std::clamp<int64_t>(size64, 0, INT32_MAX));
                    // On top of the floor.
                    outSizes[i] += extra;
                    distributed += extra;
                }
            }

            // Distribute remainder to first Fractional track.
            const int64_t remainder = freeSpace - distributed;
            if (remainder > 0)
            {
                for (size_t i = 0; i < trackCount; ++i)
                {
                    const GridTrack& track = tracks[i];
                    if (track.type == GridTrack::Type::Fractional)
                    {
                        const int64_t boosted = static_cast<int64_t>(outSizes[i]) + remainder;
                        outSizes[i] = static_cast<int32_t>(std::min<int64_t>(boosted, INT32_MAX));
                        break;
                    }
                }
            }
        }
        else if (hasBoundedAvailableSpace && frCount == 0 && availableSpace > fixedTotal)
        {
            int32_t freeSpace = static_cast<int32_t>(static_cast<int64_t>(availableSpace) - fixedTotal);
            size_t autoZeroCount = 0;

            for (size_t i = 0; i < trackCount; ++i)
            {
                const GridTrack& track = tracks[i];
                if (track.type == GridTrack::Type::Auto && intrinsicSizes[i] == 0)
                    autoZeroCount++;
            }

            if (autoZeroCount > 0)
            {
                int32_t perTrack = freeSpace / static_cast<int32_t>(autoZeroCount);
                int32_t remainder = freeSpace - (perTrack * static_cast<int32_t>(autoZeroCount));

                for (size_t i = 0; i < trackCount; ++i)
                {
                    const GridTrack& track = tracks[i];
                    if (track.type == GridTrack::Type::Auto && intrinsicSizes[i] == 0)
                    {
                        outSizes[i] += perTrack;
                        if (remainder > 0)
                        {
                            outSizes[i] += 1;
                            remainder--;
                        }
                    }
                }
            }
        }
    }

    void LayoutGrid::ComputeOffsets(std::span<const int32_t> sizes,
                                    int16_t gap,
                                    std::span<int32_t> outOffsets)
    {
        assert(outOffsets.size() == sizes.size() + 1);
        outOffsets[0] = 0;
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            outOffsets[i + 1] = outOffsets[i] + sizes[i] + 
                               ((i + 1 < sizes.size()) ? gap : 0);
        }
    }

    int32_t LayoutGrid::HorizontalSizing(const ElementRect&           parentRect,
                                         std::span<const ElementRect> childRects)
    {
        if (childRects.empty())
            return 0;

        //TACHYON_PROFILE_SCOPE("LayoutGrid::HorizontalSizing");
        const GridProperties& gridLayout = parentRect.layoutStyle->layoutData.grid;

        GridLayout layout {};
        ComputeGridLayout(gridLayout, childRects, INT32_MAX, INT32_MAX, layout);

        return layout.columnOffsets.empty() ? 0 : layout.columnOffsets.back();
    }

    int32_t LayoutGrid::VerticalSizing(const ElementRect&           parentRect,
                                       std::span<const ElementRect> childRects)
    {
        if (childRects.empty())
            return 0;

        //TACHYON_PROFILE_SCOPE("LayoutGrid::VerticalSizing");
        const GridProperties& gridLayout = parentRect.layoutStyle->layoutData.grid;

        GridLayout layout{};
        ComputeGridLayout(gridLayout, childRects, INT32_MAX, INT32_MAX, layout);

        return layout.rowOffsets.empty() ? 0 : layout.rowOffsets.back();
    }

    void LayoutGrid::HorizontalPositioning(const ElementRect&      parentRect,
                                           std::span<ElementRect>  childRects)
    {
        if (childRects.empty())
            return;

        //TACHYON_PROFILE_SCOPE("LayoutGrid::HorizontalPositioning");
        const GridProperties& gridLayout = parentRect.layoutStyle->layoutData.grid;
        const IntRect& areaRect = parentRect.innerRect;

        GridLayout layout{};
        GridPlacement placement{};
        ComputeGridLayout(gridLayout, childRects, areaRect.w, areaRect.h, layout, &placement);

        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const int2 cell = placement.cellPositions[i];
            if (cell.x >= static_cast<int32_t>(layout.effectiveColumnCount))
                continue;

            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            // Calculate cell bounds accounting for spans.
            int32_t cellX = areaRect.x + layout.columnOffsets[cell.x];
            const size_t clampedEnd = std::min<size_t>(cell.x + grid.column.span,
                                                       layout.effectiveColumnCount);
            int32_t cellEndX = areaRect.x + layout.columnOffsets[clampedEnd];

            // Offsets include a gap after each non-last track. Remove the trailing gap from this span.
            if (clampedEnd < layout.effectiveColumnCount)
                cellEndX -= gridLayout.column.gap;

            int32_t cellW = std::max(0, cellEndX - cellX);

            if (gridLayout.alignItemsSelf == AlignItems::Stretch)
            {
                // Stretch fills the cell only for an auto width; a set width starts the cell.
                childRects[i].rect.x = cellX;
                if (!childRects[i].constraints.HasFixedWidth())
                    childRects[i].rect.w = cellW;
            } else
            {
                IntRect cellRect{ cellX, 0, cellW, 0 };
                LayoutHelpers::JustifyChildren<0, false>(
                    childRects.subspan(i, 1),
                    gridLayout.justifyItems, 0, cellRect, childRects[i].rect.w, 1);
            }
        }
    }

    IntRect LayoutGrid::VerticalPositioning(const ElementRect&     parentRect,
                                            const IntRect&         clipRect,
                                            std::span<ElementRect> childRects)
    {
        if (childRects.empty())
        {
            return {};
        }

        //TACHYON_PROFILE_SCOPE("LayoutGrid::VerticalPositioning");
        const GridProperties& gridLayout = parentRect.layoutStyle->layoutData.grid;
        const IntRect& areaRect = parentRect.innerRect;

        GridLayout layout{};
        GridPlacement placement;
        ComputeGridLayout(gridLayout, childRects, areaRect.w, areaRect.h, layout, &placement);

        IntRect contentRect {};

        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            childRects[i].clipRect = clipRect;
            const int2 cell = placement.cellPositions[i];
            if (cell.y >= static_cast<int32_t>(layout.effectiveRowCount))
                continue;

            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            // Calculate cell bounds accounting for spans.
            int32_t cellY = areaRect.y + layout.rowOffsets[cell.y];
            const size_t clampedEnd = std::min<size_t>(cell.y + grid.row.span,
                                                       layout.effectiveRowCount);
            int32_t cellEndY = areaRect.y + layout.rowOffsets[clampedEnd];

            // Offsets include a gap after each non-last track. Remove the trailing gap from this span.
            if (clampedEnd < layout.effectiveRowCount)
                cellEndY -= gridLayout.row.gap;

            int32_t cellH = std::max(0, cellEndY - cellY);

            IntRect cellRect{ 0, cellY, 0, cellH };

            LayoutHelpers::Align<1>(gridLayout.alignItems, cellRect, childRects[i]);

            // Bounds start at the content box's top-left (the empty rect they grow from): what a
            // scroll area can scroll over begins there, wherever the content starts.
            contentRect = IntRect::Union(contentRect, childRects[i].rect - areaRect.position);
        }

        return contentRect;
    }

} // namespace tachyon