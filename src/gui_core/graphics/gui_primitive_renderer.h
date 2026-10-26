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

namespace interstice
{
    using PipelineSet = std::array<RHI::GpuPipeline, kPrimitiveKindCount>;

    // One contiguous same-kind run of primitives, drawn with one call.
    struct PrimitiveRenderPass
    {
        int           firstPrimitive;
        int           primitiveCount;
        PrimitiveKind kind;
    };

    struct PrimitiveRenderTarget
    {
    public:
        // Per-kind primitive stream: CPU bytes appended by the emit methods and
        // packed together into the current frame slot's GPU upload arena.
        struct Stream
        {
            std::vector<std::byte> cpu;
            int                    passStart = 0; // primitive index at current pass start
        };
        std::array<Stream, kPrimitiveKindCount> streams;

        std::vector<PrimitiveRenderPass> renderPasses;
        int           currentKind = -1; // -1 = no open pass
        interstice::float3x2 currentTransform;

    public:
        void FinishRenderPass();
        void StartRenderPass(PrimitiveKind kind);

        // Append another target's passes, translated by `delta` in screen space.
        // Kind-agnostic: every primitive starts with the same header (transform0,
        // transform1, clipMin, clipMax).
        void AppendCaptured(const PrimitiveRenderTarget& src, interstice::int2 delta);

    public:
        void Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);
        void Reset();

        void SetTransform(interstice::float3x2 transform);
        void RenderBox(interstice::IntRect rect, interstice::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, interstice::VisualColorStyleIndex colorStyleIndex = {}, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid, interstice::IntRect snappedPx = {});
        void RenderGlyph(interstice::float2 pos, interstice::float2 size, interstice::float2 uvMin, interstice::float2 uvMax, float scale, int bpp, int mode, interstice::VisualColorStyleIndex textColorIndex, interstice::IntRect clipRect, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid);
        void RenderLine(interstice::int2 p0, interstice::int2 p1, interstice::int2 p2, interstice::int2 p3, interstice::IntRect clipRect, const interstice::LineStyle& lineStyle);
        void RenderDot(interstice::float2 pos, interstice::IntRect clipRect, float size, interstice::color4f color);
        void RenderTriangle(interstice::float2 p0, interstice::float2 p1, interstice::float2 p2, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid);
        void RenderQuad(interstice::IntRect rect, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid);
        void RenderShadow(interstice::IntRect rect, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur);

    private:
        // Close any open pass of a different kind, then append one primitive
        // to its kind's stream.
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
        std::unordered_map<interstice::ID, PrimitiveRenderTarget> idTargets;
        PrimitiveRenderTarget* currentTarget{ nullptr };
        PrimitiveRenderTarget* resizingTarget{ nullptr };

    public:
        void Reset() override;
        void SetTransform(interstice::float3x2 transform) override;
        void RenderBox(interstice::IntRect rect, interstice::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, interstice::VisualColorStyleIndex colorStyleIndex = {}, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid, interstice::IntRect snappedPx = {}) override;
        void RenderGlyph(interstice::float2 pos, interstice::float2 size, interstice::float2 uvMin, interstice::float2 uvMax, float scale, int bpp, int mode, interstice::VisualColorStyleIndex textColorIndex, interstice::IntRect clipRect, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid) override;
        void RenderLine(interstice::int2 p0, interstice::int2 p1, interstice::int2 p2, interstice::int2 p3, interstice::IntRect clipRect, const interstice::LineStyle& lineStyle) override;
        void RenderDot(interstice::float2 pos, interstice::IntRect clipRect, float size, interstice::color4f color) override;
        void RenderTriangle(interstice::float2 p0, interstice::float2 p1, interstice::float2 p2, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid) override;
        void RenderQuad(interstice::IntRect rect, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid) override;
        void RenderShadow(interstice::IntRect rect, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur) override;

    public:
        PrimitiveRenderTarget& GetTarget(interstice::ID id)
        {
            auto target = idTargets.find(id);
            if (target == idTargets.end())
            {
                idTargets[id] = PrimitiveRenderTarget();
                return idTargets[id];
            }
            return target->second;
        }

        // Drop the CPU-side target when its window is destroyed. Transient GPU
        // storage belongs to the window's frame slots, not to this target.
        void ReleaseTarget(interstice::ID id);
        void ReleaseAllTargets();

        bool StartResizing(interstice::ID id) override
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

        bool BeginWindow(interstice::ID id) override;
        void EndWindow() override;

        // Draw caching. Capture targets are CPU-only; their primitives are merged
        // into the window target on capture/replay.
        void BeginScopeCapture(interstice::ID scopeId) override;
        void EndScopeCapture(interstice::ID scopeId) override;
        bool ReplayScopeCapture(interstice::ID scopeId, interstice::int2 delta) override;
        bool HasScopeCapture(interstice::ID scopeId) const override;
        void InvalidateScopeCapture(interstice::ID scopeId) override;
        void ClearScopeCaptures() override;

        void Render(interstice::ID id, RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);

    private:
        // Captured primitives per cached scope, kept across frames. m_captureSaved
        // holds the window target while a capture redirects emit calls.
        std::unordered_map<interstice::ID, PrimitiveRenderTarget> scopeCaptures;
        PrimitiveRenderTarget* m_captureSaved = nullptr;
    };
}
