#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/diagnostics/gui_profiler.h>

#include <algorithm>
#include <cstdint>

namespace tachyon
{

// Snapped edges are 16-bit: an edge past the range (a sizer stretched to kStretchHeight)
// saturates, it doesn't wrap around to the other side.
static int16_t SaturateInt16(int32_t v) { return int16_t(std::clamp<int32_t>(v, INT16_MIN, INT16_MAX)); }

void PrimitiveRenderer::Render(tachyon::ID id, RHI::RenderCommandList& cmdList,
                               const PipelineSet& pipelines, GPUDrawPushConstants& push_constants)
{
    auto& target = GetTarget(id);
    target.Render(cmdList, pipelines, push_constants);
}


void PrimitiveRenderTarget::Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                                   GPUDrawPushConstants& push_constants)
{
    FinishRenderPass();

    std::array<RHI::UploadRequest, kPrimitiveKindCount> uploads{};
    std::array<DeviceAddress, kPrimitiveKindCount> addresses{};
    for (size_t k = 0; k < kPrimitiveKindCount; ++k)
    {
        const auto& cpu = streams[k].cpu;
        uploads[k] = RHI::UploadRequest{ cpu.data(), cpu.size(), 16 };
    }
    cmdList.UploadTransientBuffers(uploads, addresses);

    // Pre-fill the canonical slots (Quad aliases Triangle's and is written
    // per-pass below; pre-filling it here would clobber Triangle's address).
    for (size_t k = 0; k < kPrimitiveKindCount; k++)
    {
        if (PrimitiveKind(k) == PrimitiveKind::Quad)
            continue;
        push_constants.primitiveAddresses[kPrimitiveSpecs[k].pushSlot] = addresses[k];
    }

    for (const auto& pass : renderPasses)
    {
        const auto& spec = kPrimitiveSpecs[size_t(pass.kind)];
        push_constants.primitiveAddresses[spec.pushSlot] = addresses[size_t(pass.kind)];
        cmdList.DrawPrimitives(pipelines[size_t(pass.kind)], push_constants,
                               uint32_t(pass.primitiveCount), uint32_t(pass.firstPrimitive));
    }
}


void PrimitiveRenderTarget::Reset()
{
    renderPasses.clear();
    for (auto& stream : streams)
    {
        stream.cpu.clear();
        stream.passStart = 0;
    }
    currentKind = -1;
}


void PrimitiveRenderTarget::SetTransform(tachyon::float3x2 transform)
{
    currentTransform = transform;
}

void PrimitiveRenderTarget::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex, tachyon::IntRect snappedPx)
{
    Append(PrimitiveKind::Box, Box
                    {
                        .transform0   = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1   = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin      = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax      = { clipRect.Max(0), clipRect.Max(1) },

                        .pos          = { rect.x, rect.y }, .size = { rect.w, rect.h },

                        .textureIndex = textureIndex,
                        .styleIndex   = fromStyleIndex,
                        .colorIndex   = int16_t(colorStyleIndex),
                        .styleIndexTo = toStyleIndex,
                        .blendT       = blendT,
                        .snappedPx    = { SaturateInt16(snappedPx.x), SaturateInt16(snappedPx.y),
                                          SaturateInt16(snappedPx.x + snappedPx.w), SaturateInt16(snappedPx.y + snappedPx.h) }
                    });
}

void PrimitiveRenderTarget::RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex)
{
    Append(PrimitiveKind::Triangle, Triangle
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .p0 = { p0.x, p0.y },
                        .p1 = { p1.x, p1.y },
                        .p2 = { p2.x, p2.y },

                        .textureIndex = textureIndex,
                        .colorIndex = int16_t(colorStyleIndex)
                    });
}


void PrimitiveRenderTarget::RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, tachyon::ImageIndex textureIndex)
{
    Append(PrimitiveKind::Quad, Quad
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { rect.x, rect.y }, .size = { rect.w, rect.h },

                        .textureIndex = textureIndex,
                        .colorIndex = int16_t(colorStyleIndex),
                        .cornerRadius = cornerRadius
                    });
}

void PrimitiveRenderTarget::RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur)
{
    blur = std::clamp(blur, 0, 4096);
    Append(PrimitiveKind::Quad, Quad
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { rect.x - blur, rect.y - blur }, .size = { rect.w + 2 * blur, rect.h + 2 * blur },

                        .colorIndex = int16_t(colorStyleIndex),
                        .blur = int16_t(blur),
                        .cornerRadius = cornerRadius
                    });
}

void PrimitiveRenderTarget::RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex)
{
    Append(PrimitiveKind::Glyph, Glyph
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { pos.x, pos.y }, .size = { size.x, size.y },

                        .uvMin = { uvMin.x, uvMin.y }, .uvMax = { uvMax.x, uvMax.y },

                        .scale = scale,
                        .bpp = uint16_t(bpp),
                        .mode = uint16_t(mode),

                        .textureIndex = textureIndex,

                        .textColorIndex = int16_t(textColorIndex),
                     });
}

void PrimitiveRenderTarget::RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3,
                                       tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle)
{
    Append(PrimitiveKind::Line, Line
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .colorIndex     = int(lineStyle.colorStyleIndex),
                        .lineWidth      = lineStyle.lineWidth,
                        .dashSize       = lineStyle.dashSize,
                        .dashOffset     = lineStyle.dashOffset,
                        .gradientOffset = lineStyle.gradientOffset,
                        .gradientScale  = lineStyle.gradientScale,

                        .p0 = { int16_t(p0.x), int16_t(p0.y) },
                        .p1 = { int16_t(p1.x), int16_t(p1.y) },
                        .p2 = { int16_t(p2.x), int16_t(p2.y) },
                        .p3 = { int16_t(p3.x), int16_t(p3.y) }
                    });
}

void PrimitiveRenderTarget::RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color)
{
    Append(PrimitiveKind::Dot, Dot
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .color = color,
                        .size = size,
                        .pos = { pos.x, pos.y }
                    });
}


void PrimitiveRenderTarget::FinishRenderPass()
{
    if (currentKind < 0)
        return;

    const auto& spec   = kPrimitiveSpecs[size_t(currentKind)];
    auto&       stream = streams[size_t(currentKind)];
    const int   count  = int(stream.cpu.size() / spec.stride);
    renderPasses.push_back(PrimitiveRenderPass
    {
        .firstPrimitive = stream.passStart,
        .primitiveCount = count - stream.passStart,
        .kind           = PrimitiveKind(currentKind)
    });
    currentKind = -1;
}

void PrimitiveRenderTarget::StartRenderPass(PrimitiveKind kind)
{
    if (currentKind == int(kind))
        return;

    FinishRenderPass();
    auto& stream = streams[size_t(kind)];
    stream.passStart = int(stream.cpu.size() / kPrimitiveSpecs[size_t(kind)].stride);
    currentKind = int(kind);
}

void PrimitiveRenderTarget::AppendCaptured(const PrimitiveRenderTarget& src, tachyon::int2 delta)
{
    TACHYON_PROFILE_SCOPE("AppendCaptured");
    // Every primitive kind starts with the same header, so the translate is
    // kind-agnostic: transform0 @0 (z @8), transform1 @16 (z @24), then the
    // screen-space clip rect clipMin @32 / clipMax @40 (two int2s).
    static_assert(offsetof(Box, transform0) == 0,  "primitive header layout changed");
    static_assert(offsetof(Box, transform1) == 16, "primitive header layout changed");
    static_assert(offsetof(Box, clipMin)    == 32, "primitive header layout changed");
    static_assert(offsetof(Box, clipMax)    == 40, "primitive header layout changed");

    FinishRenderPass(); // close any open pass in the destination first

    const bool translate = (delta.x != 0 || delta.y != 0);
    for (const auto& pass : src.renderPasses)
    {
        const size_t   k       = size_t(pass.kind);
        const uint32_t stride  = kPrimitiveSpecs[k].stride;
        const auto&    srcBytes = src.streams[k].cpu;
        auto&          dstBytes = streams[k].cpu;

        const int    dstFirst = int(dstBytes.size() / stride);
        const size_t srcOff   = size_t(pass.firstPrimitive) * stride;
        const size_t len      = size_t(pass.primitiveCount) * stride;
        const size_t base     = dstBytes.size();
        dstBytes.insert(dstBytes.end(), srcBytes.begin() + srcOff, srcBytes.begin() + srcOff + len);

        if (translate)
        {
            for (int p = 0; p < pass.primitiveCount; p++)
            {
                std::byte* hdr = dstBytes.data() + base + size_t(p) * stride;
                float* t = reinterpret_cast<float*>(hdr);
                t[2] += float(delta.x); // transform0.z (translation x)
                t[6] += float(delta.y); // transform1.z (translation y)
                int32_t* clip = reinterpret_cast<int32_t*>(hdr + 32);
                clip[0] += delta.x; clip[1] += delta.y; // clipMin
                clip[2] += delta.x; clip[3] += delta.y; // clipMax
                if (pass.kind == PrimitiveKind::Box)
                {
                    Box* box = reinterpret_cast<Box*>(hdr);
                    if (box->snappedPx[2] > box->snappedPx[0] || box->snappedPx[3] > box->snappedPx[1])
                    {
                        box->snappedPx[0] = SaturateInt16(box->snappedPx[0] + delta.x);
                        box->snappedPx[1] = SaturateInt16(box->snappedPx[1] + delta.y);
                        box->snappedPx[2] = SaturateInt16(box->snappedPx[2] + delta.x);
                        box->snappedPx[3] = SaturateInt16(box->snappedPx[3] + delta.y);
                    }
                }
            }
        }

        renderPasses.push_back(PrimitiveRenderPass{ dstFirst, pass.primitiveCount, pass.kind });
    }
    currentKind = -1;
}


void PrimitiveRenderer::Reset()
{
    if (resizingTarget != nullptr)
    {
        resizingTarget->Reset();
    } else
    {
        for (auto& target : idTargets) { target.second.Reset(); }
    }
    currentTarget = nullptr;
}

void PrimitiveRenderer::SetTransform(tachyon::float3x2 transform)
{
    assert(currentTarget != nullptr);
    currentTarget->SetTransform(transform);
}

void PrimitiveRenderer::ReleaseTarget(tachyon::ID id)
{
    auto it = idTargets.find(id);
    if (it == idTargets.end())
        return;

    if (currentTarget  == &it->second) currentTarget  = nullptr;
    if (resizingTarget == &it->second) resizingTarget = nullptr;
    idTargets.erase(it);
}

void PrimitiveRenderer::ReleaseAllTargets()
{
    idTargets.clear();
    currentTarget  = nullptr;
    resizingTarget = nullptr;
}

// Make it possible to render windows without caption/border/transparency etc.
bool PrimitiveRenderer::BeginWindow(tachyon::ID id)
{
    assert(currentTarget == nullptr);
    auto& target = GetTarget(id);
    if (resizingTarget != nullptr)
    {
        if (resizingTarget != &target)
            return false;
    }
    currentTarget = &target;
    return true;
}

void PrimitiveRenderer::EndWindow()
{
    assert(currentTarget != nullptr);
    currentTarget = nullptr;
}

void PrimitiveRenderer::BeginScopeCapture(tachyon::ID scopeId)
{
    assert(currentTarget != nullptr);
    assert(m_captureSaved == nullptr && "nested scope capture is not supported");
    m_captureSaved = currentTarget;
    PrimitiveRenderTarget& cap = scopeCaptures[scopeId];
    cap.Reset();                 // fresh capture (CPU-only; never uploaded)
    currentTarget = &cap;        // redirect emit calls into the capture buffer
}

void PrimitiveRenderer::EndScopeCapture(tachyon::ID scopeId)
{
    assert(m_captureSaved != nullptr);
    PrimitiveRenderTarget& cap = scopeCaptures[scopeId];
    cap.FinishRenderPass();
    currentTarget  = m_captureSaved;
    m_captureSaved = nullptr;
    // Emit the just-captured subtree into the window this frame (no translation).
    currentTarget->AppendCaptured(cap, tachyon::int2{ 0, 0 });
}

bool PrimitiveRenderer::ReplayScopeCapture(tachyon::ID scopeId, tachyon::int2 delta)
{
    assert(currentTarget != nullptr);
    auto it = scopeCaptures.find(scopeId);
    if (it == scopeCaptures.end() || it->second.renderPasses.empty())
        return false;
    currentTarget->AppendCaptured(it->second, delta);
    return true;
}

bool PrimitiveRenderer::HasScopeCapture(tachyon::ID scopeId) const
{
    auto it = scopeCaptures.find(scopeId);
    return it != scopeCaptures.end() && !it->second.renderPasses.empty();
}

void PrimitiveRenderer::InvalidateScopeCapture(tachyon::ID scopeId)
{
    scopeCaptures.erase(scopeId);
}

void PrimitiveRenderer::ClearScopeCaptures()
{
    scopeCaptures.clear();
}

void PrimitiveRenderer::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex, tachyon::IntRect snappedPx)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderBox(rect, clipRect, fromStyleIndex, toStyleIndex, blendT, colorStyleIndex, textureIndex, snappedPx);
}

void PrimitiveRenderer::RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderGlyph(pos, size, uvMin, uvMax, scale, bpp, mode, textColorIndex, clipRect, textureIndex);
}

void PrimitiveRenderer::RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderLine(p0, p1, p2, p3, clipRect, lineStyle);
}

void PrimitiveRenderer::RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderDot(pos, clipRect, size, color);
}

void PrimitiveRenderer::RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderTriangle(p0, p1, p2, clipRect, colorStyleIndex, textureIndex);
}

void PrimitiveRenderer::RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, tachyon::ImageIndex textureIndex)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderQuad(rect, clipRect, colorStyleIndex, cornerRadius, textureIndex);
}

void PrimitiveRenderer::RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderShadow(rect, clipRect, colorStyleIndex, cornerRadius, blur);
}

}
