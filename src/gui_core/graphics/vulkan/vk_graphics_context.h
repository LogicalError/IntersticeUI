#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_types.h>

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

    /// The default graphics backend, on Vulkan 1.3. Makes its own device, or renders on a
    /// host's (SetExternalDevice); a lost device it made is made again (RecreateDevice).
    class VulkanGraphicsContext : public interstice::GraphicsContext
    {
    public:
        /// Library internal: the image set behind GetImageSet.
        VulkanImageSet* vulkanImageSet = nullptr;
        /// Library internal: the box styles' GPU buffer.
        interstice::Buffer* visualBoxStylesMesh = nullptr;
        /// Library internal: the color styles' GPU buffer.
        interstice::Buffer* colorStylesMesh = nullptr;
        /// Library internal: the gradient pages' image indices.
        std::vector<interstice::ImageIndex> gradientPageImageIndices;
        /// The device; set it up (external device, debug config) before Initialize.
        VulkanDeviceContext deviceContext;

    public:
        VulkanGraphicsContext();

        interstice::GraphicsAPI GetAPI() const override { return interstice::GraphicsAPI::Vulkan; }

        // ---- Setup, before GUIApplication::Initialize (see VulkanDeviceContext) ----
        /// Render on the host's device instead of creating one.
        void SetExternalDevice(const VulkanDeviceContext::ExternalDevice& external) { deviceContext.SetExternalDevice(external); }
        /// How surfaces are made for the window backend's windows; SDLWindowBackend installs its own.
        void SetSurfaceFactory(VulkanDeviceContext::SurfaceFactory factory) { deviceContext.SetSurfaceFactory(std::move(factory)); }

        void SetShaderDirectory(std::string_view directory) override { deviceContext.shaderDirectory = std::string(directory); }
        void SetAllocationHooks(const interstice::AllocationHooks& hooks) override { deviceContext.SetAllocationHooks(hooks); }
        Result<void> Initialize(interstice::IWindowBackend* windowBackend) override;
        void Cleanup() override;
        bool DeviceLost() const override { return interstice::vulkan::IsDeviceLost(); }
        interstice::Result<void> RecreateDevice(interstice::IWindowBackend* windowBackend) override;
        /// Diagnostic: as if the device were lost, so the next frame recreates it.
        void SimulateDeviceLoss() { interstice::vulkan::SetDeviceLost(true); }
        void BeginFrame() override;
        void EndFrame() override {}

    public:
        /// An image nothing is uploaded to (undefined contents), for sampling only.
        interstice::Image* CreateImage(interstice::uint2 size, interstice::ImageFormat format, interstice::Sampler sampler, bool mipmapped = false);
        /// GraphicsContext::CreateImage; null data makes a cleared image.
        interstice::Image* CreateImage(void* data, interstice::uint2 size, interstice::ImageFormat format, interstice::Sampler sampler, bool mipmapped = false) override;
        /// Destroys the image's Vulkan objects now, without waiting for the GPU; doesn't delete
        /// the object. DestroyImageAtEnd is the safe way.
        void DestroyImage(interstice::Image* img);
        void DestroyImageAtEnd(interstice::Image* img, interstice::DeviceScope scope,
                               const std::source_location& loc = std::source_location::current()) override;

        /// Library internal: the color styles in a GPU buffer for the shaders.
        interstice::Buffer* UploadVisualColorStyles(std::span<const interstice::VisualColorStyle> colorStyles);
        /// Library internal: destroys the buffer's GPU objects now.
        void DestroyVisualColorStyles(interstice::Buffer* buffer);
        /// Library internal: destroys and deletes the buffer when the scope's queue flushes.
        void DestroyVisualColorStylesAtEnd(interstice::Buffer* buffer, interstice::DeviceScope scope,
                                           const std::source_location& loc = std::source_location::current());

        /// Library internal: the box styles in a GPU buffer for the shaders.
        interstice::Buffer* UploadVisualBoxStyles(std::span<const interstice::VisualBoxStyle> visualBoxStyles);
        /// Library internal: destroys the buffer's GPU objects now.
        void DestroyVisualBoxStyles(interstice::Buffer* buffer);
        /// Library internal: destroys and deletes the buffer when the scope's queue flushes.
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

        /// An image the host renders itself (a 3D viewport, a preview) as an ImageIndex, drawn like
        /// GUI-owned images (CustomDrawContext::RenderQuad, Element::Image). The view and sampler
        /// stay the host's and must outlive every GUI frame that uses the index. Call it while the
        /// UI is built (OnGUI).
        ///
        /// The GUI samples the image in the fragment stage as SHADER_READ_ONLY_OPTIMAL, in its frame
        /// submission. The host must (1) have it in that layout, with its writes made available to
        /// the fragment stage, in a command buffer submitted to the GUI's queue (the one given to
        /// SetExternalDevice) before the GUI's frame, and (2) before writing it again, transition it
        /// away with a FRAGMENT_SHADER source stage so the write waits for the GUI's reads.
        interstice::ImageIndex RegisterExternalImage(VkImageView view, VkSampler sampler);
        /// Points an existing index at a new view, for example after a resize, under the same
        /// rules as RegisterExternalImage. No in-flight GUI frame may still sample the old view.
        void UpdateExternalImage(interstice::ImageIndex index, VkImageView view, VkSampler sampler);

        /// The host image a render-to-image window (WindowFlags::RenderToImage) is
        /// copied into after each of its frames; VK_NULL_HANDLE stops the copies. May
        /// be set before the window exists. The image needs TRANSFER_DST usage; the
        /// window's pixels are scaled to `extent`. The GUI's frame (one submission on
        /// the shared queue) moves it from SHADER_READ_ONLY_OPTIMAL (UNDEFINED the
        /// first time) to TRANSFER_DST, copies, and leaves it in SHADER_READ_ONLY_OPTIMAL
        /// for the fragment stage. The host reads it in work submitted after the GUI's
        /// frame and must leave it in SHADER_READ_ONLY_OPTIMAL.
        void SetWindowImageTarget(interstice::ID window, VkImage image, VkExtent2D extent);

        /// Library internal: a window's live-resize redraw starts (GUIRenderer::StartResizing).
        bool StartResizing(interstice::ID id) { return guiRenderer->StartResizing(id); }
        /// Library internal: the live-resize redraw ends.
        void StopResizing() { guiRenderer->StopResizing(); }

    private:
        struct ImageTarget { VkImage image; VkExtent2D extent; };
        std::unordered_map<interstice::ID, ImageTarget>                  m_imageTargets;
        std::unordered_map<interstice::ID, interstice::vulkan::VulkanWindowContext*> m_windowContexts;

    public:
        /// Library internal: clears the renderer's recorded primitives.
        void Reset() { guiRenderer->Reset(); }

        /// Library internal: the box styles' buffer, uploaded again when they changed.
        interstice::Buffer* PrepareVisualBoxStylesBuffer(interstice::StyleManager& styleManager);
        /// Library internal: the color styles' buffer, uploaded again when they changed.
        interstice::Buffer* PrepareVisualColorStylesBuffer(interstice::StyleManager& styleManager, interstice::GradientManager& gradientManager);

        /// Library internal: the push constants PrepareFrame sets for the frame's draws.
        interstice::GPUDrawPushConstants pushConstants{};

        /// GraphicsContext::PrepareFrame: uploads changed styles and registers the gradient pages.
        void PrepareFrame(interstice::StyleManager& styleManager, interstice::GradientManager& gradientManager, float elapsedTimeSecs) override;
        /// Library internal: PrepareFrame with the last call's arguments; lets a live resize pick
        /// up the style changes its onGui just made.
        void RePrepareFrame();
        /// Library internal: records a window's primitives into `cmd`.
        bool RenderWindowFrame(VkCommandBuffer cmd, FrameData& currentFrame, interstice::ID windowID, interstice::uint2 extent);

        float GetLastFrameBuildSeconds() const override { return m_frameBuildSeconds; }
        /// Library internal: window contexts report their CPU seconds between presents
        /// (GetLastFrameBuildSeconds); the last one in a frame wins.
        void  SetFrameBuildSeconds(float seconds) { m_frameBuildSeconds = seconds; }

    private:
        interstice::StyleManager*    m_lastStyleManager{ nullptr };
        interstice::GradientManager* m_lastGradientManager{ nullptr };
        float                 m_lastElapsedTimeSecs{ 0.0f };
        float                 m_frameBuildSeconds{ 0.0f };
    };
}
