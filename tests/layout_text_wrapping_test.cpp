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

namespace GUI
{
    using namespace testing;
    using ::testing::NiceMock;
    using GUITest::MockWindowBackend;

    // charWidth=8, lineHeight=16 — the StubTextSystem defaults used throughout these tests.
    static constexpr int32_t kCharWidth = 8;
    static constexpr int32_t kLineHeight = 16;

    class TextWrappingLayoutTest : public ::testing::Test
    {
    protected:
        GradientManager gradientManager{ 256 };
        StyleManager    styleManager{ gradientManager };
        StubTextSystem  textSystem;
        ScrollManagement scrollManagement;
        LayoutEngine    layoutEngine{ textSystem, scrollManagement };
        ElementStorage  storage{ styleManager };

        MockWindowBackend* mockBackend;
        std::unique_ptr<OSWindowManager> osWindowManager;

        void SetUp() override
        {
            mockBackend = new NiceMock<MockWindowBackend>();
            osWindowManager = std::make_unique<OSWindowManager>(nullptr, mockBackend);
        }

        Element MakeTextElement(StyleRef styleRef, ID id, std::string_view text)
        {
            Element elem;
            elem.id = id;
            elem.styleRef = styleRef;
            elem.textID = textSystem.RegisterText(id, IconID::Invalid, text, styleRef, ElementState::None);
            return elem;
        }
    };

    // ---------------------------------------------------------------------------
    // Minimum element sizing from text content
    // ---------------------------------------------------------------------------

    // An auto-sized leaf with text uses the text system's minWidth (= one char) for its width.
    TEST_F(TextWrappingLayoutTest, AutoSizedTextLeaf_Width_IsMinimumCharWidth)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 1} };
        auto r = storage.Add(MakeTextElement(styleRef, id, "Hello World"));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_EQ(rect.rect.w, kCharWidth);
    }

    // An auto-sized leaf with text uses the text system's minHeight (= one line) for its height.
    TEST_F(TextWrappingLayoutTest, AutoSizedTextLeaf_Height_IsSingleLineHeight)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 2} };
        auto r = storage.Add(MakeTextElement(styleRef, id, "Hello World"));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_EQ(rect.rect.h, kLineHeight);
    }

    // Padding and border are added on top of the text's minimum dimensions.
    TEST_F(TextWrappingLayoutTest, AutoSizedTextLeaf_StyleDimensionsContributeTo_TotalSize)
    {
        const int32_t padding = 4;
        const int32_t border = 2;
        const int32_t margin = 3;

        Style style;
        style.padding = BoxEdges(padding);
        style.borderWidths = BoxEdges(border);
        style.margin = BoxEdges(margin);
        StyleRef styleRef = styleManager.Register(style);

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 3} };
        auto r = storage.Add(MakeTextElement(styleRef, id, "Hello"));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);

        // rect is the border box; margin is stripped off into rect.x/y
        EXPECT_EQ(rect.rect.x, margin);
        EXPECT_EQ(rect.rect.y, margin);
        EXPECT_EQ(rect.rect.w, padding * 2 + border * 2 + kCharWidth);
        EXPECT_EQ(rect.rect.h, padding * 2 + border * 2 + kLineHeight);
    }

    // A fixed-width element with a long label stays one line tall: layout doesn't
    // grow it for wrapped text; callers can size from contentBounds.
    TEST_F(TextWrappingLayoutTest, FixedWidthTextLeaf_Height_IsNotGrownByWrapping)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        // 50 chars, needs 400px to fit on one line, but the element is only 80px wide
        const std::string longText(50, 'x');

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 4} };
        auto r = storage.Add(MakeTextElement(styleRef, id, longText),
            LayoutOverride::FixedWidth(80));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_EQ(rect.rect.w, 80);
        // Height is still one line — wrapping does not inflate the element rect
        EXPECT_EQ(rect.rect.h, kLineHeight);
    }

    // ---------------------------------------------------------------------------
    // contentBounds reflects actual (wrapped) text dimensions
    // ---------------------------------------------------------------------------

    // When text overflows the element width, contentBounds captures the full wrapped
    // height so the caller can set up scrolling or measure overflow.
    TEST_F(TextWrappingLayoutTest, FixedWidthTextLeaf_ContentBounds_ReflectsWrappedHeight)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        // "Hello World Hello World" = 23 chars = 184px unwrapped.
        // constrained to 100px → charsPerLine = 100/8 = 12 → ceil(23/12) = 2 lines → 32px tall.
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 5} };
        auto r = storage.Add(MakeTextElement(styleRef, id, "Hello World Hello World"),
            LayoutOverride::FixedWidth(100));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_EQ(rect.rect.h, kLineHeight);    // element rect: one line
        EXPECT_EQ(rect.contentBounds.h, 2 * kLineHeight); // content: two wrapped lines
    }

    // When the text fits within the element width, contentBounds height equals one line.
    TEST_F(TextWrappingLayoutTest, FixedWidthTextLeaf_ContentBounds_SingleLine_WhenTextFits)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        // "Hello" = 5 chars = 40px, fits easily in 200px
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 6} };
        auto r = storage.Add(MakeTextElement(styleRef, id, "Hello"),
            LayoutOverride::FixedWidth(200));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_EQ(rect.contentBounds.h, kLineHeight);
    }

    // contentBounds is never smaller than the element's inner rect.
    TEST_F(TextWrappingLayoutTest, FixedSizeTextLeaf_ContentBounds_IsAtLeastInnerRectSize)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        // Short text, large element — content fits comfortably inside
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 7} };
        auto r = storage.Add(MakeTextElement(styleRef, id, "Hi"),
            LayoutOverride::FixedSize(200, 80));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_GE(rect.contentBounds.w, rect.innerRect.w);
        EXPECT_GE(rect.contentBounds.h, rect.innerRect.h);
    }

    // A longer text in the same fixed-width container results in a taller contentBounds
    // than a shorter text, because more lines are needed.
    TEST_F(TextWrappingLayoutTest, LongerText_InNarrowContainer_Has_TallerContentBounds)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        // Both elements are 80px wide. "Short" = 5 chars (1 line). "LongLongLongLong" = 16 chars (2 lines).
        // charsPerLine = 80/8 = 10. ceil(5/10)=1 line. ceil(16/10)=2 lines.
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        ID shortId{ .value{.integer = 8} };
        auto shortR = storage.Add(MakeTextElement(styleRef, shortId, "Short"),
            LayoutOverride::FixedWidth(80));
        ASSERT_EQ(shortR.status, ElementStorageResult::Success);

        ID longId{ .value{.integer = 9} };
        auto longR = storage.Add(MakeTextElement(styleRef, longId, "LongLongLongLong"),
            LayoutOverride::FixedWidth(80));
        ASSERT_EQ(longR.status, ElementStorageResult::Success);

        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& shortRect = storage.GetElementRectUnsafe(shortR.ref);
        const ElementRect& longRect = storage.GetElementRectUnsafe(longR.ref);

        EXPECT_EQ(shortRect.contentBounds.h, 1 * kLineHeight);
        EXPECT_EQ(longRect.contentBounds.h, 2 * kLineHeight);
        EXPECT_LT(shortRect.contentBounds.h, longRect.contentBounds.h);
    }

    // ---------------------------------------------------------------------------
    // Text in flex layout
    // ---------------------------------------------------------------------------

    // Text items in a flex row get their minimum (1-char) width, not the unwrapped
    // text width, leaving the container to distribute the rest.
    TEST_F(TextWrappingLayoutTest, FlexRow_TextChildren_Width_IsMinimumIntrinsicWidth)
    {
        Style containerStyle;
        containerStyle.display = Display::Flex;
        containerStyle.layoutData.flex.direction = FlexDirection::Row;
        containerStyle.layoutData.flex.wrap = FlexWrap::NoWrap;
        containerStyle.padding = BoxEdges(0);
        containerStyle.borderWidths = BoxEdges(0);

        Style itemStyle;
        itemStyle.padding = BoxEdges(0);
        itemStyle.borderWidths = BoxEdges(0);

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef itemRef = styleManager.Register(itemStyle);

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentR = storage.Add(
            [&] { Element e; e.id = ID{ .value{.integer = 10} }; e.styleRef = containerRef; return e; }(),
            LayoutOverride::FixedWidth(400));
        ASSERT_EQ(parentR.status, ElementStorageResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        std::vector<ElementRef> itemRefs;
        const char* labels[] = { "Short", "A Much Longer Label" };
        for (int i = 0; i < 2; ++i)
        {
            ID id{ .value{.integer = static_cast<uint64_t>(11 + i)} };
            auto r = storage.Add(MakeTextElement(itemRef, id, labels[i]));
            ASSERT_EQ(r.status, ElementStorageResult::Success);
            itemRefs.push_back(r.ref);
        }

        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Both items have the same minimum width (one char) — their text length
        // does not affect the element width without explicit size constraints.
        const ElementRect& r0 = storage.GetElementRectUnsafe(itemRefs[0]);
        const ElementRect& r1 = storage.GetElementRectUnsafe(itemRefs[1]);
        EXPECT_EQ(r0.rect.w, kCharWidth);
        EXPECT_EQ(r1.rect.w, kCharWidth);
    }

    // Text items in a flex column are all one line tall regardless of text length.
    TEST_F(TextWrappingLayoutTest, FlexColumn_TextChildren_Height_IsOneLineRegardlessOfTextLength)
    {
        Style containerStyle;
        containerStyle.display = Display::Flex;
        containerStyle.layoutData.flex.direction = FlexDirection::Column;
        containerStyle.padding = BoxEdges(0);
        containerStyle.borderWidths = BoxEdges(0);

        Style itemStyle;
        itemStyle.padding = BoxEdges(0);
        itemStyle.borderWidths = BoxEdges(0);
        itemStyle.textLayoutStyle.textWrapping = TextWrap::Wrap;

        StyleRef containerRef = styleManager.Register(containerStyle);
        StyleRef itemRef = styleManager.Register(itemStyle);

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto parentR = storage.Add(
            [&] { Element e; e.id = ID{ .value{.integer = 20} }; e.styleRef = containerRef; return e; }(),
            LayoutOverride::FixedWidth(80));
        ASSERT_EQ(parentR.status, ElementStorageResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        std::vector<ElementRef> itemRefs;
        const char* labels[] = { "Hi", "A Much Longer Label That Should Wrap" };
        for (int i = 0; i < 2; ++i)
        {
            ID id{ .value{.integer = static_cast<uint64_t>(21 + i)} };
            auto r = storage.Add(MakeTextElement(itemRef, id, labels[i]));
            ASSERT_EQ(r.status, ElementStorageResult::Success);
            itemRefs.push_back(r.ref);
        }

        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // All items are one line tall — wrap mode does not auto-size element height
        for (ElementRef ref : itemRefs)
        {
            const ElementRect& item = storage.GetElementRectUnsafe(ref);
            EXPECT_EQ(item.rect.h, kLineHeight);
        }
    }

    // An explicitly fixed-height text element has the requested height even though
    // the text would require more lines to display fully.
    TEST_F(TextWrappingLayoutTest, FixedSizeTextLeaf_RectMatchesConstraints_IgnoresTextLength)
    {
        Style style;
        style.padding = BoxEdges(0);
        style.borderWidths = BoxEdges(0);
        StyleRef styleRef = styleManager.Register(style);

        const std::string longText(40, 'A'); // 40 chars = 320px, but element is 60x50

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ID id{ .value{.integer = 30} };
        auto r = storage.Add(MakeTextElement(styleRef, id, longText),
            LayoutOverride::FixedSize(60, 50));
        ASSERT_EQ(r.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& rect = storage.GetElementRectUnsafe(r.ref);
        EXPECT_EQ(rect.rect.w, 60);
        EXPECT_EQ(rect.rect.h, 50);
    }
}