// Core GPU resource types of the Vulkan backend: Vulkan handles (images, buffers)
// with their VMA allocations and metadata.

#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <vulkan/vulkan.h>
#define VMA_DEBUG_LOG_FORMAT
#include <vk_mem_alloc.h>

#include <cstdio>

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

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

    /// Logs a failed Vulkan call; the frame carries on. A lost device is
    /// remembered: the GUI recreates it before its next frame (DeviceLost / RecreateDevice).
    void OnVulkanError(VkResult result, const char* call);
    bool IsDeviceLost();
    void SetDeviceLost(bool lost);

    #define TACHYON_VK_CHECK(x)                                              \
        do                                                                  \
        {                                                                   \
            const VkResult tachyon_vk_result = (x);                      \
            if (tachyon_vk_result != VK_SUCCESS)                         \
                ::tachyon::vulkan::OnVulkanError(tachyon_vk_result, #x); \
        } while (0)

    /// Waits for `fence` however long the GPU takes: VK_TIMEOUT only means the GPU is slow (a busy
    /// or stalled driver, a paused debugger). A real error such as device loss is reported and
    /// ends the wait.
    inline void WaitForFence(VkDevice device, VkFence fence)
    {
        VkResult result;
        do
        {
            result = vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000);
        } while (result == VK_TIMEOUT);
        TACHYON_VK_CHECK(result);
    }
    /// @}
}