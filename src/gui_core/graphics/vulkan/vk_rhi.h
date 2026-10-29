#pragma once

// Vulkan implementation of the GUI RHI (gui_rhi.h).

#include <gui_core/graphics/gui_rhi.h>
#include <gui_core/graphics/vulkan/vk_meshes.h>

#include <vulkan/vulkan.h>

#include <array>

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    class VulkanDeviceContext;
    struct DeletionQueue;
    struct FrameData;

    /// The Vulkan-side GUI pipeline set, built once at device init from
    /// kPrimitiveSpecs. Indexed by PrimitiveKind; all share one layout.
    struct GUIRenderContext
    {
        bool Create(VulkanDeviceContext& deviceContext);

        /// The index buffer of quad-shaped primitives (0,1,3, 0,3,2 for each four vertices)
        /// for at least `primitiveCount` of them, grown as passes need. A replaced buffer
        /// stays until shutdown: frames in flight may still read it.
        VkBuffer QuadIndices(VulkanDeviceContext& deviceContext, uint32_t primitiveCount);

        std::array<tachyon::RHI::GpuPipeline, tachyon::kPrimitiveKindCount> pipelines{};
        Buffer   quadIndexBuffer {};
        uint32_t quadIndexCapacity = 0; ///< primitives quadIndexBuffer holds indices for
    };

    class VulkanRenderDevice final : public tachyon::RHI::RenderDevice
    {
    public:
        explicit VulkanRenderDevice(VulkanDeviceContext& deviceContext)
            : dc(deviceContext) {}

        tachyon::RHI::GpuPipeline CreatePrimitivePipeline(const tachyon::PrimitiveSpec& spec) override;

        /// All GUI pipelines share one layout (identical push range + image
        /// set). Created lazily by the first CreatePrimitivePipeline call.
        VkPipelineLayout sharedGuiLayout = VK_NULL_HANDLE;
        void DestroySharedLayout();

    private:
        VulkanDeviceContext& dc;
    };

    class VulkanRenderCommandList final : public tachyon::RHI::RenderCommandList
    {
    public:
        VulkanRenderCommandList(VulkanDeviceContext& deviceContext,
                                VkCommandBuffer commandBuffer,
                                VkDescriptorSet imageDescriptorSet,
                                FrameData& frameData)
            : dc(deviceContext), cmd(commandBuffer)
            , imageSet(imageDescriptorSet), frame(frameData) {}

        void DrawPrimitives(const tachyon::RHI::GpuPipeline& pipeline,
                            const tachyon::GPUDrawPushConstants& pushConstants,
                            uint32_t primitiveCount, uint32_t firstPrimitive, tachyon::PrimitiveShape shape) override;
        void UploadTransientBuffers(std::span<const tachyon::RHI::UploadRequest> requests,
                                    std::span<tachyon::DeviceAddress> addresses) override;
        tachyon::RHI::ResidentBufferId UploadResidentBuffers(std::span<const tachyon::RHI::UploadRequest> requests,
                                                             std::span<tachyon::DeviceAddress> addresses) override;
        tachyon::RHI::ResidentState ResidentBufferState(tachyon::RHI::ResidentBufferId id) const override;
        void ReleaseResidentBuffer(tachyon::RHI::ResidentBufferId id) override;

    private:
        VulkanDeviceContext& dc;
        VkCommandBuffer      cmd;
        VkDescriptorSet      imageSet;
        FrameData&           frame;
    };
    /// @}
}
