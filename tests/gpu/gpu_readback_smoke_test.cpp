// Pixel-level smoke tests on render-to-image windows: RenderWindowFrame renders
// offscreen (BGRA8) and ReadPixels copies the frame back, with no OS window.
// Assertions are structural only - a pixel inside a primitive
// must differ from one outside it, sampled from the same frame - never
// golden-image comparisons, so they hold across drivers and formats.

#include "gpu_test_harness.h"

#include <gui_core/style/gui_style.h>
#include <gui_core/graphics/vulkan/vk_text_atlas_manager.h>
#include <gui_core/graphics/gui_pixel_snap.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace GpuTestHarness;

namespace
{
    class GpuReadbackTest : public GpuTest
    {
    protected:
        static constexpr tachyon::int2 kSize{ 320, 240 };

        tachyon::VisualColorStyleIndex solidColor{};
        tachyon::VisualColorStyleIndex gradientColor{};

        std::vector<uint8_t> framePixels = std::vector<uint8_t>(size_t(kSize.x) * kSize.y * 4);

        tachyon::vulkan::VulkanWindowContext* CreateReadbackWindow() { return CreateImageWindowContext(kSize); }

        void RegisterColorStyles()
        {
            solidColor = styleManager.RegisterColorStyle(
                tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
            gradientColor = styleManager.RegisterColorStyle(
                tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 },
                                        tachyon::color4b{ 0, 255, 0, 255 },
                                        tachyon::color4b{ 0, 0, 255, 255 },
                                        tachyon::color4b{ 255, 0, 255, 255 },
                                        tachyon::ColorType::InterpolatedBox));
        }

        // Render one frame whose primitives are emitted by `emit`, then leave
        // the result in framePixels.
        template <typename EmitFn>
        void RenderFrame(tachyon::vulkan::VulkanWindowContext* ctx, EmitFn&& emit)
        {
            graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
            tachyon::GUIRenderer* r = graphicsContext->guiRenderer;
            r->Reset();
            if (r->BeginWindow(ctx->GetID()))
            {
                emit(r);
                r->EndWindow();
            }
            ctx->RenderWindowFrame(graphicsContext, tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, false);
            ASSERT_TRUE(ctx->ReadPixels(framePixels));
        }

        // Raw 32-bit pixel of the last read frame (BGRA8, tightly packed).
        uint32_t PixelAt(const tachyon::vulkan::VulkanWindowContext*, int x, int y) const
        {
            const size_t off = (size_t(y) * kSize.x + size_t(x)) * 4;
            uint32_t v = 0;
            std::memcpy(&v, framePixels.data() + off, 4);
            return v;
        }

        static constexpr tachyon::IntRect kFullClip{ 0, 0, kSize.x, kSize.y };
    };

    TEST_F(GpuReadbackTest, QuadFillChangesCoveredPixels)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Solid quad over the left half; the right half stays background.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, solidColor);
        });

        const uint32_t inside  = PixelAt(ctx, kSize.x / 4,     kSize.y / 2);
        const uint32_t outside = PixelAt(ctx, kSize.x * 3 / 4, kSize.y / 2);
        EXPECT_NE(inside, outside)
            << "a solid quad must change the pixels it covers";
    }

    // A quad shows its color as given, like a box would: the quad shader used to convert
    // every color as if it were linear, which lightened anything custom-drawn.
    TEST_F(GpuReadbackTest, QuadKeepsItsColor)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex color = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 128, 64, 32, 255 }));

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, color);
        });

        const uint32_t px = PixelAt(ctx, kSize.x / 2, kSize.y / 2);   // BGRA8
        const int b = int(px & 0xFF), g = int((px >> 8) & 0xFF), rr = int((px >> 16) & 0xFF);
        EXPECT_NEAR(rr, 128, 2);
        EXPECT_NEAR(g,  64, 2);
        EXPECT_NEAR(b,  32, 2);
    }

    // A quad rounded to half its size is a disc (CustomDrawContext::RenderDisc, switch
    // knobs, slider thumbs): covered at every angle just inside its radius, empty just
    // outside, and the same in mirrored spots.
    TEST_F(GpuReadbackTest, QuadRoundedToHalfItsSizeIsADisc)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr int kD = 24, cx = 100, cy = 100;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ cx - kD / 2, cy - kD / 2, kD, kD }, kFullClip, solidColor, float(kD) / 2.0f);
        });

        const uint32_t empty = PixelAt(ctx, 10, 10);
        // Pixel centres sit at +0.5; sample at radius 9 (inside) and 11.5 along the diagonals
        // (inside a square's corner, outside the circle).
        for (int a = 0; a < 16; ++a)
        {
            const float t = float(a) * 3.14159265f / 8.0f;
            const int x = cx + int(std::floor(9.0f * std::cos(t))), y = cy + int(std::floor(9.0f * std::sin(t)));
            EXPECT_NE(PixelAt(ctx, x, y), empty) << "inside the disc at angle step " << a;
        }
        const int d = int(11.5f / 1.41421356f) + 1;
        EXPECT_EQ(PixelAt(ctx, cx + d,     cy + d),     empty);
        EXPECT_EQ(PixelAt(ctx, cx - d - 1, cy + d),     empty);
        EXPECT_EQ(PixelAt(ctx, cx + d,     cy - d - 1), empty);
        EXPECT_EQ(PixelAt(ctx, cx - d - 1, cy - d - 1), empty);
        // Mirrored edge pixels match.
        EXPECT_EQ(PixelAt(ctx, cx + 11, cy), PixelAt(ctx, cx - 12, cy));
        EXPECT_EQ(PixelAt(ctx, cx, cy + 11), PixelAt(ctx, cx, cy - 12));
    }

    TEST_F(GpuReadbackTest, DotRendersAtKnownPosition)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderDot(tachyon::float2{ 16, 16 }, kFullClip, 8.0f, tachyon::color4f{ 1, 1, 1, 1 });
        });

        const uint32_t atDot     = PixelAt(ctx, 16, 16);
        const uint32_t farCorner = PixelAt(ctx, kSize.x - 8, kSize.y - 8);
        EXPECT_NE(atDot, farCorner)
            << "the dot's center pixel must differ from untouched background";
    }

    TEST_F(GpuReadbackTest, GradientQuadVariesAcrossSpan)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Full-frame 4-corner interpolated quad: left third and right third
        // sample different ends of the gradient.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, gradientColor);
        });

        const uint32_t left  = PixelAt(ctx, kSize.x / 6,     kSize.y / 2);
        const uint32_t right = PixelAt(ctx, kSize.x * 5 / 6, kSize.y / 2);
        EXPECT_NE(left, right)
            << "an interpolated quad must vary across its span";
    }

    TEST_F(GpuReadbackTest, ScopeCaptureReplayDrawsTranslated)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        tachyon::GUIRenderer* r = graphicsContext->guiRenderer;
        r->ClearScopeCaptures();
        const tachyon::ID scope = tachyon::ID::Invalid(); // any key for the capture map

        // Frame 1: capture a quad at the left edge (x = 0..40); EndScopeCapture
        // also draws it into the window this frame.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* rr) {
            rr->BeginScopeCapture(scope);
            rr->SetTransform(tachyon::float3x2::Identity());
            rr->RenderQuad(tachyon::IntRect{ 0, 0, 40, kSize.y }, kFullClip, solidColor);
            rr->EndScopeCapture(scope);
        });
        const uint32_t bg = PixelAt(ctx, kSize.x - 20, kSize.y / 2);
        EXPECT_NE(PixelAt(ctx, 20, kSize.y / 2), bg) << "captured quad draws at the left this frame";

        // Frame 2: replay the cached quad translated right by 200px. It must now
        // cover x ~200..240 and have left the original left edge as background.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* rr) {
            EXPECT_TRUE(rr->ReplayScopeCapture(scope, tachyon::int2{ 200, 0 }));
        });
        EXPECT_NE(PixelAt(ctx, 220, kSize.y / 2), bg) << "replayed quad appears at the translated position";
        EXPECT_EQ(PixelAt(ctx, 20,  kSize.y / 2), bg) << "the original position is background again";
    }

    TEST_F(GpuReadbackTest, LineRendersAlongPath)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Thick horizontal line at y=40; a pixel on the path differs from one
        // well below it.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderLine(tachyon::int2{ 16, 40 }, tachyon::int2{ kSize.x - 16, 40 }, kFullClip,
                          tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 4.0f });
        });

        const uint32_t onLine  = PixelAt(ctx, kSize.x / 2, 40);
        const uint32_t offLine = PixelAt(ctx, kSize.x / 2, 120);
        EXPECT_NE(onLine, offLine)
            << "a line must change the pixels along its path";
    }

    // ---- Glyph atlas pages ----

    class GpuTextAtlasTest : public GpuReadbackTest
    {
    protected:
        tachyon::vulkan::VulkanTextAtlasManager atlas;

        void SetUp() override
        {
            GpuReadbackTest::SetUp();
            if (IsSkipped() || HasFatalFailure()) return;
            ASSERT_TRUE(atlas.Initialize(graphicsContext, graphicsContext->GetImageSet()));
        }

        static std::vector<uint8_t> Filled(tachyon::uint2 size, uint8_t value)
        {
            return std::vector<uint8_t>(size_t(size.x) * size.y * 4, value);
        }
    };

    // Glyphs are drawn at the size they were rasterized at, and the samplers only
    // read level 0, so atlas pages carry no mip chain.
    TEST_F(GpuTextAtlasTest, PagesHaveNoMipChain)
    {
        const tachyon::uint2 size{ 1024, 1024 };
        const auto pixels = Filled(size, 255);
        const tachyon::TextureIndex page = atlas.CreateTexture(pixels.data(), size, 4);
        ASSERT_NE(page, tachyon::TextureIndex::Invalid);

        auto* image = static_cast<tachyon::vulkan::VulkanImage*>(atlas.fontTextures[uint32_t(page) - 1].image);
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(vk->deviceContext.allocator, image->allocation, &info);
        EXPECT_LT(info.size, VkDeviceSize(size.x) * size.y * 4 * 6 / 5)
            << "a full mip chain adds a third on top of level 0";
    }

    // An atlas update must not wait for the GPU: it's queued and recorded into the
    // next frame. Holding the queue lock proves it doesn't submit on its own.
    TEST_F(GpuTextAtlasTest, UpdatesDoNotWaitForTheGpu)
    {
        const tachyon::uint2 size{ 256, 256 };
        const auto pixels = Filled(size, 255);
        const tachyon::TextureIndex page = atlas.CreateTexture(pixels.data(), size, 4);
        ASSERT_NE(page, tachyon::TextureIndex::Invalid);

        std::atomic<bool> updated = false;
        std::thread updater;
        {
            auto queueLock = vk->deviceContext.LockQueue();
            updater = std::thread([&] { atlas.UpdateTexture(page, pixels.data(), size); updated = true; });
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            EXPECT_TRUE(updated) << "the update waited on the queue";
        }
        updater.join();
    }

    // A page created and then updated before any frame still reaches the screen,
    // validation-clean (sync validation checks the upload barriers).
    TEST_F(GpuTextAtlasTest, UpdatedPageIsSampledByTheNextFrame)
    {
        RegisterColorStyles();
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        const tachyon::uint2 size{ 64, 64 };
        const auto black = Filled(size, 0);
        const auto white = Filled(size, 255);
        const tachyon::TextureIndex page = atlas.CreateTexture(black.data(), size, 4);
        atlas.UpdateTexture(page, white.data(), size);
        const tachyon::ImageIndex imageIndex = atlas.GetImageIndex(page);
        atlas.SyncTextureIndices();
        const tachyon::VisualColorStyleIndex textured = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 },
                                    tachyon::ColorType::StretchedTexture));
        sink.Clear();

        for (int frame = 0; frame < 2; ++frame)
        {
            graphicsContext->BeginFrame();
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderQuad(tachyon::IntRect{ 40, 40, 80, 80 }, kFullClip, textured, 0.0f, imageIndex);
            });
        }

        EXPECT_NE(PixelAt(ctx, 80, 80), PixelAt(ctx, 200, 200)) << "the page's pixels never reached the screen";
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
    }

    // ---- Image uploads (gradient pages, user images) ----

    // Creating an image with pixel data doesn't wait for the GPU either: the
    // upload (and its mipmaps) is recorded into the next frame.
    TEST_F(GpuReadbackTest, ImageUploadsDoNotWaitForTheGpu)
    {
        const tachyon::uint2 size{ 256, 256 };
        std::vector<uint8_t> pixels(size_t(size.x) * size.y * 4, 255);

        std::atomic<tachyon::Image*> image = nullptr;
        std::thread creator;
        {
            auto queueLock = vk->deviceContext.LockQueue();
            creator = std::thread([&] {
                image = graphicsContext->CreateImage(pixels.data(), size, tachyon::ImageFormat::R8G8B8A8,
                                                     tachyon::Sampler::LinearRepeat, true);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            EXPECT_NE(image.load(), nullptr) << "creating the image waited on the queue";
        }
        creator.join();
        if (image.load() != nullptr)
            vk->deviceContext.DestroyImage(image.load());
    }

    // A mipmapped image created before any frame is filled (all levels) by the
    // next frame and sampled correctly, validation-clean.
    TEST_F(GpuReadbackTest, MipmappedImageIsSampledByTheNextFrame)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        const tachyon::uint2 size{ 64, 64 };
        std::vector<uint8_t> pixels(size_t(size.x) * size.y * 4, 255);
        tachyon::Image* image = graphicsContext->CreateImage(pixels.data(), size, tachyon::ImageFormat::R8G8B8A8,
                                                         tachyon::Sampler::LinearRepeat, true);
        ASSERT_NE(image, nullptr);
        const tachyon::ImageIndex imageIndex = graphicsContext->RegisterImage(image);
        const tachyon::VisualColorStyleIndex textured = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 },
                                    tachyon::ColorType::StretchedTexture));
        sink.Clear();

        for (int frame = 0; frame < 2; ++frame)
        {
            graphicsContext->BeginFrame();
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderQuad(tachyon::IntRect{ 40, 40, 80, 80 }, kFullClip, textured, 0.0f, imageIndex);
            });
        }

        EXPECT_NE(PixelAt(ctx, 80, 80), PixelAt(ctx, 200, 200)) << "the image's pixels never reached the screen";
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
        graphicsContext->UnregisterImage(imageIndex);
        vk->deviceContext.DestroyImageAtEnd(image, tachyon::DeviceScope::Frame);
    }

    // ---- Borders under a fractional scale (HiDPI, node-view zoom) ----

    // A translucent fill (a vector field's axis label) meets an opaque border at the
    // rounded corners: no pixel there may be more saturated than the fill and the border.
    TEST_F(GpuReadbackTest, TranslucentFillDoesNotGlowAtRoundedCorners)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        tachyon::Style style{};
        style.borderWidths = tachyon::BoxEdges(1);
        style.borderRadius = tachyon::BoxCorners(6);
        for (int i = 0; i < tachyon::Style::kStateCount; ++i)
        {
            style.states[i].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 239, 106, 106, 46 });
            style.states[i].SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 42, 47, 57, 255 }));
        }
        const tachyon::VisualBoxStyleIndex box = styleManager.Register(style).visualBoxStyleIndex;
        constexpr int kX = 20, kY = 20, kW = 80, kH = 40;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderBox(tachyon::IntRect{ kX, kY, kW, kH }, kFullClip, box);
        });

        // Redness: red over the other channels (BGRA8).
        auto redness = [](uint32_t p) { return int((p >> 16) & 0xFF) - int((p >> 8) & 0xFF); };
        const int fill   = redness(PixelAt(ctx, kX + kW / 2, kY + kH / 2));
        const int border = redness(PixelAt(ctx, kX, kY + kH / 2));
        const int limit  = std::max(fill, border) + 3;
        for (int y = kY; y < kY + 8; ++y)
            for (int x = kX; x < kX + 8; ++x)
                EXPECT_LE(redness(PixelAt(ctx, x, y)), limit) << "corner pixel " << x << "," << y << ": " << std::hex << (PixelAt(ctx, x, y) & 0xFFFFFF) << std::dec << " fill " << fill << " border " << border;
    }

    // A box shadow fades across its edge over the blur: full inside, about half at the
    // edge, nearly gone a blur outside it, and steadily darker in between.
    TEST_F(GpuReadbackTest, ShadowFadesOverItsBlur)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr int kX = 100, kY = 60, kW = 120, kH = 120, kBlur = 16;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderShadow(tachyon::IntRect{ kX, kY, kW, kH }, kFullClip, solidColor, 6.0f, kBlur);
        });

        auto level = [&](int x) { return int((PixelAt(ctx, x, kY + kH / 2) >> 8) & 0xFF); }; // green
        const int bg     = int((PixelAt(ctx, 4, 4) >> 8) & 0xFF);
        const int inside = level(kX + kW / 2);
        ASSERT_GT(inside - bg, 200) << "full inside the shape";
        const float atEdge = float(level(kX) - bg) / float(inside - bg);
        EXPECT_GT(atEdge, 0.25f);
        EXPECT_LT(atEdge, 0.85f);
        EXPECT_LE(level(kX - kBlur) - bg, (inside - bg) / 10) << "a blur outside the edge";
        EXPECT_EQ(level(kX - kBlur - 4), bg) << "past the quad, which extends one blur";
        for (int x = kX - kBlur; x < kX + kBlur; ++x)
            EXPECT_LE(level(x), level(x + 1) + 1) << "falls off steadily at " << x;
    }

    // A child placed just inside its parent's 1-unit border (a node's header)
    // must meet the border at any scale: no background shows between them,
    // whichever way the edges round to pixels.
    TEST_F(GpuReadbackTest, ChildMeetsParentBorderAtFractionalScale)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        // Shaped like a node: rounded box with a 1-unit border, and a header just
        // inside it with a rounded top and a bottom separator.
        tachyon::Style nodeStyle{};
        nodeStyle.borderWidths = tachyon::BoxEdges(1);
        nodeStyle.borderRadius = tachyon::BoxCorners(6);
        tachyon::Style headerStyle{};
        headerStyle.borderWidths = tachyon::BoxEdges(0);
        headerStyle.borderWidths.bottom = 1;
        headerStyle.borderRadius = tachyon::BoxCorners(5, 5, 0, 0);
        for (int i = 0; i < tachyon::Style::kStateCount; ++i)
        {
            nodeStyle.states[i].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 0, 255 });
            nodeStyle.states[i].SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
            headerStyle.states[i].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 });
            headerStyle.states[i].SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 255, 255 }));
        }
        const tachyon::VisualBoxStyleIndex node   = styleManager.Register(nodeStyle).visualBoxStyleIndex;
        const tachyon::VisualBoxStyleIndex header = styleManager.Register(headerStyle).visualBoxStyleIndex;

        // Pan offsets in fractions of a unit put the edges at every sub-pixel phase.
        constexpr int kCases = 6;
        constexpr int kX = 20, kY = 20, kW = 60, kH = 60;
        for (const float kScale : { 1.0f, 1.25f, 1.5f, 2.0f })
        for (int c = 0; c < kCases; ++c)
        {
            const float pan = float(c) / float(kCases);
            // Snapped the way the render pass does: the node absolutely, the header to the node.
            auto toPx = [&](tachyon::IntRect r) {
                return tachyon::float4{ (r.x + pan) * kScale, (r.y + pan) * kScale,
                                    (r.x + r.w + pan) * kScale, (r.y + r.h + pan) * kScale };
            };
            const tachyon::IntRect nodeRect{ kX, kY, kW, kH }, headerRect{ kX + 1, kY + 1, kW - 2, 14 };
            const tachyon::IntRect nodeSnapped   = tachyon::SnapAbsolute(toPx(nodeRect));
            const tachyon::IntRect headerSnapped = tachyon::SnapToParent(toPx(nodeRect), nodeSnapped, toPx(headerRect));
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->SetTransform(tachyon::float3x2::Scale(kScale) * tachyon::float3x2::Translate(pan, pan));
                r->RenderBox(nodeRect,   kFullClip, node,   tachyon::VisualColorStyleIndex{}, tachyon::ImageIndex::Invalid, nodeSnapped);
                r->RenderBox(headerRect, kFullClip, header, tachyon::VisualColorStyleIndex{}, tachyon::ImageIndex::Invalid, headerSnapped);
            });

            auto isBorder = [](uint32_t p) { return ((p >> 16) & 0xFF) > 150 && ((p >> 8) & 0xFF) > 150; };
            auto isHeader = [](uint32_t p) { return ((p >> 16) & 0xFF) > 150 && ((p >> 8) & 0xFF) < 100; };

            // Down the middle: border rows, then header rows, with nothing dark between.
            const int px = int((kX + kW / 2 + pan) * kScale);
            int topBorderRows = 0;
            bool sawGap = false, reachedHeader = false;
            std::string column;
            for (int py = int((kY + pan) * kScale) - 2; py < int((kY + 6 + pan) * kScale); ++py)
            {
                const uint32_t p = PixelAt(ctx, px, py);
                char buf[24];
                std::snprintf(buf, sizeof(buf), " %d:%06x", py, p & 0xFFFFFF);
                column += buf;
                if (reachedHeader) continue;
                if (isBorder(p))      ++topBorderRows;
                else if (isHeader(p)) reachedHeader = true;
                else if (topBorderRows > 0) sawGap = true;
            }
            // Across the left side below the header: the side border's columns.
            const int py = int((kY + 40 + pan) * kScale);
            int sideBorderCols = 0;
            for (int x = int((kX + pan) * kScale) - 2; x < int((kX + 4 + pan) * kScale); ++x)
                if (isBorder(PixelAt(ctx, x, py))) ++sideBorderCols;

            // The header's bottom border is at its bottom: blue right above the node background.
            bool bottomLineAtBottom = false;
            for (int y = int((kY + 1 + 14 + pan) * kScale) - 3; y <= int((kY + 1 + 14 + pan) * kScale) + 1; ++y)
            {
                const uint32_t above = PixelAt(ctx, px, y), below = PixelAt(ctx, px, y + 1);
                if ((above & 0xFF) > 150 && ((above >> 16) & 0xFF) < 100 && ((below >> 16) & 0xFF) < 100 && (below & 0xFF) < 100)
                    bottomLineAtBottom = true;
            }
            EXPECT_TRUE(bottomLineAtBottom) << "header border-bottom not drawn at the bottom, scale " << kScale << " pan " << pan;

            EXPECT_TRUE(reachedHeader) << "scale " << kScale << " pan " << pan << column;
            EXPECT_FALSE(sawGap) << "background between the top border and the header, scale " << kScale << " pan " << pan << column;
            // A 1-unit border is the same whole number of pixels everywhere.
            const int expected = std::max(1, tachyon::SnapPx(kScale));
            EXPECT_EQ(topBorderRows, expected) << "top border, scale " << kScale << " pan " << pan << column;
            EXPECT_EQ(sideBorderCols, expected) << "side border, scale " << kScale << " pan " << pan;
        }
    }

    // A host image set as a render-to-image window's target receives its frames:
    // the copy and its barriers are sync-validation clean, and the image ends up
    // holding the window's pixels.
    TEST_F(GpuReadbackTest, ImageTargetReceivesTheWindowsFrames)
    {
        auto& dc = vk->deviceContext;
        tachyon::ID id{};
        id.value.dynamicParts.mode      = tachyon::IDMode::Static;
        id.value.dynamicParts.hashValue = 9300;
        auto* ctx = static_cast<tachyon::vulkan::VulkanWindowContext*>(graphicsContext->CreateWindowContext(
            nullptr, nullptr, id, kSize, tachyon::WindowFlags::RenderToImage, [] {}));
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        // The host's image is RGBA, so the copy also converts from the GUI's BGRA.
        VkImageCreateInfo imageInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType   = VK_IMAGE_TYPE_2D;
        imageInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent      = { uint32_t(kSize.x), uint32_t(kSize.y), 1 };
        imageInfo.mipLevels   = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VkImage target = VK_NULL_HANDLE;
        VmaAllocation targetAllocation = nullptr;
        ASSERT_EQ(vmaCreateImage(dc.allocator, &imageInfo, &allocInfo, &target, &targetAllocation, nullptr), VK_SUCCESS);
        vk->SetWindowImageTarget(id, target, VkExtent2D{ uint32_t(kSize.x), uint32_t(kSize.y) });

        RegisterColorStyles();
        sink.Clear();
        for (int i = 0; i < 3; ++i)
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, solidColor, 0.0f);
            });
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();

        // Read the target back as a host would: after the GUI's frame, out of
        // SHADER_READ_ONLY and back into it.
        const size_t bytes = size_t(kSize.x) * kSize.y * 4;
        tachyon::vulkan::Buffer readback = dc.CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        VkCommandBuffer cmd = dc.immCommandBuffer;
        ASSERT_EQ(vkResetFences(dc.device, 1, &dc.immFence), VK_SUCCESS);
        ASSERT_EQ(vkResetCommandBuffer(cmd, 0), VK_SUCCESS);
        VkCommandBufferBeginInfo begin{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        const auto transition = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
        {
            VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            barrier.srcStageMask = srcStage; barrier.srcAccessMask = srcAccess;
            barrier.dstStageMask = dstStage; barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = from; barrier.newLayout = to;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = target;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dependency);
        };
        transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = imageInfo.extent;
        vkCmdCopyImageToBuffer(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
        transition(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        VkMemoryBarrier2 toHost{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;   toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;   toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo hostDependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        hostDependency.memoryBarrierCount = 1;
        hostDependency.pMemoryBarriers = &toHost;
        vkCmdPipelineBarrier2(cmd, &hostDependency);
        vkEndCommandBuffer(cmd);
        VkCommandBufferSubmitInfo cmdInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cmdInfo.commandBuffer = cmd;
        VkSubmitInfo2 submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        {
            auto lock = dc.LockQueue();
            ASSERT_EQ(vkQueueSubmit2(dc.graphicsQueue, 1, &submit, dc.immFence), VK_SUCCESS);
        }
        tachyon::vulkan::WaitForFence(dc.device, dc.immFence);
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();

        vmaInvalidateAllocation(dc.allocator, readback.allocation, 0, VK_WHOLE_SIZE);
        void* mapped = nullptr;
        vmaMapMemory(dc.allocator, readback.allocation, &mapped);
        const auto* rgba = static_cast<const uint8_t*>(mapped);
        for (const tachyon::int2 p : { tachyon::int2{ kSize.x / 4, kSize.y / 2 }, tachyon::int2{ kSize.x * 3 / 4, kSize.y / 2 } })
        {
            const size_t off = (size_t(p.y) * kSize.x + size_t(p.x)) * 4;
            EXPECT_EQ(rgba[off + 0], framePixels[off + 2]) << "red at " << p.x;
            EXPECT_EQ(rgba[off + 1], framePixels[off + 1]) << "green at " << p.x;
            EXPECT_EQ(rgba[off + 2], framePixels[off + 0]) << "blue at " << p.x;
            EXPECT_EQ(rgba[off + 3], framePixels[off + 3]) << "alpha at " << p.x;
        }
        vmaUnmapMemory(dc.allocator, readback.allocation);

        dc.DestroyBuffer(readback);
        graphicsContext->DestroyWindowContext(ctx);
        dc.WaitIdle();
        vmaDestroyImage(dc.allocator, target, targetAllocation);
    }
}
