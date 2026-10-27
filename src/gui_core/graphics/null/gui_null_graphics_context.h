#pragma once

#include <gui_core/graphics/gui_graphics_context.h>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    /// A graphics backend that draws nothing: the UI is built, laid out and
    /// recorded as usual, but no GPU is used and windows stay empty. For headless
    /// runs and tests, and as the minimal example of a backend.
    class NullGraphicsContext : public GraphicsContext
    {
    public:
        NullGraphicsContext();
        ~NullGraphicsContext() override;

        GraphicsAPI GetAPI() const override { return GraphicsAPI::None; }

        void SetShaderDirectory(std::string_view) override {}
        void SetAllocationHooks(const AllocationHooks&) override {}
        Result<void> Initialize(IWindowBackend* windowBackend) override;
        void Cleanup() override;
        void BeginFrame() override;
        void EndFrame() override {}

        Image* CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped = false) override;
        void   DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location& loc = std::source_location::current()) override;

        TextAtlasManager* CreateTextAtlasManager() override;

        IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size, WindowFlags windowFlags, OnGuiFunc onGui) override;
        void DestroyWindowContext(IWindowContext* context) override;

        ImageSet*  GetImageSet() const override;
        ImageIndex RegisterImage(Image* image) override;
        void       UnregisterImage(ImageIndex index) override;

        void PrepareFrame(StyleManager&, GradientManager&, float) override {}

    private:
        ImageSet* m_imageSet = nullptr;
    };
    /// @}
}
