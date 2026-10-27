#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_result.h>

#include <gui_core/graphics/gui_image.h>
#include <gui_core/memory/gui_allocation_hooks.h>

#include <gui_core/style/gui_style.h>

#include <gui_core/windows/gui_window_backend.h>     // OSWindowHandle (used in pure-virtual signatures)

#include <source_location>
#include <span>
#include <string_view>
#include <vector>

namespace tachyon
{
    enum class WindowFlags : uint32_t;
    enum class VisualColorStyleIndex : uint16_t;

    class GUIRenderer;
    class TextAtlasManager;
    class StyleManager;
    class GradientManager;
    class ImageSet;
    class Image;
    class Buffer;
    class VisualColorStyle;
    struct VisualBoxStyle;
    class ITextSystem;
    class GUIStateManager;
    class IWindowBackend;
    class IWindowContext;

    /// When a deferred GPU resource destruction runs: Frame once the frames that may use it
    /// are done, Application at Cleanup.
    enum class DeviceScope
    {
        Frame,
        Application
    };

    /// The graphics API a backend presents with; window backends create their
    /// windows to match (SDL: SDL_WINDOW_VULKAN).
    enum class GraphicsAPI : uint8_t
    {
        None,       ///< nothing is presented (NullGraphicsContext)
        Vulkan,
    };

    /// A graphics backend: the device, the window contexts that render the GUI's windows, and
    /// the images and text atlases they draw with. GUIApplication::SetGraphicsBackend sets one;
    /// VulkanGraphicsContext is the default, NullGraphicsContext draws nothing.
    class GraphicsContext
    {
    public:
        /// Where the UI records its draw primitives. Owned by the context: made in its
        /// constructor, deleted by Cleanup.
        GUIRenderer* guiRenderer = nullptr;

    public:
        GraphicsContext() = default;

        virtual ~GraphicsContext() {}

    public:
        /// The API it presents with; window backends make their windows for it.
        virtual GraphicsAPI GetAPI() const = 0;

        /// Where the compiled shaders are (default "shaders_compiled", relative to the
        /// working directory). Call before Initialize.
        virtual void SetShaderDirectory(std::string_view directory) = 0;
        /// Host memory functions for the graphics API's allocations. Call before Initialize.
        virtual void SetAllocationHooks(const AllocationHooks& hooks) = 0;
        /// Brings the device up; GUIApplication::Initialize calls it once, before any window
        /// context. It may make windows with the backend (Vulkan: a hidden one, to pick a GPU that
        /// can present to it).
        virtual Result<void> Initialize(IWindowBackend* windowBackend) = 0;
        /// True once the GPU device is lost; RecreateDevice then makes a new one.
        virtual bool DeviceLost() const { return false; }
        /// Makes a new device after DeviceLost, and everything the backend owns on it (the window
        /// contexts are the caller's to make again). The default fails with ErrorCode::Unsupported.
        virtual Result<void> RecreateDevice(IWindowBackend*) { return Error{ ErrorCode::Unsupported, "this backend can't recreate its device" }; }
        /// Waits for the GPU and destroys what the backend made, the renderer included. The
        /// window contexts are destroyed before it.
        virtual void Cleanup() = 0;
        /// Starts a frame's UI build: resets the renderer for new primitives (Vulkan: also
        /// frees what frame-scoped deletions held long enough).
        virtual void BeginFrame() = 0;
        /// Ends the UI build, before the uploads and the window contexts' recording.
        virtual void EndFrame() = 0;

    public:
        /// Makes an image from `size.x` x `size.y` tightly packed pixels of `format` (Vulkan: `data`
        /// null for a cleared image); `mipmapped` generates its mip levels. Doesn't wait for the GPU:
        /// the upload goes with the next frame. RegisterImage makes it drawable.
        virtual Image*  CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped = false) = 0;
        /// Destroys the image and deletes the object once the GPU is done with it: a few frames
        /// on for DeviceScope::Frame, at Cleanup for DeviceScope::Application. `loc` is for diagnostics.
        virtual void    DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location& loc = std::source_location::current()) = 0;

    public:
        /// A new text atlas manager for the backend. The caller owns it and keeps it past Cleanup.
        virtual TextAtlasManager* CreateTextAtlasManager() = 0;

        // ---- Window-context lifecycle ----

        /// Makes the context that renders an OS window: `nativeWindowHandle` comes from
        /// IWindowBackend::GetNativeHandle, `size` is in pixels and `onGui` builds the window's UI.
        /// nullptr on failure. Destroy it with DestroyWindowContext.
        virtual IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size, WindowFlags windowFlags, OnGuiFunc onGui) = 0;
        /// Waits for the context's GPU work and destroys it now; its native window must still exist.
        virtual void DestroyWindowContext(IWindowContext* context) = 0;

    public:
        /// The images the shaders sample, by ImageIndex.
        virtual ImageSet* GetImageSet() const = 0;
        /// Adds the image to the image set; elements draw it by the returned index.
        virtual ImageIndex RegisterImage(Image* image) = 0;
        /// Frees the index for reuse; the image stays (DestroyImageAtEnd destroys it).
        virtual void UnregisterImage(ImageIndex index) = 0;

        /// Uploads the styles and gradient pages for this frame's draws, after the UI build and
        /// before the window contexts record. `elapsedTimeSecs`: the frame clock's time, for
        /// shader animation.
        virtual void PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs) = 0;

        /// Wall-clock seconds of CPU work in the last frame: from just after one present returns
        /// to just before the next, so without the vsync wait. 0 for backends that don't measure it.
        virtual float GetLastFrameBuildSeconds() const { return 0.0f; }

    };
}
