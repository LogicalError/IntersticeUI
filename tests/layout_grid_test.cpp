#include <gtest/gtest.h>
#include <gmock/gmock.h>


#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>

#include <gui_core/layout/gui_layout_engine.h>

#include <gui_core/windows/gui_window_backend.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/state/gui_scroll_management.h>

#include "gui_text_system_stub.h"
#include "mock_window_backend.h"

namespace interstice
{
    using namespace testing;
    using ::testing::Return;
    using ::testing::_;
    using ::testing::NiceMock;
    using GUITest::MockWindowBackend;

    class LayoutGridTest : public ::testing::Test
    {
    protected:
        GradientManager gradientManager{ 256 };
        StyleManager styleManager{ gradientManager };
        StubTextSystem textSystem;
        ScrollManagement scrollManagement;
        LayoutEngine layoutEngine{ textSystem,scrollManagement };
        ElementStorage storage{ styleManager };
        StyleRef defaultStyleIndex;

        MockWindowBackend* mockBackend;
        std::unique_ptr<OSWindowManager> osWindowManager;

        void SetUp() override
        {
            mockBackend = new NiceMock<MockWindowBackend>();
            osWindowManager = std::make_unique<OSWindowManager>(nullptr, mockBackend);

            Style baseStyle;
            baseStyle.display = Display::Grid; // Set root to Grid
            defaultStyleIndex = styleManager.Register(baseStyle);
        }
    };

    // Test 1: Verify explicit grid placement (rowStart/columnStart)
    TEST_F(LayoutGridTest, ExplicitItemPlacement)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle;
        childStyle.layoutData.grid.column.start = 1;
        childStyle.layoutData.grid.row.start = 1;
        // Tell the grid that the tracks this child inhabits should be 100px Fixed
        childStyle.layoutData.grid.column.track = GridTrack::Fixed(100.0f);
        childStyle.layoutData.grid.row.track = GridTrack::Fixed(100.0f);
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        // 200x200 Grid with two 100px tracks
        [[maybe_unused]] ElementRef parentRef = storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 200)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        // Child should occupy the bottom-right quadrant
        ElementRef childRef = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(100, 100)).ref;

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& child = storage.GetElementRectUnsafe(childRef);
        EXPECT_EQ(child.rect.x, 100);
        EXPECT_EQ(child.rect.y, 100);
    }

    // Test 2: Verify that grid gaps correctly offset child positions
    TEST_F(LayoutGridTest, GridGapsAffectPosition)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        parentStyle.layoutData.grid.column.gap = 10;
        parentStyle.layoutData.grid.row.gap = 20;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style child0Style;
        child0Style.layoutData.grid.column.start = 0;
        child0Style.layoutData.grid.row.start = 0;
        child0Style.layoutData.grid.column.track = GridTrack::Fixed(100.0f);
        child0Style.layoutData.grid.row.track = GridTrack::Fixed(100.0f);

        Style child1Style = child0Style;
        child1Style.layoutData.grid.column.start = 1;

        StyleRef child0Idx = styleManager.Register(child0Style);
        StyleRef child1Idx = styleManager.Register(child1Style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(500, 500)), ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.Add(Element{ .styleRef = child0Idx }, LayoutOverride::FixedSize(100, 100)), ElementStorageResult::Success);
        auto result2 = storage.Add(Element{ .styleRef = child1Idx }, LayoutOverride::FixedSize(100, 100));
        EXPECT_EQ(result2.status, ElementStorageResult::Success);
        ElementRef child2Ref = result2.ref;

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& child2 = storage.GetElementRectUnsafe(child2Ref);
        // Second child in the next explicit column should be at (first column width + column gap)
        EXPECT_EQ(child2.rect.x, 110);
    }

    // Fractional tracks share free space by their (fractional) weights: 0.5fr
    // gets half of what 1fr gets, not the same.
    TEST_F(LayoutGridTest, FractionalTracksUseFractionalWeights)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style half;
        half.layoutData.grid.column.start = 0;
        half.layoutData.grid.row.start    = 0;
        half.layoutData.grid.column.track = GridTrack::Fractional(0.5f);
        half.layoutData.grid.row.track    = GridTrack::Fixed(10.0f);
        Style one = half;
        one.layoutData.grid.column.start  = 1;
        one.layoutData.grid.column.track  = GridTrack::Fractional(1.0f);
        StyleRef halfIdx = styleManager.Register(half);
        StyleRef oneIdx  = styleManager.Register(one);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(300, 100)), ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = halfIdx }, LayoutOverride::FixedSize(10, 10)), ElementStorageResult::Success);
        ElementRef oneRef = storage.Add(Element{ .styleRef = oneIdx }, LayoutOverride::FixedSize(10, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Free space 280 split 1:2 -> the 0.5fr column is 10 + 93 wide.
        EXPECT_NEAR(storage.GetElementRectUnsafe(oneRef).rect.x, 103, 1);
    }

    // Test 3: Verify Column Spanning
    TEST_F(LayoutGridTest, ColumnSpanOccupiesMultipleCells)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style spanStyle;
        spanStyle.layoutData.grid.column.span = 2;
        StyleRef spanIdx = styleManager.Register(spanStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 100)), ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        ElementRef childRef = storage.Add(Element{ .styleRef = spanIdx }, LayoutOverride::FixedSize(200, 50)).ref;

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& child = storage.GetElementRectUnsafe(childRef);
        EXPECT_EQ(child.rect.w, 200); // Should span the full width of two 100px columns
    }
}