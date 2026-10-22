#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_rhi.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_initializers.h>
#include <gui_core/graphics/vulkan/vk_pipelines.h>
#include <gui_core/graphics/vulkan/vk_defaults.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static constexpr GUI::Diagnostics::LogCategory kLogVulkan = GUI::Diagnostics::MakeLogCategory("vulkan");

using namespace Vulkan;

bool GUIRenderContext::Create(Vulkan::VulkanDeviceContext& deviceContext)
{
    VulkanRenderDevice device(deviceContext);

    for (size_t k = 0; k < GUI::kPrimitiveKindCount; k++)
    {
        pipelines[k] = device.CreatePrimitivePipeline(GUI::kPrimitiveSpecs[k]);
        if (!pipelines[k].IsValid())
        {
            for (size_t j = 0; j < k; j++)
                vkDestroyPipeline(deviceContext.device,
                                  reinterpret_cast<VkPipeline>(pipelines[j].pipeline), nullptr);
            device.DestroySharedLayout();
            return false;
        }
    }

    deviceContext.mainDeletionQueue.push_function(
        [vkDevice = deviceContext.device, pipelines = pipelines]()
        {
            for (const auto& p : pipelines)
                vkDestroyPipeline(vkDevice, reinterpret_cast<VkPipeline>(p.pipeline), nullptr);
            // All pipelines share one layout; destroy it once.
            vkDestroyPipelineLayout(vkDevice,
                                    reinterpret_cast<VkPipelineLayout>(pipelines[0].layout), nullptr);
        });

    return true;
}

// Reads a SPIR-V file into words; empty on failure.
static std::vector<uint32_t> ReadSpirvFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};

    const size_t fileSize = (size_t)file.tellg();
    if (fileSize == 0 || fileSize % sizeof(uint32_t) != 0) return {};

    std::vector<uint32_t> words(fileSize / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), (std::streamsize)fileSize)) return {};
    return words;
}

static VkShaderModule LoadShader(const Vulkan::VulkanDeviceContext& dc, const std::string& filePath)
{
    std::vector<uint32_t> words = ReadSpirvFile(filePath);
    return words.empty() ? nullptr : dc.CreateShader(words);
}

GUI::RHI::GpuPipeline VulkanRenderDevice::CreatePrimitivePipeline(const GUI::PrimitiveSpec& spec)
{
    const std::string base = std::string("shaders_compiled/") + spec.shaderBaseName;

    VkShaderModule frag = LoadShader(dc, base + ".frag.spv");
    if (frag == nullptr)
    {
        LOG_ERROR(kLogVulkan, "Error when building the {} fragment shader module", spec.shaderBaseName);
        return {};
    }
    VkShaderModule vert = LoadShader(dc, base + ".vert.spv");
    if (vert == nullptr)
    {
        LOG_ERROR(kLogVulkan, "Error when building the {} vertex shader module", spec.shaderBaseName);
        vkDestroyShaderModule(dc.device, frag, nullptr);
        return {};
    }

    if (sharedGuiLayout == VK_NULL_HANDLE)
    {
        VkPushConstantRange bufferRange{};
        bufferRange.offset = 0;
        bufferRange.size = sizeof(GUI::GPUDrawPushConstants);
        bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();
        layoutInfo.pSetLayouts = &dc.singleImageDescriptorLayout;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pPushConstantRanges = &bufferRange;
        layoutInfo.pushConstantRangeCount = 1;
        VK_CHECK(vkCreatePipelineLayout(dc.device, &layoutInfo, nullptr, &sharedGuiLayout));
    }

    // The fixed GUI pipeline state, identical for every primitive kind.
    PipelineBuilder builder;
    builder._pipelineLayout = sharedGuiLayout;
    builder.set_shaders(vert, frag);
    builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.set_multisampling_none();
    builder.enable_blending_premultipliedalphablend();
    builder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    builder.set_color_attachment_format(Vulkan::drawImageFormat);
    builder.set_depth_format(Vulkan::depthImageFormat);

    VkPipeline pipeline = builder.build_pipeline(dc.device);

    vkDestroyShaderModule(dc.device, frag, nullptr);
    vkDestroyShaderModule(dc.device, vert, nullptr);

    return { reinterpret_cast<uint64_t>(pipeline), reinterpret_cast<uint64_t>(sharedGuiLayout) };
}

void VulkanRenderDevice::DestroySharedLayout()
{
    if (sharedGuiLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(dc.device, sharedGuiLayout, nullptr);
        sharedGuiLayout = VK_NULL_HANDLE;
    }
}

void VulkanRenderCommandList::DrawPrimitives(const GUI::RHI::GpuPipeline& pipeline,
                                             const GUI::GPUDrawPushConstants& pushConstants,
                                             uint32_t primitiveCount, uint32_t firstPrimitive)
{
    const auto vkPipeline = reinterpret_cast<VkPipeline>(pipeline.pipeline);
    const auto vkLayout   = reinterpret_cast<VkPipelineLayout>(pipeline.layout);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkLayout, 0, 1, &imageSet, 0, nullptr);
    vkCmdPushConstants(cmd, vkLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(GUI::GPUDrawPushConstants), &pushConstants);
    // The GUI shaders expand each primitive to 6 vertices.
    vkCmdDraw(cmd, primitiveCount * 6, 1, firstPrimitive * 6, 0);
}

void VulkanRenderCommandList::UploadTransientBuffers(
    std::span<const GUI::RHI::UploadRequest> requests,
    std::span<GUI::DeviceAddress> addresses)
{
    assert(requests.size() == addresses.size());
    std::fill(addresses.begin(), addresses.end(), GUI::DeviceAddress{ 0 });

    auto calculateEnd = [&](size_t start) -> size_t
    {
        size_t offset = start;
        for (const auto& request : requests)
        {
            if (request.bytes == 0)
                continue;
            const size_t alignment = std::max<size_t>(request.alignment, 1);
            assert(std::has_single_bit(alignment));
            offset = (offset + alignment - 1) & ~(alignment - 1);
            offset += request.bytes;
        }
        return offset;
    };

    size_t batchStart = frame.primitiveUploadOffset;
    size_t requiredEnd = calculateEnd(batchStart);
    if (requiredEnd == batchStart)
        return;

    if (requiredEnd > frame.primitiveUploadCapacity)
    {
        // If earlier uploads were recorded into this command list, keep their
        // old arena alive until this slot's fence signals. At normal GUI render
        // time the offset is zero, so the fence wait in BeginWindowFrame makes
        // immediate replacement safe.
        Buffer oldBuffer = frame.primitiveUploadBuffer;
        const bool oldUsedThisFrame = frame.primitiveUploadOffset != 0;

        constexpr size_t kMinimumArenaCapacity = 64 * 1024;
        const size_t newCapacity = std::bit_ceil(std::max(requiredEnd, kMinimumArenaCapacity));
        frame.primitiveUploadBuffer = dc.CreateBuffer(
            newCapacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        frame.primitiveUploadCapacity = newCapacity;

        VkBufferDeviceAddressInfo addressInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = frame.primitiveUploadBuffer.buffer
        };
        frame.primitiveUploadAddress = vkGetBufferDeviceAddress(dc.device, &addressInfo);
        frame.primitiveUploadOffset = 0;
        batchStart = 0;
        requiredEnd = calculateEnd(0);

        if (oldBuffer.buffer != VK_NULL_HANDLE)
        {
            if (oldUsedThisFrame)
            {
                frame.flushQueue.push_function([allocator = dc.allocator, oldBuffer]()
                {
                    vmaDestroyBuffer(allocator, oldBuffer.buffer, oldBuffer.allocation);
                });
            }
            else
            {
                vmaDestroyBuffer(dc.allocator, oldBuffer.buffer, oldBuffer.allocation);
            }
        }
    }

    auto* mapped = static_cast<std::byte*>(frame.primitiveUploadBuffer.info.pMappedData);
    size_t offset = batchStart;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        const auto& request = requests[i];
        if (request.bytes == 0)
            continue;
        const size_t alignment = std::max<size_t>(request.alignment, 1);
        offset = (offset + alignment - 1) & ~(alignment - 1);
        std::memcpy(mapped + offset, request.data, request.bytes);
        addresses[i] = frame.primitiveUploadAddress + offset;
        offset += request.bytes;
    }

    vmaFlushAllocation(dc.allocator, frame.primitiveUploadBuffer.allocation,
                       batchStart, offset - batchStart);
    frame.primitiveUploadOffset = offset;
}
