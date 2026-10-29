#pragma once

// Shared headless fixture for real-SkribidiTextSystem tests. Boots the actual
// text system (real font loading + HarfBuzz shaping + bidi) with a stub atlas
// manager and clipboard and a nullptr GraphicsContext - the shaping /
// measurement / editing paths never deref the renderer. Tests must run from
// workdirectory/ (CMake's gtest_discover_tests sets that) so fonts resolve.

#include <gtest/gtest.h>

#include <gui_core/text/gui_text_system.h>
#include <gui_core/text/gui_text_atlas_manager.h>
#include <gui_core/text/gui_clipboard.h>
#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace GUITest
{
    // Stub atlas: hands back distinct, valid TextureIndex values so glyph
    // rasterization paths (editor caret, selection) don't treat every glyph as
    // sharing slot 0. No real GPU upload happens.
    class StubTextAtlasManager : public tachyon::TextAtlasManager
    {
    public:
        bool Initialize(tachyon::GraphicsContext*, tachyon::ImageSet*) override { return true; }
        tachyon::TextureIndex CreateTexture(const uint8_t*, tachyon::uint2, uint8_t) override
        {
            return static_cast<tachyon::TextureIndex>(++m_next);
        }
        void UpdateTexture(tachyon::TextureIndex, const uint8_t*, tachyon::uint2) override {}
        tachyon::ImageIndex GetImageIndex(tachyon::TextureIndex) override { return tachyon::ImageIndex{ 0 }; }
        void SyncTextureIndices() override {}
    private:
        uint32_t m_next = 0;
    };

    class StubClipboard : public tachyon::IClipboard
    {
    public:
        std::string Get() const override { return m_data; }
        void Set(std::string_view s) override { m_data = std::string(s); }
    private:
        std::string m_data;
    };

    // Draws nothing; lets the real text editor render (RenderText) headless.
    class StubGUIRenderer : public tachyon::GUIRenderer
    {
    public:
        void Reset() override {}
        void SetTransform(tachyon::float3x2) override {}
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect, uint32_t style, uint32_t, float,
                       tachyon::VisualColorStyleIndex, tachyon::ImageIndex, tachyon::IntRect) override
        {
            lastBoxStyle = style;
            boxes.push_back(rect);
        }
        void RenderLine(tachyon::int2 p0, tachyon::int2, tachyon::int2, tachyon::int2, tachyon::IntRect, const tachyon::LineStyle&) override
        {
            lineStarts.push_back(p0);
        }
        void RenderCircle(tachyon::float2, float, tachyon::IntRect, const tachyon::LineStyle&) override {}
        void RenderDot(tachyon::float2, tachyon::IntRect, float, tachyon::color4f) override {}
        void RenderTriangle(tachyon::float2, tachyon::float2, tachyon::float2, tachyon::IntRect,
                            tachyon::VisualColorStyleIndex, tachyon::ImageIndex) override {}
        void RenderQuad(tachyon::IntRect, tachyon::IntRect, tachyon::VisualColorStyleIndex, float, tachyon::ImageIndex) override {}
        void RenderShadow(tachyon::IntRect, tachyon::IntRect, tachyon::VisualColorStyleIndex, float, int) override {}
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int, int,
                         tachyon::VisualColorStyleIndex color, tachyon::IntRect, tachyon::ImageIndex, float shadowBlur) override
        {
            glyphDraws.push_back({ pos, size, uvMin, uvMax, color, shadowBlur });
            if (shadowBlur >= 0.0f)
                return;
            glyphRight = std::max(glyphRight, pos.x + size.x);
            glyphTop   = std::min(glyphTop, pos.y);
            lastGlyphScale = scale;
        }
        bool StartResizing(tachyon::ID) override { return false; }
        void StopResizing() override {}
        bool BeginWindow(tachyon::ID) override { return true; }
        void EndWindow() override {}

        struct GlyphDraw
        {
            tachyon::float2 pos, size, uvMin, uvMax;
            tachyon::VisualColorStyleIndex color;
            float shadowBlur; // below 0: the glyph itself
        };
        std::vector<GlyphDraw> glyphDraws; // every glyph quad drawn since last cleared, shadows included

        float glyphRight     = 0.0f; // right edge of the rightmost glyph drawn since last reset
        float glyphTop       = 1e9f; // top edge of the highest glyph drawn since last reset
        float lastGlyphScale = 0.0f; // atlas-to-layout scale of the last glyph (1 / raster scale)
        uint32_t lastBoxStyle = 0;   // style slot of the last box drawn (a selection's), state included
        std::vector<tachyon::IntRect> boxes;   // boxes drawn since last cleared
        std::vector<tachyon::int2> lineStarts; // first point of each line (carets) since last cleared
    };

    // Only carries the stub renderer; the text system reads nothing else from it.
    class StubGraphicsContext : public tachyon::GraphicsContext
    {
    public:
        StubGraphicsContext() { guiRenderer = &renderer; }
        StubGUIRenderer& Renderer() { return renderer; }
        tachyon::GraphicsAPI GetAPI() const override { return tachyon::GraphicsAPI::None; }
        void SetAllocationHooks(const tachyon::AllocationHooks&) override {}
        tachyon::Result<void> Initialize(tachyon::IWindowBackend*) override { return {}; }
        void Cleanup() override {}
        void BeginFrame() override {}
        void EndFrame() override {}
        tachyon::Image* CreateImage(void*, tachyon::uint2, tachyon::ImageFormat, tachyon::Sampler, bool) override { return nullptr; }
        void DestroyImageAtEnd(tachyon::Image*, tachyon::DeviceScope, const std::source_location&) override {}
        tachyon::TextAtlasManager* CreateTextAtlasManager() override { return nullptr; }
        tachyon::IWindowContext* CreateWindowContext(void*, tachyon::OSWindowHandle, tachyon::ID, tachyon::int2,
                                                 tachyon::WindowFlags, tachyon::OnGuiFunc) override { return nullptr; }
        void DestroyWindowContext(tachyon::IWindowContext*) override {}
        tachyon::ImageSet* GetImageSet() const override { return nullptr; }
        tachyon::ImageIndex RegisterImage(tachyon::Image*) override { return tachyon::ImageIndex::Invalid; }
        void UnregisterImage(tachyon::ImageIndex) override {}
        void PrepareFrame(tachyon::StyleManager&, tachyon::GradientManager&, float) override {}
    private:
        StubGUIRenderer renderer;
    };

    class SkribidiHeadlessTest : public ::testing::Test
    {
    protected:
        tachyon::GradientManager       gradientManager{ 256 };
        tachyon::StyleManager          styleManager{ gradientManager };
        tachyon::GUIStateManager       stateManager{};
        StubTextAtlasManager       atlasManager{};
        StubClipboard              clipboard{};
        StubGraphicsContext        graphicsContext{};
        std::unique_ptr<tachyon::SkribidiTextSystem> textSystem;

        void SetUp() override
        {
            textSystem = std::make_unique<tachyon::SkribidiTextSystem>();
            // Shaping tests cover other scripts, so the Noto fallbacks load too.
            textSystem->SetFallbackFontDirectory("fonts/Noto-otf");
            ASSERT_TRUE(textSystem->Initialize(&graphicsContext,
                                               &styleManager,
                                               &atlasManager,
                                               &stateManager,
                                               &clipboard))
                << "SkribidiTextSystem failed to initialize — fonts may be missing. "
                   "Run from workdirectory/ (gtest_discover_tests sets that).";
        }

        void TearDown() override
        {
            if (textSystem) textSystem->Shutdown();
            textSystem.reset();
        }

        // A plain wrap-mode text style with a real font size so glyphs shape.
        tachyon::StyleRef MakeTextStyle(float fontSize = 14.0f, tachyon::FontID fontId = tachyon::FontID::Invalid,
                                    tachyon::FontFamily family = tachyon::FontFamily::Default)
        {
            tachyon::Style s{};
            s.borderWidths    = tachyon::BoxEdges(0);
            s.padding         = tachyon::BoxEdges(0);
            s.margin          = tachyon::BoxEdges(0);
            s.textLayoutStyle = {};
            s.textLayoutStyle.textWrapping = tachyon::TextWrap::Wrap;
            s.SetOverflow(tachyon::Overflow::Auto);
            for (int i = 0; i < tachyon::Style::kStateCount; ++i)
            {
                s.states[i].textStyle.fontSize = fontSize;
                s.states[i].textStyle.fontId   = fontId;
                s.states[i].textStyle.family   = family;
            }
            return styleManager.Register(s);
        }

        // Measured natural width of `text` under `style`. `seed` keeps element
        // IDs distinct across calls in one test.
        int32_t MeasureWidth(std::string_view text, tachyon::StyleRef style, uint64_t seed)
        {
            tachyon::ID id = tachyon::ID{ .value{ .integer = seed } };
            tachyon::TextID tid = textSystem->RegisterText(id, tachyon::IconID::Invalid, text,
                                                       style, tachyon::ElementState::None);
            EXPECT_NE(tid, tachyon::TextID::Invalid);
            return textSystem->GetTextMeasurement(tid).maxWidth;
        }
    };
}
