#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/text/gui_text_atlas_manager.h>
#include <gui_core/windows/gui_window_flags.h>

#include <unordered_map>

namespace tachyon
{
    namespace
    {
        class NullImage : public Image {};

        class NullImageSet : public ImageSet
        {
        public:
            void Clear() override { m_count = 0; }
            ImageIndex Add(Image*) override { return static_cast<ImageIndex>(++m_count); }
            void Remove(ImageIndex) override {}

        private:
            uint32_t m_count = 0;
        };

        class NullTextAtlasManager : public TextAtlasManager
        {
        public:
            bool Initialize(GraphicsContext*, ImageSet*) override { return true; }
            TextureIndex CreateTexture(const uint8_t*, uint2, uint8_t) override { return static_cast<TextureIndex>(++m_count); }
            void UpdateTexture(TextureIndex, const uint8_t*, uint2) override {}
            ImageIndex GetImageIndex(TextureIndex) override { return ImageIndex::Invalid; }
            void SyncTextureIndices() override {}

        private:
            uint32_t m_count = 0;
        };

        class NullWindowContext : public IWindowContext
        {
        public:
            NullWindowContext(ID id, OSWindowHandle handle, WindowFlags flags)
                : m_id(id), m_handle(handle), m_flags(flags) {}

            ID             GetID() const override { return m_id; }
            OSWindowHandle GetWindowHandle() const override { return m_handle; }
            bool IsTransparent() const override { return (m_flags & WindowFlags::Transparent) != WindowFlags::None; }
            void RenderWindowFrame(GraphicsContext*, IntRect, bool) override {}

        private:
            ID             m_id;
            OSWindowHandle m_handle;
            WindowFlags    m_flags;
        };
    }

    NullGraphicsContext::NullGraphicsContext()
    {
        guiRenderer = new PrimitiveRenderer();
    }

    NullGraphicsContext::~NullGraphicsContext()
    {
        Cleanup();
    }

    Result<void> NullGraphicsContext::Initialize(IWindowBackend*)
    {
        if (m_imageSet == nullptr)
            m_imageSet = new NullImageSet();
        return {};
    }

    void NullGraphicsContext::Cleanup()
    {
        delete m_imageSet;
        m_imageSet = nullptr;
        delete guiRenderer;
        guiRenderer = nullptr;
    }

    void NullGraphicsContext::BeginFrame()
    {
        if (guiRenderer != nullptr)
            guiRenderer->Reset();
    }

    Image* NullGraphicsContext::CreateImage(void*, uint2, ImageFormat, Sampler, bool)
    {
        return new NullImage();
    }

    void NullGraphicsContext::DestroyImageAtEnd(Image* img, DeviceScope, const std::source_location&)
    {
        delete static_cast<NullImage*>(img);
    }

    TextAtlasManager* NullGraphicsContext::CreateTextAtlasManager()
    {
        return new NullTextAtlasManager();
    }

    IWindowContext* NullGraphicsContext::CreateWindowContext(void*, OSWindowHandle osHandle, ID id, int2, WindowFlags windowFlags, OnGuiFunc)
    {
        return new NullWindowContext(id, osHandle, windowFlags);
    }

    void NullGraphicsContext::DestroyWindowContext(IWindowContext* context)
    {
        if (context == nullptr)
            return;
        if (guiRenderer != nullptr)
            static_cast<PrimitiveRenderer*>(guiRenderer)->ReleaseTarget(context->GetID());
        delete static_cast<NullWindowContext*>(context);
    }

    ImageSet* NullGraphicsContext::GetImageSet() const { return m_imageSet; }
    ImageIndex NullGraphicsContext::RegisterImage(Image* image) { return m_imageSet->Add(image); }
    void NullGraphicsContext::UnregisterImage(ImageIndex index) { m_imageSet->Remove(index); }
}
