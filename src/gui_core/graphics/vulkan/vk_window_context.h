#pragma once

#include <chrono>

#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

namespace tachyon
{
    class GraphicsContext;
}


namespace tachyon::vulkan
{
    class VulkanGraphicsContext;

    constexpr unsigned int FRAME_OVERLAP = 2;
    static_assert(FRAME_DELETION_SLOT_COUNT == FRAME_OVERLAP + 1,
                  "Frame-scope deletion ring needs one more slot than FRAME_OVERLAP");

    struct FrameData
    {
        VkSemaphore swapchainSemaphore = {};
        VkFence renderFence = {};

        DescriptorAllocatorGrowable frameDescriptors = {};
        DeletionQueue flushQueue = {};
        DeletionQueue deletionQueue = {};

        VkCommandPool commandPool = {};
        VkCommandBuffer mainCommandBuffer = {};

        /// Fence-protected, persistently mapped upload arena for GUI primitive
        /// streams, reused each time this frame slot comes round again.
        Buffer          primitiveUploadBuffer {};
        VkDeviceAddress primitiveUploadAddress = 0;
        size_t          primitiveUploadCapacity = 0;
        size_t          primitiveUploadOffset = 0;

        /// Per-slot draw and depth images, so in-flight frames don't share them (frame N's
        /// blit reads drawImage while frame N+1 writes it). The slot's renderFence wait in
        /// BeginWindowFrame gates their reuse.
        VulkanImage drawImage = {};
        VulkanImage depthImage = {};
    };

    struct SwapchainState
    {
        // swapchain
        VkSwapchainKHR              swapchain = {};
        VkFormat                    swapchainImageFormat = {};
        VkExtent2D                  swapchainExtent = {};
        ::std::vector<VkImage>      swapchainImages;
        ::std::vector<VkImageView>  swapchainImageViews;
        // Signaled by the frame's submit, waited on by its present; one per image because
        // no fence covers the present's wait, so a frame slot's semaphore could still be pending.
        ::std::vector<VkSemaphore>  presentSemaphores;
        uint32_t                    swapchainImageIndex = 0;
        VkPresentModeKHR            presentMode = VK_PRESENT_MODE_FIFO_KHR;
    };

    struct WindowVulkanState
    {
        // commands
        FrameData       frames[FRAME_OVERLAP];
        VkCommandBuffer commandBuffer = {};
    };

    class VulkanWindowContext : public tachyon::IWindowContext
    {
    public:
        tachyon::ID                  id { tachyon::ID::Invalid()};
        VulkanDeviceContext*     deviceContext{ nullptr };
        tachyon::OSWindowHandle      osHandle { nullptr };
        void*                    nativeHandle { nullptr };
        tachyon::OnGuiFunc           onGui{};

        tachyon::WindowFlags         windowFlags = tachyon::WindowFlags::None;
                                 
        SwapchainState           swapchain = {};
        WindowVulkanState        state = {};
                                 
        // surface               
        VkSurfaceKHR             surface = {};
        VkExtent2D               windowExtent = {};
        VkExtent2D               drawExtent = {};
                                 
        bool                     resize_requested{ false };
        bool                     freeze_rendering{ false };
        tachyon::int2                mouse_pos = { 0,0 };
        int                      frameNumber{ 0 };

        /// When the last vkQueuePresentKHR returned. The frame-build time runs from here to
        /// just before the next present: the CPU work, without the vsync wait.
        std::chrono::steady_clock::time_point lastPresentEnd{};
        bool                     havePresentEnd{ false };
        double                   lastBuildSeconds{ 0.0 };
        bool                     lastBuildValid{ false };
        /// Recorded and submitted, waiting for PresentWindowFrame.
        bool                     presentPending{ false };
        FrameData*               readbackPending{ nullptr };
        tachyon::int2         recordedPosition{};

        /// Transparent window: each frame is read back for the Windows layered window.
        bool                     needsReadback{ false };
        bool                     renderToImage{ false };      ///< WindowFlags::RenderToImage: no surface
        VkImage                  targetImage{ VK_NULL_HANDLE }; ///< host image each frame is copied into
        VkExtent2D               targetExtent{};
        bool                     targetWritten{ false };       ///< targetImage holds a previous frame
        tachyon::vulkan::AllocatedBuffer  readbackBuffer = {};
        std::vector<uint8_t>     cachedBitmap;

        FrameData& GetCurrentFrame();
        FrameData& GetLastFrame();

		bool IsTransparent() const override { return (windowFlags & tachyon::WindowFlags::Transparent) != tachyon::WindowFlags::None; }

        void CreateSwapchain();
        void DestroySwapchain();
        void ResizeSwapchain(tachyon::int2 size);
        void FlushWindowContext();

        /// Creates a draw and depth image pair sized to windowExtent; each FRAME_OVERLAP slot
        /// has its own.
        void CreateDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage, VkFormat colorFormat);
        void DestroyDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage);

        VkCommandBuffer BeginWindowFrame(FrameData& currentFrame);
        void EndWindowFrame(FrameData& currentFrame);

        void PerformReadback(const VulkanImage& drawImage);
        bool ReadPixels(std::span<uint8_t> bgra) override;
        /// The host image each frame is copied into (render-to-image windows); VK_NULL_HANDLE stops.
        void SetImageTarget(VkImage image, VkExtent2D extent);
        void TransitionTarget(VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to,
                              VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                              VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
        void UpdateLayeredWindowFromCache(int screenX, int screenY);

        static VulkanWindowContext* Create(tachyon::ID id, void* nativeHandle, tachyon::OSWindowHandle osHandle, tachyon::int2 size, tachyon::WindowFlags windowFlags, VulkanDeviceContext* deviceContext, tachyon::OnGuiFunc onGUI);
        void Destroy();


        tachyon::ID GetID() const override { return id; }

        void RenderWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing) override;
        void RecordWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect) override;
        void PresentWindowFrame() override;
        void RecordFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing);
        
        void UpdateTransparentWindow(tachyon::int2 screenPos) override;

        tachyon::OSWindowHandle GetWindowHandle() const override { return osHandle; }
    };

}
