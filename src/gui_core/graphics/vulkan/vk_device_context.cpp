#include <vulkan/vulkan.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_defaults.h>

using namespace Vulkan;

void VulkanDeviceContext::WaitIdle() const
{
    auto queueLock = LockQueue();
    VK_CHECK(vkDeviceWaitIdle(device));
}

bool VulkanDeviceContext::Initialize()
{
    // CreateSurface performs the instance/device bring-up (or adopts a host device).
    if (!initialized)
        return false;
    if (!renderContext.Create(*this))
        return false;
    return true;
}