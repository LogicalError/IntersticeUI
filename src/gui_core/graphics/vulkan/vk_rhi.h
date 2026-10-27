#pragma once

// Vulkan implementation of the GUI RHI (gui_rhi.h).

#include <gui_core/graphics/gui_rhi.h>

#include <vulkan/vulkan.h>

#include <array>

namespace tachyon::vulkan
{
    class VulkanDeviceContext;
    struct DeletionQueue;
    struct FrameData;

    /// The Vulkan-side GUI pipeline set, built once at device init from
    /// kPrimitiveSpecs. Indexed by PrimitiveKind; all share one layout.
    struct GUIRenderContext
    {
        bool Create(VulkanDeviceContext& deviceContext);

        std::array<tachyon::RHI::GpuPipeline, tachyon::kPrimitiveKindCount> pipelines{};
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
                            uint32_t primitiveCount, uint32_t firstPrimitive) override;
        void UploadTransientBuffers(std::span<const tachyon::RHI::UploadRequest> requests,
                                    std::span<tachyon::DeviceAddress> addresses) override;

    private:
        VulkanDeviceContext& dc;
        VkCommandBuffer      cmd;
        VkDescriptorSet      imageSet;
        FrameData&           frame;
    };
}
