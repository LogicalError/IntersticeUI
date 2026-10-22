#pragma once

#include <gui_core/graphics/gui_buffer.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Vulkan
{
    struct Buffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    struct VisualBoxStyleBuffers   { Buffer boxStyleBuffer; VkDeviceAddress styleBufferAddress; };
    struct VisualColorStyleBuffers { Buffer colorStyleBuffer; VkDeviceAddress colorStyleBufferAddress; };


    class VulkanBuffer : public GUI::Buffer
    {
    public:
        Vulkan::VisualBoxStyleBuffers visualBoxStyleBuffers;
        Vulkan::VisualColorStyleBuffers visualColorStyleBuffers;
    };
}
