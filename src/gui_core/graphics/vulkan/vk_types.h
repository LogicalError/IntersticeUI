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

    // A failed Vulkan call is logged and the frame carries on (#47). A lost device is
    // remembered: the GUI recreates it before its next frame (DeviceLost / RecreateDevice).
    void OnVulkanError(VkResult result, const char* call);
    bool IsDeviceLost();
    void SetDeviceLost(bool lost);

    #define INTERSTICE_VK_CHECK(x)                                              \
        do                                                                  \
        {                                                                   \
            const VkResult interstice_vk_result = (x);                      \
            if (interstice_vk_result != VK_SUCCESS)                         \
                ::interstice::vulkan::OnVulkanError(interstice_vk_result, #x); \
        } while (0)

    // Wait for one fence however long the GPU takes. VK_TIMEOUT only means the GPU is slow (a busy
    // or stalled driver, a paused debugger), so keep waiting; a real error such as device loss
    // is reported and the wait ends.
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