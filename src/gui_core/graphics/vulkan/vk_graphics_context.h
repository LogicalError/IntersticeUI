#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

namespace interstice
{
    class IWindowBackend;
    class GradientManager;
    class StyleManager;
    class ScrollManagement;
    class IWindowContext;
    class StyleManager;
    class GradientManager;
    class ImageSet;
}

namespace interstice::vulkan
{
    struct FrameData;
    class VulkanImageSet;
    class VulkanWindowContext;

    class VulkanGraphicsContext : public interstice::GraphicsContext
    {
    public:
        VulkanImageSet* vulkanImageSet = nullptr;
        interstice::Buffer* visualBoxStylesMesh = nullptr;
        interstice::Buffer* colorStylesMesh = nullptr;
        std::vector<interstice::ImageIndex> gradientPageImageIndices;
        VulkanDeviceContext deviceContext;

    public:
        VulkanGraphicsContext();

        interstice::GraphicsAPI GetAPI() const override { return interstice::GraphicsAPI::Vulkan; }

        // ---- Setup, before GUIApplication::Initialize (see VulkanDeviceContext) ----
        // Render on the host's device instead of creating one.
        void SetExternalDevice(const VulkanDeviceContext::ExternalDevice& external) { deviceContext.SetExternalDevice(external); }
        // How surfaces are made for the window backend's windows; SDLWindowBackend installs its own.
        void SetSurfaceFactory(VulkanDeviceContext::SurfaceFactory factory) { deviceContext.SetSurfaceFactory(std::move(factory)); }

        void SetShaderDirectory(std::string_view directory) override { deviceContext.shaderDirectory = std::string(directory); }
        void SetAllocationHooks(const interstice::AllocationHooks& hooks) override { deviceContext.SetAllocationHooks(hooks); }
        bool Initialize(interstice::IWindowBackend* windowBackend) override;
        void Cleanup() override;
        void BeginFrame() override;
        void EndFrame() override {}

    public:
        interstice::Image* CreateImage(interstice::uint2 size, interstice::ImageFormat format, interstice::Sampler sampler, bool mipmapped = false);
        interstice::Image* CreateImage(void* data, interstice::uint2 size, interstice::ImageFormat format, interstice::Sampler sampler, bool mipmapped = false) override;
        void DestroyImage(interstice::Image* img);
        void DestroyImageAtEnd(interstice::Image* img, interstice::DeviceScope scope,
                               const std::source_location& loc = std::source_location::current()) override;

        interstice::Buffer* UploadVisualColorStyles(std::span<const interstice::VisualColorStyle> colorStyles);
        void DestroyVisualColorStyles(interstice::Buffer* buffer);
        void DestroyVisualColorStylesAtEnd(interstice::Buffer* buffer, interstice::DeviceScope scope,
                                           const std::source_location& loc = std::source_location::current());

        interstice::Buffer* UploadVisualBoxStyles(std::span<const interstice::VisualBoxStyle> visualBoxStyles);
        void DestroyVisualBoxStyles(interstice::Buffer* buffer);
        void DestroyVisualBoxStylesAtEnd(interstice::Buffer* buffer, interstice::DeviceScope scope,
                                         const std::source_location& loc = std::source_location::current());

    public:
        interstice::IWindowContext* CreateWindowContext(void* nativeHandle, interstice::OSWindowHandle osHandle, 
                                                 interstice::ID id, interstice::int2 size, interstice::WindowFlags windowFlags,
                                                 interstice::OnGuiFunc onGui) override;
        void DestroyWindowContext(interstice::IWindowContext* context) override;

        interstice::TextAtlasManager* CreateTextAtlasManager() override;

        interstice::ImageSet* GetImageSet() const override;
        interstice::ImageIndex RegisterImage(interstice::Image* image) override;
        void UnregisterImage(interstice::ImageIndex index) override;

        // ---- Host-rendered images ----
        //
        // Lets a host application show an image it renders itself (a 3D viewport,
        // a preview) inside a GUI element, through the same ImageIndex used for GUI
        // owned images (for example CustomDrawContext::RenderQuad or Element::Image).
        // The view and sampler stay owned by the host and must outlive every GUI
        // frame that references the index.
        //
        // Synchronization contract: the GUI samples the image in the fragment stage
        // as VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, in its frame submission. The
        // host must (1) have the image in that layout, with its writes made
        // available to the fragment shader stage, by the end of the command buffer
        // it submits to the GUI's queue (the same queue given to SetExternalDevice)
        // BEFORE the GUI's frame is submitted, and (2) before writing the image
        // again, transition it away with a source stage of FRAGMENT_SHADER so the
        // write waits for the GUI's earlier reads. Submission order on one queue
        // makes both barriers effective across command buffers. Call these from
        // the UI build phase (OnGUI), before the GUI prepares and submits the frame.
        interstice::ImageIndex RegisterExternalImage(VkImageView view, VkSampler sampler);
        // Points an existing index at a new view, for example after a resize. The
        // host must make sure no in-flight GUI frame still samples the old view.
        void UpdateExternalImage(interstice::ImageIndex index, VkImageView view, VkSampler sampler);

        // The host image a render-to-image window (WindowFlags::RenderToImage) is
        // copied into after each of its frames; VK_NULL_HANDLE stops the copies. May
        // be set before the window exists. The image needs TRANSFER_DST usage; the
        // window's pixels are scaled to `extent`. The GUI's frame (one submission on
        // the shared queue) moves it from SHADER_READ_ONLY_OPTIMAL (UNDEFINED the
        // first time) to TRANSFER_DST, copies, and leaves it in SHADER_READ_ONLY_OPTIMAL
        // for the fragment stage. The host reads it in work submitted after the GUI's
        // frame and must leave it in SHADER_READ_ONLY_OPTIMAL.
        void SetWindowImageTarget(interstice::ID window, VkImage image, VkExtent2D extent);

        bool StartResizing(interstice::ID id) { return guiRenderer->StartResizing(id); }
        void StopResizing() { guiRenderer->StopResizing(); }

    private:
        struct ImageTarget { VkImage image; VkExtent2D extent; };
        std::unordered_map<interstice::ID, ImageTarget>                  m_imageTargets;
        std::unordered_map<interstice::ID, interstice::vulkan::VulkanWindowContext*> m_windowContexts;

    public:
        void Reset() { guiRenderer->Reset(); }

        interstice::Buffer* PrepareVisualBoxStylesBuffer(interstice::StyleManager& styleManager);
        interstice::Buffer* PrepareVisualColorStylesBuffer(interstice::StyleManager& styleManager, interstice::GradientManager& gradientManager);

        interstice::GPUDrawPushConstants pushConstants{};

        void PrepareFrame(interstice::StyleManager& styleManager, interstice::GradientManager& gradientManager, float elapsedTimeSecs) override;
        // PrepareFrame with the last call's managers; lets live resize pick up
        // style buffer updates that onGui() just produced.
        void RePrepareFrame();
        bool RenderWindowFrame(VkCommandBuffer cmd, FrameData& currentFrame, interstice::ID windowID, interstice::uint2 extent);

        float GetLastFrameBuildSeconds() const override { return m_frameBuildSeconds; }
        // Window contexts report CPU work between presents (excludes the vsync
        // stall in vkQueuePresentKHR). Last writer per frame wins.
        void  SetFrameBuildSeconds(float seconds) { m_frameBuildSeconds = seconds; }

    private:
        interstice::StyleManager*    m_lastStyleManager{ nullptr };
        interstice::GradientManager* m_lastGradientManager{ nullptr };
        float                 m_lastElapsedTimeSecs{ 0.0f };
        float                 m_frameBuildSeconds{ 0.0f };
    };
}
