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
    /// @addtogroup graphics
    /// @{

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

    /// GPU storage kept across frames (UploadResidentBuffers); 0 is none.
    using ResidentBufferId = uint64_t;

    /// Where a resident buffer is.
    enum class ResidentState : uint8_t
    {
        Gone,    ///< released, or never made (or lost with its device)
        Pending, ///< its copy to the GPU isn't recorded yet: it can't be drawn from this frame
        Ready,   ///< on the GPU: draws recorded from now on can read it
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
        /// `primitiveCount` primitives starting at `firstPrimitive`, each as `shape` says;
        /// the vertex shader builds the vertices from the primitive.
        virtual void DrawPrimitives(const GpuPipeline& pipeline,
                                    const GPUDrawPushConstants& pushConstants,
                                    uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape) = 0;

        /// Copies a batch of transient data into storage that remains valid until
        /// this command list's frame fence signals. One device address is
        /// returned per request; empty requests produce address 0.
        virtual void UploadTransientBuffers(std::span<const UploadRequest> requests,
                                            std::span<DeviceAddress> addresses) = 0;

        /// Copies a batch of data into GPU memory that stays until released, for data drawn
        /// again over many frames (a cached scope's primitives). The copy is made at the start
        /// of a later frame (see ResidentBufferState); the addresses are its final ones. One
        /// address per request; empty requests produce address 0, all-empty no buffer.
        virtual ResidentBufferId UploadResidentBuffers(std::span<const UploadRequest> requests,
                                                       std::span<DeviceAddress> addresses) = 0;
        virtual ResidentState ResidentBufferState(ResidentBufferId id) const = 0;
        /// Frees the buffer once no frame in flight reads it; an unknown id does nothing.
        virtual void ReleaseResidentBuffer(ResidentBufferId id) = 0;
    };
    /// @}
}
