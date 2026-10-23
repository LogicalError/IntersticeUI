#pragma once

#include <gui_core/graphics/gui_buffer.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace interstice::vulkan
{
    struct Buffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    struct VisualBoxStyleBuffers   { Buffer boxStyleBuffer; VkDeviceAddress styleBufferAddress; };
    struct VisualColorStyleBuffers { Buffer colorStyleBuffer; VkDeviceAddress colorStyleBufferAddress; };


    class VulkanBuffer : public interstice::Buffer
    {
    public:
        interstice::vulkan::VisualBoxStyleBuffers visualBoxStyleBuffers;
        interstice::vulkan::VisualColorStyleBuffers visualColorStyleBuffers;
    };
}
