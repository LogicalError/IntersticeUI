// Wrap-mode text measures its natural glyph extent, not the layout box handed to
// skribidi (16384 px), which would push following siblings out of their clip rect.

#include <gtest/gtest.h>

#include <gui_core/text/gui_text_system.h>
#include <gui_core/text/gui_text_atlas_manager.h>
#include <gui_core/text/gui_clipboard.h>
#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/state/gui_state_manager.h>

#include <climits>
#include <memory>

namespace GUI
{
    namespace
    {
        // Never invoked (measurement doesn't rasterize), but Initialize needs one.
        class DummyTextAtlasManager : public TextAtlasManager
        {
        public:
            bool Initialize(GraphicsContext*, ImageSet*) override { return true; }
            TextureIndex CreateTexture(const uint8_t*, uint2, uint8_t) override { return TextureIndex::Invalid; }
            void UpdateTexture(TextureIndex, const uint8_t*, uint2) override {}
            ImageIndex GetImageIndex(TextureIndex) override { return ImageIndex{ 0 }; }
            void SyncTextureIndices() override {}
        };

        class DummyClipboard : public IClipboard
        {
        public:
            std::string Get() const override { return {}; }
            void Set(std::string_view) override {}
        };

        class SkribidiTextMeasurementTest : public ::testing::Test
        {
        protected:
            GradientManager       gradientManager{ 256 };
            StyleManager          styleManager{ gradientManager };
            GUIStateManager       stateManager{};
            DummyTextAtlasManager atlasManager{};
            DummyClipboard        clipboard{};
            std::unique_ptr<SkribidiTextSystem> textSystem;

            void SetUp() override
            {
                textSystem = std::make_unique<SkribidiTextSystem>();
                // The measurement path never dereferences the GraphicsContext.
                ASSERT_TRUE(textSystem->Initialize(/*graphicsContext*/ nullptr,
                                                   &styleManager,
                                                   &atlasManager,
                                                   &stateManager,
                                                   &clipboard))
                    << "SkribidiTextSystem failed to initialize — fonts may be missing. "
                       "This test must run from workdirectory/ (CMake gtest_discover_tests sets that).";
            }

            void TearDown() override
            {
                textSystem->Shutdown();
                textSystem.reset();
            }

            // Like `.static-text`: text-wrap = wrap, no border, no padding.
            StyleRef MakeStaticTextStyle()
            {
                Style s{};
                s.borderWidths    = BoxEdges(0);
                s.padding         = BoxEdges(0);
                s.margin          = BoxEdges(0);
                s.textLayoutStyle = {};
                s.textLayoutStyle.textWrapping = TextWrap::Wrap;
                s.SetOverflow(GUI::Overflow::Auto);
                // A zero font size shapes nothing and measures 0.
                for (int i = 0; i < Style::kStateCount; ++i)
                    s.states[i].textStyle.fontSize = 13.0f;
                return styleManager.Register(s);
            }
        };

        TEST_F(SkribidiTextMeasurementTest, ShortWrappedText_HasNaturalGlyphWidth_NotLayoutBoxWidth)
        {
            StyleRef style = MakeStaticTextStyle();
            ID id = ID{ .value{ .integer = 1 } };
            TextID textId = textSystem->RegisterText(id, IconID::Invalid, ":", style, ElementState::None);
            ASSERT_NE(textId, TextID::Invalid);

            TextMeasurement m = textSystem->GetTextMeasurement(textId);
            // `:` is a few pixels wide; hundreds means the layout box was returned.
            EXPECT_GT(m.maxWidth, 0) << "measurement returned zero width";
            EXPECT_LT(m.maxWidth, 256)
                << "Text(\":\") measured at " << m.maxWidth
                << " px — the wrap-mode bug is back: MeasureTextInternal is "
                   "returning the layout box width instead of the actual glyph "
                   "extent. See gui_text_system_skribidi.cpp MeasureTextInternal "
                   "for the line-bounds extraction that fixes this.";
        }

        // Same with an explicit wrap context, as the layout engine passes for
        // wrap-mode elements with a finite available width.
        TEST_F(SkribidiTextMeasurementTest, ShortWrappedText_WithWrapContext_HasNaturalGlyphWidth)
        {
            StyleRef style = MakeStaticTextStyle();
            ID id = ID{ .value{ .integer = 2 } };
            TextID textId = textSystem->RegisterText(id, IconID::Invalid, ":", style, ElementState::None);
            ASSERT_NE(textId, TextID::Invalid);

            TextWrapContext wrap{};
            wrap.maxWidth = 16384; // mimic the live engine handing skribidi the full available width
            TextMeasurement m = textSystem->MeasureText(textId, wrap);
            EXPECT_GT(m.maxWidth, 0);
            EXPECT_LT(m.maxWidth, 256)
                << "Text(\":\") in wrap context measured at " << m.maxWidth << " px";
        }
    }
}
