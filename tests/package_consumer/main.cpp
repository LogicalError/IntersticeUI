// Links the installed package and exercises the public host-integration API.
// Exit code 0 means the package is usable; it creates no window and needs no GPU
// beyond what CheckPhysicalDeviceSupport queries.
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <gui_controls/gui_application.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

#include <cstdio>
#include <filesystem>
#include <vector>

int main()
{
    const auto requirements = Vulkan::VulkanDeviceContext::GetDeviceRequirements();
    std::printf("GUI requires Vulkan %u.%u and %zu device extension(s)\n",
        VK_API_VERSION_MAJOR(requirements.apiVersion),
        VK_API_VERSION_MINOR(requirements.apiVersion),
        requirements.deviceExtensions.size());

    if (!std::filesystem::exists(std::filesystem::path(CONSUMER_RESOURCE_DIR) / "themes"))
    {
        std::printf("resources missing under %s\n", CONSUMER_RESOURCE_DIR);
        return 1;
    }

    // Header-only use of the application hook proves gui_controls headers resolve.
    GUI::GUIApplication* application = nullptr;
    (void)application;
    return 0;
}
