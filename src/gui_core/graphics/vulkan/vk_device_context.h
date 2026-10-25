#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/gui_graphics_context.h>   // interstice::DeviceScope
#include <gui_core/graphics/gui_image.h>
#include <gui_core/windows/gui_window_backend.h>      // interstice::OSWindowHandle

#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_meshes.h>
#include <gui_core/graphics/vulkan/vk_rhi.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/memory/gui_allocation_hooks.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <string>
#include <vector>
#include <functional>
#include <source_location>
#include <span>

namespace interstice
{
    class Buffer;
    class VisualColorStyle;
    struct VisualBoxStyle;
}

namespace interstice::vulkan
{
    struct Style;


    struct DeletionQueue
    {
        ::std::deque<::std::function<void()>> deletors;

        void push_function(::std::function<void()>&& function)
        {
            deletors.push_back(function);
        }

        void flush()
        {
            // Reverse order: later resources may depend on earlier ones.
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
            {
                (*it)();
            }

            deletors.clear();
        }
    };

    // Need FRAME_OVERLAP+1 slots so the slot being pushed-to is distinct from
    // the slot being flushed in the same BeginFrame call. Static-asserted in
    // vk_window_context.h against the canonical FRAME_OVERLAP definition.
    constexpr unsigned int FRAME_DELETION_SLOT_COUNT = 3;

    class VulkanDeviceContext
    {
    public:
        using SurfaceFactory = std::function<VkSurfaceKHR(VkInstance, const VkAllocationCallbacks*, interstice::OSWindowHandle)>;
        void SetSurfaceFactory(SurfaceFactory factory) { m_surfaceFactory = std::move(factory); }

        // Host memory functions for every Vulkan object, vk-bootstrap and VMA.
        // Call before initialization.
        void SetAllocationHooks(const interstice::AllocationHooks& hooks);
        // What the vkCreate*/vkDestroy* calls pass; null when no hooks are set.
        const VkAllocationCallbacks* AllocationCallbacks() const { return m_hooks.IsSet() ? &m_allocationCallbacks : nullptr; }

        // Diagnostic hook for the GPU test harness. When set BEFORE device init,
        // the instance is built with this debug callback (instead of the default
        // printing messenger) and optionally synchronization-validation, so tests
        // can record validation errors and fail on them. No effect once initialized.
        struct InstanceDebugConfig
        {
            PFN_vkDebugUtilsMessengerCallbackEXT callback = nullptr;
            void*                                userData = nullptr;
            bool                                 enableSyncValidation = false;
        };
        void SetInstanceDebugConfig(const InstanceDebugConfig& cfg) { m_instanceDebugConfig = cfg; }

        // ---- Host-provided device ----------------------------------------
        //
        // By default the GUI creates and owns its own VkInstance and VkDevice.
        // A host application that already owns a Vulkan device (for example a
        // game engine hosting this GUI as its editor) can instead hand its
        // handles to the GUI so both render on the same device and can share
        // resources. Call SetExternalDevice BEFORE initialization. The GUI never
        // destroys anything it did not create.
        //
        // The host's device must satisfy GetDeviceRequirements(), and its
        // instance must enable the extensions the window backend needs to create
        // surfaces (for SDL: SDL_Vulkan_GetInstanceExtensions). The queue must
        // support graphics and presentation to the GUI's windows.
        struct ExternalDevice
        {
            VkInstance       instance = VK_NULL_HANDLE;
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            VkDevice         device = VK_NULL_HANDLE;
            VkQueue          graphicsQueue = VK_NULL_HANDLE;
            uint32_t         graphicsQueueFamily = 0;
            // Optional. When null the GUI creates (and owns) its own allocator.
            // Must be created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT.
            VmaAllocator     allocator = VK_NULL_HANDLE;
            // Optional. Vulkan queues need external synchronization: if the host
            // submits to graphicsQueue from other threads it must pass the mutex
            // it locks around its own vkQueueSubmit/vkQueuePresentKHR calls, and
            // the GUI will lock the same mutex. When null the GUI uses a private
            // mutex, which only protects against its own threads.
            std::mutex*      queueMutex = nullptr;
        };
        void SetExternalDevice(const ExternalDevice& external) { m_externalDevice = external; }
        [[nodiscard]] bool UsesExternalDevice() const noexcept { return m_externalDevice.has_value(); }

        // What a device must provide for the GUI to run on it.
        struct DeviceRequirements
        {
            uint32_t                         apiVersion = VK_API_VERSION_1_3;
            VkPhysicalDeviceFeatures         features{};
            VkPhysicalDeviceVulkan11Features features11{};
            VkPhysicalDeviceVulkan12Features features12{};
            VkPhysicalDeviceVulkan13Features features13{};
            std::vector<const char*>         deviceExtensions;
        };
        // The feature structs have no pNext chain; a host chains them itself.
        [[nodiscard]] static DeviceRequirements GetDeviceRequirements();
        // Checks that a physical device can support GetDeviceRequirements().
        // Appends a line per missing capability to `problems` when non-null.
        // This cannot verify what the host actually enabled at device creation.
        [[nodiscard]] static bool CheckPhysicalDeviceSupport(VkPhysicalDevice physicalDevice, std::string* problems);

        // Locks the queue for the duration of a submit or present. Every
        // vkQueueSubmit/vkQueuePresentKHR on graphicsQueue must hold this.
        [[nodiscard]] std::unique_lock<std::mutex> LockQueue() const { return std::unique_lock<std::mutex>(*m_queueMutex); }

        // vkDeviceWaitIdle needs every queue externally synchronized, so it holds the queue lock.
        void WaitIdle() const;

    public:
        interstice::Image* CreateImage(interstice::uint2 size, interstice::ImageFormat format, interstice::Sampler sampler, bool mipmapped = false) const;
        // Doesn't wait for the GPU: the pixel upload (and mip generation) is
        // recorded at the start of the next window frame (RecordPendingUploads).
        interstice::Image* CreateImage(const void* data, interstice::uint2 size, interstice::ImageFormat format, interstice::Sampler sampler, bool mipmapped = false);
        // Records the queued uploads into cmd, outside any rendering scope.
        void RecordPendingUploads(VkCommandBuffer cmd);
        void DestroyImage(interstice::Image* img);
        // A lost device: frees the images' Vulkan objects (the image objects stay), then
        // makes them again on the new device, uploading the pixels they were made from.
        void ReleaseLiveImages();
        void RebuildLiveImages();
        void DestroyImageAtEnd(interstice::Image* img, interstice::DeviceScope scope,
                               const std::source_location& loc =
                               std::source_location::current());

        interstice::Buffer* UploadVisualColorStyles(::std::span<const interstice::VisualColorStyle> colorStyles) const;
        void DestroyVisualColorStyles(interstice::Buffer* buffer);
        void DestroyVisualColorStylesAtEnd(interstice::Buffer* buffer, interstice::DeviceScope scope,
                                           const std::source_location& loc =
                                           std::source_location::current());

        interstice::Buffer* UploadVisualBoxStyles(::std::span<const interstice::VisualBoxStyle> styles) const;
        void DestroyVisualBoxStyles(interstice::Buffer* buffer);
        void DestroyVisualBoxStylesAtEnd(interstice::Buffer* buffer, interstice::DeviceScope scope,
                                         const std::source_location& loc =
                                         std::source_location::current());

        DeletionQueue& GetDeletionQueue(interstice::DeviceScope scope)
        {
            switch (scope)
            {
                case interstice::DeviceScope::Frame:       return frameDeletionQueues[frameNumber % FRAME_DELETION_SLOT_COUNT];
                case interstice::DeviceScope::Application: return mainDeletionQueue;
                default: throw;
            }
        }

        // Called once per frame after all per-window BeginWindowFrame calls have
        // run. Flushes the slot that was last pushed-to FRAME_OVERLAP frames ago,
        // so the per-window slot-fence waits guarantee its GPU work is done.
        void AdvanceFrameAndFlushDeletions()
        {
            const size_t flushSlot = (frameNumber + 1) % FRAME_DELETION_SLOT_COUNT;
            frameDeletionQueues[flushSlot].flush();
            ++frameNumber;
        }

    public:
        std::string      shaderDirectory { "shaders_compiled" };  // where the compiled shaders are
        GUIRenderContext renderContext;

        VkInstance       instance;
        VkDevice         device;
        VmaAllocator     allocator;
        VkPhysicalDevice chosenGPU;

        // immediate submit structures
        VkFence         immFence;
        VkCommandBuffer immCommandBuffer;
        VkCommandPool   immCommandPool;

        // queue
        VkQueue         graphicsQueue;
        uint32_t        graphicsQueueFamily;

        // layout
        VkDescriptorSetLayout singleImageDescriptorLayout;

        // default sampler
        VkSampler defaultSamplerLinear;
        VkSampler defaultSamplerNearest;
        VkSampler defaultSamplerLinearClampToEdge;

        // queues
        DeletionQueue mainDeletionQueue;
        std::array<DeletionQueue, FRAME_DELETION_SLOT_COUNT> frameDeletionQueues;
        uint64_t frameNumber{ 0 };



        // misc
        VkDebugUtilsMessengerEXT debug_messenger;
        VkCommandPoolCreateInfo commandPoolInfo;
        VkFenceCreateInfo fenceCreateInfo;

        bool initialized{ false };
        interstice::Error initError{};   // why the bring-up failed, when it did

        bool Initialize();
        void Flush();  // Wait for GPU and flush deletion queues
        void Destroy(); // Destroy Vulkan resources

        VkSurfaceKHR CreateSurface(interstice::OSWindowHandle windowHandle);

    private:
        // Creates the command pool, fence, descriptor layout and samplers, for owned
        // and external devices alike. On failure destroys what it created, returns false.
        bool InitializeSharedResources();
        bool AdoptExternalDevice();

        SurfaceFactory m_surfaceFactory;
        interstice::AllocationHooks  m_hooks;
        VkAllocationCallbacks m_allocationCallbacks{};
        InstanceDebugConfig m_instanceDebugConfig;
        std::optional<ExternalDevice> m_externalDevice;
        bool m_ownsInstance  = true;
        bool m_ownsDevice    = true;
        bool m_ownsAllocator = true;

        struct PendingUpload { Buffer staging; VkImage image; VkExtent3D extent; bool mipmapped; };
        std::vector<PendingUpload> m_pendingUploads;
        mutable std::unordered_set<VulkanImage*> m_liveImages;   // made by CreateImage, not yet destroyed

        std::mutex  m_ownedQueueMutex;
        std::mutex* m_queueMutex = &m_ownedQueueMutex;

    public:

        Buffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
        void DestroyBuffer(const Buffer& buffer) const;

        // Host-visible, persistently-mapped, shader-device-address storage buffer
        // written with a direct memcpy - no staging buffer and no blocking
        // immediate_submit. Returns an empty buffer (outAddress = 0) for size 0.
        Buffer CreateMappedStorageBuffer(const void* data, size_t size, VkDeviceAddress& outAddress) const;

        VulkanImage CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkSampler sampler, bool mipmapped = false) const;
        void DestroyImage(const VulkanImage& img);

        // Style buffers only; per-primitive uploads go through the RHI.
        VisualBoxStyleBuffers   UploadMesh(::std::span<const interstice::VisualBoxStyle> visualBoxStyles) const;
        VisualColorStyleBuffers UploadMesh(::std::span<const interstice::VisualColorStyle> colorStyles) const;
        void  DestroyMesh(const VisualBoxStyleBuffers& mesh) const;
        void  DestroyMesh(const VisualColorStyleBuffers& mesh) const;


        VkShaderModule CreateShader(::std::span<uint32_t> spirvBuffer) const;
        void DestroyShader(const VkShaderModule& shader) const;
    };
}