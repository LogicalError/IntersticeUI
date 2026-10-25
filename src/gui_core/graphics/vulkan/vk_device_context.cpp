#include <vulkan/vulkan.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_defaults.h>

#include <atomic>

using namespace interstice::vulkan;

namespace
{
    std::atomic<bool> g_deviceLost{ false };
}

void interstice::vulkan::OnVulkanError(VkResult result, const char* call)
{
    const auto category = ::interstice::Diagnostics::MakeLogCategory("vulkan");
    if (result == VK_ERROR_DEVICE_LOST)
    {
        if (!g_deviceLost.exchange(true))
            INTERSTICE_LOG_CRITICAL(category, "Vulkan device lost in {}; it is recreated before the next frame", call);
        return;
    }
    if (g_deviceLost)
        return;   // the calls after a loss fail too
    if (result < 0)
        INTERSTICE_LOG_CRITICAL(category, "Vulkan error {} in {}", string_VkResult(result), call);
    else
        INTERSTICE_LOG_WARNING(category, "Vulkan status {} in {}", string_VkResult(result), call);
}

bool interstice::vulkan::IsDeviceLost()          { return g_deviceLost; }
void interstice::vulkan::SetDeviceLost(bool lost) { g_deviceLost = lost; }

void VulkanDeviceContext::WaitIdle() const
{
    auto queueLock = LockQueue();
    INTERSTICE_VK_CHECK(vkDeviceWaitIdle(device));
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