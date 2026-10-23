// All-six-primitive GPU coverage: boxes, glyphs, dots, lines, triangles and
// quads all flow through the real upload + draw + present path on an actual
// device, with validation + sync-validation recording. A pipeline/buffer/dispatch
// mix-up for any kind surfaces as a validation error or a teardown leak.

#include "gpu_test_harness.h"

#include <gui_core/style/gui_style.h>

using namespace GpuTestHarness;

namespace
{
    class GpuPrimitiveTest : public GpuTest
    {
    protected:
        GUI::VisualColorStyleIndex solidColor{};
        GUI::VisualColorStyleIndex gradientColor{};

        // Quad/triangle/line/glyph index into the color-style buffer on the
        // GPU; register real entries so the draws don't read a sentinel slot.
        void RegisterColorStyles()
        {
            solidColor = styleManager.RegisterColorStyle(
                GUI::ColorStyle::Create(GUI::color4b{ 255, 255, 255, 255 }));
            gradientColor = styleManager.RegisterColorStyle(
                GUI::ColorStyle::Create(GUI::color4b{ 255, 0, 0, 255 },
                                        GUI::color4b{ 0, 255, 0, 255 },
                                        GUI::color4b{ 0, 0, 255, 255 },
                                        GUI::color4b{ 255, 0, 255, 255 },
                                        GUI::ColorType::InterpolatedBox));
        }

        void EmitAllPrimitives(GUI::GUIRenderer* r, GUI::int2 size)
        {
            const GUI::IntRect clip{ 0, 0, size.x, size.y };
            r->RenderBox(GUI::IntRect{ 4, 4, 64, 32 }, clip,
                         /*fromStyleIndex*/0, /*toStyleIndex*/0, /*blendT*/1.0f);
            r->RenderQuad(GUI::IntRect{ 8, 48, 48, 24 }, clip, gradientColor);
            r->RenderTriangle(GUI::float2{ 80, 8 }, GUI::float2{ 120, 8 },
                              GUI::float2{ 100, 40 }, clip, solidColor);
            r->RenderLine(GUI::int2{ 8, 90 }, GUI::int2{ 120, 90 }, clip,
                          GUI::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 2.0f });
            r->RenderDot(GUI::float2{ 140, 20 }, clip, 6.0f, GUI::color4f{ 1, 1, 1, 1 });
            r->RenderGlyph(GUI::float2{ 8, 110 }, GUI::float2{ 16, 16 },
                           GUI::float2{ 0, 0 }, GUI::float2{ 1, 1 },
                           /*scale*/1.0f, /*bpp*/8, /*mode*/0, solidColor, clip);
        }

        // One full window frame emitting every primitive kind — the all-kinds
        // analogue of the harness's RenderOneFrameWithDot.
        void RenderOneFrameAllKinds(Vulkan::VulkanWindowContext* ctx, GUI::int2 size)
        {
            graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
            GUI::GUIRenderer* r = graphicsContext->guiRenderer;
            r->Reset();
            if (r->BeginWindow(ctx->GetID()))
            {
                EmitAllPrimitives(r, size);
                r->EndWindow();
            }
            ctx->RenderWindowFrame(graphicsContext, GUI::IntRect{ 0, 0, size.x, size.y }, false);
        }
    };

    TEST_F(GpuPrimitiveTest, AllSixPrimitiveKindsRenderValidationClean)
    {
        auto* ctx = CreateImageWindowContext({ 320, 240 });
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        RegisterColorStyles();
        sink.Clear();
        RenderOneFrameAllKinds(ctx, { 320, 240 });
        EXPECT_FALSE(sink.HasErrors())
            << "rendering all six primitive kinds tripped validation/sync errors:\n"
            << sink.Dump();
    }

    // Several frames so FRAME_OVERLAP cycling and the per-frame retire path run
    // for EVERY buffer kind, not just dots.
    TEST_F(GpuPrimitiveTest, AllSixPrimitiveKindsAcrossThreeFramesValidationClean)
    {
        auto* ctx = CreateImageWindowContext({ 320, 240 });
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        RegisterColorStyles();
        sink.Clear();
        for (int i = 0; i < 3; ++i)
            RenderOneFrameAllKinds(ctx, { 320, 240 });
        EXPECT_FALSE(sink.HasErrors())
            << "multi-frame all-kinds render tripped validation/sync errors:\n"
            << sink.Dump();
    }

    // Window destroy must reclaim all six per-kind mesh buffers. A kind missed by
    // ReleaseTargetBuffers shows up as a growing VMA allocation count across
    // cycles and as leak errors at device destroy.
    TEST_F(GpuPrimitiveTest, WindowDestroyAfterAllKindsReclaimsBuffers)
    {
        auto allocCount = [&] {
            VmaTotalStatistics s{};
            vmaCalculateStatistics(vk->deviceContext.allocator, &s);
            return s.total.statistics.allocationCount;
        };

        RegisterColorStyles();
        m_checkLeaksAtTeardown = true;

        uint32_t afterFirst = 0;
        const int kCycles = 4;
        for (int i = 0; i < kCycles; ++i)
        {
            auto* ctx = CreateImageWindowContext({ 320, 240 });
            if (ctx == nullptr)
                GTEST_SKIP() << "no Vulkan device available";
            RenderOneFrameAllKinds(ctx, { 320, 240 });

            graphicsContext->DestroyWindowContext(ctx);
            m_windowContexts.back().context = nullptr;
            vk->deviceContext.Flush();

            if (i == 0) afterFirst = allocCount();
        }

        EXPECT_LE(allocCount(), afterFirst)
            << "per-kind render-target buffers accumulate across window "
               "create/destroy cycles";
    }
}
