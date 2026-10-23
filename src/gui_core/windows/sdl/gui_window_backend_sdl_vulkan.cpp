// vulkan.h + vma must precede the vk_* headers (they assume the Vulkan/VMA
// types are already declared).
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl_vulkan.h>
#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace interstice
{
    static VkSurfaceKHR CreateSDLVulkanSurface(VkInstance instance, const VkAllocationCallbacks* callbacks, OSWindowHandle windowHandle)
    {
        if (instance == VK_NULL_HANDLE || windowHandle == nullptr)
            return VK_NULL_HANDLE;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(SDLWindowBackend::ToSDL(windowHandle), instance, callbacks, &surface))
            return VK_NULL_HANDLE;
        return surface;
    }

    void InstallSDLVulkanSurfaceFactory(GraphicsContext* graphicsContext)
    {
        if (graphicsContext == nullptr) return;
        auto* vk = static_cast<interstice::vulkan::VulkanGraphicsContext*>(graphicsContext);
        vk->deviceContext.SetSurfaceFactory(&CreateSDLVulkanSurface);
    }
} // namespace interstice
