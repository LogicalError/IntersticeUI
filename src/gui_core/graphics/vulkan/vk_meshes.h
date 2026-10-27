#pragma once

#include <gui_core/graphics/gui_buffer.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    struct Buffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    struct VisualBoxStyleBuffers   { Buffer boxStyleBuffer; VkDeviceAddress styleBufferAddress; };
    struct VisualColorStyleBuffers { Buffer colorStyleBuffer; VkDeviceAddress colorStyleBufferAddress; };


    class VulkanBuffer : public tachyon::Buffer
    {
    public:
        tachyon::vulkan::VisualBoxStyleBuffers visualBoxStyleBuffers;
        tachyon::vulkan::VisualColorStyleBuffers visualColorStyleBuffers;
    };
    /// @}
}
