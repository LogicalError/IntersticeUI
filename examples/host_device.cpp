// Hosting on an existing Vulkan device: the "host" (think game engine) creates
// the instance and device, hands them to the GUI, renders into its own image
// every frame and the GUI shows that image in a window.
#include <gui_controls/gui_application.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>

// What a host already has: its own device, queue and an image it renders into.
struct Host
{
    vkb::Instance       instance;
    vkb::PhysicalDevice physicalDevice;
    vkb::Device         device;
    VkQueue             queue       = VK_NULL_HANDLE;
    uint32_t            queueFamily = 0;
    std::mutex          queueMutex;   // shared with the GUI: both submit to `queue`

    static constexpr VkExtent3D kImageSize { 256, 256, 1 };
    VkImage         image   = VK_NULL_HANDLE;
    VkDeviceMemory  memory  = VK_NULL_HANDLE;
    VkImageView     view    = VK_NULL_HANDLE;
    VkSampler       sampler = VK_NULL_HANDLE;
    VkCommandPool   pool    = VK_NULL_HANDLE;
    VkCommandBuffer cmd     = VK_NULL_HANDLE;
    VkFence         fence   = VK_NULL_HANDLE;
    bool            firstFrame = true;

    bool CreateDevice()
    {
        // The window backend needs these instance extensions to create surfaces.
        vkb::InstanceBuilder builder;
        builder.set_app_name("host_device").require_api_version(1, 3, 0);
        Uint32 count = 0;
        if (const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count))
            for (Uint32 i = 0; i < count; ++i)
                builder.enable_extension(extensions[i]);
        auto builtInstance = builder.build();
        if (!builtInstance) return false;
        instance = builtInstance.value();

        // Enable everything the GUI needs on the device.
        auto requirements = tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements();
        auto selected = vkb::PhysicalDeviceSelector{ instance }
            .set_minimum_version(1, 3)
            .set_required_features(requirements.features)
            .set_required_features_11(requirements.features11)
            .set_required_features_12(requirements.features12)
            .set_required_features_13(requirements.features13)
            .add_required_extensions(requirements.deviceExtensions)
            .defer_surface_initialization()
            .select();
        if (!selected) return false;
        physicalDevice = selected.value();
        auto builtDevice = vkb::DeviceBuilder{ physicalDevice }.build();
        if (!builtDevice) return false;
        device      = builtDevice.value();
        queue       = device.get_queue(vkb::QueueType::graphics).value();
        queueFamily = device.get_queue_index(vkb::QueueType::graphics).value();
        return true;
    }

    tachyon::vulkan::VulkanDeviceContext::ExternalDevice Describe()
    {
        tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
        external.instance            = instance.instance;
        external.physicalDevice      = physicalDevice.physical_device;
        external.device              = device.device;
        external.graphicsQueue       = queue;
        external.graphicsQueueFamily = queueFamily;
        external.queueMutex          = &queueMutex;
        return external;
    }

    bool CreateImage()
    {
        VkDevice d = device.device;
        VkImageCreateInfo imageInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType   = VK_IMAGE_TYPE_2D;
        imageInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent      = kImageSize;
        imageInfo.mipLevels   = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (vkCreateImage(d, &imageInfo, nullptr, &image) != VK_SUCCESS) return false;

        VkMemoryRequirements needs;
        vkGetImageMemoryRequirements(d, image, &needs);
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice.physical_device, &props);
        VkMemoryAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.allocationSize  = needs.size;
        allocInfo.memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
            if ((needs.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                allocInfo.memoryTypeIndex = i;
                break;
            }
        if (allocInfo.memoryTypeIndex == UINT32_MAX) return false;
        if (vkAllocateMemory(d, &allocInfo, nullptr, &memory) != VK_SUCCESS) return false;
        vkBindImageMemory(d, image, memory, 0);

        VkImageViewCreateInfo viewInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image            = image;
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = imageInfo.format;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(d, &viewInfo, nullptr, &view) != VK_SUCCESS) return false;

        VkSamplerCreateInfo samplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
        if (vkCreateSampler(d, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) return false;

        VkCommandPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        if (vkCreateCommandPool(d, &poolInfo, nullptr, &pool) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo bufferInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        bufferInfo.commandPool        = pool;
        bufferInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(d, &bufferInfo, &cmd) != VK_SUCCESS) return false;
        VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        return vkCreateFence(d, &fenceInfo, nullptr, &fence) == VK_SUCCESS;
    }

    // The host's "rendering": clear the image to a color. It follows the GUI's
    // contract: wait for the GUI's reads before writing, and leave the image
    // readable by the fragment stage.
    void Render(float r, float g, float b)
    {
        vkWaitForFences(device.device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device.device, 1, &fence);
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        Barrier(firstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkClearColorValue color{ { r, g, b, 1.0f } };
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        Barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        vkEndCommandBuffer(cmd);
        firstFrame = false;

        VkCommandBufferSubmitInfo cmdInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cmdInfo.commandBuffer = cmd;
        VkSubmitInfo2 submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos    = &cmdInfo;
        std::lock_guard lock(queueMutex);
        vkQueueSubmit2(queue, 1, &submit, fence);
    }

    void Barrier(VkImageLayout from, VkImageLayout to,
                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask        = srcStage;
        barrier.srcAccessMask       = srcAccess;
        barrier.dstStageMask        = dstStage;
        barrier.dstAccessMask       = dstAccess;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers    = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    void Destroy()
    {
        if (VkDevice d = device.device)
        {
            vkDeviceWaitIdle(d);
            vkDestroyFence(d, fence, nullptr);
            vkDestroyCommandPool(d, pool, nullptr);
            vkDestroySampler(d, sampler, nullptr);
            vkDestroyImageView(d, view, nullptr);
            vkDestroyImage(d, image, nullptr);
            vkFreeMemory(d, memory, nullptr);
            vkb::destroy_device(device);
        }
        if (instance.instance)
            vkb::destroy_instance(instance);
    }
};

class HostedApp : public tachyon::GUIApplication
{
public:
    explicit HostedApp(Host& host) : host(host)
    {
        // The GUI renders on the host's device; without one it creates its own and the example quits.
        auto backend = std::make_unique<tachyon::vulkan::VulkanGraphicsContext>();
        if (this->host.CreateDevice())
            backend->SetExternalDevice(this->host.Describe());
        else
            std::printf("no Vulkan 1.3 device with the features the GUI needs\n");
        vk = backend.get();
        SetGraphicsBackend(std::move(backend));
    }

    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        if (host.device.device == VK_NULL_HANDLE || !host.CreateImage())
            return tachyon::Error{ tachyon::ErrorCode::Failed, "the host device or its image could not be created" };
        // The GUI samples the host's view and sampler; the host keeps owning them.
        hostImage = vk->RegisterExternalImage(host.view, host.sampler);
        windowID  = idManager.CreateStaticID();
        return {};
    }

    void OnGUI() override
    {
        // The host submits its frame while the UI is built, before the GUI submits.
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        host.Render(0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::sin(t + 2.1f), 0.5f + 0.5f * std::sin(t + 4.2f));

        if (BeginWindow(windowID, "Host device", tachyon::LayoutOverride{}.Size(320, 340)))
        {
            BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
            Text("Rendered by the host:");
            Image(hostImage, tachyon::uint2{ Host::kImageSize.width, Host::kImageSize.height });
            EndVertical();
        }
        EndWindow();
    }

private:
    Host&                                 host;
    tachyon::vulkan::VulkanGraphicsContext*  vk        = nullptr;   // owned by the app
    tachyon::ImageIndex                       hostImage = tachyon::ImageIndex::Invalid;
    tachyon::ID                               windowID  = tachyon::ID::Invalid();
    std::chrono::steady_clock::time_point start     = std::chrono::steady_clock::now();
};

int main()
{
    // The host's instance needs SDL's surface extensions, so SDL runs before the GUI starts.
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;
    Host host;
    {
        HostedApp app(host);
        app.SetResourceRoot(TACHYON_RESOURCE_DIR);
        tachyon::Result<void> result = app.Initialize();
        if (result)
            result = app.Run();
        app.Cleanup();   // the GUI destroys only what it created; the device stays
        if (!result)
            std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
    }
    host.Destroy();
    SDL_Quit();
    return 0;
}
