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

    // The SDK's vk_enum_string_helper.h isn't in every distribution's Vulkan headers.
    const char* ResultName(VkResult result)
    {
        switch (result)
        {
            case VK_SUCCESS:                        return "VK_SUCCESS";
            case VK_NOT_READY:                      return "VK_NOT_READY";
            case VK_TIMEOUT:                        return "VK_TIMEOUT";
            case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
            case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
            case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS:         return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_FRAGMENTED_POOL:          return "VK_ERROR_FRAGMENTED_POOL";
            case VK_ERROR_OUT_OF_POOL_MEMORY:       return "VK_ERROR_OUT_OF_POOL_MEMORY";
            case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
            case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
            default:                                return "VkResult";
        }
    }
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
        INTERSTICE_LOG_CRITICAL(category, "Vulkan error {} ({}) in {}", ResultName(result), int(result), call);
    else
        INTERSTICE_LOG_WARNING(category, "Vulkan status {} ({}) in {}", ResultName(result), int(result), call);
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