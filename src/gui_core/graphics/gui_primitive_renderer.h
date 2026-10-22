#pragma once

// API-neutral GUI primitive renderer: per-window targets accumulate per-kind
// primitive streams, then Render() uploads each stream and replays the pass
// list through the RHI. No graphics-API types appear here — a backend
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

namespace GUI
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
        GUI::float3x2 currentTransform;

    public:
        void FinishRenderPass();
        void StartRenderPass(PrimitiveKind kind);

        // Append another target's passes, translated by `delta` in screen space.
        // Kind-agnostic: every primitive starts with the same header (transform0,
        // transform1, clipMin, clipMax).
        void AppendCaptured(const PrimitiveRenderTarget& src, GUI::int2 delta);

    public:
        void Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);
        void Reset();

        void SetTransform(GUI::float3x2 transform);
        void RenderBox(GUI::IntRect rect, GUI::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, GUI::VisualColorStyleIndex colorStyleIndex = {}, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid, GUI::IntRect snappedPx = {});
        void RenderGlyph(GUI::float2 pos, GUI::float2 size, GUI::float2 uvMin, GUI::float2 uvMax, float scale, int bpp, int mode, GUI::VisualColorStyleIndex textColorIndex, GUI::IntRect clipRect, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid);
        void RenderLine(GUI::int2 p0, GUI::int2 p1, GUI::int2 p2, GUI::int2 p3, GUI::IntRect clipRect, const GUI::LineStyle& lineStyle);
        void RenderDot(GUI::float2 pos, GUI::IntRect clipRect, float size, GUI::color4f color);
        void RenderTriangle(GUI::float2 p0, GUI::float2 p1, GUI::float2 p2, GUI::IntRect clipRect, GUI::VisualColorStyleIndex colorStyleIndex, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid);
        void RenderQuad(GUI::IntRect rect, GUI::IntRect clipRect, GUI::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid);

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
        std::unordered_map<GUI::ID, PrimitiveRenderTarget> idTargets;
        PrimitiveRenderTarget* currentTarget{ nullptr };
        PrimitiveRenderTarget* resizingTarget{ nullptr };

    public:
        void Reset() override;
        void SetTransform(GUI::float3x2 transform) override;
        void RenderBox(GUI::IntRect rect, GUI::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, GUI::VisualColorStyleIndex colorStyleIndex = {}, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid, GUI::IntRect snappedPx = {}) override;
        void RenderGlyph(GUI::float2 pos, GUI::float2 size, GUI::float2 uvMin, GUI::float2 uvMax, float scale, int bpp, int mode, GUI::VisualColorStyleIndex textColorIndex, GUI::IntRect clipRect, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid) override;
        void RenderLine(GUI::int2 p0, GUI::int2 p1, GUI::int2 p2, GUI::int2 p3, GUI::IntRect clipRect, const GUI::LineStyle& lineStyle) override;
        void RenderDot(GUI::float2 pos, GUI::IntRect clipRect, float size, GUI::color4f color) override;
        void RenderTriangle(GUI::float2 p0, GUI::float2 p1, GUI::float2 p2, GUI::IntRect clipRect, GUI::VisualColorStyleIndex colorStyleIndex, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid) override;
        void RenderQuad(GUI::IntRect rect, GUI::IntRect clipRect, GUI::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid) override;

    public:
        PrimitiveRenderTarget& GetTarget(GUI::ID id)
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
        void ReleaseTarget(GUI::ID id);
        void ReleaseAllTargets();

        bool StartResizing(GUI::ID id) override
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

        bool BeginWindow(GUI::ID id) override;
        void EndWindow() override;

        // Draw caching. Capture targets are CPU-only; their primitives are merged
        // into the window target on capture/replay.
        void BeginScopeCapture(GUI::ID scopeId) override;
        void EndScopeCapture(GUI::ID scopeId) override;
        bool ReplayScopeCapture(GUI::ID scopeId, GUI::int2 delta) override;
        bool HasScopeCapture(GUI::ID scopeId) const override;
        void InvalidateScopeCapture(GUI::ID scopeId) override;
        void ClearScopeCaptures() override;

        void Render(GUI::ID id, RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);

    private:
        // Captured primitives per cached scope, kept across frames. m_captureSaved
        // holds the window target while a capture redirects emit calls.
        std::unordered_map<GUI::ID, PrimitiveRenderTarget> scopeCaptures;
        PrimitiveRenderTarget* m_captureSaved = nullptr;
    };
}
