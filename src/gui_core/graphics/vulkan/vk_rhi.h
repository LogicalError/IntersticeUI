#pragma once

// Vulkan implementation of the GUI RHI (gui_rhi.h).

#include <gui_core/graphics/gui_rhi.h>

#include <vulkan/vulkan.h>

#include <array>

namespace interstice::vulkan
{
    class VulkanDeviceContext;
    struct DeletionQueue;
    struct FrameData;

    /// The Vulkan-side GUI pipeline set, built once at device init from
    /// kPrimitiveSpecs. Indexed by PrimitiveKind; all share one layout.
    struct GUIRenderContext
    {
        bool Create(VulkanDeviceContext& deviceContext);

        std::array<interstice::RHI::GpuPipeline, interstice::kPrimitiveKindCount> pipelines{};
    };

    class VulkanRenderDevice final : public interstice::RHI::RenderDevice
    {
    public:
        explicit VulkanRenderDevice(VulkanDeviceContext& deviceContext)
            : dc(deviceContext) {}

        interstice::RHI::GpuPipeline CreatePrimitivePipeline(const interstice::PrimitiveSpec& spec) override;

        /// All GUI pipelines share one layout (identical push range + image
        /// set). Created lazily by the first CreatePrimitivePipeline call.
        VkPipelineLayout sharedGuiLayout = VK_NULL_HANDLE;
        void DestroySharedLayout();

    private:
        VulkanDeviceContext& dc;
    };

    class VulkanRenderCommandList final : public interstice::RHI::RenderCommandList
    {
    public:
        VulkanRenderCommandList(VulkanDeviceContext& deviceContext,
                                VkCommandBuffer commandBuffer,
                                VkDescriptorSet imageDescriptorSet,
                                FrameData& frameData)
            : dc(deviceContext), cmd(commandBuffer)
            , imageSet(imageDescriptorSet), frame(frameData) {}

        void DrawPrimitives(const interstice::RHI::GpuPipeline& pipeline,
                            const interstice::GPUDrawPushConstants& pushConstants,
                            uint32_t primitiveCount, uint32_t firstPrimitive) override;
        void UploadTransientBuffers(std::span<const interstice::RHI::UploadRequest> requests,
                                    std::span<interstice::DeviceAddress> addresses) override;

    private:
        VulkanDeviceContext& dc;
        VkCommandBuffer      cmd;
        VkDescriptorSet      imageSet;
        FrameData&           frame;
    };
}
