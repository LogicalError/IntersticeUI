#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>

#include <gui_core/layout/gui_layout_engine.h>
#include <gui_core/layout/gui_layout_helpers.h>

#include <gui_core/windows/gui_window_backend.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/state/gui_scroll_management.h>

#include "gui_text_system_stub.h"
#include "mock_window_backend.h"

namespace GUI
{
    using namespace testing;
    using ::testing::Return;
    using ::testing::_;
    using ::testing::NiceMock;
    using GUITest::MockWindowBackend;

    class LayoutFlexTest : public ::testing::Test
    {
    protected:
        GradientManager gradientManager{256};
        StyleManager styleManager{gradientManager};
        StubTextSystem textSystem;
        ScrollManagement scrollManagement;
        LayoutEngine layoutEngine{textSystem, scrollManagement};
        ElementStorage storage{styleManager};
        StyleRef defaultStyleIndex;

        MockWindowBackend* mockBackend;
        std::unique_ptr<OSWindowManager> osWindowManager;

        Style CreateBasicStyle(uint16_t margin = 0, uint16_t padding = 0, uint16_t border = 0)
        {
            Style style;
            style.margin = { margin, margin, margin, margin };
            style.padding = { padding, padding, padding, padding };
            style.borderWidths = { border, border, border, border };
            return style;
        }

        void SetUp() override
        {
            mockBackend = new NiceMock<MockWindowBackend>();
            osWindowManager = std::make_unique<OSWindowManager>(nullptr, mockBackend);

            // Define a base style (Index 0)
            Style baseStyle;
            baseStyle.margin = {0, 0, 0, 0};
            baseStyle.padding = {0, 0, 0, 0};
            defaultStyleIndex = styleManager.Register(baseStyle);
        }
    };

    TEST_F(LayoutFlexTest, ParentPaddingAffectsChildPosition)
    {
        Style parentStyle, childStyle;
        // Distinct primes make a wrong sum unlikely to match the expected value.
        parentStyle.margin       = {2131, 2179, 2239, 2281}; // top, right, bottom, left
        parentStyle.padding      = {2137, 2203, 2243, 2287}; // top, right, bottom, left
        parentStyle.borderWidths = {2141, 2207, 2251, 2293}; // top, right, bottom, left
    
        childStyle.margin        = {2143, 2213, 2267, 2297}; // top, right, bottom, left
        childStyle.padding       = {2153, 2221, 2269, 2309}; // top, right, bottom, left
        childStyle.borderWidths  = {2161, 2237, 2273, 2311}; // top, right, bottom, left

        const int parentWidth = 15313;
        const int parentHeight = 15319;
        const int childWidth = 15329;
        const int childHeight = 15331;
        const int gap = 15331;

        // Parent with no margin/border, row direction
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.gap = gap;
    
        StyleRef parentIdx = styleManager.Register(parentStyle);
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
            ElementRef parentRef = storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(parentWidth, parentHeight)).ref;
            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
                ElementRef childRef = storage.Add(Element{ .styleRef = childIdx}, LayoutOverride::FixedSize(childWidth, childHeight)).ref;
    
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &child = storage.GetElementRectUnsafe(childRef);
    
        EXPECT_EQ(child.rect.x, parentStyle.margin.left + parentStyle.borderWidths.left + parentStyle.padding.left + 
                                childStyle.margin.left);
        EXPECT_EQ(child.rect.y, parentStyle.margin.top + parentStyle.borderWidths.top + parentStyle.padding.top + 
                                childStyle.margin.top);
    }

    // A display:none child is out of flow: the next sibling sits as if it weren't there.
    TEST_F(LayoutFlexTest, DisplayNoneChildConsumesNoFlexSpace)
    {
        Style parentStyle = CreateBasicStyle(0, 0, 0);
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.gap = 0;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        StyleRef visibleIdx = styleManager.Register(CreateBasicStyle(0, 0, 0));

        Style hiddenStyle = CreateBasicStyle(0, 0, 0);
        hiddenStyle.display = Display::None;
        StyleRef hiddenIdx = styleManager.Register(hiddenStyle);

        constexpr int W = 100;

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
            EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(300, 50)), ElementStorageResult::Success);
            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
                ElementRef c1 = storage.Add(Element{ .styleRef = visibleIdx }, LayoutOverride::FixedSize(W, 50)).ref;
                EXPECT_EQ(storage.Add(Element{ .styleRef = hiddenIdx }, LayoutOverride::FixedSize(W, 50)), ElementStorageResult::Success); // display:none
                ElementRef c3 = storage.Add(Element{ .styleRef = visibleIdx }, LayoutOverride::FixedSize(W, 50)).ref;
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& r1 = storage.GetElementRectUnsafe(c1);
        const ElementRect& r3 = storage.GetElementRectUnsafe(c3);
        EXPECT_EQ(r3.rect.x, r1.rect.x + W)
            << "the display:none child consumed flex space (should be out of flow)";
    }

    // A display:none flex/grid container must not trip the children-layout dispatch.
    TEST_F(LayoutFlexTest, DisplayNoneContainerDoesNotAssertInLayout)
    {
        Style rootStyle = CreateBasicStyle(0, 0, 0);
        rootStyle.layoutData.flex.direction = FlexDirection::Column;
        StyleRef rootIdx = styleManager.Register(rootStyle);

        Style hiddenContainerStyle = CreateBasicStyle(0, 0, 0); // display:Flex by default
        hiddenContainerStyle.display = Display::None;
        StyleRef hiddenIdx = styleManager.Register(hiddenContainerStyle);

        StyleRef childIdx = styleManager.Register(CreateBasicStyle(0, 0, 0));

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
            EXPECT_EQ(storage.Add(Element{ .styleRef = rootIdx }, LayoutOverride::FixedSize(300, 200)), ElementStorageResult::Success);
            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
                EXPECT_EQ(storage.Add(Element{ .styleRef = hiddenIdx }, LayoutOverride::FixedSize(100, 100)), ElementStorageResult::Success);
                EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
                    EXPECT_EQ(storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(50, 50)), ElementStorageResult::Success);
                EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage); // must not assert/abort
        SUCCEED();
    }

    TEST_F(LayoutFlexTest, ParentMarginsNotAddedToTotalSize)
    {
        // Parent with margin 5, padding 0, border 0, column direction
        Style parentStyle = CreateBasicStyle(5, 0, 0);
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle(0, 0, 0);
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
        ElementRef parentRef = storage.Add(Element{.styleRef = parentIdx}).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        ElementRef childRef = storage.Add(Element{.styleRef = childIdx}, LayoutOverride::FixedSize(30, 40)).ref;
    
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &parent = storage.GetElementRectUnsafe(parentRef);
    
        // Parent border box: child 30x40
        EXPECT_EQ(parent.rect.w, 30);
        EXPECT_EQ(parent.rect.h, 40);
    }

    TEST_F(LayoutFlexTest, ParentBordersInSizing)
    {
        // Parent with border 3, margin 0, padding 0, row
        Style parentStyle = CreateBasicStyle(0, 0, 3);
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle(0, 0, 0);
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
        ElementRef parentRef = storage.Add(Element{.styleRef = parentIdx}).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        ElementRef child1Ref = storage.Add(Element{.styleRef = childIdx}, LayoutOverride::FixedSize(20, 20)).ref;
        ElementRef child2Ref = storage.Add(Element{.styleRef = childIdx}, LayoutOverride::FixedSize(30, 20)).ref;
    
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &parent = storage.GetElementRectUnsafe(parentRef);
    
        // Content width: 20 + 30 = 50
        // Border box: 50 + border horizontal 6 = 56
        // No margin: rect.w = 56
        EXPECT_EQ(parent.rect.w, 56);
        EXPECT_EQ(parent.rect.h, 26); // Height: max child 20 + border vertical 6
    }


    TEST_F(LayoutFlexTest, FixedSizeConstraintsForcesSizeToRect)
    {
        Style style = CreateBasicStyle(0, 0, 0);
        StyleRef styleRef = styleManager.Register(style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
        ElementRef elementRef = storage.Add(Element{.styleRef = styleRef}, LayoutOverride::FixedSize(100, 50)).ref;
    
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &element = storage.GetElementRectUnsafe(elementRef);
    
        // Clip rect should be parent's content rect: x=5,y=5,w=90,h=40 (since padding 5*2=10)
        EXPECT_EQ(element.clipRect.x, 0);
        EXPECT_EQ(element.clipRect.y, 0);
        EXPECT_EQ(element.clipRect.w, 100);
        EXPECT_EQ(element.clipRect.h, 50);
    }

    TEST_F(LayoutFlexTest, ClippingWithOverflowHidden)
    {
        // Parent fixed size, overflow hidden, padding 5
        Style parentStyle = CreateBasicStyle(0, 5, 0);
        parentStyle.SetOverflow(Overflow::Hidden);
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle(0, 0, 0);
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
        ElementRef parentRef = storage.Add(Element{.styleRef = parentIdx}, LayoutOverride::FixedSize(100, 50)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        // Child larger than parent content (100-10=90 width)
        ElementRef childRef = storage.Add(Element{.styleRef = childIdx}, LayoutOverride::FixedSize(120, 60)).ref;
    
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &parent = storage.GetElementRectUnsafe(parentRef);
    
        // Parent's content rect: x=0,y=0,w=100,h=50
        EXPECT_EQ(parent.rect.x, 0);
        EXPECT_EQ(parent.rect.y, 0);
        EXPECT_EQ(parent.rect.w, 100);
        EXPECT_EQ(parent.rect.h, 50);

        const ElementRect &child = storage.GetElementRectUnsafe(childRef);
    
        // Clip rect should be parent's content rect: x=5,y=5,w=90,h=40 (since padding 5*2=10)
        EXPECT_EQ(child.clipRect.x, 5);
        EXPECT_EQ(child.clipRect.y, 5);
        EXPECT_EQ(child.clipRect.w, 90);
        EXPECT_EQ(child.clipRect.h, 40);
    }

    TEST_F(LayoutFlexTest, ClippingInDeeperHierarchy)
    {
        // Grandparent: overflow visible, fixed 200x100
        // Parent: overflow hidden, fixed 100x50, positioned inside grandparent
        // Child: large 150x70

        Style gpStyle = CreateBasicStyle(0, 10, 0); // padding 10
        gpStyle.SetOverflow(Overflow::Visible);
        gpStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef gpIdx = styleManager.Register(gpStyle);

        Style pStyle = CreateBasicStyle(0, 5, 0); // padding 5
        pStyle.SetOverflow(Overflow::Hidden);
        pStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef pIdx = styleManager.Register(pStyle);

        Style cStyle = CreateBasicStyle(0, 0, 0);
        StyleRef cIdx = styleManager.Register(cStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
        ElementRef gpRef = storage.Add(Element{.styleRef = gpIdx}, LayoutOverride::FixedSize(200, 100)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        ElementRef pRef = storage.Add(Element{.styleRef = pIdx}, LayoutOverride::FixedSize(100, 50)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        ElementRef cRef = storage.Add(Element{.styleRef = cIdx}, LayoutOverride::FixedSize(150, 70)).ref;
    
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &parent = storage.GetElementRectUnsafe(pRef);
        const ElementRect &child = storage.GetElementRectUnsafe(cRef);
    
        // Parent position: after gp padding 10
        EXPECT_EQ(parent.rect.x, 10);
        EXPECT_EQ(parent.rect.y, 10);

        // Child position: after parent padding 5, relative to parent
        EXPECT_EQ(child.rect.x, 15); // gp 10 + p 5
        EXPECT_EQ(child.rect.y, 15);

        // Child clip: since parent hidden, clip to parent content: x=15,y=15,w=90,h=40 (100-10,50-10)
        EXPECT_EQ(child.clipRect.x, 15);
        EXPECT_EQ(child.clipRect.y, 15);
        EXPECT_EQ(child.clipRect.w, 90);
        EXPECT_EQ(child.clipRect.h, 40);
    }

    TEST_F(LayoutFlexTest, NestedOverflowVisiblePropagatesClip)
    {
        // Grandparent: hidden, fixed 200x100, padding 10
        // Parent: visible, fixed 100x50, padding 5
        // Child: large 150x70

        Style gpStyle = CreateBasicStyle(0, 10, 0);
        gpStyle.SetOverflow(Overflow::Hidden);
        gpStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef gpIdx = styleManager.Register(gpStyle);

        Style pStyle = CreateBasicStyle(0, 5, 0);
        pStyle.SetOverflow(Overflow::Visible);
        pStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef pIdx = styleManager.Register(pStyle);

        Style cStyle = CreateBasicStyle(0, 0, 0);
        StyleRef cIdx = styleManager.Register(cStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    
        ElementRef gpRef = storage.Add(Element{.styleRef = gpIdx}, LayoutOverride::FixedSize(200, 100)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        ElementRef pRef = storage.Add(Element{.styleRef = pIdx}, LayoutOverride::FixedSize(100, 50)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
    
        ElementRef cRef = storage.Add(Element{.styleRef = cIdx}, LayoutOverride::FixedSize(150, 70)).ref;
    
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect &child = storage.GetElementRectUnsafe(cRef);
    
        // Since parent visible, child's clip should be gp's content rect (hidden)
        // gp content: x=10,y=10,w=180,h=80
        EXPECT_EQ(child.clipRect.x, 10);
        EXPECT_EQ(child.clipRect.y, 10);
        EXPECT_EQ(child.clipRect.w, 180);
        EXPECT_EQ(child.clipRect.h, 80);
    }

    TEST_F(LayoutFlexTest, HorizontalRowLayout_PositionsChildrenSequentially)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.gap = 10;

        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{.styleRef = rowStyleIdx}).ref;

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage
                                .Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50,20))
                                .ref;
        ElementRef child2 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50,20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.x, 60);   // 50 (width) + 10 (gap)
    }

    TEST_F(LayoutFlexTest, VerticalColumnLayout_CalculatesTotalHeight)
    {
        Style colStyle;
        colStyle.layoutData.flex.direction = FlexDirection::Column;
        colStyle.layoutData.flex.gap = 5;

        StyleRef colStyleIdx = styleManager.Register(colStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{.styleRef = colStyleIdx}).ref;

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50,30)).ref;
        ElementRef child2 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50,40)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Total height = 30 (child1) + 5 (gap) + 40 (child2) = 75
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.h, 75);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.y, 35); // Starts after child 1 + gap
    }

    TEST_F(LayoutFlexTest, JustifyContentCenter_OffsetsStartingX)
    {
        Style centerStyle;
        centerStyle.layoutData.flex.direction = FlexDirection::Row;
        centerStyle.layoutData.flex.justifyContent = JustifyContent::Center;
        StyleRef styleIdx = styleManager.Register(centerStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef =
            storage
                .Add(Element{.styleRef = styleIdx}, LayoutOverride::FixedSize(200, 100))
                .ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child =
            storage
                .Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(100, 30))
                .ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        // Root is 200 wide, child is 100 wide.
        // Center should place child at x = (200 - 100) / 2 = 50
        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child).rect.x, 50);
    }

    TEST_F(LayoutFlexTest, RowWrap_MovesItemsToNextLine)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        rowStyle.layoutData.flex.gap = 10;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef =
            storage
                .Add(Element{.styleRef = rowStyleIdx}, LayoutOverride::FixedSize(120, 100))
                .ref;

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef child2 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef child3 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(50, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.y, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.x, 60);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.y, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.y, 30);

        const ElementRect &root = storage.GetElementRectUnsafe(rootRef);
        EXPECT_EQ(root.rect.w, 120);
        EXPECT_EQ(root.rect.h, 100);
    }

    TEST_F(LayoutFlexTest, ColumnWrap_MovesItemsToNextColumn)
    {
        Style colStyle;
        colStyle.layoutData.flex.direction = FlexDirection::Column;
        colStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        colStyle.layoutData.flex.gap = 5;
        StyleRef colStyleIdx = styleManager.Register(colStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef =
            storage
                .Add(Element{.styleRef = colStyleIdx}, LayoutOverride::FixedSize(200, 60))
                .ref;

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef child2 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef child3 = storage.Add(Element{.styleRef = defaultStyleIndex}, LayoutOverride::FixedSize(30, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.y, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.y, 25);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.x, 35);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.y, 0);

        const ElementRect &root = storage.GetElementRectUnsafe(rootRef);
        EXPECT_EQ(root.rect.w, 200);
        EXPECT_EQ(root.rect.h, 60);
    }

    TEST_F(LayoutFlexTest, RowReverse_PositionsChildrenFromRight)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::RowReverse;
        rowStyle.layoutData.flex.gap = 10;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(200, 80)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef child2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef child3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.x, 170);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.x, 130);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.x, 90);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 200);
    }

    TEST_F(LayoutFlexTest, ColumnReverse_PositionsChildrenFromBottom)
    {
        Style colStyle;
        colStyle.layoutData.flex.direction = FlexDirection::ColumnReverse;
        colStyle.layoutData.flex.gap = 5;
        StyleRef colStyleIdx = styleManager.Register(colStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = colStyleIdx }, LayoutOverride::FixedSize(120, 100)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef child2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.y, 80);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.y, 55);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.h, 100);
    }

    TEST_F(LayoutFlexTest, RowWrapReverse_PlacesLaterLinesAboveEarlierLines)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.wrap = FlexWrap::WrapReverse;
        rowStyle.layoutData.flex.gap = 10;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(120, 80)).ref;

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef child2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef child3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // First line (child1/child2) is placed at the bottom for wrap-reverse.
        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.y, 60);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.y, 60);

        // Wrapped line (child3) should be above the first line.
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.y, 30);
        EXPECT_LT(storage.GetElementRectUnsafe(child3).rect.y, storage.GetElementRectUnsafe(child1).rect.y);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.h, 80);
    }

    TEST_F(LayoutFlexTest, RowWrap_BreakBeforeForcesNewLine)
    {
        Style containerStyle;
        containerStyle.layoutData.flex.direction = FlexDirection::Row;
        containerStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        containerStyle.layoutData.flex.gap = 10;

        Style breakStyle;
        breakStyle.breakBehavior = BreakBehavior::Before;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef breakRef = styleManager.Register(breakStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = containerRef }, LayoutOverride::FixedSize(220, 100)).ref;

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef child1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef child2 = storage.Add(Element{ .styleRef = breakRef }, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef child3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child1).rect.y, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.y, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.y, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(child2).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(child3).rect.x, 60);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 220);
    }

    TEST_F(LayoutFlexTest, JustifyContentSpaceBetween_DistributesChildrenAcrossRow)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.justifyContent = JustifyContent::SpaceBetween;
        rowStyle.layoutData.flex.gap = 0;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(100, 40)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        ElementRef c3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.x, 40);
        EXPECT_EQ(storage.GetElementRectUnsafe(c3).rect.x, 80);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 100);
    }

    // Lay out three fixed-size children in a row container and return their x.
    // (Fixed-width children never shrink, so a too-narrow row overflows.)
    static std::vector<int32_t> RowXs(LayoutFlexTest& t, StyleManager& sm, ElementStorage& storage,
                                      LayoutEngine& engine, OSWindowManager& wm, StyleRef childStyle,
                                      FlexDirection dir, JustifyContent justify, int16_t gap,
                                      int32_t width, int32_t childW)
    {
        (void)t;
        Style rowStyle;
        rowStyle.layoutData.flex.direction      = dir;
        rowStyle.layoutData.flex.justifyContent = justify;
        rowStyle.layoutData.flex.gap            = gap;
        const StyleRef rowIdx = sm.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = rowIdx }, LayoutOverride::FixedSize(width, 40));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        std::vector<ElementRef> refs;
        for (int i = 0; i < 3; ++i)
            refs.push_back(storage.Add(Element{ .styleRef = childStyle }, LayoutOverride::FixedSize(childW, 10)).ref);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
        engine.Layout(wm, storage);

        std::vector<int32_t> xs;
        for (auto r : refs) xs.push_back(storage.GetElementRectUnsafe(r).rect.x);
        return xs;
    }

    // A fixed width is the border box: a row's margin lies outside it and must
    // not be taken out of the room its children grow or shrink into.
    TEST_F(LayoutFlexTest, FixedWidthRowWithMarginGivesChildrenTheFullWidth)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.margin = BoxEdges(0, 20);
        const StyleRef rowIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = rowIdx }, LayoutOverride::FixedSize(200, 40));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        const ElementRef a = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride{}.FlexGrow(1)).ref;
        const ElementRef b = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride{}.FlexGrow(1)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(a).rect.w, 100);
        EXPECT_EQ(storage.GetElementRectUnsafe(b).rect.w, 100);
    }

    // A wrapping column breaks into a new column only when its items exceed the
    // content height; the margin lies outside the fixed height.
    TEST_F(LayoutFlexTest, FixedHeightWrappingColumnWithMarginWrapsAtItsContentHeight)
    {
        Style colStyle;
        colStyle.layoutData.flex.direction = FlexDirection::Column;
        colStyle.layoutData.flex.wrap      = FlexWrap::Wrap;
        colStyle.margin = BoxEdges(20, 0);
        const StyleRef colIdx = styleManager.Register(colStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = colIdx }, LayoutOverride::FixedSize(200, 100));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        std::vector<ElementRef> items;
        for (int i = 0; i < 3; ++i)
            items.push_back(storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(40, 30)).ref);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
        layoutEngine.Layout(*osWindowManager, storage);

        // 3 x 30 = 90 fits the 100px content height: one column.
        const int32_t x0 = storage.GetElementRectUnsafe(items[0]).rect.x;
        EXPECT_EQ(storage.GetElementRectUnsafe(items[1]).rect.x, x0);
        EXPECT_EQ(storage.GetElementRectUnsafe(items[2]).rect.x, x0);
    }

    // A fixed size is the border box: its content size excludes padding and
    // border, but not the margin, which lies outside it.
    TEST_F(LayoutFlexTest, FixedContentSizeExcludesOnlyPaddingAndBorder)
    {
        LayoutStyle style;
        style.margin       = BoxEdges(20);
        style.padding      = BoxEdges(5);
        style.borderWidths = BoxEdges(1);
        ElementRect rect{};
        rect.layoutStyle  = &style;
        rect.constraints  = LayoutOverride::FixedSize(200, 100);

        EXPECT_EQ(LayoutHelpers::FixedContentSize<0>(rect), 200 - 12);
        EXPECT_EQ(LayoutHelpers::FixedContentSize<1>(rect), 100 - 12);
    }

    // An out-of-flow child at the start of a wrapping row takes no room: the
    // first in-flow item gets no gap in front of it.
    TEST_F(LayoutFlexTest, LeadingOutOfFlowChildAddsNoGapInAWrappingRow)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.wrap      = FlexWrap::Wrap;
        rowStyle.layoutData.flex.gap       = 10;
        const StyleRef rowIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = rowIdx }, LayoutOverride::FixedSize(100, 100));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride{}.Absolute(0, 0).Size(5, 5));
        const ElementRef a = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(45, 10)).ref;
        const ElementRef b = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(45, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
        layoutEngine.Layout(*osWindowManager, storage);

        // 45 + 10 + 45 fills the row exactly: one line.
        EXPECT_EQ(storage.GetElementRectUnsafe(a).rect.y, storage.GetElementRectUnsafe(b).rect.y);
        EXPECT_EQ(storage.GetElementRectUnsafe(a).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(b).rect.x, 55);
    }

    // ---- Resolving flexible lengths (grow / shrink with min / max) ----

    class LayoutFlexResolveTest : public LayoutFlexTest
    {
    protected:
        // A row (or column) of the given main size holding `kids`, each an
        // auto-sized item whose content is `contentMain` along the main axis.
        std::vector<ElementRef> Lay(FlexDirection dir, int32_t mainSize,
                                    std::vector<LayoutOverride> kids, int32_t contentMain,
                                    StyleRef kidStyle = {})
        {
            const bool row = (dir == FlexDirection::Row);
            Style parentStyle;
            parentStyle.layoutData.flex.direction = dir;
            const StyleRef parentIdx = styleManager.Register(parentStyle);
            if (kidStyle == StyleRef{}) kidStyle = defaultStyleIndex;

            EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
            (void)storage.Add(Element{ .styleRef = parentIdx },
                              row ? LayoutOverride::FixedSize(mainSize, 40) : LayoutOverride::FixedSize(40, mainSize));
            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            std::vector<ElementRef> refs;
            for (auto& k : kids)
            {
                refs.push_back(storage.Add(Element{ .styleRef = kidStyle }, std::move(k)).ref);
                EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
                (void)storage.Add(Element{ .styleRef = defaultStyleIndex },
                                  row ? LayoutOverride::FixedSize(contentMain, 10) : LayoutOverride::FixedSize(10, contentMain));
                EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
            }
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
            EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
            layoutEngine.Layout(*osWindowManager, storage);
            return refs;
        }
        int32_t W(ElementRef r) const { return storage.GetElementRectUnsafe(r).rect.w; }
        int32_t H(ElementRef r) const { return storage.GetElementRectUnsafe(r).rect.h; }
    };

    // An item that hits its max-width stops growing; its unused share goes to
    // the others.
    TEST_F(LayoutFlexResolveTest, GrowStopsAtMaxWidthAndRedistributes)
    {
        const auto k = Lay(FlexDirection::Row, 300,
                           { LayoutOverride{}.FlexGrow(1).MaxWidth(60), LayoutOverride{}.FlexGrow(1) }, 20);
        EXPECT_EQ(W(k[0]), 60);
        EXPECT_EQ(W(k[1]), 240);
    }

    // An item that hits its min-width stops shrinking; the others give up the rest.
    TEST_F(LayoutFlexResolveTest, ShrinkStopsAtMinWidthAndRedistributes)
    {
        const auto k = Lay(FlexDirection::Row, 200,
                           { LayoutOverride{}.FlexShrink(1).MinWidth(120), LayoutOverride{}.FlexShrink(1) }, 150);
        EXPECT_EQ(W(k[0]), 120);
        EXPECT_EQ(W(k[1]), 80);
    }

    // flex-basis sets a row item's starting width: with basis 0 and equal grow,
    // items share the row equally regardless of their content.
    TEST_F(LayoutFlexResolveTest, RowFlexBasisSetsTheStartingWidth)
    {
        Style basis0;
        basis0.layoutData.flex.flexBasis = 0;
        basis0.layoutData.flex.flexGrow  = 1;
        const StyleRef basisIdx = styleManager.Register(basis0);

        Style parentStyle;
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        const StyleRef parentIdx = styleManager.Register(parentStyle);
        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(300, 40));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        std::vector<ElementRef> k;
        for (int32_t content : { 50, 150 })
        {
            k.push_back(storage.Add(Element{ .styleRef = basisIdx }, LayoutOverride::Auto()).ref);
            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(content, 10));
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        }
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(W(k[0]), 150);
        EXPECT_EQ(W(k[1]), 150);
    }

    // An explicit width wins over flex-basis (fit-content table sections rely on it).
    TEST_F(LayoutFlexResolveTest, ExplicitWidthIsNotReplacedByFlexBasis)
    {
        Style basis0;
        basis0.layoutData.flex.flexBasis = 0;
        const StyleRef basisIdx = styleManager.Register(basis0);
        const auto k = Lay(FlexDirection::Row, 300, { LayoutOverride{}.Width(90) }, 20, basisIdx);
        EXPECT_EQ(W(k[0]), 90);
    }

    // The same rules on a column's main axis.
    TEST_F(LayoutFlexResolveTest, ColumnGrowStopsAtMaxHeightAndRedistributes)
    {
        const auto k = Lay(FlexDirection::Column, 300,
                           { LayoutOverride{}.FlexGrow(1).MaxHeight(60), LayoutOverride{}.FlexGrow(1) }, 20);
        EXPECT_EQ(H(k[0]), 60);
        EXPECT_EQ(H(k[1]), 240);
    }

    TEST_F(LayoutFlexResolveTest, ColumnShrinkStopsAtMinHeightAndRedistributes)
    {
        const auto k = Lay(FlexDirection::Column, 200,
                           { LayoutOverride{}.FlexShrink(1).MinHeight(120), LayoutOverride{}.FlexShrink(1) }, 150);
        EXPECT_EQ(H(k[0]), 120);
        EXPECT_EQ(H(k[1]), 80);
    }

    // In row-reverse the main start is the right edge, so flex-end packs left.
    TEST_F(LayoutFlexTest, RowReverseFlexEndPacksAtTheLeft)
    {
        const auto xs = RowXs(*this, styleManager, storage, layoutEngine, *osWindowManager, defaultStyleIndex,
                              FlexDirection::RowReverse, JustifyContent::FlexEnd, 10, 200, 30);
        EXPECT_EQ(xs, (std::vector<int32_t>{ 80, 40, 0 }));
    }

    TEST_F(LayoutFlexTest, RowReverseSpaceBetweenSpansTheRow)
    {
        const auto xs = RowXs(*this, styleManager, storage, layoutEngine, *osWindowManager, defaultStyleIndex,
                              FlexDirection::RowReverse, JustifyContent::SpaceBetween, 0, 100, 20);
        EXPECT_EQ(xs, (std::vector<int32_t>{ 80, 40, 0 }));
    }

    // gap is a minimum: space-between adds the free space on top of it, so the
    // last item still ends flush with the far edge.
    TEST_F(LayoutFlexTest, SpaceBetweenKeepsTheGap)
    {
        const auto xs = RowXs(*this, styleManager, storage, layoutEngine, *osWindowManager, defaultStyleIndex,
                              FlexDirection::Row, JustifyContent::SpaceBetween, 10, 100, 20);
        EXPECT_EQ(xs, (std::vector<int32_t>{ 0, 40, 80 }));
    }

    // With no free space, space-between falls back to flex-start (no overlap).
    TEST_F(LayoutFlexTest, SpaceBetweenWithNegativeFreeSpaceActsLikeFlexStart)
    {
        const auto xs = RowXs(*this, styleManager, storage, layoutEngine, *osWindowManager, defaultStyleIndex,
                              FlexDirection::Row, JustifyContent::SpaceBetween, 0, 100, 50);
        EXPECT_EQ(xs, (std::vector<int32_t>{ 0, 50, 100 }));
    }

    TEST_F(LayoutFlexTest, JustifyContentSpaceAround_DistributesChildrenWithOuterHalfSpacing)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.justifyContent = JustifyContent::SpaceAround;
        rowStyle.layoutData.flex.gap = 0;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(100, 40)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.x, 15);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.x, 65);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 100);
    }

    TEST_F(LayoutFlexTest, JustifyContentSpaceEvenly_DistributesChildrenWithEqualOuterSpacing)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.justifyContent = JustifyContent::SpaceEvenly;
        rowStyle.layoutData.flex.gap = 0;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(100, 40)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(20, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.x, 20);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.x, 60);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 100);
    }

    TEST_F(LayoutFlexTest, RowWrapAlignItemsCenter_CentersChildrenWithinEachLine)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        rowStyle.layoutData.flex.alignItems = AlignItems::Center;
        rowStyle.layoutData.flex.gap = 10;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(120, 100)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 10)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef c3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 10)).ref;
        ElementRef c4 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.y, 5);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.y, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(c3).rect.y, 35);
        EXPECT_EQ(storage.GetElementRectUnsafe(c4).rect.y, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 120);
    }

    TEST_F(LayoutFlexTest, RowWrapAlignItemsStretch_StretchesChildrenToLineHeight)
    {
        Style rowStyle;
        rowStyle.layoutData.flex.direction = FlexDirection::Row;
        rowStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        rowStyle.layoutData.flex.alignItems = AlignItems::Stretch;
        rowStyle.layoutData.flex.gap = 10;
        StyleRef rowStyleIdx = styleManager.Register(rowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = rowStyleIdx }, LayoutOverride::FixedSize(120, 100)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedWidth(50)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        ElementRef c3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedWidth(50)).ref;
        ElementRef c4 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(50, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.h, 20);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.h, 20);
        EXPECT_EQ(storage.GetElementRectUnsafe(c3).rect.h, 20);
        EXPECT_EQ(storage.GetElementRectUnsafe(c4).rect.h, 20);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.w, 120);
    }

    TEST_F(LayoutFlexTest, ColumnWrapAlignItemsCenter_CentersChildrenWithinEachColumn)
    {
        Style colStyle;
        colStyle.layoutData.flex.direction = FlexDirection::Column;
        colStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        colStyle.layoutData.flex.alignItems = AlignItems::Center;
        colStyle.layoutData.flex.gap = 5;
        StyleRef colStyleIdx = styleManager.Register(colStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = colStyleIdx }, LayoutOverride::FixedSize(200, 60)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(10, 20)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef c3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(10, 20)).ref;
        ElementRef c4 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.x, 10);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.x, 0);
        EXPECT_EQ(storage.GetElementRectUnsafe(c3).rect.x, 45);
        EXPECT_EQ(storage.GetElementRectUnsafe(c4).rect.x, 35);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.h, 60);
    }

    TEST_F(LayoutFlexTest, ColumnWrapAlignItemsStretch_StretchesChildrenToColumnWidth)
    {
        Style colStyle;
        colStyle.layoutData.flex.direction = FlexDirection::Column;
        colStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        colStyle.layoutData.flex.alignItems = AlignItems::Stretch;
        colStyle.layoutData.flex.gap = 5;
        StyleRef colStyleIdx = styleManager.Register(colStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ElementRef rootRef = storage.Add(Element{ .styleRef = colStyleIdx }, LayoutOverride::FixedSize(200, 60)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ElementRef c1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedHeight(20)).ref;
        ElementRef c2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        ElementRef c3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedHeight(20)).ref;
        ElementRef c4 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(30, 20)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1).rect.w, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2).rect.w, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(c3).rect.w, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(c4).rect.w, 30);
        EXPECT_EQ(storage.GetElementRectUnsafe(rootRef).rect.h, 60);
    }

    // ============================================================================
    // Flex-grow
    // ============================================================================

    TEST_F(LayoutFlexTest, FlexGrowFillsAvailableSpace)
    {
        // Parent 200px wide, child auto-sized. flex-grow: 1 should fill parent.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexGrow = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(0, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.w, 200);
    }

    TEST_F(LayoutFlexTest, FlexGrowZeroDoesNotGrow)
    {
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexGrow = 0;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(50, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.w, 50);
    }

    TEST_F(LayoutFlexTest, FlexGrowProportionalDistribution)
    {
        // Two children: grow 2 and 1. 300px parent, children start at 0.
        // Child1 should get 200, child2 should get 100.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle1 = CreateBasicStyle();
        childStyle1.layoutData.flex.flexGrow = 2;
        Style childStyle2 = CreateBasicStyle();
        childStyle2.layoutData.flex.flexGrow = 1;

        StyleRef childIdx1 = styleManager.Register(childStyle1);
        StyleRef childIdx2 = styleManager.Register(childStyle2);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(300, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c1 = storage.Add(Element{ .styleRef = childIdx1 }, LayoutOverride::FixedSize(0, 20));
        auto c2 = storage.Add(Element{ .styleRef = childIdx2 }, LayoutOverride::FixedSize(0, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1.ref).rect.w, 200);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2.ref).rect.w, 100);
    }

    TEST_F(LayoutFlexTest, FlexGrowSpreadsLeftoverPixelsAndFillsExactly)
    {
        // 100 / 3 leaves a remainder: all 100px must be handed out, at most 1px apart.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexGrow = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c1 = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(0, 20));
        auto c2 = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(0, 20));
        auto c3 = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(0, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const int w1 = storage.GetElementRectUnsafe(c1.ref).rect.w;
        const int w2 = storage.GetElementRectUnsafe(c2.ref).rect.w;
        const int w3 = storage.GetElementRectUnsafe(c3.ref).rect.w;
        EXPECT_EQ(w1 + w2 + w3, 100) << "all pixels distributed (no leftover dropped)";
        const int lo = std::min(w1, std::min(w2, w3));
        const int hi = std::max(w1, std::max(w2, w3));
        EXPECT_LE(hi - lo, 1) << "leftover spread evenly; no child absorbs >1px extra";
    }

    TEST_F(LayoutFlexTest, FlexGrowInColumn)
    {
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexGrow = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 200));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(50, 0));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.h, 200);
    }

    // ============================================================================
    // Flex-shrink
    // ============================================================================

    TEST_F(LayoutFlexTest, FlexShrinkReducesChildWidth)
    {
        // Parent 100px, child 200px with flex-shrink: 1. Should shrink to 100.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexShrink = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx });
        // Give child content width via a nested fixed child
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(200, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.w, 100);
    }

    TEST_F(LayoutFlexTest, FlexShrinkZeroOptsOut)
    {
        // Parent 100px, two children 80px each (via nested content, not fixed constraints).
        // One has shrink 0, the other shrink 1. Only the shrinkable child should reduce.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style noShrinkStyle = CreateBasicStyle();
        noShrinkStyle.layoutData.flex.flexShrink = 0;
        Style shrinkStyle = CreateBasicStyle();
        shrinkStyle.layoutData.flex.flexShrink = 1;

        StyleRef noShrinkIdx = styleManager.Register(noShrinkStyle);
        StyleRef shrinkIdx = styleManager.Register(shrinkStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        // Use nested fixed-size children to give content width without fixing the outer element
        auto c1 = storage.Add(Element{ .styleRef = noShrinkIdx });
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(80, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        auto c2 = storage.Add(Element{ .styleRef = shrinkIdx });
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(80, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // c1 keeps its 80px (shrink 0), c2 absorbs all the overflow
        EXPECT_EQ(storage.GetElementRectUnsafe(c1.ref).rect.w, 80);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2.ref).rect.w, 20);
    }

    TEST_F(LayoutFlexTest, FlexShrinkDoesNotApplyWithWrapping)
    {
        // With wrap enabled, children should wrap rather than shrink.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexShrink = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 100));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c1 = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(60, 20));
        auto c2 = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(60, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Both should keep their 60px width — c2 wraps to next line
        EXPECT_EQ(storage.GetElementRectUnsafe(c1.ref).rect.w, 60);
        EXPECT_EQ(storage.GetElementRectUnsafe(c2.ref).rect.w, 60);
    }

    TEST_F(LayoutFlexTest, FlexShrinkRespectsFixedConstraints)
    {
        // Fixed-size constraint children should not shrink.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        StyleRef childIdx = styleManager.Register(CreateBasicStyle());

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(200, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Fixed-size child should keep its 200px
        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.w, 200);
    }

    // ============================================================================
    // FlexOverride from constraints
    // ============================================================================

    TEST_F(LayoutFlexTest, FlexOverrideDisablesGrow)
    {
        // Style has flex-grow: 1 but constraint overrides to 0.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexGrow = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx },
            LayoutOverride(LayoutOverride::FixedSize(50, 20)).NoGrow());
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Despite flex-grow: 1, constraint override keeps it at 50
        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.w, 50);
    }

    TEST_F(LayoutFlexTest, FlexOverrideIncreasesGrow)
    {
        // Style has flex-grow: 0 but constraint overrides to 1.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexGrow = 0;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(Element{ .styleRef = childIdx },
            LayoutOverride(LayoutOverride::FixedSize(0, 20)).FlexGrow(1));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(child.ref).rect.w, 200);
    }

    TEST_F(LayoutFlexTest, FlexOverrideDisablesShrink)
    {
        // Two content-sized children overflow. One has NoShrinkX constraint.
        Style parentStyle = CreateBasicStyle();
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle = CreateBasicStyle();
        childStyle.layoutData.flex.flexShrink = 1;
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(100, 50));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        // c1: content-sized 80px, NoShrinkX override via constraint
        auto c1 = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride{}.NoShrink());
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(80, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        // c2: content-sized 80px, normal shrink
        auto c2 = storage.Add(Element{ .styleRef = childIdx });
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        (void)storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(80, 20));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.GetElementRectUnsafe(c1.ref).rect.w, 80); // NoShrink
        EXPECT_EQ(storage.GetElementRectUnsafe(c2.ref).rect.w, 20); // absorbs all overflow
    }

    // A stretch column (like dock content) holding a wrapping grow row with no fixed
    // size: the row takes its width from the stretch, so its boxes wrap.
    TEST_F(LayoutFlexTest, RowWrap_InsideStretchColumn_WithoutFixedWidth)
    {
        // Outer column: column direction, nowrap, align-items:stretch, padding 6, overflow auto.
        Style outerColStyle = CreateBasicStyle(0, 6, 0);
        outerColStyle.SetOverflow(Overflow::Auto);
        outerColStyle.layoutData.flex.direction = FlexDirection::Column;
        outerColStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        outerColStyle.layoutData.flex.alignItems = AlignItems::Stretch;
        outerColStyle.layoutData.flex.flexGrow = 1;
        StyleRef outerIdx = styleManager.Register(outerColStyle);

        // Inner row: row direction, wrap, align-items:stretch, padding 6, overflow auto, flex-grow 1.
        Style innerRowStyle = CreateBasicStyle(0, 6, 0);
        innerRowStyle.SetOverflow(Overflow::Auto);
        innerRowStyle.layoutData.flex.direction = FlexDirection::Row;
        innerRowStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        innerRowStyle.layoutData.flex.alignItems = AlignItems::Stretch;
        innerRowStyle.layoutData.flex.flexGrow = 1;
        StyleRef innerIdx = styleManager.Register(innerRowStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto outer = storage.Add(Element{ .styleRef = outerIdx }, LayoutOverride::FixedSize(240, 400));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        // flex-grow:1 but no fixed size: the width comes from the outer stretch.
        auto row = storage.Add(Element{ .styleRef = innerIdx }, LayoutOverride{}.FlexGrow(1));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        // 3 boxes of width 100; outer is 240 wide, so line 1 fits 2 boxes,
        // box #3 must wrap to line 2.
        auto b1 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(100, 50));
        auto b2 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(100, 50));
        auto b3 = storage.Add(Element{ .styleRef = defaultStyleIndex }, LayoutOverride::FixedSize(100, 50));
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // outer 240 - 2*6 padding = 228 inner; row is stretched across it.
        EXPECT_EQ(storage.GetElementRectUnsafe(row.ref).rect.w, 228);
        // b1/b2 sit on the first line (y == outer-pad + row-pad == 12).
        EXPECT_EQ(storage.GetElementRectUnsafe(b1.ref).rect.y, 12);
        EXPECT_EQ(storage.GetElementRectUnsafe(b2.ref).rect.y, 12);
        // b3 must have wrapped to a second line.
        EXPECT_GT(storage.GetElementRectUnsafe(b3.ref).rect.y, 12);
    }
}