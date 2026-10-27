#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/gui_graphics_context.h>   // tachyon::DeviceScope
#include <gui_core/graphics/gui_image.h>
#include <gui_core/windows/gui_window_backend.h>      // tachyon::OSWindowHandle

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

namespace tachyon
{
    class Buffer;
    class VisualColorStyle;
    struct VisualBoxStyle;
}

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    struct Style;


    /// Library internal: destroy functions that run later, newest first.
    struct DeletionQueue
    {
        /// The queued functions, oldest first.
        ::std::deque<::std::function<void()>> deletors;

        /// Queues a function for the next flush.
        void push_function(::std::function<void()>&& function)
        {
            deletors.push_back(function);
        }

        /// Runs and clears the queued functions.
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

    /// Frame deletion queue slots: FRAME_OVERLAP + 1, so the slot pushed to is never the one
    /// flushed in the same frame (static-asserted against FRAME_OVERLAP).
    constexpr unsigned int FRAME_DELETION_SLOT_COUNT = 3;

    /// The Vulkan device the GUI renders with: its own, or a host's (SetExternalDevice). It
    /// lives in VulkanGraphicsContext::deviceContext; set it up before GUIApplication::Initialize.
    class VulkanDeviceContext
    {
    public:
        /// Makes a VkSurfaceKHR for one of the window backend's windows with the given instance
        /// and allocation callbacks; VK_NULL_HANDLE on failure. The GUI destroys the surfaces.
        using SurfaceFactory = std::function<VkSurfaceKHR(VkInstance, const VkAllocationCallbacks*, tachyon::OSWindowHandle)>;
        /// How surfaces are made; set before initialization. SDLWindowBackend installs its own.
        void SetSurfaceFactory(SurfaceFactory factory) { m_surfaceFactory = std::move(factory); }

        /// Host memory functions for every Vulkan object, vk-bootstrap and VMA.
        /// Call before initialization.
        void SetAllocationHooks(const tachyon::AllocationHooks& hooks);
        /// What the vkCreate*/vkDestroy* calls pass; null when no hooks are set.
        const VkAllocationCallbacks* AllocationCallbacks() const { return m_hooks.IsSet() ? &m_allocationCallbacks : nullptr; }

        /// Diagnostic hook for the GPU test harness: the instance is built with this debug
        /// callback instead of the default printing messenger, and optionally with
        /// synchronization validation, so tests can fail on validation errors.
        struct InstanceDebugConfig
        {
            PFN_vkDebugUtilsMessengerCallbackEXT callback = nullptr;   ///< Gets the validation messages
            void*                                userData = nullptr;   ///< Passed to callback
            bool                                 enableSyncValidation = false;   ///< adds sync validation
        };
        /// Sets the instance debug config; call before initialization (no effect after).
        void SetInstanceDebugConfig(const InstanceDebugConfig& cfg) { m_instanceDebugConfig = cfg; }

        // ---- Host-provided device ----------------------------------------

        /// A host's Vulkan handles for SetExternalDevice, so the GUI renders on the host's device
        /// (an engine hosting it as its editor, say) and can share resources with it. The GUI
        /// never destroys what it didn't create. The device must satisfy GetDeviceRequirements(),
        /// the instance must enable the window backend's surface extensions (SDL:
        /// SDL_Vulkan_GetInstanceExtensions).
        struct ExternalDevice
        {
            /// Must enable the window backend's surface extensions.
            VkInstance       instance = VK_NULL_HANDLE;
            /// The GPU `device` was made on.
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            /// Made with GetDeviceRequirements() enabled.
            VkDevice         device = VK_NULL_HANDLE;
            /// Every GUI submit and present goes here; it supports graphics and presenting to the
            /// GUI's windows.
            VkQueue          graphicsQueue = VK_NULL_HANDLE;
            /// graphicsQueue's family.
            uint32_t         graphicsQueueFamily = 0;
            /// Optional. When null the GUI creates (and owns) its own allocator.
            /// Must be created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT.
            VmaAllocator     allocator = VK_NULL_HANDLE;
            /// Optional. Vulkan queues need external synchronization: if the host
            /// submits to graphicsQueue from other threads it must pass the mutex
            /// it locks around its own vkQueueSubmit/vkQueuePresentKHR calls, and
            /// the GUI will lock the same mutex. When null the GUI uses a private
            /// mutex, which only protects against its own threads.
            std::mutex*      queueMutex = nullptr;
        };
        /// Renders on the host's device instead of making one; call before initialization.
        void SetExternalDevice(const ExternalDevice& external) { m_externalDevice = external; }
        /// Whether SetExternalDevice was called; a lost host device isn't recreated.
        [[nodiscard]] bool UsesExternalDevice() const noexcept { return m_externalDevice.has_value(); }

        /// What a device must provide for the GUI to run on it.
        struct DeviceRequirements
        {
            uint32_t                         apiVersion = VK_API_VERSION_1_3;   ///< The lowest Vulkan version
            VkPhysicalDeviceFeatures         features{};     ///< Vulkan 1.0 features to enable
            VkPhysicalDeviceVulkan11Features features11{};   ///< Vulkan 1.1 features to enable
            VkPhysicalDeviceVulkan12Features features12{};   ///< Vulkan 1.2 features to enable
            VkPhysicalDeviceVulkan13Features features13{};   ///< Vulkan 1.3 features to enable
            std::vector<const char*>         deviceExtensions;   ///< Device extensions to enable
        };
        /// What a host's device must enable for the GUI. The feature structs have no pNext
        /// chain; a host chains them itself.
        [[nodiscard]] static DeviceRequirements GetDeviceRequirements();
        /// Checks that a physical device can support GetDeviceRequirements().
        /// Appends a line per missing capability to `problems` when non-null.
        /// This cannot verify what the host actually enabled at device creation.
        [[nodiscard]] static bool CheckPhysicalDeviceSupport(VkPhysicalDevice physicalDevice, std::string* problems);

        /// Locks the queue for the duration of a submit or present. Every
        /// vkQueueSubmit/vkQueuePresentKHR on graphicsQueue must hold this.
        [[nodiscard]] std::unique_lock<std::mutex> LockQueue() const { return std::unique_lock<std::mutex>(*m_queueMutex); }

        /// Waits until the device is idle, holding the queue lock (vkDeviceWaitIdle needs every
        /// queue externally synchronized).
        void WaitIdle() const;

    public:
        /// An image nothing is uploaded to (undefined contents), for sampling only.
        tachyon::Image* CreateImage(tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped = false) const;
        /// An image from `data` pixels (null: cleared). Doesn't wait for the GPU: the upload and
        /// mip generation are recorded at the start of the next window frame (RecordPendingUploads).
        tachyon::Image* CreateImage(const void* data, tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped = false);
        /// Records the queued uploads into `cmd`, outside any rendering scope.
        void RecordPendingUploads(VkCommandBuffer cmd);
        /// Destroys the image's Vulkan objects now, without waiting for the GPU; doesn't delete
        /// the object.
        void DestroyImage(tachyon::Image* img);
        /// After a device loss: frees the live images' Vulkan objects; the image objects stay.
        void ReleaseLiveImages();
        /// Makes the released images again on the new device, uploading the pixels they were
        /// made from.
        void RebuildLiveImages();
        /// Destroys the image and deletes the object when the scope's deletion queue flushes.
        void DestroyImageAtEnd(tachyon::Image* img, tachyon::DeviceScope scope,
                               const std::source_location& loc =
                               std::source_location::current());

        /// Library internal: the color styles in a GPU buffer for the shaders.
        tachyon::Buffer* UploadVisualColorStyles(::std::span<const tachyon::VisualColorStyle> colorStyles) const;
        /// Library internal: destroys the buffer's GPU objects now.
        void DestroyVisualColorStyles(tachyon::Buffer* buffer);
        /// Library internal: destroys and deletes the buffer when the scope's queue flushes.
        void DestroyVisualColorStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope,
                                           const std::source_location& loc =
                                           std::source_location::current());

        /// Library internal: the box styles in a GPU buffer for the shaders.
        tachyon::Buffer* UploadVisualBoxStyles(::std::span<const tachyon::VisualBoxStyle> styles) const;
        /// Library internal: destroys the buffer's GPU objects now.
        void DestroyVisualBoxStyles(tachyon::Buffer* buffer);
        /// Library internal: destroys and deletes the buffer when the scope's queue flushes.
        void DestroyVisualBoxStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope,
                                         const std::source_location& loc =
                                         std::source_location::current());

        /// Library internal: the current frame's deletion queue, or for DeviceScope::Application
        /// the one flushed at Destroy.
        DeletionQueue& GetDeletionQueue(tachyon::DeviceScope scope)
        {
            switch (scope)
            {
                case tachyon::DeviceScope::Frame:       return frameDeletionQueues[frameNumber % FRAME_DELETION_SLOT_COUNT];
                case tachyon::DeviceScope::Application: return mainDeletionQueue;
                default: throw;
            }
        }

        /// Call once per frame, after every window's BeginWindowFrame: flushes the slot last
        /// pushed to FRAME_OVERLAP frames ago, whose GPU work those fence waits have finished.
        void AdvanceFrameAndFlushDeletions()
        {
            const size_t flushSlot = (frameNumber + 1) % FRAME_DELETION_SLOT_COUNT;
            frameDeletionQueues[flushSlot].flush();
            ++frameNumber;
        }

    public:
        std::string      shaderDirectory;  ///< Where to load the compiled shaders from; empty uses the built-in ones
        /// Library internal: the GUI's pipelines.
        GUIRenderContext renderContext;

        /// The instance; the host's with SetExternalDevice.
        VkInstance       instance;
        /// The device; the host's with SetExternalDevice.
        VkDevice         device;
        /// The allocator; the host's when it passed one.
        VmaAllocator     allocator;
        /// The GPU the device runs on.
        VkPhysicalDevice chosenGPU;

        /// Library internal: the immediate-submit fence.
        VkFence         immFence;
        /// Library internal.
        VkCommandBuffer immCommandBuffer;
        /// Library internal.
        VkCommandPool   immCommandPool;

        /// The queue every GUI submit and present goes to; the host's with SetExternalDevice.
        VkQueue         graphicsQueue;
        /// graphicsQueue's family.
        uint32_t        graphicsQueueFamily;

        /// Library internal: the layout of a one-image descriptor set.
        VkDescriptorSetLayout singleImageDescriptorLayout;

        /// For Sampler::LinearRepeat.
        VkSampler defaultSamplerLinear;
        /// For Sampler::NearestRepeat.
        VkSampler defaultSamplerNearest;
        /// For Sampler::LinearClampToEdge.
        VkSampler defaultSamplerLinearClampToEdge;

        /// Flushed at Destroy (DeviceScope::Application).
        DeletionQueue mainDeletionQueue;
        /// One per frame slot; see AdvanceFrameAndFlushDeletions.
        std::array<DeletionQueue, FRAME_DELETION_SLOT_COUNT> frameDeletionQueues;
        /// Frames advanced so far; picks the frame deletion slot.
        uint64_t frameNumber{ 0 };



        /// The default debug messenger, when the GUI made the instance with one.
        VkDebugUtilsMessengerEXT debug_messenger;
        /// Library internal: reused for the window contexts' command pools.
        VkCommandPoolCreateInfo commandPoolInfo;
        /// Library internal: reused for the window contexts' fences.
        VkFenceCreateInfo fenceCreateInfo;

        /// Whether the device is up (made or adopted).
        bool initialized{ false };
        tachyon::Error initError{};   ///< why the bring-up failed, when it did

        /// Library internal: finishes the bring-up that the first CreateSurface does; false when
        /// that failed (see initError).
        bool Initialize();
        void Flush();  ///< Waits for the GPU and flushes the frame deletion queues
        void Destroy(); ///< Waits for the GPU, flushes every deletion queue and destroys what the GUI made

        /// A surface for the window, through the surface factory. The first call also makes the
        /// device (or adopts the host's) on a GPU that can present to it. VK_NULL_HANDLE on failure.
        VkSurfaceKHR CreateSurface(tachyon::OSWindowHandle windowHandle);

    private:
        /// Creates the command pool, fence, descriptor layout and samplers, for owned
        /// and external devices alike. On failure destroys what it created, returns false.
        bool InitializeSharedResources();
        bool AdoptExternalDevice();

        SurfaceFactory m_surfaceFactory;
        tachyon::AllocationHooks  m_hooks;
        VkAllocationCallbacks m_allocationCallbacks{};
        InstanceDebugConfig m_instanceDebugConfig;
        std::optional<ExternalDevice> m_externalDevice;
        bool m_ownsInstance  = true;
        bool m_ownsDevice    = true;
        bool m_ownsAllocator = true;

        struct PendingUpload { Buffer staging; VkImage image; VkExtent3D extent; bool mipmapped; };
        std::vector<PendingUpload> m_pendingUploads;
        mutable std::unordered_set<VulkanImage*> m_liveImages;   ///< made by CreateImage, not yet destroyed

        std::mutex  m_ownedQueueMutex;
        std::mutex* m_queueMutex = &m_ownedQueueMutex;

    public:

        /// Library internal: a VMA buffer.
        Buffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
        /// Library internal: destroys a CreateBuffer buffer now.
        void DestroyBuffer(const Buffer& buffer) const;

        /// Library internal: a host-visible, persistently mapped storage buffer with a device
        /// address, filled by memcpy (no staging, no blocking submit). Empty, with `outAddress`
        /// 0, for size 0.
        Buffer CreateMappedStorageBuffer(const void* data, size_t size, VkDeviceAddress& outAddress) const;

        /// Library internal: a VMA image with its view.
        VulkanImage CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkSampler sampler, bool mipmapped = false) const;
        /// Library internal: destroys the image and its view now.
        void DestroyImage(const VulkanImage& img);

        /// Library internal: the box styles in GPU buffers. Per-primitive uploads go through the RHI.
        VisualBoxStyleBuffers   UploadMesh(::std::span<const tachyon::VisualBoxStyle> visualBoxStyles) const;
        /// Library internal: the color styles in GPU buffers.
        VisualColorStyleBuffers UploadMesh(::std::span<const tachyon::VisualColorStyle> colorStyles) const;
        /// Library internal: destroys the style buffers now.
        void  DestroyMesh(const VisualBoxStyleBuffers& mesh) const;
        /// Library internal: destroys the style buffers now.
        void  DestroyMesh(const VisualColorStyleBuffers& mesh) const;


        /// Library internal: a shader module from SPIR-V words.
        VkShaderModule CreateShader(::std::span<uint32_t> spirvBuffer) const;
        /// Library internal.
        void DestroyShader(const VkShaderModule& shader) const;
    };
    /// @}
}