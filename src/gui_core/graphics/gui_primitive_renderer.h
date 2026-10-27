#pragma once

// API-neutral GUI primitive renderer: per-window targets accumulate per-kind
// primitive streams, then Render() uploads each stream and replays the pass
// list through the RHI. No graphics-API types appear here - a backend
// supplies a RenderCommandList with a fence-protected transient upload arena
// and the pipeline set built from kPrimitiveSpecs.

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/gui_render_primitives.h>
#include <gui_core/graphics/gui_rhi.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    using PipelineSet = std::array<RHI::GpuPipeline, kPrimitiveKindCount>;

    /// One contiguous same-kind run of primitives, drawn with one call.
    struct PrimitiveRenderPass
    {
        int           firstPrimitive;
        int           primitiveCount;
        PrimitiveKind kind;
    };

    struct PrimitiveRenderTarget
    {
    public:
        /// Per-kind primitive stream: CPU bytes appended by the emit methods and
        /// packed together into the current frame slot's GPU upload arena.
        struct Stream
        {
            std::vector<std::byte> cpu;
            int                    passStart = 0; ///< index of the open pass's first primitive
        };
        std::array<Stream, kPrimitiveKindCount> streams;

        std::vector<PrimitiveRenderPass> renderPasses;
        int           currentKind = -1; ///< kind of the open pass; -1 when none
        tachyon::float3x2 currentTransform;

    public:
        void FinishRenderPass();
        void StartRenderPass(PrimitiveKind kind);

        /// Appends another target's passes, translated by `delta` pixels. Works for every kind:
        /// each primitive starts with the same header (transform0, transform1, clipMin, clipMax).
        void AppendCaptured(const PrimitiveRenderTarget& src, tachyon::int2 delta);

    public:
        void Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);
        void Reset();

        void SetTransform(tachyon::float3x2 transform);
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, tachyon::VisualColorStyleIndex colorStyleIndex = {}, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {});
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid);
        void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle);
        void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color);
        void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid);
        void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid);
        void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur);

    private:
        /// Closes an open pass of a different kind, then appends `primitive` to its kind's stream.
        template <typename T>
        void Append(PrimitiveKind kind, const T& primitive)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            assert(sizeof(T) == kPrimitiveSpecs[size_t(kind)].stride);
            StartRenderPass(kind);
            auto& cpu = streams[size_t(kind)].cpu;
            const auto* bytes = reinterpret_cast<const std::byte*>(&primitive);
            cpu.insert(cpu.end(), bytes, bytes + sizeof(T));
        }
    };

    class PrimitiveRenderer : public GUIRenderer
    {
    public:
        std::unordered_map<tachyon::ID, PrimitiveRenderTarget> idTargets;
        PrimitiveRenderTarget* currentTarget{ nullptr };
        PrimitiveRenderTarget* resizingTarget{ nullptr };

    public:
        void Reset() override;
        void SetTransform(tachyon::float3x2 transform) override;
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, tachyon::VisualColorStyleIndex colorStyleIndex = {}, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {}) override;
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) override;
        void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle) override;
        void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color) override;
        void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) override;
        void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) override;
        void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur) override;

    public:
        PrimitiveRenderTarget& GetTarget(tachyon::ID id)
        {
            auto target = idTargets.find(id);
            if (target == idTargets.end())
            {
                idTargets[id] = PrimitiveRenderTarget();
                return idTargets[id];
            }
            return target->second;
        }

        /// Drops the CPU-side target of window `id`, when the window is destroyed. Transient GPU
        /// storage belongs to the window's frame slots, not to this target.
        void ReleaseTarget(tachyon::ID id);
        void ReleaseAllTargets();

        bool StartResizing(tachyon::ID id) override
        {
            assert(resizingTarget == nullptr);
            auto target = idTargets.find(id);
            if (target == idTargets.end())
            {
                resizingTarget = nullptr;
            } else
                resizingTarget = &target->second;
            return resizingTarget != nullptr;
        }

        void StopResizing() override
        {
            assert(resizingTarget != nullptr);
            resizingTarget = nullptr;
        }

        bool BeginWindow(tachyon::ID id) override;
        void EndWindow() override;

        // Draw caching. Capture targets are CPU-only; their primitives are merged
        // into the window target on capture/replay.
        void BeginScopeCapture(tachyon::ID scopeId) override;
        void EndScopeCapture(tachyon::ID scopeId) override;
        bool ReplayScopeCapture(tachyon::ID scopeId, tachyon::int2 delta) override;
        bool HasScopeCapture(tachyon::ID scopeId) const override;
        void InvalidateScopeCapture(tachyon::ID scopeId) override;
        void ClearScopeCaptures() override;

        void Render(tachyon::ID id, RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);

    private:
        /// Captured primitives per cached scope, kept across frames.
        std::unordered_map<tachyon::ID, PrimitiveRenderTarget> scopeCaptures;
        /// The window target while a capture redirects emit calls.
        PrimitiveRenderTarget* m_captureSaved = nullptr;
    };
    /// @}
}
