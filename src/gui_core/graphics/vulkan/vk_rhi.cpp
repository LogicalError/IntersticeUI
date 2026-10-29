#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_embedded_files_internal.h>
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
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogVulkan = tachyon::Diagnostics::MakeLogCategory("vulkan");

using namespace tachyon::vulkan;

bool GUIRenderContext::Create(tachyon::vulkan::VulkanDeviceContext& deviceContext)
{
    VulkanRenderDevice device(deviceContext);

    for (size_t k = 0; k < tachyon::kPrimitiveKindCount; k++)
    {
        pipelines[k] = device.CreatePrimitivePipeline(tachyon::kPrimitiveSpecs[k]);
        if (!pipelines[k].IsValid())
        {
            for (size_t j = 0; j < k; j++)
                vkDestroyPipeline(deviceContext.device,
                                  reinterpret_cast<VkPipeline>(pipelines[j].pipeline), deviceContext.AllocationCallbacks());
            device.DestroySharedLayout();
            return false;
        }
    }

    deviceContext.mainDeletionQueue.push_function(
        [vkDevice = deviceContext.device, callbacks = deviceContext.AllocationCallbacks(), pipelines = pipelines]()
        {
            for (const auto& p : pipelines)
                vkDestroyPipeline(vkDevice, reinterpret_cast<VkPipeline>(p.pipeline), callbacks);
            // All pipelines share one layout; destroy it once.
            vkDestroyPipelineLayout(vkDevice,
                                    reinterpret_cast<VkPipelineLayout>(pipelines[0].layout), callbacks);
        });
    deviceContext.mainDeletionQueue.push_function([this, &deviceContext]()
        {
            if (quadIndexBuffer.buffer != VK_NULL_HANDLE)
                deviceContext.DestroyBuffer(quadIndexBuffer);
            quadIndexBuffer   = {};
            quadIndexCapacity = 0;
        });

    return true;
}

VkBuffer GUIRenderContext::QuadIndices(tachyon::vulkan::VulkanDeviceContext& deviceContext, uint32_t primitiveCount)
{
    if (primitiveCount <= quadIndexCapacity)
        return quadIndexBuffer.buffer;

    if (quadIndexBuffer.buffer != VK_NULL_HANDLE)
        deviceContext.mainDeletionQueue.push_function([&deviceContext, old = quadIndexBuffer]() { deviceContext.DestroyBuffer(old); });

    constexpr uint32_t kMinimumPrimitives = 4096;
    const uint32_t capacity = std::bit_ceil(std::max(primitiveCount, kMinimumPrimitives));
    quadIndexBuffer = deviceContext.CreateBuffer(size_t(capacity) * 6 * sizeof(uint32_t),
                                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    auto* indices = static_cast<uint32_t*>(quadIndexBuffer.info.pMappedData);
    static constexpr uint32_t kCorners[6] = { 0, 1, 3, 0, 3, 2 };
    for (uint32_t p = 0; p < capacity; ++p)
        for (uint32_t i = 0; i < 6; ++i)
            indices[p * 6 + i] = p * 4 + kCorners[i];
    vmaFlushAllocation(deviceContext.allocator, quadIndexBuffer.allocation, 0, VK_WHOLE_SIZE);
    quadIndexCapacity = capacity;
    return quadIndexBuffer.buffer;
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

// A shader compiled into the library, copied into words; empty when there is none.
static std::vector<uint32_t> BuiltInSpirv(const std::string& fileName)
{
    const std::span<const std::byte> data = tachyon::detail::FindEmbeddedFile(tachyon::detail::EmbeddedShaderFiles(), fileName);
    if (data.empty() || data.size() % sizeof(uint32_t) != 0) return {};
    std::vector<uint32_t> words(data.size() / sizeof(uint32_t));
    std::memcpy(words.data(), data.data(), data.size());
    return words;
}

// The shader `fileName` from the shader directory when one is set, else the built-in one.
static VkShaderModule LoadShader(const tachyon::vulkan::VulkanDeviceContext& dc, const std::string& fileName)
{
    std::vector<uint32_t> words = dc.shaderDirectory.empty()
        ? BuiltInSpirv(fileName)
        : ReadSpirvFile((std::filesystem::path(dc.shaderDirectory) / fileName).string());
    return words.empty() ? nullptr : dc.CreateShader(words);
}

tachyon::RHI::GpuPipeline VulkanRenderDevice::CreatePrimitivePipeline(const tachyon::PrimitiveSpec& spec)
{
    const std::string base(spec.shaderBaseName);

    VkShaderModule frag = LoadShader(dc, base + ".frag.spv");
    if (frag == nullptr)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "Error when building the {} fragment shader module", spec.shaderBaseName);
        return {};
    }
    VkShaderModule vert = LoadShader(dc, base + ".vert.spv");
    if (vert == nullptr)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "Error when building the {} vertex shader module", spec.shaderBaseName);
        vkDestroyShaderModule(dc.device, frag, dc.AllocationCallbacks());
        return {};
    }

    if (sharedGuiLayout == VK_NULL_HANDLE)
    {
        VkPushConstantRange bufferRange{};
        bufferRange.offset = 0;
        bufferRange.size = sizeof(tachyon::GPUDrawPushConstants);
        bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkPipelineLayoutCreateInfo layoutInfo = tachyon::vulkan::pipeline_layout_create_info();
        layoutInfo.pSetLayouts = &dc.singleImageDescriptorLayout;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pPushConstantRanges = &bufferRange;
        layoutInfo.pushConstantRangeCount = 1;
        TACHYON_VK_CHECK(vkCreatePipelineLayout(dc.device, &layoutInfo, dc.AllocationCallbacks(), &sharedGuiLayout));
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
    // Depth is draw order (later is nearer): occluders write it first; the rest only test it,
    // so what an opaque box drawn later covers is skipped.
    builder.enable_depthtest(spec.depthOnly, VK_COMPARE_OP_GREATER_OR_EQUAL);
    if (spec.depthOnly)
        builder._colorBlendAttachment.colorWriteMask = 0;
    builder.set_color_attachment_format(tachyon::vulkan::drawImageFormat);
    builder.set_depth_format(tachyon::vulkan::depthImageFormat);

    VkPipeline pipeline = builder.build_pipeline(dc.device, dc.AllocationCallbacks());

    vkDestroyShaderModule(dc.device, frag, dc.AllocationCallbacks());
    vkDestroyShaderModule(dc.device, vert, dc.AllocationCallbacks());

    return { reinterpret_cast<uint64_t>(pipeline), reinterpret_cast<uint64_t>(sharedGuiLayout) };
}

void VulkanRenderDevice::DestroySharedLayout()
{
    if (sharedGuiLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(dc.device, sharedGuiLayout, dc.AllocationCallbacks());
        sharedGuiLayout = VK_NULL_HANDLE;
    }
}

void VulkanRenderCommandList::DrawPrimitives(const tachyon::RHI::GpuPipeline& pipeline,
                                             const tachyon::GPUDrawPushConstants& pushConstants,
                                             uint32_t primitiveCount, uint32_t firstPrimitive, tachyon::PrimitiveShape shape)
{
    const auto vkPipeline = reinterpret_cast<VkPipeline>(pipeline.pipeline);
    const auto vkLayout   = reinterpret_cast<VkPipelineLayout>(pipeline.layout);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkLayout, 0, 1, &imageSet, 0, nullptr);
    vkCmdPushConstants(cmd, vkLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(tachyon::GPUDrawPushConstants), &pushConstants);
    if (shape == tachyon::PrimitiveShape::Quad)
    {
        // Four vertices a primitive, two triangles from them: the offset picks the pass's first.
        vkCmdBindIndexBuffer(cmd, dc.renderContext.QuadIndices(dc, primitiveCount), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, primitiveCount * 6, 1, 0, int32_t(firstPrimitive * 4), 0);
    }
    else
        vkCmdDraw(cmd, primitiveCount * 3, 1, firstPrimitive * 3, 0);
}

void VulkanRenderCommandList::UploadTransientBuffers(
    std::span<const tachyon::RHI::UploadRequest> requests,
    std::span<tachyon::DeviceAddress> addresses)
{
    assert(requests.size() == addresses.size());
    std::fill(addresses.begin(), addresses.end(), tachyon::DeviceAddress{ 0 });

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

    // The upload worker copies while this thread records the draws; the frame's submit waits
    // for it. The streams stay as they are until then.
    auto* mapped = static_cast<std::byte*>(frame.primitiveUploadBuffer.info.pMappedData);
    size_t offset = batchStart;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        const auto& request = requests[i];
        if (request.bytes == 0)
            continue;
        const size_t alignment = std::max<size_t>(request.alignment, 1);
        offset = (offset + alignment - 1) & ~(alignment - 1);
        dc.Uploads().Copy(mapped + offset, request.data, request.bytes);
        addresses[i] = frame.primitiveUploadAddress + offset;
        offset += request.bytes;
    }

    dc.Uploads().Then([allocator = dc.allocator, allocation = frame.primitiveUploadBuffer.allocation, batchStart, size = offset - batchStart]()
                      { vmaFlushAllocation(allocator, allocation, batchStart, size); });
    frame.primitiveUploadOffset = offset;
}

tachyon::RHI::ResidentBufferId VulkanRenderCommandList::UploadResidentBuffers(
    std::span<const tachyon::RHI::UploadRequest> requests,
    std::span<tachyon::DeviceAddress> addresses)
{
    return dc.CreateResidentBuffer(requests, addresses);
}

tachyon::RHI::ResidentState VulkanRenderCommandList::ResidentBufferState(tachyon::RHI::ResidentBufferId id) const
{
    return dc.GetResidentBufferState(id);
}

void VulkanRenderCommandList::ReleaseResidentBuffer(tachyon::RHI::ResidentBufferId id)
{
    dc.ReleaseResidentBuffer(id);
}
