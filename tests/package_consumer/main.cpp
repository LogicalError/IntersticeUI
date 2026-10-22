// Links the installed package, creates a GUIApplication from the installed
// resources and renders a few frames in a window. Exit code 0 means the package
// works. Needs a Vulkan driver (CI uses a software one).
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <gui_controls/gui_application.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

#include <cstdio>

namespace
{
    class ConsumerApp : public GUI::GUIApplication
    {
    public:
        GUI::ID windowID     = GUI::ID::Invalid();
        int     framesBuilt  = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Package consumer", GUI::LayoutOverride{}.Size(320, 200)))
            {
                Label("IntersticeUI package consumer");
                EndWindow();
            }
            if (++framesBuilt >= 3)
                applicationQuit = true;
        }
    };
}

int main()
{
    const auto requirements = Vulkan::VulkanDeviceContext::GetDeviceRequirements();
    std::printf("GUI requires Vulkan %u.%u and %zu device extension(s)\n",
        VK_API_VERSION_MAJOR(requirements.apiVersion),
        VK_API_VERSION_MINOR(requirements.apiVersion),
        requirements.deviceExtensions.size());

    // Loads the installed resources, whatever the working directory is.
    ConsumerApp app;
    app.SetResourceRoot(INTERSTICE_RESOURCE_DIR);
    if (!app.Initialize())
    {
        std::printf("GUIApplication::Initialize failed\n");
        app.Cleanup();
        return 1;
    }
    app.windowID = app.idManager.CreateStaticID();
    app.Run();
    app.Cleanup();

    std::printf("rendered %d frames\n", app.framesBuilt);
    return app.framesBuilt >= 3 ? 0 : 1;
}
