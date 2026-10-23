#pragma once

#include <gui_core/gui_base_types.h>

#include <gui_core/graphics/gui_image.h>
#include <gui_core/memory/gui_allocation_hooks.h>

#include <gui_core/style/gui_style.h>

#include <gui_core/windows/gui_window_backend.h>     // OSWindowHandle (used in pure-virtual signatures)

#include <source_location>
#include <span>
#include <string_view>
#include <vector>

namespace interstice
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

    // Per-allocation lifetime scope. Frame-scoped deletion queues are flushed
    // per-frame; Application-scoped queues flush only on shutdown.
    enum class DeviceScope
    {
        Frame,
        Application
    };

    class GraphicsContext
    {
    public:
        GUIRenderer* guiRenderer = nullptr;

    public:
        static GraphicsContext* CreateContext();

    public:
        GraphicsContext() = default;

        virtual ~GraphicsContext() {}

    public:
        // Where the compiled shaders are (default "shaders_compiled", relative to the
        // working directory). Call before Initialize.
        virtual void SetShaderDirectory(std::string_view directory) = 0;
        // Host memory functions for the graphics API's allocations. Call before Initialize.
        virtual void SetAllocationHooks(const AllocationHooks& hooks) = 0;
        virtual bool Initialize(IWindowBackend* windowBackend) = 0;
        virtual void Cleanup() = 0;
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

    public:
        virtual Image*  CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped = false) = 0;
        virtual void    DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location& loc = std::source_location::current()) = 0;

    public:
        virtual TextAtlasManager* CreateTextAtlasManager() = 0;

        // ---- Window-context lifecycle ----

        virtual IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size, WindowFlags windowFlags, OnGuiFunc onGui) = 0;
        // Waits for the context's GPU work and destroys it now; its native window must still exist.
        virtual void DestroyWindowContext(IWindowContext* context) = 0;

    public:
        virtual ImageSet* GetImageSet() const = 0;
        virtual ImageIndex RegisterImage(Image* image) = 0;
        virtual void UnregisterImage(ImageIndex index) = 0;

        // elapsedTimeSecs: the frame clock's time, for shader animation.
        virtual void PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs) = 0;

        // Wall-clock seconds of CPU work in the most recent frame, measured
        // between consecutive presents (from just after one present returns to
        // just before the next), which excludes the vsync stall. Backends that
        // don't measure it return 0.
        virtual float GetLastFrameBuildSeconds() const { return 0.0f; }

    };
}
