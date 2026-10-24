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

#include <cstring>

namespace interstice
{
    namespace
    {
        TEST(PrimitiveCacheTest, AppendCapturedTranslatesAndPreservesPasses)
        {
            PrimitiveRenderTarget src;
            src.SetTransform(interstice::float3x2::Translate(100.0f, 50.0f));
            src.RenderBox(interstice::IntRect{ 10, 20, 30, 40 }, interstice::IntRect{ 0, 0, 200, 200 }, 1, 1, 1.0f);
            // Different kind → a second pass.
            src.RenderGlyph(interstice::float2{ 5, 6 }, interstice::float2{ 7, 8 }, interstice::float2{ 0, 0 }, interstice::float2{ 1, 1 },
                            1.0f, 1, 0, interstice::VisualColorStyleIndex{}, interstice::IntRect{ 0, 0, 200, 200 });
            src.FinishRenderPass();
            ASSERT_EQ(src.renderPasses.size(), 2u);

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, interstice::int2{ 10, 20 });

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

        TEST(PrimitiveCacheTest, AppendCapturedZeroDeltaIsVerbatim)
        {
            PrimitiveRenderTarget src;
            src.SetTransform(interstice::float3x2::Translate(100.0f, 50.0f));
            src.RenderBox(interstice::IntRect{ 10, 20, 30, 40 }, interstice::IntRect{ 0, 0, 200, 200 }, 1, 1, 1.0f);
            src.FinishRenderPass();

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, interstice::int2{ 0, 0 });

            const auto& sb = src.streams[size_t(PrimitiveKind::Box)].cpu;
            const auto& db = dst.streams[size_t(PrimitiveKind::Box)].cpu;
            ASSERT_EQ(db.size(), sb.size());
            EXPECT_EQ(0, std::memcmp(db.data(), sb.data(), sb.size())); // byte-identical when not moved
        }

        TEST(PrimitiveCacheTest, ScopeCaptureReplayMatchesDirectRender)
        {
            PrimitiveRenderer r;
            r.BeginWindow(interstice::ID::Invalid()); // any id works for a CPU-only target

            // Capture a "subtree" of two primitives under scope S.
            const interstice::ID scope = interstice::ID::Invalid();
            r.BeginScopeCapture(scope);
            r.SetTransform(interstice::float3x2::Translate(0.0f, 0.0f));
            r.RenderBox(interstice::IntRect{ 0, 0, 50, 50 }, interstice::IntRect{ 0, 0, 100, 100 }, 1, 1, 1.0f);
            r.EndScopeCapture(scope); // emits into the window (delta 0) and stores the capture

            EXPECT_TRUE(r.HasScopeCapture(scope));

            // Replaying translated appends another, shifted copy.
            const size_t before = r.currentTarget->streams[size_t(PrimitiveKind::Box)].cpu.size();
            EXPECT_TRUE(r.ReplayScopeCapture(scope, interstice::int2{ 5, 7 }));
            const size_t after = r.currentTarget->streams[size_t(PrimitiveKind::Box)].cpu.size();
            EXPECT_EQ(after, before + sizeof(Box));

            r.EndWindow();
        }

        // A replayed box carries its parent-snapped pixel rect along with its move.
        TEST(PrimitiveScaleTest, ReplayMovesTheSnappedRect)
        {
            PrimitiveRenderTarget src;
            src.RenderBox(interstice::IntRect{ 0, 0, 10, 10 }, interstice::IntRect{ 0, 0, 100, 100 }, 1, 1, 1.0f, {},
                          interstice::ImageIndex::Invalid, interstice::IntRect{ 3, 4, 15, 16 });
            src.FinishRenderPass();

            PrimitiveRenderTarget dst;
            dst.AppendCaptured(src, interstice::int2{ 10, 20 });
            const Box* b = reinterpret_cast<const Box*>(dst.streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_EQ(b->snappedPx[0], 13);
            EXPECT_EQ(b->snappedPx[1], 24);
            EXPECT_EQ(b->snappedPx[2], 28);
            EXPECT_EQ(b->snappedPx[3], 40);
        }

        // A rounded polyline runs straight through a point where it doesn't turn: the
        // runs meet there instead of stopping short for a corner that's never drawn.
        TEST(PrimitiveScaleTest, RoundedPolylineHasNoGapWhereItRunsStraight)
        {
            interstice::GradientManager gradients{ 16 };
            interstice::StyleManager    styles{ gradients };
            interstice::ElementStorage  storage{ styles };
            PrimitiveRenderer    r;
            r.BeginWindow(interstice::ID::Invalid());
            interstice::CustomDrawContext ctx{ r, storage, interstice::ElementRef::Invalid(), interstice::IntRect{ 0, 0, 200, 60 },
                                        interstice::IntRect{ 0, 0, 200, 60 }, interstice::IntRect{ 0, 0, 500, 500 },
                                        interstice::float3x2{}, interstice::int2{ 0, 0 } };
            const interstice::int2 points[] = { { 10, 20 }, { 100, 20 }, { 190, 20 } };
            ctx.RenderPolyline(points, /*closed=*/false, interstice::LineStyle{ .lineWidth = 2.0f }, 10.0f);
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
            interstice::GradientManager gradients{ 16 };
            interstice::StyleManager    styles{ gradients };
            interstice::ElementStorage  storage{ styles };
            PrimitiveRenderer    r;
            r.BeginWindow(interstice::ID::Invalid());

            // Owner zoomed 1.5x (a node view) at layout (20.5, 20.5): pixels from 30.75.
            const interstice::float3x2 layoutToWindow = interstice::float3x2::Scale(1.5f) * interstice::float3x2::Translate(20.5f, 20.5f);
            const interstice::float4   ownerPx{ 30.75f, 30.75f, 120.75f, 120.75f };
            const interstice::IntRect  ownerSnapped = interstice::SnapAbsolute(ownerPx);
            interstice::CustomDrawContext ctx{ r, storage, interstice::ElementRef::Invalid(), interstice::IntRect{ 0, 0, 60, 60 },
                                        interstice::IntRect{ 0, 0, 60, 60 }, interstice::IntRect{ 0, 0, 500, 500 },
                                        layoutToWindow, interstice::int2{ 0, 0 }, nullptr, ownerPx, ownerSnapped };
            ctx.RenderBox(interstice::IntRect{ 1, 1, 58, 10 }, interstice::VisualBoxStyleIndex::Invalid);
            r.currentTarget->FinishRenderPass();

            const Box* b = reinterpret_cast<const Box*>(r.currentTarget->streams[size_t(PrimitiveKind::Box)].cpu.data());
            EXPECT_EQ(b->snappedPx[0] - ownerSnapped.x, 2) << "a 1-unit inset is 2 pixels";
            EXPECT_EQ(b->snappedPx[1] - ownerSnapped.y, 2);
            EXPECT_EQ((ownerSnapped.x + ownerSnapped.w) - b->snappedPx[2], 2);
            r.EndWindow();
        }
    }
}
