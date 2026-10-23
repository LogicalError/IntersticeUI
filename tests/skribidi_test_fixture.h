#pragma once

// Shared headless fixture for real-SkribidiTextSystem tests. Boots the actual
// text system (real font loading + HarfBuzz shaping + bidi) with a stub atlas
// manager and clipboard and a nullptr GraphicsContext — the shaping /
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

namespace GUITest
{
    // Stub atlas: hands back distinct, valid TextureIndex values so glyph
    // rasterization paths (editor caret, selection) don't treat every glyph as
    // sharing slot 0. No real GPU upload happens.
    class StubTextAtlasManager : public GUI::TextAtlasManager
    {
    public:
        bool Initialize(GUI::GraphicsContext*, GUI::ImageSet*) override { return true; }
        GUI::TextureIndex CreateTexture(const uint8_t*, GUI::uint2, uint8_t) override
        {
            return static_cast<GUI::TextureIndex>(++m_next);
        }
        void UpdateTexture(GUI::TextureIndex, const uint8_t*, GUI::uint2) override {}
        GUI::ImageIndex GetImageIndex(GUI::TextureIndex) override { return GUI::ImageIndex{ 0 }; }
        void SyncTextureIndices() override {}
    private:
        uint32_t m_next = 0;
    };

    class StubClipboard : public GUI::IClipboard
    {
    public:
        std::string Get() const override { return m_data; }
        void Set(std::string_view s) override { m_data = std::string(s); }
    private:
        std::string m_data;
    };

    // Draws nothing; lets the real text editor render (RenderText) headless.
    class StubGUIRenderer : public GUI::GUIRenderer
    {
    public:
        void Reset() override {}
        void SetTransform(GUI::float3x2) override {}
        void RenderBox(GUI::IntRect, GUI::IntRect, uint32_t, uint32_t, float,
                       GUI::VisualColorStyleIndex, GUI::ImageIndex, GUI::IntRect) override {}
        void RenderLine(GUI::int2, GUI::int2, GUI::int2, GUI::int2, GUI::IntRect, const GUI::LineStyle&) override {}
        void RenderDot(GUI::float2, GUI::IntRect, float, GUI::color4f) override {}
        void RenderTriangle(GUI::float2, GUI::float2, GUI::float2, GUI::IntRect,
                            GUI::VisualColorStyleIndex, GUI::ImageIndex) override {}
        void RenderQuad(GUI::IntRect, GUI::IntRect, GUI::VisualColorStyleIndex, float, GUI::ImageIndex) override {}
        void RenderGlyph(GUI::float2 pos, GUI::float2 size, GUI::float2, GUI::float2, float scale, int, int,
                         GUI::VisualColorStyleIndex, GUI::IntRect, GUI::ImageIndex) override
        {
            glyphRight = std::max(glyphRight, pos.x + size.x);
            glyphTop   = std::min(glyphTop, pos.y);
            lastGlyphScale = scale;
        }
        bool StartResizing(GUI::ID) override { return false; }
        void StopResizing() override {}
        bool BeginWindow(GUI::ID) override { return true; }
        void EndWindow() override {}

        float glyphRight     = 0.0f; // right edge of the rightmost glyph drawn since last reset
        float glyphTop       = 1e9f; // top edge of the highest glyph drawn since last reset
        float lastGlyphScale = 0.0f; // atlas-to-layout scale of the last glyph (1 / raster scale)
    };

    // Only carries the stub renderer; the text system reads nothing else from it.
    class StubGraphicsContext : public GUI::GraphicsContext
    {
    public:
        StubGraphicsContext() { guiRenderer = &renderer; }
        StubGUIRenderer& Renderer() { return renderer; }
        void SetShaderDirectory(std::string_view) override {}
        void SetAllocationHooks(const GUI::AllocationHooks&) override {}
        bool Initialize(GUI::IWindowBackend*) override { return true; }
        void Cleanup() override {}
        void BeginFrame() override {}
        void EndFrame() override {}
        GUI::Image* CreateImage(void*, GUI::uint2, GUI::ImageFormat, GUI::Sampler, bool) override { return nullptr; }
        void DestroyImageAtEnd(GUI::Image*, GUI::DeviceScope, const std::source_location&) override {}
        GUI::TextAtlasManager* CreateTextAtlasManager() override { return nullptr; }
        GUI::IWindowContext* CreateWindowContext(void*, GUI::OSWindowHandle, GUI::ID, GUI::int2,
                                                 GUI::WindowFlags, GUI::OnGuiFunc) override { return nullptr; }
        void DestroyWindowContext(GUI::IWindowContext*) override {}
        GUI::ImageSet* GetImageSet() const override { return nullptr; }
        GUI::ImageIndex RegisterImage(GUI::Image*) override { return GUI::ImageIndex::Invalid; }
        void UnregisterImage(GUI::ImageIndex) override {}
        void PrepareFrame(GUI::StyleManager&, GUI::GradientManager&, float) override {}
    private:
        StubGUIRenderer renderer;
    };

    class SkribidiHeadlessTest : public ::testing::Test
    {
    protected:
        GUI::GradientManager       gradientManager{ 256 };
        GUI::StyleManager          styleManager{ gradientManager };
        GUI::GUIStateManager       stateManager{};
        StubTextAtlasManager       atlasManager{};
        StubClipboard              clipboard{};
        StubGraphicsContext        graphicsContext{};
        std::unique_ptr<GUI::SkribidiTextSystem> textSystem;

        void SetUp() override
        {
            textSystem = std::make_unique<GUI::SkribidiTextSystem>();
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
        GUI::StyleRef MakeTextStyle(float fontSize = 14.0f, GUI::FontID fontId = GUI::FontID::Invalid,
                                    GUI::FontFamily family = GUI::FontFamily::Default)
        {
            GUI::Style s{};
            s.borderWidths    = GUI::BoxEdges(0);
            s.padding         = GUI::BoxEdges(0);
            s.margin          = GUI::BoxEdges(0);
            s.textLayoutStyle = {};
            s.textLayoutStyle.textWrapping = GUI::TextWrap::Wrap;
            s.SetOverflow(GUI::Overflow::Auto);
            for (int i = 0; i < GUI::Style::kStateCount; ++i)
            {
                s.states[i].textStyle.fontSize = fontSize;
                s.states[i].textStyle.fontId   = fontId;
                s.states[i].textStyle.family   = family;
            }
            return styleManager.Register(s);
        }

        // Measured natural width of `text` under `style`. `seed` keeps element
        // IDs distinct across calls in one test.
        int32_t MeasureWidth(std::string_view text, GUI::StyleRef style, uint64_t seed)
        {
            GUI::ID id = GUI::ID{ .value{ .integer = seed } };
            GUI::TextID tid = textSystem->RegisterText(id, GUI::IconID::Invalid, text,
                                                       style, GUI::ElementState::None);
            EXPECT_NE(tid, GUI::TextID::Invalid);
            return textSystem->GetTextMeasurement(tid).maxWidth;
        }
    };
}
