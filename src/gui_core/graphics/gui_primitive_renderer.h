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

    /// One contiguous same-kind run of primitives, drawn with one call; or, with `replay`
    /// set, a cached scope's draw (its own passes), and the other fields unused.
    struct PrimitiveRenderPass
    {
        int           firstPrimitive;
        int           primitiveCount;
        PrimitiveKind kind;
        int           replay = -1; ///< index into PrimitiveRenderTarget::replays
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

        /// A cached scope drawn here, moved `delta` window pixels from where it was captured;
        /// its primitives' draw orders start at `baseOrder`.
        struct ScopeReplay { tachyon::ID scope; tachyon::int2 delta; uint32_t baseOrder = 0; };
        std::vector<ScopeReplay> replays;

    public:
        void FinishRenderPass();
        void StartRenderPass(PrimitiveKind kind);

        /// Appends another target's passes, translated by `delta` pixels. Works for every kind:
        /// each primitive starts with the same header (transform0, transform1, clipMin, clipMax).
        void AppendCaptured(const PrimitiveRenderTarget& src, tachyon::int2 delta);
        /// Closes the open pass and adds a pass that draws cached scope `scope`, moved by `delta`;
        /// it takes `primitiveCount` draw orders.
        void AddReplay(tachyon::ID scope, tachyon::int2 delta, uint32_t primitiveCount);

    public:
        /// Uploads the streams and draws the passes; replay passes are left to the caller
        /// (PrimitiveRenderer). Returns the bytes uploaded.
        size_t Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                      GPUDrawPushConstants& push_constants);
        void Reset();

        void SetTransform(tachyon::float3x2 transform);
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, tachyon::VisualColorStyleIndex colorStyleIndex = {}, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {});
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, float shadowBlur = -1.0f);
        void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle);
        void RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle);
        void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color);
        void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid);
        void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid);
        void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur);
        void RenderOccluder(tachyon::IntRect rect, tachyon::IntRect clipRect);

        /// The draw order the next primitive gets; also counts a replay's primitives.
        uint32_t nextOrder = 0;

    private:
        /// Closes an open pass of a different kind, then appends `primitive` to its kind's stream.
        template <typename T>
        void Append(PrimitiveKind kind, const T& primitive)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            assert(sizeof(T) == kPrimitiveSpecs[size_t(kind)].stride);
            StartRenderPass(kind);
            auto& cpu = streams[size_t(kind)].cpu;
            T ordered = primitive;
            ordered.transform0.w = float(nextOrder++);
            const auto* bytes = reinterpret_cast<const std::byte*>(&ordered);
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
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, float shadowBlur = -1.0f) override;
        void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle) override;
        void RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle) override;
        void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color) override;
        void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) override;
        void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) override;
        void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur) override;
        void RenderOccluder(tachyon::IntRect rect, tachyon::IntRect clipRect) override;

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

        // Draw caching. A capture's primitives go to the GPU once and are drawn from there,
        // moved by the world matrix, each time it is replayed; until their copy is on the GPU
        // they are uploaded with the frame.
        void BeginScopeCapture(tachyon::ID scopeId) override;
        void EndScopeCapture(tachyon::ID scopeId) override;
        bool ReplayScopeCapture(tachyon::ID scopeId, tachyon::int2 delta) override;
        bool HasScopeCapture(tachyon::ID scopeId) const override;
        void InvalidateScopeCapture(tachyon::ID scopeId) override;
        void ClearScopeCaptures() override;

        void Render(tachyon::ID id, RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);

        /// Primitive bytes the last Render uploaded with the frame; cached scopes drawn from
        /// their GPU copy add nothing.
        size_t LastFrameUploadBytes() const { return m_lastUploadBytes; }
        /// Captured scopes with a GPU copy (made or on its way).
        size_t ResidentScopeCount() const;

        /// A capture neither recorded nor replayed for this many frames is dropped, its GPU copy
        /// freed (as UICacheManager drops its scopes).
        static constexpr uint64_t kEvictAfterFrames = 240;

    private:
        /// A cached scope's primitives, and their copy on the GPU.
        struct ScopeCapture
        {
            PrimitiveRenderTarget target;
            RHI::ResidentBufferId resident = 0;
            std::array<DeviceAddress, kPrimitiveKindCount> addresses{}; ///< in the resident buffer
            uint64_t lastUsedFrame = 0;
        };

        /// The addresses `capture` is drawn from this frame: its GPU copy when that is ready,
        /// else a transient upload of its primitives.
        std::array<DeviceAddress, kPrimitiveKindCount> CaptureAddresses(ScopeCapture& capture, RHI::RenderCommandList& cmdList);
        /// Drops the capture's GPU copy (freed on the next Render).
        void ReleaseResident(ScopeCapture& capture);

        /// Captured primitives per cached scope, kept across frames.
        std::unordered_map<tachyon::ID, ScopeCapture> scopeCaptures;
        /// GPU copies to free, on the next Render (which has the command list).
        std::vector<RHI::ResidentBufferId> m_releasedResidents;
        /// The window target while a capture redirects emit calls.
        PrimitiveRenderTarget* m_captureSaved = nullptr;
        uint64_t m_frame = 0;
        size_t   m_lastUploadBytes = 0;
    };
    /// @}
}
