// Core GPU resource types of the Vulkan backend: Vulkan handles (images, buffers)
// with their VMA allocations and metadata.

#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#define VMA_DEBUG_LOG_FORMAT
#include <vk_mem_alloc.h>

#include <cstdio>

namespace interstice::vulkan
{
    struct AllocatedImage 
    {
        VkImage image;
        VkImageView imageView;
        VmaAllocation allocation;
        VkExtent3D imageExtent;
        VkFormat imageFormat;
    };

    struct AllocatedBuffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    #define INTERSTICE_VK_CHECK(x)                                                     \
        do                                                                  \
        {                                                                   \
            VkResult err = x;                                               \
            if (err)                                                        \
            {                                                               \
                INTERSTICE_LOG_CRITICAL(::interstice::Diagnostics::MakeLogCategory("vulkan"), "Vulkan error: {}", string_VkResult(err)); \
                abort();                                                    \
            }                                                               \
        } while (0)

    // Wait for one fence however long the GPU takes. VK_TIMEOUT only means the GPU is slow (a busy
    // or stalled driver, a paused debugger), so keep waiting; only real errors such as device loss abort.
    inline void WaitForFence(VkDevice device, VkFence fence)
    {
        VkResult result;
        do
        {
            result = vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000);
        } while (result == VK_TIMEOUT);
        INTERSTICE_VK_CHECK(result);
    }
}