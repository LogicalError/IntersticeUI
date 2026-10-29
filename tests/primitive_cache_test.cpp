// CPU tests for PrimitiveRenderTarget::AppendCaptured: replays a captured target's
// primitives translated in screen space, preserving the pass structure, so a cached
// subtree's draw can be replayed at a new position without re-emitting it.

#include <gtest/gtest.h>

#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_pixel_snap.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/graphics/gui_render_primitives.h>
#include <gui_core/gui_base_types.h>

#include <cstdint>
#include <cstring>

namespace tachyon
{
    namespace
    {
        TEST(PrimitiveCacheTest, AppendCapturedTranslatesAndPreservesPasses)
        {
            PrimitiveRenderTarget src;
            src.SetTransform(tachyon::float3x2::Translate(100.0f, 50.0f));
            src.RenderBox(tachyon::IntRect{ 10, 20, 30, 40 }, tachyon::IntRect{ 0, 0, 200, 200 }, 1, 1, 1.0f);
            // Different kind -> a second pass.
            src.RenderGlyph(tachyon::float2{ 5, 6 }, tachyon::float2{ 7, 8 }, tachyon::float2{ 0, 0 }, tachyon::float2{ 1, 1 },
                            1.0f, 1, 0, tachyon::VisualColorStyleIndex{}, tachyon::IntRect{ 0, 0, 200, 200 });
            src.FinishRenderPass();
            ASSERT_EQ(src.renderPasses.size(), 2u);

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, tachyon::int2{ 10, 20 });

            // Passes preserved in order.
            ASSERT_EQ(dst.renderPasses.size(), 2u);
            EXPECT_EQ(dst.renderPasses[0].kind, PrimitiveKind::Box);
            EXPECT_EQ(dst.renderPasses[1].kind, PrimitiveKind::Glyph);

            // Box: transform translation + screen clip shifted by delta; local coords unchanged.
            const Box* b = reinterpret_cast<const Box*>(dst.streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_FLOAT_EQ(b->transform0.z, 110.0f); // 100 + 10
            EXPECT_FLOAT_EQ(b->transform1.z, 70.0f);  // 50 + 20
            EXPECT_EQ(b->clipMin.x, 10);  EXPECT_EQ(b->clipMin.y, 20);
            EXPECT_EQ(b->clipMax.x, 210); EXPECT_EQ(b->clipMax.y, 220);
            EXPECT_EQ(b->pos.x, 10);  EXPECT_EQ(b->pos.y, 20);
            EXPECT_EQ(b->size.x, 30); EXPECT_EQ(b->size.y, 40);

            // Glyph: same translation applied.
            const Glyph* g = reinterpret_cast<const Glyph*>(dst.streams[size_t(PrimitiveKind::Glyph)].cpu.data());
            EXPECT_FLOAT_EQ(g->transform0.z, 110.0f);
            EXPECT_FLOAT_EQ(g->transform1.z, 70.0f);
        }

        // A glyph is itself unless given a shadow blur, which it keeps in 1/8 pixels.
        TEST(PrimitiveCacheTest, AGlyphKeepsItsShadowBlur)
        {
            PrimitiveRenderTarget t;
            const tachyon::IntRect clip{ 0, 0, 200, 200 };
            t.RenderGlyph(tachyon::float2{ 5, 6 }, tachyon::float2{ 7, 8 }, tachyon::float2{ 0, 0 }, tachyon::float2{ 1, 1 },
                          1.0f, 1, 1, tachyon::VisualColorStyleIndex{}, clip);
            t.RenderGlyph(tachyon::float2{ 5, 6 }, tachyon::float2{ 7, 8 }, tachyon::float2{ 0, 0 }, tachyon::float2{ 1, 1 },
                          1.0f, 1, 1, tachyon::VisualColorStyleIndex{}, clip, tachyon::ImageIndex::Invalid, 0.0f);
            t.RenderGlyph(tachyon::float2{ 5, 6 }, tachyon::float2{ 7, 8 }, tachyon::float2{ 0, 0 }, tachyon::float2{ 1, 1 },
                          1.0f, 1, 1, tachyon::VisualColorStyleIndex{}, clip, tachyon::ImageIndex::Invalid, 2.5f);
            t.FinishRenderPass();
            const Glyph* g = reinterpret_cast<const Glyph*>(t.streams[size_t(PrimitiveKind::Glyph)].cpu.data());
            EXPECT_EQ(g[0].shadowBlur, -1);
            EXPECT_EQ(g[1].shadowBlur, 0);
            EXPECT_EQ(g[2].shadowBlur, 20);
        }

        TEST(PrimitiveCacheTest, AppendCapturedZeroDeltaIsVerbatim)
        {
            PrimitiveRenderTarget src;
            src.SetTransform(tachyon::float3x2::Translate(100.0f, 50.0f));
            src.RenderBox(tachyon::IntRect{ 10, 20, 30, 40 }, tachyon::IntRect{ 0, 0, 200, 200 }, 1, 1, 1.0f);
            src.FinishRenderPass();

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, tachyon::int2{ 0, 0 });

            const auto& sb = src.streams[size_t(PrimitiveKind::Box)].cpu;
            const auto& db = dst.streams[size_t(PrimitiveKind::Box)].cpu;
            ASSERT_EQ(db.size(), sb.size());
            EXPECT_EQ(0, std::memcmp(db.data(), sb.data(), sb.size())); // byte-identical when not moved
        }

        TEST(PrimitiveCacheTest, ScopeCaptureReplayMatchesDirectRender)
        {
            PrimitiveRenderer r;
            r.BeginWindow(tachyon::ID::Invalid()); // any id works for a CPU-only target

            // Capture a "subtree" of two primitives under scope S.
            const tachyon::ID scope = tachyon::ID::Invalid();
            r.BeginScopeCapture(scope);
            r.SetTransform(tachyon::float3x2::Translate(0.0f, 0.0f));
            r.RenderBox(tachyon::IntRect{ 0, 0, 50, 50 }, tachyon::IntRect{ 0, 0, 100, 100 }, 1, 1, 1.0f);
            r.EndScopeCapture(scope); // emits into the window (delta 0) and stores the capture

            EXPECT_TRUE(r.HasScopeCapture(scope));
            ASSERT_EQ(r.currentTarget->replays.size(), 1u) << "drawn where it was captured";
            EXPECT_EQ(r.currentTarget->replays[0].delta, (tachyon::int2{ 0, 0 }));

            // Replaying adds a pass that draws the capture moved, without copying its primitives.
            const size_t before = r.currentTarget->streams[size_t(PrimitiveKind::Box)].cpu.size();
            EXPECT_TRUE(r.ReplayScopeCapture(scope, tachyon::int2{ 5, 7 }));
            EXPECT_EQ(r.currentTarget->streams[size_t(PrimitiveKind::Box)].cpu.size(), before);
            ASSERT_EQ(r.currentTarget->replays.size(), 2u);
            EXPECT_EQ(r.currentTarget->replays[1].delta, (tachyon::int2{ 5, 7 }));
            EXPECT_EQ(r.currentTarget->renderPasses.back().replay, 1);

            r.EndWindow();
        }

        // A replayed box carries its parent-snapped pixel rect along with its move.
        TEST(PrimitiveScaleTest, ReplayMovesTheSnappedRect)
        {
            PrimitiveRenderTarget src;
            src.RenderBox(tachyon::IntRect{ 0, 0, 10, 10 }, tachyon::IntRect{ 0, 0, 100, 100 }, 1, 1, 1.0f, {},
                          tachyon::ImageIndex::Invalid, tachyon::IntRect{ 3, 4, 15, 16 });
            src.FinishRenderPass();

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, tachyon::int2{ 10, 20 });
            const Box* b = reinterpret_cast<const Box*>(dst.streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_EQ(b->snappedPx[0], 13);
            EXPECT_EQ(b->snappedPx[1], 24);
            EXPECT_EQ(b->snappedPx[2], 28);
            EXPECT_EQ(b->snappedPx[3], 40);
        }

        // A box past the 16-bit range (a table sizer stretched to the table's height) keeps
        // its far edge at the end of the range; wrapped, it drew as a 1px stripe at the top.
        TEST(PrimitiveScaleTest, ASnappedRectPastTheRangeSaturates)
        {
            PrimitiveRenderTarget src;
            src.RenderBox(tachyon::IntRect{ 211, 11, 4, 32768 }, tachyon::IntRect{ 0, 0, 800, 600 }, 1, 1, 1.0f, {},
                          tachyon::ImageIndex::Invalid, tachyon::IntRect{ 211, 11, 4, 32768 });
            src.FinishRenderPass();
            const Box* b = reinterpret_cast<const Box*>(src.streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_EQ(b->snappedPx[1], 11);
            EXPECT_EQ(b->snappedPx[3], INT16_MAX);

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, tachyon::int2{ 0, 20 });
            const Box* moved = reinterpret_cast<const Box*>(dst.streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_EQ(moved->snappedPx[1], 31);
            EXPECT_EQ(moved->snappedPx[3], INT16_MAX) << "a moved cached box saturates too";
        }

        // A rounded polyline runs straight through a point where it doesn't turn: the
        // runs meet there instead of stopping short for a corner that's never drawn.
        TEST(PrimitiveScaleTest, RoundedPolylineHasNoGapWhereItRunsStraight)
        {
            tachyon::GradientManager gradients{ 16 };
            tachyon::StyleManager    styles{ gradients };
            tachyon::ElementStorage  storage{ styles };
            PrimitiveRenderer    r;
            r.BeginWindow(tachyon::ID::Invalid());
            tachyon::CustomDrawContext ctx{ r, storage, tachyon::ElementRef::Invalid(), tachyon::IntRect{ 0, 0, 200, 60 },
                                        tachyon::IntRect{ 0, 0, 200, 60 }, tachyon::IntRect{ 0, 0, 500, 500 },
                                        tachyon::float3x2{}, tachyon::int2{ 0, 0 } };
            const tachyon::int2 points[] = { { 10, 20 }, { 100, 20 }, { 190, 20 } };
            ctx.RenderPolyline(points, /*closed=*/false, tachyon::LineStyle{ .lineWidth = 2.0f }, 10.0f);
            r.currentTarget->FinishRenderPass();

            const auto& stream = r.currentTarget->streams[size_t(PrimitiveKind::Line)].cpu;
            const size_t count = stream.size() / sizeof(Line);
            ASSERT_EQ(count, 2u);
            const Line* lines = reinterpret_cast<const Line*>(stream.data());
            EXPECT_EQ(lines[0].p0.x, 10);
            EXPECT_EQ(lines[0].p3.x, 100) << "the first run reaches the middle point";
            EXPECT_EQ(lines[1].p0.x, 100);
            EXPECT_EQ(lines[1].p3.x, 190);
            r.EndWindow();
        }

        // A box drawn from a custom-draw callback snaps to the element doing the
        // drawing, like element boxes snap to their parent (here under a 1.5x zoom).
        TEST(PrimitiveScaleTest, CustomDrawBoxSnapsToItsOwner)
        {
            tachyon::GradientManager gradients{ 16 };
            tachyon::StyleManager    styles{ gradients };
            tachyon::ElementStorage  storage{ styles };
            PrimitiveRenderer    r;
            r.BeginWindow(tachyon::ID::Invalid());

            // Owner zoomed 1.5x (a node view) at layout (20.5, 20.5): pixels from 30.75.
            const tachyon::float3x2 layoutToWindow = tachyon::float3x2::Scale(1.5f) * tachyon::float3x2::Translate(20.5f, 20.5f);
            const tachyon::float4   ownerPx{ 30.75f, 30.75f, 120.75f, 120.75f };
            const tachyon::IntRect  ownerSnapped = tachyon::SnapAbsolute(ownerPx);
            tachyon::CustomDrawContext ctx{ r, storage, tachyon::ElementRef::Invalid(), tachyon::IntRect{ 0, 0, 60, 60 },
                                        tachyon::IntRect{ 0, 0, 60, 60 }, tachyon::IntRect{ 0, 0, 500, 500 },
                                        layoutToWindow, tachyon::int2{ 0, 0 }, nullptr, ownerPx, ownerSnapped };
            ctx.RenderBox(tachyon::IntRect{ 1, 1, 58, 10 }, tachyon::VisualBoxStyleIndex::Invalid);
            r.currentTarget->FinishRenderPass();

            const Box* b = reinterpret_cast<const Box*>(r.currentTarget->streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_EQ(b->snappedPx[0] - ownerSnapped.x, 2) << "a 1-unit inset is 2 pixels";
            EXPECT_EQ(b->snappedPx[1] - ownerSnapped.y, 2);
            EXPECT_EQ((ownerSnapped.x + ownerSnapped.w) - b->snappedPx[2], 2);
            r.EndWindow();
        }
    }
}
