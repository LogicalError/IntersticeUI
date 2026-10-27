#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>

#include <gui_core/layout/gui_layout_engine.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/state/gui_scroll_management.h>

#include "gui_text_system_stub.h"
#include "mock_window_backend.h"

namespace tachyon
{

    using namespace testing;
    using ::testing::Return;
    using ::testing::_;
    using ::testing::NiceMock;
    using GUITest::MockWindowBackend;

    class LayoutEngineTest : public ::testing::Test
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

        Element CreateElement(const StyleRef & styleRef, ID id = ID::Invalid())
        {
            Element element;
            if (id != ID::Invalid())
            {
                element.id = id;
            }
            else
            {
                static uint64_t nextID = 1;
                element.id = ID{.value{.integer = nextID++}};
            }

            element.styleRef = styleRef;
            return element;
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

    TEST_F(LayoutEngineTest, EmptyStorage)
    {
        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.ElementCount(), 0u);
    }

    TEST_F(LayoutEngineTest, SingleLeafElement)
    {
        Style style;
        // Distinct primes make a wrong sum unlikely to match the expected value.
        style.margin       = {2131, 2179, 2239, 2281}; // top, right, bottom, left
        style.padding      = {2137, 2203, 2243, 2287}; // top, right, bottom, left
        style.borderWidths = {2141, 2207, 2251, 2293}; // top, right, bottom, left
        StyleRef styleRef = styleManager.Register(style);
        Element element = CreateElement(styleRef);


        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(std::move(element));
        EXPECT_EQ(addResult.status, ElementStorageResult::Success);
        ElementRef leafRef = addResult.ref;
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        EXPECT_EQ(storage.ElementCount(), 1u);
        const ElementRect &result = storage.GetElementRectUnsafe(leafRef);

        EXPECT_EQ(result.rect.x, style.margin.left); // root rect origin (0) + margin 1
        EXPECT_EQ(result.rect.y, style.margin.top); // root rect origin (0) + margin 2

        // Size should be minimum required by style (padding + border)
        EXPECT_EQ(result.rect.w, style.padding.Horizontal() + style.borderWidths.Horizontal());
        EXPECT_EQ(result.rect.h, style.padding.Vertical() + style.borderWidths.Vertical());
    }

    // Parent with two children using distinct prime margins/padding/borders.
    class ParentWithChildrenTest : public LayoutEngineTest
    {
    protected:
        Style parentStyle;
        Style childStyle;
        const ElementRect* p  = nullptr;
        const ElementRect* c1 = nullptr;
        const ElementRect* c2 = nullptr;

        void SetUp() override
        {
            LayoutEngineTest::SetUp();

            parentStyle.margin       = {2131, 2179, 2239, 2281}; // top, right, bottom, left
            parentStyle.padding      = {2137, 2203, 2243, 2287};
            parentStyle.borderWidths = {2141, 2207, 2251, 2293};

            childStyle.margin        = {2143, 2213, 2267, 2297};
            childStyle.padding       = {2153, 2221, 2269, 2309};
            childStyle.borderWidths  = {2161, 2237, 2273, 2311};

            StyleRef parentStyleRef = styleManager.Register(parentStyle);
            StyleRef childStyleRef = styleManager.Register(childStyle);

            ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

                auto parentResult = storage.Add(CreateElement(parentStyleRef, ID{.value{.integer = 1}}));
                ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

                ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

                    auto child1Result = storage.Add(CreateElement(childStyleRef, ID{.value{.integer = 2}}));
                    ASSERT_EQ(child1Result.status, ElementStorageResult::Success);

                    auto child2Result = storage.Add(CreateElement(childStyleRef, ID{.value{.integer = 3}}));
                    ASSERT_EQ(child2Result.status, ElementStorageResult::Success);

                ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);

            ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

            layoutEngine.Layout(*osWindowManager, storage);
            ASSERT_EQ(storage.ElementCount(), 3u);

            p  = storage.GetElementRectPtr(parentResult.ref);
            c1 = storage.GetElementRectPtr(child1Result.ref);
            c2 = storage.GetElementRectPtr(child2Result.ref);

            ASSERT_NE(p, nullptr);
            ASSERT_NE(c1, nullptr);
            ASSERT_NE(c2, nullptr);
        }
    };

    TEST_F(ParentWithChildrenTest, ParentSizeFitsChildrenPlusPaddingAndBorder)
    {
        int childBorderWidth  = childStyle.borderWidths.Horizontal() + childStyle.padding.Horizontal();
        int childBorderHeight = childStyle.borderWidths.Vertical()   + childStyle.padding.Vertical();
        int childFullWidth    = childStyle.margin.Horizontal() + childBorderWidth;
        int childFullHeight   = childStyle.margin.Vertical()   + childBorderHeight;
        int parentWidth  = (childFullWidth * 2) + parentStyle.borderWidths.Horizontal() + parentStyle.padding.Horizontal();
        int parentHeight = childFullHeight      + parentStyle.borderWidths.Vertical()   + parentStyle.padding.Vertical();

        EXPECT_EQ(p->rect.w, parentWidth);
        EXPECT_EQ(p->rect.h, parentHeight);
    }

    TEST_F(ParentWithChildrenTest, ParentPositionAtOwnMargin)
    {
        EXPECT_EQ(p->rect.x, parentStyle.margin.left);
        EXPECT_EQ(p->rect.y, parentStyle.margin.top);
    }

    TEST_F(ParentWithChildrenTest, FirstChildSizedByPaddingAndBorder)
    {
        int childBorderWidth  = childStyle.borderWidths.Horizontal() + childStyle.padding.Horizontal();
        int childBorderHeight = childStyle.borderWidths.Vertical()   + childStyle.padding.Vertical();

        EXPECT_EQ(c1->rect.w, childBorderWidth);
        EXPECT_EQ(c1->rect.h, childBorderHeight);
    }

    TEST_F(ParentWithChildrenTest, FirstChildPositionedInsideParentPadding)
    {
        EXPECT_EQ(c1->rect.x,
                  parentStyle.margin.left + parentStyle.borderWidths.left + parentStyle.padding.left +
                  childStyle.margin.left);
        EXPECT_EQ(c1->rect.y,
                  parentStyle.margin.top + parentStyle.borderWidths.top + parentStyle.padding.top +
                  childStyle.margin.top);
    }

    TEST_F(ParentWithChildrenTest, SecondChildSizedByPaddingAndBorder)
    {
        int childBorderWidth  = childStyle.borderWidths.Horizontal() + childStyle.padding.Horizontal();
        int childBorderHeight = childStyle.borderWidths.Vertical()   + childStyle.padding.Vertical();

        EXPECT_EQ(c2->rect.w, childBorderWidth);
        EXPECT_EQ(c2->rect.h, childBorderHeight);
    }

    TEST_F(ParentWithChildrenTest, SecondChildPositionedAfterFirstChild)
    {
        EXPECT_EQ(c2->rect.x,
                  c1->rect.x + c1->rect.w + childStyle.margin.right +
                  childStyle.margin.left);
        EXPECT_EQ(c2->rect.y,
                  parentStyle.margin.top + parentStyle.borderWidths.top + parentStyle.padding.top +
                  childStyle.margin.top);
    }

    TEST_F(LayoutEngineTest, GridAutoPlacementUsesSpanForColumnInference)
    {
        Style gridContainerStyle;
        gridContainerStyle.display = Display::Grid;
        gridContainerStyle.layoutData.grid.column.gap = 0;
        gridContainerStyle.layoutData.grid.row.gap = 0;

        Style gridCellStyle;
        gridCellStyle.layoutData.grid.column.track = GridTrack::Fixed(20.0f);
        gridCellStyle.layoutData.grid.row.track = GridTrack::Fixed(12.0f);

        Style gridHeaderStyle = gridCellStyle;
        gridHeaderStyle.layoutData.grid.column.span = 3;

        StyleRef gridContainerRef = styleManager.Register(gridContainerStyle);
        StyleRef gridCellRef = styleManager.Register(gridCellStyle);
        StyleRef gridHeaderRef = styleManager.Register(gridHeaderStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto containerResult = storage.Add(CreateElement(gridContainerRef, ID{ .value{ .integer = 2001 } }),
                                           LayoutOverride::FixedSize(200, 120));
        ASSERT_EQ(containerResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        auto headerResult = storage.Add(CreateElement(gridHeaderRef, ID{ .value{ .integer = 2002 } }));
        ASSERT_EQ(headerResult.status, ElementStorageResult::Success);

        auto aResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 2003 } }));
        ASSERT_EQ(aResult.status, ElementStorageResult::Success);
        auto bResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 2004 } }));
        ASSERT_EQ(bResult.status, ElementStorageResult::Success);
        auto cResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 2005 } }));
        ASSERT_EQ(cResult.status, ElementStorageResult::Success);
        auto dResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 2006 } }));
        ASSERT_EQ(dResult.status, ElementStorageResult::Success);
        auto eResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 2007 } }));
        ASSERT_EQ(eResult.status, ElementStorageResult::Success);
        auto fResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 2008 } }));
        ASSERT_EQ(fResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& header = storage.GetElementRectUnsafe(headerResult.ref);
        const ElementRect& a = storage.GetElementRectUnsafe(aResult.ref);
        const ElementRect& b = storage.GetElementRectUnsafe(bResult.ref);
        const ElementRect& c = storage.GetElementRectUnsafe(cResult.ref);
        const ElementRect& d = storage.GetElementRectUnsafe(dResult.ref);

        // Header occupies first row and should push the remaining items to following rows,
        // preserving a 3-column grid.
        EXPECT_EQ(header.rect.y, storage.GetElementRectUnsafe(containerResult.ref).innerRect.y);

        EXPECT_GT(a.rect.y, header.rect.y);
        EXPECT_EQ(a.rect.x, header.rect.x);

        EXPECT_EQ(b.rect.y, a.rect.y);
        EXPECT_GT(b.rect.x, a.rect.x);

        EXPECT_EQ(c.rect.y, a.rect.y);
        EXPECT_GT(c.rect.x, b.rect.x);

        EXPECT_GT(d.rect.y, a.rect.y);
        EXPECT_EQ(d.rect.x, a.rect.x);
    }

    TEST_F(LayoutEngineTest, GridCellsStretchToColumnWidthByDefault)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;

        Style cellStyle;
        cellStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        cellStyle.layoutData.grid.row.track = GridTrack::Fixed(20.0f);

        Style headerStyle = cellStyle;
        headerStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        headerStyle.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        headerStyle.layoutData.grid.column.span = 3;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef cellRef = styleManager.Register(cellStyle);
        StyleRef headerRef = styleManager.Register(headerStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 3001 } }),
                                        LayoutOverride::FixedSize(300, 120));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto headerResult = storage.Add(CreateElement(headerRef, ID{ .value{ .integer = 3002 } }));
        ASSERT_EQ(headerResult.status, ElementStorageResult::Success);

        auto aResult = storage.Add(CreateElement(cellRef, ID{ .value{ .integer = 3003 } }));
        ASSERT_EQ(aResult.status, ElementStorageResult::Success);
        auto bResult = storage.Add(CreateElement(cellRef, ID{ .value{ .integer = 3004 } }));
        ASSERT_EQ(bResult.status, ElementStorageResult::Success);
        auto cResult = storage.Add(CreateElement(cellRef, ID{ .value{ .integer = 3005 } }));
        ASSERT_EQ(cResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const ElementRect& a = storage.GetElementRectUnsafe(aResult.ref);
        const ElementRect& b = storage.GetElementRectUnsafe(bResult.ref);
        const ElementRect& c = storage.GetElementRectUnsafe(cResult.ref);

        // Three 1fr columns in a 300px container should be stretched to about 100px each.
        EXPECT_GE(a.rect.w, 95);
        EXPECT_GE(b.rect.w, 95);
        EXPECT_GE(c.rect.w, 95);

        EXPECT_EQ(a.rect.x, parent.innerRect.x);
        EXPECT_EQ(b.rect.x, a.rect.x + a.rect.w);
        EXPECT_EQ(c.rect.x, b.rect.x + b.rect.w);
    }

    TEST_F(LayoutEngineTest, FlexRowWrapChildrenKeepIntrinsicWidths)
    {
        Style containerStyle;
        containerStyle.display = Display::Flex;
        containerStyle.layoutData.flex.direction = FlexDirection::Row;
        containerStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        containerStyle.layoutData.flex.gap = 10;
        containerStyle.padding = BoxEdges(0);
        containerStyle.borderWidths = BoxEdges(0);

        Style itemStyle;
        itemStyle.padding = BoxEdges(2);
        itemStyle.borderWidths = BoxEdges(2);
        itemStyle.textLayoutStyle.textWrapping = TextWrap::NoWrap;
        itemStyle.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef itemRef = styleManager.Register(itemStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 9001 } }),
                                        LayoutOverride::FixedSize(180, 200));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        std::vector<ElementRef> itemRefs;
        const char* labels[] = { "First Item", "Second Item", "Third Item", "Fourth Item", "Fifth Item", "Sixth Item" };
        for (int i = 0; i < 6; ++i)
        {
            ID id{ .value{ .integer = static_cast<uint64_t>(9100 + i) } };
            Element item = CreateElement(itemRef, id);
            item.textID = textSystem.RegisterText(id, IconID::Invalid, labels[i], itemRef, ElementState::None);
            auto itemResult = storage.Add(std::move(item));
            ASSERT_EQ(itemResult.status, ElementStorageResult::Success);
            itemRefs.push_back(itemResult.ref);
        }

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const int32_t parentInnerWidth = parent.innerRect.w;

        // In row-wrap mode, children should keep intrinsic width and should not be stretched
        // to the container width.
        for (ElementRef ref : itemRefs)
        {
            const ElementRect& item = storage.GetElementRectUnsafe(ref);
            EXPECT_LT(item.rect.w, parentInnerWidth);
        }
    }

    TEST_F(LayoutEngineTest, FlexRowWrapAutoParentMixedStylesDoNotStretchLaterItems)
    {
        Style boxStyle;
        boxStyle.padding = BoxEdges(10);
        boxStyle.borderWidths = BoxEdges(0);
        boxStyle.SetOverflow(Overflow::Hidden);
        boxStyle.display = Display::Flex;
        boxStyle.layoutData.flex.direction = FlexDirection::Row;
        boxStyle.layoutData.flex.wrap = FlexWrap::NoWrap;

        Style itemListStyle = boxStyle;
        itemListStyle.padding = BoxEdges(0);
        itemListStyle.borderWidths = BoxEdges(2);
        itemListStyle.SetOverflow(Overflow::Auto);
        itemListStyle.layoutData.flex.direction = FlexDirection::Row;
        itemListStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        itemListStyle.layoutData.flex.gap = 10;

        Style buttonStyle = boxStyle;
        buttonStyle.padding = BoxEdges(2);
        buttonStyle.borderWidths = BoxEdges(2);
        buttonStyle.SetOverflow(Overflow::Auto);
        buttonStyle.textLayoutStyle.textWrapping = TextWrap::NoWrap;
        buttonStyle.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;

        Style knobStyle = boxStyle;
        knobStyle.padding = BoxEdges(0);
        knobStyle.borderWidths = BoxEdges(2);

        StyleRef listRef = styleManager.Register(itemListStyle);
        StyleRef buttonRef = styleManager.Register(buttonStyle);
        StyleRef knobRef = styleManager.Register(knobStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(listRef, ID{ .value{ .integer = 10001 } }));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        std::vector<ElementRef> itemRefs;
        const char* labels[] = { "First Item", "Second Item", "Third Item", "Fourth Item", "Fifth Item", "Sixth Item" };
        for (int i = 0; i < 6; ++i)
        {
            ID id{ .value{ .integer = static_cast<uint64_t>(10100 + i) } };
            Element item = CreateElement(i == 0 ? buttonRef : knobRef, id);
            item.textID = textSystem.RegisterText(id, IconID::Invalid, labels[i], item.styleRef, ElementState::None);
            auto itemResult = storage.Add(std::move(item));
            ASSERT_EQ(itemResult.status, ElementStorageResult::Success);
            itemRefs.push_back(itemResult.ref);
        }

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const int32_t innerWidth = parent.innerRect.w;

        // Later items must not stretch to the parent width in auto-size + wrap mode.
        for (ElementRef itemRef : itemRefs)
        {
            const ElementRect& item = storage.GetElementRectUnsafe(itemRef);
            EXPECT_LT(item.rect.w, innerWidth);
        }
    }

    TEST_F(LayoutEngineTest, MakeTextCursorVisibleScrollsAncestorWhenTextboxIsClipped)
    {
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        parentStyle.SetOverflow(Overflow::Auto);

        Style spacerStyle;

        Style textBoxStyle;
        textBoxStyle.SetOverflow(Overflow::Auto);
        textBoxStyle.padding = BoxEdges(4);

        StyleRef parentStyleRef = styleManager.Register(parentStyle);
        StyleRef spacerStyleRef = styleManager.Register(spacerStyle);
        StyleRef textBoxStyleRef = styleManager.Register(textBoxStyle);

        ID parentId{ .value{ .integer = 1001 } };
        ID spacerId{ .value{ .integer = 1002 } };
        ID textBoxId{ .value{ .integer = 1003 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(parentStyleRef, parentId), LayoutOverride::FixedSize(120, 70));
        EXPECT_EQ(parentResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        auto spacerResult = storage.Add(CreateElement(spacerStyleRef, spacerId), LayoutOverride::FixedSize(120, 120));
        EXPECT_EQ(spacerResult.status, ElementStorageResult::Success);

        Element textBoxElement = CreateElement(textBoxStyleRef, textBoxId);
        textBoxElement.textID = textSystem.RegisterText(textBoxId, IconID::Invalid,
                                                        "line1\nline2\nline3\nline4\nline5\nline6",
                                                        textBoxStyleRef,
                                                        ElementState::None);
        auto textBoxResult = storage.Add(std::move(textBoxElement), LayoutOverride::FixedSize(120, 50));
        EXPECT_EQ(textBoxResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        ScrollOffsetHandle parentHandle = scrollManagement.GetScrollOffsetHandle(parentId);
        ASSERT_NE(parentHandle, ScrollOffsetHandle::Invalid);
        EXPECT_EQ(scrollManagement.GetScrollOffset(parentHandle).y, 0);

        bool scrolled = scrollManagement.MakeTextCursorVisibleInTextBox(
            storage,
            textBoxResult.ref,
            int2{0, 90},
            16);

        EXPECT_TRUE(scrolled);
        EXPECT_LT(scrollManagement.GetScrollOffset(parentHandle).y, 0);
    }

    TEST_F(LayoutEngineTest, FixedPositionConstraintsOffsetIsApplied)
    {
        Style style;
        style.margin = { 0, 0, 0, 0 };
        style.padding = { 0, 0, 0, 0 };
        style.borderWidths = { 0, 0, 0, 0 };
        StyleRef styleRef = styleManager.Register(style);

        Element element = CreateElement(styleRef);

        LayoutOverride position = LayoutOverride{}.Absolute(17, 9);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(std::move(element), std::move(position.Size(30, 12)));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.rect.x, 17);
        EXPECT_EQ(rect.rect.y, 9);
        EXPECT_EQ(rect.rect.w, 30);
        EXPECT_EQ(rect.rect.h, 12);
    }

    TEST_F(LayoutEngineTest, SizeConstraintsMinMaxAreAppliedForLeaf)
    {
        Style style;
        style.margin = { 0, 0, 0, 0 };
        style.padding = { 0, 0, 0, 0 };
        style.borderWidths = { 0, 0, 0, 0 };
        StyleRef styleRef = styleManager.Register(style);

        LayoutOverride constraints {};
        constraints.width.min = 25;
        constraints.width.max = 40;
        constraints.height.min = 14;
        constraints.height.max = 20;

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(CreateElement(styleRef), std::move(constraints));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.rect.w, 25);
        EXPECT_EQ(rect.rect.h, 14);
    }

    TEST_F(LayoutEngineTest, AutoSizedGridContainerWithFrTracksHasNonZeroSize)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.layoutData.grid.column.gap = 4;
        containerStyle.layoutData.grid.row.gap = 4;

        Style cellStyle;
        cellStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        cellStyle.layoutData.grid.row.track = GridTrack::Fixed(24.0f);

        Style headerStyle = cellStyle;
        headerStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        headerStyle.layoutData.grid.row.track = GridTrack::Fixed(24.0f);
        headerStyle.layoutData.grid.column.span = 3;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef cellRef = styleManager.Register(cellStyle);
        StyleRef headerRef = styleManager.Register(headerStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 4001 } }));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        Element header = CreateElement(headerRef, ID{ .value{ .integer = 4002 } });
        header.textID = textSystem.RegisterText(header.id, IconID::Invalid, "Grid Header", headerRef, ElementState::None);
        auto headerResult = storage.Add(std::move(header));
        ASSERT_EQ(headerResult.status, ElementStorageResult::Success);

        for (int i = 0; i < 6; ++i)
        {
            ID id{ .value{ .integer = static_cast<uint64_t>(4100 + i) } };
            Element cell = CreateElement(cellRef, id);
            std::string label = "Cell " + std::to_string(i);
            cell.textID = textSystem.RegisterText(id, IconID::Invalid, label, cellRef, ElementState::None);
            auto cellResult = storage.Add(std::move(cell));
            ASSERT_EQ(cellResult.status, ElementStorageResult::Success);
        }

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parentRect = storage.GetElementRectUnsafe(parentResult.ref);

        EXPECT_GT(parentRect.rect.w, 0);
        EXPECT_GT(parentRect.rect.h, 0);

        // Should at least cover one header row + one data row in this setup.
        EXPECT_GE(parentRect.innerRect.h, 24 + 4 + 24);
    }

    TEST_F(LayoutEngineTest, GridAutoSizeIncludesContainerPaddingAndBorder)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.padding = BoxEdges(7);
        containerStyle.borderWidths = BoxEdges(3);
        containerStyle.layoutData.grid.column.gap = 6;

        Style cell0Style;
        cell0Style.layoutData.grid.column.start = 0;
        cell0Style.layoutData.grid.row.start = 0;
        cell0Style.layoutData.grid.column.track = GridTrack::Fixed(40.0f);
        cell0Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);

        Style cell1Style = cell0Style;
        cell1Style.layoutData.grid.column.track = GridTrack::Fixed(40.0f);
        cell1Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        cell1Style.layoutData.grid.column.start = 1;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef cell0Ref = styleManager.Register(cell0Style);
        StyleRef cell1Ref = styleManager.Register(cell1Style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 5001 } }));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c0Result = storage.Add(CreateElement(cell0Ref, ID{ .value{ .integer = 5002 } }));
        ASSERT_EQ(c0Result.status, ElementStorageResult::Success);
        auto c1Result = storage.Add(CreateElement(cell1Ref, ID{ .value{ .integer = 5003 } }));
        ASSERT_EQ(c1Result.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);

        const int32_t expectedContentWidth = 40 + 6 + 40;
        const int32_t expectedWidth = expectedContentWidth
            + containerStyle.padding.Horizontal()
            + containerStyle.borderWidths.Horizontal()
            + containerStyle.margin.Horizontal();

        EXPECT_EQ(parent.rect.w, expectedWidth);
    }

    TEST_F(LayoutEngineTest, AutoSizedGridContainsSpanningHeaderWidth)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.padding = BoxEdges(6);
        containerStyle.borderWidths = BoxEdges(2);
        containerStyle.layoutData.grid.column.gap = 4;

        Style cellStyle;
        cellStyle.padding = BoxEdges(2);
        cellStyle.borderWidths = BoxEdges(1);
        cellStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        cellStyle.layoutData.grid.row.track = GridTrack::Fixed(24.0f);

        Style headerStyle = cellStyle;
        headerStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        headerStyle.layoutData.grid.row.track = GridTrack::Fixed(24.0f);
        headerStyle.layoutData.grid.column.span = 3;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef cellRef = styleManager.Register(cellStyle);
        StyleRef headerRef = styleManager.Register(headerStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 6001 } }));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        ID headerId{ .value{ .integer = 6002 } };
        Element header = CreateElement(headerRef, headerId);
        header.textID = textSystem.RegisterText(headerId, IconID::Invalid, "Grid Layout Demo", headerRef, ElementState::None);
        auto headerResult = storage.Add(std::move(header));
        ASSERT_EQ(headerResult.status, ElementStorageResult::Success);

        for (int i = 0; i < 6; ++i)
        {
            ID id{ .value{ .integer = static_cast<uint64_t>(6100 + i) } };
            Element cell = CreateElement(cellRef, id);
            std::string text = "Cell " + std::to_string(i);
            cell.textID = textSystem.RegisterText(id, IconID::Invalid, text, cellRef, ElementState::None);
            auto result = storage.Add(std::move(cell));
            ASSERT_EQ(result.status, ElementStorageResult::Success);
        }

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const ElementRect& headerRect = storage.GetElementRectUnsafe(headerResult.ref);

        // Header should be fully contained horizontally inside the parent content area.
        EXPECT_LE(headerRect.rect.x + headerRect.rect.w,
                  parent.innerRect.x + parent.innerRect.w);
    }

    TEST_F(LayoutEngineTest, AutoSizedGridDemoLikeStylesContainChildren)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.SetOverflow(Overflow::Hidden);
        containerStyle.padding = BoxEdges(8);
        containerStyle.borderWidths = BoxEdges(2);
        containerStyle.layoutData.grid.column.gap = 8;
        containerStyle.layoutData.grid.row.gap = 8;

        Style cellStyle;
        cellStyle.padding = BoxEdges(2);
        cellStyle.borderWidths = BoxEdges(2);
        cellStyle.textLayoutStyle.textWrapping = TextWrap::NoWrap;
        cellStyle.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;
        cellStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        cellStyle.layoutData.grid.row.track = GridTrack::Fixed(40.0f);

        Style headerStyle = cellStyle;;
        headerStyle.layoutData.grid.column.span = 3;
        headerStyle.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        headerStyle.layoutData.grid.row.track = GridTrack::Fixed(44.0f);

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef cellRef = styleManager.Register(cellStyle);
        StyleRef headerRef = styleManager.Register(headerStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 7001 } }));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        std::vector<ElementRef> childRefs;

        ID headerId{ .value{ .integer = 7002 } };
        Element header = CreateElement(headerRef, headerId);
        header.textID = textSystem.RegisterText(headerId, IconID::Invalid, "Grid Layout Demo", headerRef, ElementState::None);
        auto headerResult = storage.Add(std::move(header));
        ASSERT_EQ(headerResult.status, ElementStorageResult::Success);
        childRefs.push_back(headerResult.ref);

        const char* labels[] = { "Cell A", "Cell B", "Cell C", "Cell D", "Cell E", "Cell F" };
        for (int i = 0; i < 6; ++i)
        {
            ID id{ .value{ .integer = static_cast<uint64_t>(7100 + i) } };
            Element cell = CreateElement(cellRef, id);
            cell.textID = textSystem.RegisterText(id, IconID::Invalid, labels[i], cellRef, ElementState::None);
            auto result = storage.Add(std::move(cell));
            ASSERT_EQ(result.status, ElementStorageResult::Success);
            childRefs.push_back(result.ref);
        }

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const IntRect bounds = parent.innerRect;

        for (ElementRef childRef : childRefs)
        {
            const ElementRect& child = storage.GetElementRectUnsafe(childRef);
            EXPECT_GE(child.rect.x, bounds.x);
            EXPECT_GE(child.rect.y, bounds.y);
            EXPECT_LE(child.rect.x + child.rect.w, bounds.x + bounds.w);
            EXPECT_LE(child.rect.y + child.rect.h, bounds.y + bounds.h);
        }
    }

    TEST_F(LayoutEngineTest, GridStretchKeepsColumnAndRowGapsVisible)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.layoutData.grid.column.gap = 8;
        containerStyle.layoutData.grid.row.gap = 6;

        Style c00Style;
        c00Style.layoutData.grid.column.start = 0;
        c00Style.layoutData.grid.row.start = 0;
        c00Style.layoutData.grid.column.track = GridTrack::Fixed(30.0f);
        c00Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);

        Style c10Style = c00Style;
        c10Style.layoutData.grid.column.track = GridTrack::Fixed(30.0f);
        c10Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        c10Style.layoutData.grid.column.start = 1;

        Style c01Style = c00Style;
        c01Style.layoutData.grid.column.track = GridTrack::Fixed(30.0f);
        c01Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        c01Style.layoutData.grid.row.start = 1;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef c00Ref = styleManager.Register(c00Style);
        StyleRef c10Ref = styleManager.Register(c10Style);
        StyleRef c01Ref = styleManager.Register(c01Style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parentResult = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 8001 } }));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c00 = storage.Add(CreateElement(c00Ref, ID{ .value{ .integer = 8002 } }));
        ASSERT_EQ(c00.status, ElementStorageResult::Success);
        auto c10 = storage.Add(CreateElement(c10Ref, ID{ .value{ .integer = 8003 } }));
        ASSERT_EQ(c10.status, ElementStorageResult::Success);
        auto c01 = storage.Add(CreateElement(c01Ref, ID{ .value{ .integer = 8004 } }));
        ASSERT_EQ(c01.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& a = storage.GetElementRectUnsafe(c00.ref);
        const ElementRect& b = storage.GetElementRectUnsafe(c10.ref);
        const ElementRect& c = storage.GetElementRectUnsafe(c01.ref);

        EXPECT_EQ(b.rect.x - (a.rect.x + a.rect.w), 8);
        EXPECT_EQ(c.rect.y - (a.rect.y + a.rect.h), 6);
    }

    TEST_F(LayoutEngineTest, FlexColumnWrapSiblingWidthChangeKeepsNestedChildrenAnchored)
    {
        Style rootStyle;
        rootStyle.display = Display::Flex;
        rootStyle.layoutData.flex.direction = FlexDirection::Column;
        rootStyle.layoutData.flex.wrap = FlexWrap::Wrap;
        rootStyle.layoutData.flex.gap = 10;

        Style blockStyle;

        Style gridContainerStyle;
        gridContainerStyle.display = Display::Grid;
        gridContainerStyle.layoutData.grid.column.gap = 6;
        gridContainerStyle.layoutData.grid.row.gap = 6;

        Style gridCellStyle;
        gridCellStyle.layoutData.grid.column.track = GridTrack::Fixed(40.0f);
        gridCellStyle.layoutData.grid.row.track = GridTrack::Fixed(20.0f);

        StyleRef rootRef = styleManager.Register(rootStyle);
        StyleRef blockRef = styleManager.Register(blockStyle);
        StyleRef gridContainerRef = styleManager.Register(gridContainerStyle);
        StyleRef gridCellRef = styleManager.Register(gridCellStyle);

        struct Snapshot
        {
            int32_t containerX = 0;
            int32_t cellX = 0;
            int32_t localX = 0;
        };

        auto BuildAndLayout = [&](int32_t firstWidth) -> Snapshot
        {
            EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

            auto rootResult = storage.Add(CreateElement(rootRef, ID{ .value{ .integer = 20001 } }),
                                          LayoutOverride::FixedSize(480, 120));
            EXPECT_EQ(rootResult.status, ElementStorageResult::Success);

            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

            auto firstResult = storage.Add(CreateElement(blockRef, ID{ .value{ .integer = 20002 } }),
                                           LayoutOverride::FixedSize(CssPx(firstWidth), 50));
            EXPECT_EQ(firstResult.status, ElementStorageResult::Success);

            auto secondResult = storage.Add(CreateElement(blockRef, ID{ .value{ .integer = 20003 } }),
                                            LayoutOverride::FixedSize(100, 50));
            EXPECT_EQ(secondResult.status, ElementStorageResult::Success);

            auto containerResult = storage.Add(CreateElement(gridContainerRef, ID{ .value{ .integer = 20004 } }),
                                               LayoutOverride::FixedSize(140, 80));
            EXPECT_EQ(containerResult.status, ElementStorageResult::Success);

            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

            auto cellAResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 20005 } }),
                                           LayoutOverride::FixedSize(40, 20));
            EXPECT_EQ(cellAResult.status, ElementStorageResult::Success);

            auto cellBResult = storage.Add(CreateElement(gridCellRef, ID{ .value{ .integer = 20006 } }),
                                           LayoutOverride::FixedSize(40, 20));
            EXPECT_EQ(cellBResult.status, ElementStorageResult::Success);

            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
            EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

            layoutEngine.Layout(*osWindowManager, storage);

            const ElementRect& containerRect = storage.GetElementRectUnsafe(containerResult.ref);
            const ElementRect& cellBRect = storage.GetElementRectUnsafe(cellBResult.ref);
            return Snapshot
            {
                .containerX = containerRect.rect.x,
                .cellX = cellBRect.rect.x,
                .localX = cellBRect.rect.x - containerRect.rect.x
            };
        };

        const Snapshot narrow = BuildAndLayout(80);
        const Snapshot wide = BuildAndLayout(170);

        // Wider first item should push the next wrapped column to the right.
        EXPECT_GT(wide.containerX, narrow.containerX);

        // Nested child should keep its local position within the container.
        EXPECT_EQ(wide.localX, narrow.localX);

        // Child should move by exactly the same delta as its parent container.
        EXPECT_EQ(wide.cellX - narrow.cellX, wide.containerX - narrow.containerX);
    }

    TEST_F(LayoutEngineTest, GridAutoZeroIntrinsicTracksConsumeFreeSpaceEvenly)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.layoutData.grid.column.gap = 0;
        containerStyle.layoutData.grid.row.gap = 0;

        Style c0Style;
        c0Style.layoutData.grid.column.start = 0;
        c0Style.layoutData.grid.row.start = 0;
        c0Style.layoutData.grid.column.track = GridTrack::Auto();
        c0Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);

        Style c1Style = c0Style;
        c1Style.layoutData.grid.column.track = GridTrack::Auto();
        c1Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        c1Style.layoutData.grid.column.start = 1;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef c0Ref = styleManager.Register(c0Style);
        StyleRef c1Ref = styleManager.Register(c1Style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 30001 } }), LayoutOverride::FixedSize(120, 40));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c0 = storage.Add(CreateElement(c0Ref, ID{ .value{ .integer = 30002 } }), LayoutOverride::FixedSize(0, 0));
        auto c1 = storage.Add(CreateElement(c1Ref, ID{ .value{ .integer = 30003 } }), LayoutOverride::FixedSize(0, 0));
        ASSERT_EQ(c0.status, ElementStorageResult::Success);
        ASSERT_EQ(c1.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& r0 = storage.GetElementRectUnsafe(c0.ref);
        const ElementRect& r1 = storage.GetElementRectUnsafe(c1.ref);

        // The items keep their set (zero) width; the tracks split the width evenly.
        EXPECT_EQ(r0.rect.x, 0);
        EXPECT_EQ(r1.rect.x, 60);
        EXPECT_EQ(r0.rect.w, 0);
        EXPECT_EQ(r1.rect.w, 0);
    }

    TEST_F(LayoutEngineTest, GridFrTracksDistributeRemainderToFirstTrack)
    {
        Style containerStyle;
        containerStyle.display = Display::Grid;
        containerStyle.layoutData.grid.column.gap = 0;
        containerStyle.layoutData.grid.row.gap = 0;

        Style c0Style;
        c0Style.layoutData.grid.column.start = 0;
        c0Style.layoutData.grid.row.start = 0;
        c0Style.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        c0Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);

        Style c1Style = c0Style;
        c1Style.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        c1Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        c1Style.layoutData.grid.column.start = 1;

        Style c2Style = c0Style;
        c2Style.layoutData.grid.column.track = GridTrack::Fractional(1.0f);
        c2Style.layoutData.grid.row.track = GridTrack::Fixed(20.0f);
        c2Style.layoutData.grid.column.start = 2;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef c0Ref = styleManager.Register(c0Style);
        StyleRef c1Ref = styleManager.Register(c1Style);
        StyleRef c2Ref = styleManager.Register(c2Style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(containerRef, ID{ .value{ .integer = 30101 } }), LayoutOverride::FixedSize(100, 40));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto c0 = storage.Add(CreateElement(c0Ref, ID{ .value{ .integer = 30102 } }));
        auto c1 = storage.Add(CreateElement(c1Ref, ID{ .value{ .integer = 30103 } }));
        auto c2 = storage.Add(CreateElement(c2Ref, ID{ .value{ .integer = 30104 } }));
        ASSERT_EQ(c0.status, ElementStorageResult::Success);
        ASSERT_EQ(c1.status, ElementStorageResult::Success);
        ASSERT_EQ(c2.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& r0 = storage.GetElementRectUnsafe(c0.ref);
        const ElementRect& r1 = storage.GetElementRectUnsafe(c1.ref);
        const ElementRect& r2 = storage.GetElementRectUnsafe(c2.ref);

        EXPECT_EQ(r0.rect.w, 34);
        EXPECT_EQ(r1.rect.w, 33);
        EXPECT_EQ(r2.rect.w, 33);

        EXPECT_EQ(r0.rect.x, 0);
        EXPECT_EQ(r1.rect.x, 34);
        EXPECT_EQ(r2.rect.x, 67);
    }

    TEST_F(LayoutEngineTest, OverflowAutoClampsScrollOffsetWhenContentFits)
    {
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.SetOverflow(Overflow::Auto);

        Style childStyle;

        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 31001 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId), LayoutOverride::FixedSize(100, 50));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(CreateElement(childRef, ID{ .value{ .integer = 31002 } }), LayoutOverride::FixedSize(40, 20));
        ASSERT_EQ(child.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(parentId);
        scrollManagement.SetScrollOffset(handle, int2{ -25, 13 });

        layoutEngine.Layout(*osWindowManager, storage);

        const int2 clampedOffset = scrollManagement.GetScrollOffset(handle);
        EXPECT_EQ(clampedOffset.x, 0);
        EXPECT_EQ(clampedOffset.y, 0);
    }

    TEST_F(LayoutEngineTest, OverflowAutoClampsScrollOffsetToScrollableRange)
    {
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        parentStyle.SetOverflow(Overflow::Auto);

        Style childStyle;

        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 31101 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId), LayoutOverride::FixedSize(100, 50));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto child = storage.Add(CreateElement(childRef, ID{ .value{ .integer = 31102 } }), LayoutOverride::FixedSize(300, 20));
        ASSERT_EQ(child.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(parentId);
        scrollManagement.SetScrollOffset(handle, int2{ -500, 0 });

        layoutEngine.Layout(*osWindowManager, storage);

        // Scroll range in X is 300 - 100 = 200, so offset clamps to -200.
        const int2 clampedOffset = scrollManagement.GetScrollOffset(handle);
        EXPECT_EQ(clampedOffset.x, -200);
        EXPECT_EQ(clampedOffset.y, 0);
    }

    TEST_F(LayoutEngineTest, AbsolutePositionConstraintsOffsetIsApplied)
    {
        Style style;
        style.margin = { 0, 0, 0, 0 };
        style.padding = { 0, 0, 0, 0 };
        style.borderWidths = { 0, 0, 0, 0 };
        StyleRef styleRef = styleManager.Register(style);

        Element element = CreateElement(styleRef);

        LayoutOverride position = LayoutOverride{}.Absolute(EdgeInset{ 9, 7, 0, 11 });

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(std::move(element), std::move(position.Size(30, 12)));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.constraints.x.type, PositionType::Absolute);
        EXPECT_EQ(rect.constraints.y.type, PositionType::Absolute);
        EXPECT_EQ(rect.rect.x, 11);
        EXPECT_EQ(rect.rect.y, 9);
        EXPECT_EQ(rect.rect.w, 30);
        EXPECT_EQ(rect.rect.h, 12);
    }

    // A far-side inset (right / bottom) is measured from the target's far edge.
    TEST(PositionConstraintTest, FarInsetIsMeasuredFromTheFarEdge)
    {
        const IntRect target{ 100, 50, 200, 100 };
        auto placedX = [&](PositionConstraintAxis axis) {
            IntRect r{ 0, 0, 30, 12 };
            axis.Apply(r, 0, target);
            return r.x;
        };
        // 10px in from the right edge: 100 + 200 - 30 - 10.
        EXPECT_EQ(placedX(PositionConstraintAxis::Absolute(PositionConstraintAxis::kAuto, 10)), 260);
        EXPECT_EQ(placedX(PositionConstraintAxis::Fixed(PositionConstraintAxis::kAuto, 10)), 260);
        EXPECT_EQ(placedX(PositionConstraintAxis::Absolute(5)), 105) << "near insets are unchanged";
    }

    // Relative: right: 10 shifts the box 10px left of its laid-out position.
    TEST(PositionConstraintTest, RelativeFarOffsetShiftsByTheOffset)
    {
        IntRect r{ 40, 0, 30, 12 };
        PositionConstraintAxis{ .type = PositionType::Relative, .far = 10 }.Apply(r, 0, IntRect{ 0, 0, 200, 100 });
        EXPECT_EQ(r.x, 30);
    }

    // -1 is a real offset, not "unset".
    TEST(PositionConstraintTest, MinusOneIsARealInset)
    {
        const IntRect target{ 100, 50, 200, 100 };
        IntRect abs{ 0, 0, 30, 12 };
        PositionConstraintAxis::Absolute(-1).Apply(abs, 0, target);
        EXPECT_EQ(abs.x, 99);

        IntRect farAbs{ 0, 0, 30, 12 };
        PositionConstraintAxis::Absolute(PositionConstraintAxis::kAuto, -1).Apply(farAbs, 0, target);
        EXPECT_EQ(farAbs.x, 100 + 200 - 30 + 1);

        IntRect rel{ 40, 0, 30, 12 };
        PositionConstraintAxis::Relative(-1).Apply(rel, 0, target);
        EXPECT_EQ(rel.x, 39);
    }

    // The node view places nodes with Absolute(node.pos): a node at (-1, -1)
    // sits one pixel up-left of the canvas origin, not snapped to it.
    TEST_F(LayoutEngineTest, AbsoluteChildAtMinusOneKeepsItsPosition)
    {
        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        (void)storage.Add(CreateElement(defaultStyleIndex), LayoutOverride{}.Absolute(50, 50).Size(200, 200));
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        const ElementRef node = storage.Add(CreateElement(defaultStyleIndex),
                                            LayoutOverride{}.Absolute(int2{ -1, -1 }).Size(20, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
        layoutEngine.Layout(*osWindowManager, storage);

        const auto& r = storage.GetElementRectUnsafe(node).rect;
        EXPECT_EQ(r.x, 49);
        EXPECT_EQ(r.y, 49);
    }

    TEST_F(LayoutEngineTest, AnchoredPositionConstraintsStoresAnchorAndAppliesOffset)
    {
        Style style;
        style.margin = { 0, 0, 0, 0 };
        style.padding = { 0, 0, 0, 0 };
        style.borderWidths = { 0, 0, 0, 0 };
        StyleRef styleRef = styleManager.Register(style);

        Element element = CreateElement(styleRef);

        LayoutOverride position = LayoutOverride{}.Anchored(ID::Invalid(), AnchorPlacement::OutsideBottomRight, 5, 8);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(std::move(element), std::move(position.Size(30, 12)));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.constraints.x.type, PositionType::Anchored);
        EXPECT_EQ(rect.constraints.y.type, PositionType::Anchored);
        EXPECT_EQ(rect.constraints.x.anchorPlacement, GetHorizontalAxis(AnchorPlacement::OutsideBottomRight));
        EXPECT_EQ(rect.constraints.y.anchorPlacement, GetVerticalAxis(AnchorPlacement::OutsideBottomRight));
        EXPECT_EQ(rect.rect.x, 5);
        EXPECT_EQ(rect.rect.y, 8);
        EXPECT_EQ(rect.rect.w, 30);
        EXPECT_EQ(rect.rect.h, 12);
    }


    TEST_F(LayoutEngineTest, MixedPositionConstraints_AbsoluteX_RelativeY_PinsXFromParent)
    {
        // AbsoluteX pins the horizontal position to parent origin + inset; Y is unchanged (typeY=Relative, no inset).
        Style style;
        style.margin = { 0, 0, 0, 0 };
        style.padding = { 0, 0, 0, 0 };
        style.borderWidths = { 0, 0, 0, 0 };
        StyleRef styleRef = styleManager.Register(style);

        LayoutOverride position = LayoutOverride{}.Relative().WithTypeX(PositionType::Absolute);
        position.x.near = 37;

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(CreateElement(styleRef), std::move(position.Size(30, 12)));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.constraints.x.type, PositionType::Absolute);
        EXPECT_EQ(rect.constraints.y.type, PositionType::Relative);
        EXPECT_EQ(rect.rect.x, 37); // pinned by absolute X
        EXPECT_EQ(rect.rect.y, 0);  // relative Y, no inset -> unchanged
        EXPECT_EQ(rect.rect.w, 30);
        EXPECT_EQ(rect.rect.h, 12);
    }

    TEST_F(LayoutEngineTest, MixedPositionConstraints_RelativeX_AbsoluteY_PinsYFromParent)
    {
        // AbsoluteY pins the vertical position to parent origin + inset; X is unchanged (typeX=Relative, no inset).
        Style style;
        style.margin = { 0, 0, 0, 0 };
        style.padding = { 0, 0, 0, 0 };
        style.borderWidths = { 0, 0, 0, 0 };
        StyleRef styleRef = styleManager.Register(style);

        LayoutOverride position = LayoutOverride{}.Relative().WithTypeY(PositionType::Absolute);
        position.y.near = 53;

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addResult = storage.Add(CreateElement(styleRef), std::move(position.Size(30, 12)));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.constraints.x.type, PositionType::Relative);
        EXPECT_EQ(rect.constraints.y.type, PositionType::Absolute);
        EXPECT_EQ(rect.rect.x, 0);  // relative X, no inset -> unchanged
        EXPECT_EQ(rect.rect.y, 53); // pinned by absolute Y
        EXPECT_EQ(rect.rect.w, 30);
        EXPECT_EQ(rect.rect.h, 12);
    }

    TEST_F(LayoutEngineTest, MixedPositionConstraints_CenteredX_RelativeY_CentersHorizontallyInParent)
    {
        // CenteredX centers the child horizontally within the parent; Y remains flex-placed.
        Style zeroStyle;
        zeroStyle.margin = { 0, 0, 0, 0 };
        zeroStyle.padding = { 0, 0, 0, 0 };
        zeroStyle.borderWidths = { 0, 0, 0, 0 };
        StyleRef zeroStyleRef = styleManager.Register(zeroStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(zeroStyleRef), LayoutOverride::FixedSize(200, 100));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        LayoutOverride childPos = LayoutOverride{}.Relative().WithTypeX(PositionType::Centered);
        auto childResult = storage.Add(CreateElement(zeroStyleRef), std::move(childPos.Size(40, 20)));
        ASSERT_EQ(childResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const ElementRect& child  = storage.GetElementRectUnsafe(childResult.ref);

        // X is centered: parent.x + (parent.w - child.w) / 2 = 0 + (200 - 40) / 2 = 80
        EXPECT_EQ(child.rect.x, parent.rect.x + (parent.rect.w - child.rect.w) / 2);
        EXPECT_EQ(child.rect.w, 40);
        EXPECT_EQ(child.rect.h, 20);
    }

    TEST_F(LayoutEngineTest, MixedPositionConstraints_RelativeX_CenteredY_CentersVerticallyInParent)
    {
        // CenteredY centers the child vertically within the parent; X remains flex-placed.
        Style zeroStyle;
        zeroStyle.margin = { 0, 0, 0, 0 };
        zeroStyle.padding = { 0, 0, 0, 0 };
        zeroStyle.borderWidths = { 0, 0, 0, 0 };
        StyleRef zeroStyleRef = styleManager.Register(zeroStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentResult = storage.Add(CreateElement(zeroStyleRef), LayoutOverride::FixedSize(200, 100));
        ASSERT_EQ(parentResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        LayoutOverride childPos = LayoutOverride{}.Relative().WithTypeY(PositionType::Centered);
        auto childResult = storage.Add(CreateElement(zeroStyleRef), std::move(childPos.Size(40, 20)));
        ASSERT_EQ(childResult.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& parent = storage.GetElementRectUnsafe(parentResult.ref);
        const ElementRect& child  = storage.GetElementRectUnsafe(childResult.ref);

        // Y is centered: parent.y + (parent.h - child.h) / 2 = 0 + (100 - 20) / 2 = 40
        EXPECT_EQ(child.rect.y, parent.rect.y + (parent.rect.h - child.rect.h) / 2);
        EXPECT_EQ(child.rect.w, 40);
        EXPECT_EQ(child.rect.h, 20);
    }

    TEST_F(LayoutEngineTest, WindowFixedWidthUsedAsContentPlusPaddingAndBorder)
    {
        Style menuStyle;
        menuStyle.padding = tachyon::BoxEdges(10);
        menuStyle.borderWidths = tachyon::BoxEdges(2);
        StyleRef menuRef = styleManager.Register(menuStyle);

        Style buttonStyle;
        buttonStyle.padding = tachyon::BoxEdges(0);
        buttonStyle.borderWidths = tachyon::BoxEdges(0);
        StyleRef buttonRef = styleManager.Register(buttonStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        Element menu = CreateElement(menuRef, ID{ .value{.integer = 5001 } });
        auto menuRes = storage.Add(std::move(menu));
        ASSERT_EQ(menuRes.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        Element btn = CreateElement(buttonRef, ID{ .value{.integer = 5002 } });
        auto btnRes = storage.Add(std::move(btn), LayoutOverride::FixedSize(80, 12));
        ASSERT_EQ(btnRes.status, ElementStorageResult::Success);

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& p = storage.GetElementRectUnsafe(menuRes.ref);
        const ElementRect& b = storage.GetElementRectUnsafe(btnRes.ref);

        // child width should be the fixed width
        EXPECT_EQ(b.rect.w, 80);

        // parent width should include child's width + padding + border widths
        int expectedParentBorderBox = 80 + menuStyle.padding.Horizontal() + menuStyle.borderWidths.Horizontal();
        EXPECT_EQ(p.rect.w, expectedParentBorderBox + menuStyle.margin.Horizontal());
    }

    // ----------------------------------------------------------------------
    // Sticky positioning
    //
    // A Sticky child is in-flow (flex/grid reserves space for it like Relative)
    // but the parent's scroll cascade is clamped on the sticky axis so the
    // child can't be pushed past the parent's innerRect near edge.
    // ----------------------------------------------------------------------

    TEST_F(LayoutEngineTest, StickyChildSitsAtNaturalPositionWhenParentNotScrolled)
    {
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        parentStyle.SetOverflow(Overflow::Auto);
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50001 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 100));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyY().Size(100, 20));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        auto tall = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(100, 300));
        ASSERT_EQ(tall.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
        const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
        EXPECT_EQ(s.rect.y, p.innerRect.y);
    }

    TEST_F(LayoutEngineTest, StickyChildPinsToParentInnerTopWhenScrolledPastIt)
    {
        // Scroll the body up by 100 px. A sticky child at y=0 should not move:
        // the cascade clamp pins it at parent.innerRect.y.
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        parentStyle.SetOverflow(Overflow::Auto);
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50101 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 100));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyY().Size(100, 20));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        auto tall = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(100, 300));
        ASSERT_EQ(tall.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(parentId);
        scrollManagement.SetScrollOffset(handle, int2{ 0, -100 });
        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
        const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
        EXPECT_EQ(s.rect.y, p.innerRect.y)
            << "sticky pinned y=" << s.rect.y << " expected " << p.innerRect.y;
    }

    TEST_F(LayoutEngineTest, StickyChildScrollsNaturallyUntilThreshold)
    {
        // Sticky element placed AFTER a 50 px tall sibling, so its in-flow y
        // is parent.innerRect.y + 50. Small scroll: it scrolls naturally.
        // Scroll past 50 px: it pins to parent.innerRect.y.
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        parentStyle.SetOverflow(Overflow::Auto);
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50201 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 200));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        // Spacer above the sticky so its natural y = innerRect.y + 50.
        auto spacer = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(100, 50));
        ASSERT_EQ(spacer.status, ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyY().Size(100, 20));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        auto tall = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(100, 400));
        ASSERT_EQ(tall.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(parentId);

        // Small scroll (-30): not yet at the pin point. Sticky still moves.
        scrollManagement.SetScrollOffset(handle, int2{ 0, -30 });
        layoutEngine.Layout(*osWindowManager, storage);
        {
            const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
            const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
            EXPECT_EQ(s.rect.y, p.innerRect.y + 20)
                << "small scroll should let sticky drift; y=" << s.rect.y
                << " expected " << (p.innerRect.y + 20);
        }

        // Big scroll (-100): exceeds the natural offset. Sticky pins to top.
        scrollManagement.SetScrollOffset(handle, int2{ 0, -100 });
        layoutEngine.Layout(*osWindowManager, storage);
        {
            const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
            const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
            EXPECT_EQ(s.rect.y, p.innerRect.y)
                << "big scroll should pin sticky; y=" << s.rect.y
                << " expected " << p.innerRect.y;
        }
    }

    TEST_F(LayoutEngineTest, StickyThresholdOffsetsTheClampEdge)
    {
        constexpr int16_t kThreshold = 8;
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        parentStyle.SetOverflow(Overflow::Auto);
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50301 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 100));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyY(kThreshold).Size(100, 20));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        auto tall = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(100, 300));
        ASSERT_EQ(tall.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(parentId);
        scrollManagement.SetScrollOffset(handle, int2{ 0, -100 });
        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
        const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
        EXPECT_EQ(s.rect.y, p.innerRect.y + kThreshold);
    }

    TEST_F(LayoutEngineTest, StickyChildIsInFlowAndConsumesSpace)
    {
        // Sibling below the sticky element must sit BELOW the sticky's bottom
        // edge - distinguishes Sticky (in-flow) from Fixed/Absolute.
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        parentStyle.SetOverflow(Overflow::Auto);
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50401 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 200));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyY().Size(100, 30));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        auto tall = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(100, 100));
        ASSERT_EQ(tall.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
        const ElementRect& t = storage.GetElementRectUnsafe(tall.ref);
        EXPECT_EQ(t.rect.y, s.rect.y + s.rect.h);
    }

    TEST_F(LayoutEngineTest, StickyChildWithoutScrollableAncestorBehavesAsRelative)
    {
        // No scrollable ancestor, no scroll cascade. The clamp is a no-op and
        // the element sits at its natural in-flow y.
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Column;
        // No SetOverflow(Auto): parent is not scrollable.
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50501 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 200));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyY().Size(100, 20));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
        const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
        EXPECT_EQ(s.rect.y, p.innerRect.y);
    }

    TEST_F(LayoutEngineTest, StickyXClampsAgainstHorizontalScroll)
    {
        Style parentStyle;
        parentStyle.display = Display::Flex;
        parentStyle.layoutData.flex.direction = FlexDirection::Row;
        parentStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        parentStyle.SetOverflow(Overflow::Auto);
        Style childStyle;
        StyleRef parentRef = styleManager.Register(parentStyle);
        StyleRef childRef  = styleManager.Register(childStyle);

        ID parentId{ .value{ .integer = 50601 } };

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto parent = storage.Add(CreateElement(parentRef, parentId),
                                  LayoutOverride::FixedSize(100, 50));
        ASSERT_EQ(parent.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto sticky = storage.Add(CreateElement(childRef),
                                  LayoutOverride{}.StickyX().Size(20, 50));
        ASSERT_EQ(sticky.status, ElementStorageResult::Success);
        // Wide sibling so the parent has a horizontal scroll range.
        auto wide = storage.Add(CreateElement(childRef), LayoutOverride::FixedSize(300, 50));
        ASSERT_EQ(wide.status, ElementStorageResult::Success);
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(parentId);
        scrollManagement.SetScrollOffset(handle, int2{ -100, 0 });
        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& p = storage.GetElementRectUnsafe(parent.ref);
        const ElementRect& s = storage.GetElementRectUnsafe(sticky.ref);
        EXPECT_EQ(s.rect.x, p.innerRect.x);
    }
}