// Host-provided Vulkan device: a host application that owns its own VkInstance
// and VkDevice hands them to the GUI. The GUI must render on that device, must
// not destroy anything it does not own, and must reject an unusable device
// cleanly. Tests SKIP themselves when no suitable Vulkan device is present.

#include "gpu_test_harness.h"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <VkBootstrap.h>
#include <SDL3/SDL_vulkan.h>

using namespace GpuTestHarness;

namespace
{
    // Stands in for a game engine: creates its own instance and device exactly
    // as a host would, using the GUI's published requirements.
    struct HostDevice
    {
        vkb::Instance                instance;
        vkb::PhysicalDevice          physicalDevice;
        vkb::Device                  device;
        VkQueue                      queue = VK_NULL_HANDLE;
        uint32_t                     queueFamily = 0;
        std::mutex                   queueMutex;
        bool                         valid = false;

        bool Create(ValidationSink& sink)
        {
            vkb::InstanceBuilder builder;
            builder.set_app_name("external-device-host")
                   .require_api_version(1, 3, 0)
                   .request_validation_layers(true)
                   .enable_validation_layers(true)
                   .set_debug_callback(&ValidationCallback)
                   .set_debug_callback_user_data_pointer(&sink);

            // Instance extensions the window backend needs to create surfaces.
            Uint32 extensionCount = 0;
            if (const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount))
                for (Uint32 index = 0; index < extensionCount; ++index)
                    builder.enable_extension(extensions[index]);

            auto builtInstance = builder.build();
            if (!builtInstance) return false;
            instance = builtInstance.value();

            auto requirements = interstice::vulkan::VulkanDeviceContext::GetDeviceRequirements();   // vk-bootstrap fills in the sTypes
            vkb::PhysicalDeviceSelector selector{ instance };
            auto selected = selector
                .set_minimum_version(1, 3)
                .set_required_features(requirements.features)
                .set_required_features_11(requirements.features11)
                .set_required_features_12(requirements.features12)
                .set_required_features_13(requirements.features13)
                .defer_surface_initialization()
                .select();
            if (!selected) { vkb::destroy_instance(instance); return false; }
            physicalDevice = selected.value();

            auto builtDevice = vkb::DeviceBuilder{ physicalDevice }.build();
            if (!builtDevice) { vkb::destroy_instance(instance); return false; }
            device = builtDevice.value();

            queue       = device.get_queue(vkb::QueueType::graphics).value();
            queueFamily = device.get_queue_index(vkb::QueueType::graphics).value();
            valid = true;
            return true;
        }

        interstice::vulkan::VulkanDeviceContext::ExternalDevice Describe()
        {
            interstice::vulkan::VulkanDeviceContext::ExternalDevice external;
            external.instance            = instance.instance;
            external.physicalDevice      = physicalDevice.physical_device;
            external.device              = device.device;
            external.graphicsQueue       = queue;
            external.graphicsQueueFamily = queueFamily;
            external.queueMutex          = &queueMutex;
            return external;
        }

        void Destroy()
        {
            if (!valid) return;
            vkb::destroy_device(device);
            vkb::destroy_instance(instance);
            valid = false;
        }
    };

    class GpuExternalDeviceTest : public ::testing::Test
    {
    protected:
        interstice::GraphicsContext*          graphicsContext = nullptr;
        interstice::SDLWindowBackend*         windowBackend   = nullptr;
        interstice::vulkan::VulkanGraphicsContext* vk              = nullptr;
        ValidationSink                 sink;
        HostDevice                     host;
        interstice::GradientManager           gradientManager{ 256 };
        interstice::StyleManager              styleManager{ gradientManager };

        void SetUp() override
        {
            vk              = new interstice::vulkan::VulkanGraphicsContext();
            graphicsContext = vk;
            windowBackend   = new interstice::SDLWindowBackend();
            windowBackend->SetGraphicsContext(graphicsContext);
            styleManager.Register(interstice::Style{});
        }

        void TearDown() override
        {
            delete windowBackend;   windowBackend   = nullptr;
            delete graphicsContext; graphicsContext = nullptr;
            vk = nullptr;
            host.Destroy();
        }
    };
}

TEST_F(GpuExternalDeviceTest, HostDeviceMeetsPublishedRequirements)
{
    if (!host.Create(sink)) GTEST_SKIP() << "No suitable Vulkan device available";
    std::string problems;
    EXPECT_TRUE(interstice::vulkan::VulkanDeviceContext::CheckPhysicalDeviceSupport(
        host.physicalDevice.physical_device, &problems)) << problems;
}

TEST_F(GpuExternalDeviceTest, RendersOnHostDeviceAndLeavesItIntact)
{
    if (!host.Create(sink)) GTEST_SKIP() << "No suitable Vulkan device available";

    vk->deviceContext.SetExternalDevice(host.Describe());
    ASSERT_TRUE(graphicsContext->Initialize(windowBackend))
        << "GUI failed to adopt the host device.\n" << sink.Dump();

    EXPECT_TRUE(vk->deviceContext.UsesExternalDevice());
    EXPECT_EQ(vk->deviceContext.device, host.device.device) << "GUI must render on the host's device";
    EXPECT_EQ(vk->deviceContext.instance, host.instance.instance);
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();

    // Render one frame into a render-to-image window.
    const interstice::int2 size{ 64, 64 };
    interstice::ID id{};
    id.value.dynamicParts.mode      = interstice::IDMode::Static;
    id.value.dynamicParts.hashValue = 9100;
    auto* window = interstice::vulkan::VulkanWindowContext::Create(
        id, nullptr, nullptr, size, interstice::WindowFlags::RenderToImage,
        &vk->deviceContext, [] {});
    ASSERT_NE(window, nullptr);

    graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
    interstice::GUIRenderer* renderer = graphicsContext->guiRenderer;
    renderer->Reset();
    if (renderer->BeginWindow(window->GetID()))
    {
        renderer->RenderDot(interstice::float2{ 32.0f, 32.0f }, interstice::IntRect{ 0, 0, size.x, size.y },
                            10.0f, interstice::color4f{ 1, 1, 1, 1 });
        renderer->EndWindow();
    }
    window->RenderWindowFrame(graphicsContext, interstice::IntRect{ 0, 0, size.x, size.y }, false);
    EXPECT_FALSE(sink.HasErrors()) << "Validation errors rendering on the host device:\n" << sink.Dump();

    window->Destroy();
    delete window;
    gradientManager.ReleaseAll(graphicsContext);
    graphicsContext->Cleanup();

    // The GUI created nothing it must not destroy and destroyed nothing it did
    // not create: the host's device is still alive and idle-able.
    EXPECT_EQ(vkDeviceWaitIdle(host.device.device), VK_SUCCESS);
    VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateFence(host.device.device, &fenceInfo, nullptr, &fence), VK_SUCCESS)
        << "Host device is unusable after the GUI shut down";
    if (fence != VK_NULL_HANDLE) vkDestroyFence(host.device.device, fence, nullptr);
}

TEST_F(GpuExternalDeviceTest, RejectsIncompleteHostDevice)
{
    interstice::vulkan::VulkanDeviceContext::ExternalDevice incomplete;   // all handles null
    vk->deviceContext.SetExternalDevice(incomplete);
    EXPECT_FALSE(graphicsContext->Initialize(windowBackend))
        << "An external device without handles must fail cleanly, not crash";
    EXPECT_FALSE(vk->deviceContext.initialized);
}

// The 3D-viewport case: the host renders into an image it owns, the GUI samples
// it through a primitive. Proves the registration API and the documented
// synchronization contract (host barriers + submission order on one queue).
TEST_F(GpuExternalDeviceTest, SamplesHostRenderedImageInGuiPrimitive)
{
    if (!host.Create(sink)) GTEST_SKIP() << "No suitable Vulkan device available";
    vk->deviceContext.SetExternalDevice(host.Describe());
    ASSERT_TRUE(graphicsContext->Initialize(windowBackend)) << sink.Dump();
    auto& dc = vk->deviceContext;

    // A host-owned image the host fills with solid green.
    VkImageCreateInfo imageInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType   = VK_IMAGE_TYPE_2D;
    imageInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent      = { 16, 16, 1 };
    imageInfo.mipLevels   = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VkImage hostImage = VK_NULL_HANDLE;
    VmaAllocation hostAllocation = nullptr;
    ASSERT_EQ(vmaCreateImage(dc.allocator, &imageInfo, &allocInfo, &hostImage, &hostAllocation, nullptr), VK_SUCCESS);

    VkImageViewCreateInfo viewInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image    = hostImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = imageInfo.format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageView hostView = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateImageView(host.device.device, &viewInfo, nullptr, &hostView), VK_SUCCESS);

    VkCommandPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = host.queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateCommandPool(host.device.device, &poolInfo, nullptr, &pool), VK_SUCCESS);
    VkCommandBufferAllocateInfo bufferInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    bufferInfo.commandPool = pool;
    bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufferInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(host.device.device, &bufferInfo, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    const auto transition = [&](VkImageLayout from, VkImageLayout to,
        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
        VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = srcStage; barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage; barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = from; barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = hostImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    };
    transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue green{ { 0.0f, 1.0f, 0.0f, 1.0f } };
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, hostImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &green, 1, &range);
    // Documented contract: leave the image readable by the GUI's fragment stage.
    transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    {
        // Not waited on: ordering against the GUI's later submission is the contract under test.
        auto lock = dc.LockQueue();
        ASSERT_EQ(vkQueueSubmit2(host.queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    }

    const interstice::ImageIndex hostImageIndex = vk->RegisterExternalImage(hostView, dc.defaultSamplerNearest);
    ASSERT_NE(hostImageIndex, interstice::ImageIndex::Invalid);

    const interstice::int2 size{ 64, 64 };
    interstice::ID id{};
    id.value.dynamicParts.mode      = interstice::IDMode::Static;
    id.value.dynamicParts.hashValue = 9200;
    auto* window = interstice::vulkan::VulkanWindowContext::Create(
        id, nullptr, nullptr, size, interstice::WindowFlags::RenderToImage, &dc, [] {});
    ASSERT_NE(window, nullptr);

    // Only texture-typed color styles sample the image passed to a primitive.
    const auto white = styleManager.RegisterColorStyle(interstice::ColorStyle::Create(
        interstice::color4b{ 255, 255, 255, 255 }, interstice::color4b{ 255, 255, 255, 255 }, interstice::ColorType::StretchedTexture));
    graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
    interstice::GUIRenderer* renderer = graphicsContext->guiRenderer;
    renderer->Reset();
    const interstice::IntRect full{ 0, 0, size.x, size.y };
    if (renderer->BeginWindow(window->GetID()))
    {
        renderer->RenderQuad(interstice::IntRect{ 0, 0, size.x / 2, size.y }, full, white, 0.0f, hostImageIndex);
        renderer->EndWindow();
    }
    window->RenderWindowFrame(graphicsContext, full, false);
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
    std::vector<uint8_t> pixels(size_t(size.x) * size.y * 4);
    ASSERT_TRUE(window->ReadPixels(pixels));

    const auto pixel = [&](int x, int y) {
        const size_t offset = (size_t(y) * size.x + size_t(x)) * 4;   // BGRA8
        return std::array<uint8_t, 4>{ pixels[offset + 2], pixels[offset + 1], pixels[offset + 0], pixels[offset + 3] };
    };
    const auto inside = pixel(size.x / 4, size.y / 2);
    const auto outside = pixel(size.x * 3 / 4, size.y / 2);
    EXPECT_GT(inside[1], 200) << "the host-rendered green must reach the screen";
    EXPECT_LT(inside[0], 60);
    EXPECT_LT(inside[2], 60);
    EXPECT_NE(inside, outside) << "pixels outside the quad must be untouched";

    window->Destroy();
    delete window;
    gradientManager.ReleaseAll(graphicsContext);

    // The host image lives in the GUI's allocator (the host passed none), so it
    // must be released before the GUI shuts that allocator down.
    vkDeviceWaitIdle(host.device.device);
    vkDestroyCommandPool(host.device.device, pool, nullptr);
    vkDestroyImageView(host.device.device, hostView, nullptr);
    vmaDestroyImage(dc.allocator, hostImage, hostAllocation);
    graphicsContext->Cleanup();
}

// vkDeviceWaitIdle needs every queue of the device externally synchronized, so
// the GUI must hold the host's queue lock while it waits for the device.
TEST_F(GpuExternalDeviceTest, DeviceWaitIdleHoldsTheHostQueueLock)
{
    if (!host.Create(sink)) GTEST_SKIP() << "No suitable Vulkan device available";
    vk->deviceContext.SetExternalDevice(host.Describe());
    ASSERT_TRUE(graphicsContext->Initialize(windowBackend)) << sink.Dump();

    std::atomic<bool> flushed = false;
    std::thread flusher;
    {
        std::lock_guard hostSubmitting(host.queueMutex);
        flusher = std::thread([&] { vk->deviceContext.Flush(); flushed = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_FALSE(flushed) << "waited for the device while the host held its queue";
    }
    flusher.join();
    EXPECT_TRUE(flushed);

    gradientManager.ReleaseAll(graphicsContext);
    graphicsContext->Cleanup();
}

// The GUI's buffers need device addresses; a host allocator created without
// VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT can't give them, so it's rejected.
TEST_F(GpuExternalDeviceTest, RejectsHostAllocatorWithoutBufferDeviceAddress)
{
    if (!host.Create(sink)) GTEST_SKIP() << "No suitable Vulkan device available";

    auto makeAllocator = [&](VmaAllocatorCreateFlags flags)
    {
        VmaAllocatorCreateInfo info{};
        info.flags          = flags;
        info.physicalDevice = host.physicalDevice.physical_device;
        info.device         = host.device.device;
        info.instance       = host.instance.instance;
        VmaAllocator allocator = VK_NULL_HANDLE;
        EXPECT_EQ(vmaCreateAllocator(&info, &allocator), VK_SUCCESS);
        return allocator;
    };

    VmaAllocator plain = makeAllocator(0);
    auto external = host.Describe();
    external.allocator = plain;
    vk->deviceContext.SetExternalDevice(external);
    EXPECT_FALSE(graphicsContext->Initialize(windowBackend));
    vmaDestroyAllocator(plain);
}

TEST_F(GpuExternalDeviceTest, AcceptsHostAllocatorWithBufferDeviceAddress)
{
    if (!host.Create(sink)) GTEST_SKIP() << "No suitable Vulkan device available";

    VmaAllocatorCreateInfo info{};
    info.flags          = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    info.physicalDevice = host.physicalDevice.physical_device;
    info.device         = host.device.device;
    info.instance       = host.instance.instance;
    VmaAllocator allocator = VK_NULL_HANDLE;
    ASSERT_EQ(vmaCreateAllocator(&info, &allocator), VK_SUCCESS);

    auto external = host.Describe();
    external.allocator = allocator;
    vk->deviceContext.SetExternalDevice(external);
    EXPECT_TRUE(graphicsContext->Initialize(windowBackend)) << sink.Dump();
    gradientManager.ReleaseAll(graphicsContext);
    graphicsContext->Cleanup();
    vmaDestroyAllocator(allocator);
}
