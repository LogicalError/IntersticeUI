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
    class StubTextAtlasManager : public interstice::TextAtlasManager
    {
    public:
        bool Initialize(interstice::GraphicsContext*, interstice::ImageSet*) override { return true; }
        interstice::TextureIndex CreateTexture(const uint8_t*, interstice::uint2, uint8_t) override
        {
            return static_cast<interstice::TextureIndex>(++m_next);
        }
        void UpdateTexture(interstice::TextureIndex, const uint8_t*, interstice::uint2) override {}
        interstice::ImageIndex GetImageIndex(interstice::TextureIndex) override { return interstice::ImageIndex{ 0 }; }
        void SyncTextureIndices() override {}
    private:
        uint32_t m_next = 0;
    };

    class StubClipboard : public interstice::IClipboard
    {
    public:
        std::string Get() const override { return m_data; }
        void Set(std::string_view s) override { m_data = std::string(s); }
    private:
        std::string m_data;
    };

    // Draws nothing; lets the real text editor render (RenderText) headless.
    class StubGUIRenderer : public interstice::GUIRenderer
    {
    public:
        void Reset() override {}
        void SetTransform(interstice::float3x2) override {}
        void RenderBox(interstice::IntRect rect, interstice::IntRect, uint32_t style, uint32_t, float,
                       interstice::VisualColorStyleIndex, interstice::ImageIndex, interstice::IntRect) override
        {
            lastBoxStyle = style;
            boxes.push_back(rect);
        }
        void RenderLine(interstice::int2 p0, interstice::int2, interstice::int2, interstice::int2, interstice::IntRect, const interstice::LineStyle&) override
        {
            lineStarts.push_back(p0);
        }
        void RenderDot(interstice::float2, interstice::IntRect, float, interstice::color4f) override {}
        void RenderTriangle(interstice::float2, interstice::float2, interstice::float2, interstice::IntRect,
                            interstice::VisualColorStyleIndex, interstice::ImageIndex) override {}
        void RenderQuad(interstice::IntRect, interstice::IntRect, interstice::VisualColorStyleIndex, float, interstice::ImageIndex) override {}
        void RenderShadow(interstice::IntRect, interstice::IntRect, interstice::VisualColorStyleIndex, float, int) override {}
        void RenderGlyph(interstice::float2 pos, interstice::float2 size, interstice::float2, interstice::float2, float scale, int, int,
                         interstice::VisualColorStyleIndex, interstice::IntRect, interstice::ImageIndex) override
        {
            glyphRight = std::max(glyphRight, pos.x + size.x);
            glyphTop   = std::min(glyphTop, pos.y);
            lastGlyphScale = scale;
        }
        bool StartResizing(interstice::ID) override { return false; }
        void StopResizing() override {}
        bool BeginWindow(interstice::ID) override { return true; }
        void EndWindow() override {}

        float glyphRight     = 0.0f; // right edge of the rightmost glyph drawn since last reset
        float glyphTop       = 1e9f; // top edge of the highest glyph drawn since last reset
        float lastGlyphScale = 0.0f; // atlas-to-layout scale of the last glyph (1 / raster scale)
        uint32_t lastBoxStyle = 0;   // style slot of the last box drawn (a selection's), state included
        std::vector<interstice::IntRect> boxes;   // boxes drawn since last cleared
        std::vector<interstice::int2> lineStarts; // first point of each line (carets) since last cleared
    };

    // Only carries the stub renderer; the text system reads nothing else from it.
    class StubGraphicsContext : public interstice::GraphicsContext
    {
    public:
        StubGraphicsContext() { guiRenderer = &renderer; }
        StubGUIRenderer& Renderer() { return renderer; }
        interstice::GraphicsAPI GetAPI() const override { return interstice::GraphicsAPI::None; }
        void SetShaderDirectory(std::string_view) override {}
        void SetAllocationHooks(const interstice::AllocationHooks&) override {}
        interstice::Result<void> Initialize(interstice::IWindowBackend*) override { return {}; }
        void Cleanup() override {}
        void BeginFrame() override {}
        void EndFrame() override {}
        interstice::Image* CreateImage(void*, interstice::uint2, interstice::ImageFormat, interstice::Sampler, bool) override { return nullptr; }
        void DestroyImageAtEnd(interstice::Image*, interstice::DeviceScope, const std::source_location&) override {}
        interstice::TextAtlasManager* CreateTextAtlasManager() override { return nullptr; }
        interstice::IWindowContext* CreateWindowContext(void*, interstice::OSWindowHandle, interstice::ID, interstice::int2,
                                                 interstice::WindowFlags, interstice::OnGuiFunc) override { return nullptr; }
        void DestroyWindowContext(interstice::IWindowContext*) override {}
        interstice::ImageSet* GetImageSet() const override { return nullptr; }
        interstice::ImageIndex RegisterImage(interstice::Image*) override { return interstice::ImageIndex::Invalid; }
        void UnregisterImage(interstice::ImageIndex) override {}
        void PrepareFrame(interstice::StyleManager&, interstice::GradientManager&, float) override {}
    private:
        StubGUIRenderer renderer;
    };

    class SkribidiHeadlessTest : public ::testing::Test
    {
    protected:
        interstice::GradientManager       gradientManager{ 256 };
        interstice::StyleManager          styleManager{ gradientManager };
        interstice::GUIStateManager       stateManager{};
        StubTextAtlasManager       atlasManager{};
        StubClipboard              clipboard{};
        StubGraphicsContext        graphicsContext{};
        std::unique_ptr<interstice::SkribidiTextSystem> textSystem;

        void SetUp() override
        {
            textSystem = std::make_unique<interstice::SkribidiTextSystem>();
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
        interstice::StyleRef MakeTextStyle(float fontSize = 14.0f, interstice::FontID fontId = interstice::FontID::Invalid,
                                    interstice::FontFamily family = interstice::FontFamily::Default)
        {
            interstice::Style s{};
            s.borderWidths    = interstice::BoxEdges(0);
            s.padding         = interstice::BoxEdges(0);
            s.margin          = interstice::BoxEdges(0);
            s.textLayoutStyle = {};
            s.textLayoutStyle.textWrapping = interstice::TextWrap::Wrap;
            s.SetOverflow(interstice::Overflow::Auto);
            for (int i = 0; i < interstice::Style::kStateCount; ++i)
            {
                s.states[i].textStyle.fontSize = fontSize;
                s.states[i].textStyle.fontId   = fontId;
                s.states[i].textStyle.family   = family;
            }
            return styleManager.Register(s);
        }

        // Measured natural width of `text` under `style`. `seed` keeps element
        // IDs distinct across calls in one test.
        int32_t MeasureWidth(std::string_view text, interstice::StyleRef style, uint64_t seed)
        {
            interstice::ID id = interstice::ID{ .value{ .integer = seed } };
            interstice::TextID tid = textSystem->RegisterText(id, interstice::IconID::Invalid, text,
                                                       style, interstice::ElementState::None);
            EXPECT_NE(tid, interstice::TextID::Invalid);
            return textSystem->GetTextMeasurement(tid).maxWidth;
        }
    };
}
