#pragma once

// Thin render-hardware-interface for the GUI primitive renderer - deliberately
// minimal: pipeline creation from a PrimitiveSpec, transient batch upload to
// fence-protected storage, and draw. Textures, samplers, descriptor
// sets, render passes and the swapchain are NOT part of this interface; they
// stay behind GraphicsContext / the backend's window context. A second backend
// implements these two classes (plus GraphicsContext) and compiles the shader
// pairs named by kPrimitiveSpecs to its own format.

#include <gui_core/graphics/gui_render_primitives.h>

#include <span>

namespace tachyon::RHI
{
    struct GpuPipeline
    {
        uint64_t pipeline = 0;          ///< backend pipeline handle (VkPipeline on Vulkan)
        uint64_t layout   = 0;          ///< its layout, the shared GUI layout (VkPipelineLayout)
        bool IsValid() const { return pipeline != 0; }
    };

    struct UploadRequest
    {
        const void* data      = nullptr;
        size_t      bytes     = 0;
        size_t      alignment = 16;
    };

    /// Device-lifetime operations.
    class RenderDevice
    {
    public:
        virtual ~RenderDevice() = default;

        /// Builds the pipeline for one primitive kind: the spec's shader pair +
        /// the fixed GUI pipeline state (triangle list, fill, no cull,
        /// premultiplied alpha blend, depth GREATER_OR_EQUAL, the GUI draw /
        /// depth formats) on the shared GUI pipeline layout.
        virtual GpuPipeline CreatePrimitivePipeline(const PrimitiveSpec& spec) = 0;

    };

    /// One frame's recording surface for one window.
    class RenderCommandList
    {
    public:
        virtual ~RenderCommandList() = default;

        /// Binds the pipeline and the GUI image set, pushes the constants and draws
        /// `primitiveCount` primitives starting at `firstPrimitive`. The
        /// per-primitive vertex expansion is a backend/shader contract detail.
        virtual void DrawPrimitives(const GpuPipeline& pipeline,
                                    const GPUDrawPushConstants& pushConstants,
                                    uint32_t primitiveCount, uint32_t firstPrimitive) = 0;

        /// Copies a batch of transient data into storage that remains valid until
        /// this command list's frame fence signals. One device address is
        /// returned per request; empty requests produce address 0.
        virtual void UploadTransientBuffers(std::span<const UploadRequest> requests,
                                            std::span<DeviceAddress> addresses) = 0;
    };
}
