#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

namespace GUI
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

namespace Vulkan
{
    struct FrameData;
    class VulkanImageSet;

    class VulkanGraphicsContext : public GUI::GraphicsContext
    {
    public:
        VulkanImageSet* vulkanImageSet = nullptr;
        GUI::Buffer* visualBoxStylesMesh = nullptr;
        GUI::Buffer* colorStylesMesh = nullptr;
        std::vector<GUI::ImageIndex> gradientPageImageIndices;
        VulkanDeviceContext deviceContext;

    public:
        VulkanGraphicsContext();

        void SetShaderDirectory(std::string_view directory) override { deviceContext.shaderDirectory = std::string(directory); }
        void SetAllocationHooks(const GUI::AllocationHooks& hooks) override { deviceContext.SetAllocationHooks(hooks); }
        bool Initialize(GUI::IWindowBackend* windowBackend) override;
        void Cleanup() override;
        void BeginFrame() override;
        void EndFrame() override {}

    public:
        GUI::Image* CreateImage(GUI::uint2 size, GUI::ImageFormat format, GUI::Sampler sampler, bool mipmapped = false);
        GUI::Image* CreateImage(void* data, GUI::uint2 size, GUI::ImageFormat format, GUI::Sampler sampler, bool mipmapped = false) override;
        void DestroyImage(GUI::Image* img);
        void DestroyImageAtEnd(GUI::Image* img, GUI::DeviceScope scope,
                               const std::source_location& loc = std::source_location::current()) override;

        GUI::Buffer* UploadVisualColorStyles(std::span<const GUI::VisualColorStyle> colorStyles);
        void DestroyVisualColorStyles(GUI::Buffer* buffer);
        void DestroyVisualColorStylesAtEnd(GUI::Buffer* buffer, GUI::DeviceScope scope,
                                           const std::source_location& loc = std::source_location::current());

        GUI::Buffer* UploadVisualBoxStyles(std::span<const GUI::VisualBoxStyle> visualBoxStyles);
        void DestroyVisualBoxStyles(GUI::Buffer* buffer);
        void DestroyVisualBoxStylesAtEnd(GUI::Buffer* buffer, GUI::DeviceScope scope,
                                         const std::source_location& loc = std::source_location::current());

    public:
        GUI::IWindowContext* CreateWindowContext(void* nativeHandle, GUI::OSWindowHandle osHandle, 
                                                 GUI::ID id, GUI::int2 size, GUI::WindowFlags windowFlags,
                                                 GUI::OnGuiFunc onGui) override;
        void DestroyWindowContext(GUI::IWindowContext* context) override;

        GUI::TextAtlasManager* CreateTextAtlasManager() override;

        GUI::ImageSet* GetImageSet() const override;
        GUI::ImageIndex RegisterImage(GUI::Image* image) override;
        void UnregisterImage(GUI::ImageIndex index) override;

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
        GUI::ImageIndex RegisterExternalImage(VkImageView view, VkSampler sampler);
        // Points an existing index at a new view, for example after a resize. The
        // host must make sure no in-flight GUI frame still samples the old view.
        void UpdateExternalImage(GUI::ImageIndex index, VkImageView view, VkSampler sampler);

        bool StartResizing(GUI::ID id) { return guiRenderer->StartResizing(id); }
        void StopResizing() { guiRenderer->StopResizing(); }
        void Reset() { guiRenderer->Reset(); }

        GUI::Buffer* PrepareVisualBoxStylesBuffer(GUI::StyleManager& styleManager);
        GUI::Buffer* PrepareVisualColorStylesBuffer(GUI::StyleManager& styleManager, GUI::GradientManager& gradientManager);

        GUI::GPUDrawPushConstants pushConstants{};

        void PrepareFrame(GUI::StyleManager& styleManager, GUI::GradientManager& gradientManager, float elapsedTimeSecs) override;
        // PrepareFrame with the last call's managers; lets live resize pick up
        // style buffer updates that onGui() just produced.
        void RePrepareFrame();
        bool RenderWindowFrame(VkCommandBuffer cmd, FrameData& currentFrame, GUI::ID windowID, GUI::uint2 extent);

        float GetLastFrameBuildSeconds() const override { return m_frameBuildSeconds; }
        // Window contexts report CPU work between presents (excludes the vsync
        // stall in vkQueuePresentKHR). Last writer per frame wins.
        void  SetFrameBuildSeconds(float seconds) { m_frameBuildSeconds = seconds; }

    private:
        GUI::StyleManager*    m_lastStyleManager{ nullptr };
        GUI::GradientManager* m_lastGradientManager{ nullptr };
        float                 m_lastElapsedTimeSecs{ 0.0f };
        float                 m_frameBuildSeconds{ 0.0f };
    };
}
