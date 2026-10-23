#pragma once

#include <chrono>

#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

namespace GUI
{
    class GraphicsContext;
}


namespace Vulkan
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

        // Fence-protected, persistently mapped upload arena for GUI primitive
        // streams. Reused whenever this frame slot cycles back around.
        Buffer          primitiveUploadBuffer {};
        VkDeviceAddress primitiveUploadAddress = 0;
        size_t          primitiveUploadCapacity = 0;
        size_t          primitiveUploadOffset = 0;

        // Per-slot draw + depth images: the FRAME_OVERLAP in-flight frames must
        // not share these (frame N's blit reads drawImage while frame N+1 would
        // transition/write it — a cross-frame hazard). The slot's renderFence
        // wait in BeginWindowFrame already gates reuse of this slot's images.
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

    class VulkanWindowContext : public GUI::IWindowContext
    {
    public:
        GUI::ID                  id { GUI::ID::Invalid()};
        VulkanDeviceContext*     deviceContext{ nullptr };
        GUI::OSWindowHandle      osHandle { nullptr };
        void*                    nativeHandle { nullptr };
        GUI::OnGuiFunc           onGui{};

        GUI::WindowFlags         windowFlags = GUI::WindowFlags::None;
                                 
        SwapchainState           swapchain = {};
        WindowVulkanState        state = {};
                                 
        // surface               
        VkSurfaceKHR             surface = {};
        VkExtent2D               windowExtent = {};
        VkExtent2D               drawExtent = {};
                                 
        bool                     resize_requested{ false };
        bool                     freeze_rendering{ false };
        GUI::int2                mouse_pos = { 0,0 };
        int                      frameNumber{ 0 };

        // Frame-build timing measured between presents: from just after one
        // vkQueuePresentKHR returns to just before the next is called. This is
        // the CPU work per frame and excludes the vsync stall in present.
        std::chrono::steady_clock::time_point lastPresentEnd{};
        bool                     havePresentEnd{ false };
        double                   lastBuildSeconds{ 0.0 };
        bool                     lastBuildValid{ false };

        // Transparent window support (Windows layered window readback)
        bool                     needsReadback{ false };
        bool                     renderToImage{ false };      // WindowFlags::RenderToImage: no surface
        VkImage                  targetImage{ VK_NULL_HANDLE }; // host image each frame is copied into
        VkExtent2D               targetExtent{};
        bool                     targetWritten{ false };       // targetImage holds a previous frame
        Vulkan::AllocatedBuffer  readbackBuffer = {};
        std::vector<uint8_t>     cachedBitmap;

        FrameData& GetCurrentFrame();
        FrameData& GetLastFrame();

		bool IsTransparent() const override { return (windowFlags & GUI::WindowFlags::Transparent) != GUI::WindowFlags::None; }

        void CreateSwapchain();
        void DestroySwapchain();
        void ResizeSwapchain(GUI::int2 size);
        void FlushWindowContext();

        // Create / destroy a draw+depth image pair sized to windowExtent. One pair
        // lives per FRAME_OVERLAP slot so in-flight frames don't share them.
        void CreateDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage, VkFormat colorFormat);
        void DestroyDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage);

        VkCommandBuffer BeginWindowFrame(FrameData& currentFrame);
        void EndWindowFrame(FrameData& currentFrame);

        void PerformReadback(const VulkanImage& drawImage);
        bool ReadPixels(std::span<uint8_t> bgra) override;
        // Host image each frame is copied into (render-to-image windows); null stops.
        void SetImageTarget(VkImage image, VkExtent2D extent);
        void TransitionTarget(VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to,
                              VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                              VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
        void UpdateLayeredWindowFromCache(int screenX, int screenY);

        static VulkanWindowContext* Create(GUI::ID id, void* nativeHandle, GUI::OSWindowHandle osHandle, GUI::int2 size, GUI::WindowFlags windowFlags, VulkanDeviceContext* deviceContext, GUI::OnGuiFunc onGUI);
        void Destroy();


        GUI::ID GetID() const override { return id; }

        void RenderWindowFrame(GUI::GraphicsContext* graphicsContext, GUI::IntRect rect, bool resizing) override;
        
        void UpdateTransparentWindow(GUI::int2 screenPos) override;

        GUI::OSWindowHandle GetWindowHandle() const override { return osHandle; }
    };

}
