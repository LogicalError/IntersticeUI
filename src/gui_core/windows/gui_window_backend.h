#pragma once

#include <gui_core/gui_base_types.h>
#include <optional>

#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <span>


namespace tachyon
{
    /// @addtogroup windows
    /// @{

    // gui_core/windows/gui_window_flags.h
    enum class WindowFlags : uint32_t;

    // gui_core/input/gui_input_enums.h
    enum class KeyModifier  : uint8_t;
    enum class MouseButtons : uint8_t;
    enum class KeyAction    : uint8_t;
    enum class MouseAction  : uint8_t;
    enum class KeyCode      : uint32_t;

    // gui_core/style/gui_style.h
    enum class MouseCursor : uint8_t;

    class IPlatformInputHandler; // gui_core/input/gui_platform_input_handler.h
	class GraphicsContext;		 // gui_core/graphics/gui_graphics_context.h
    struct ID;                   // gui_core/gui_id.h

    /// An OS window, opaque; what it points to is up to the window backend.
    using OSWindowHandle = void*;
    
    /// Window, monitor and mouse coordinates are physical pixels. The UI scale is
    /// applied to styles, not coordinates (see MonitorInfo::contentScale).
    struct MonitorInfo
    {
        IntRect rect;       ///< Full monitor bounds (virtual screen pixels)
        IntRect workArea;   ///< Usable area (excludes taskbar/dock)
        bool isPrimary = false;    ///< The OS's main monitor
        float contentScale = 1.0f; ///< UI scale the user chose for this monitor (1.0 = 100%)
        float refreshRate  = 0.0f; ///< Hz; 0 when unknown
    };

    /// What a point of a window with a custom title bar (WindowFlags::CustomTitleBar) is to the OS.
    enum class WindowHitArea : uint8_t
    {
        Client,         ///< the GUI handles it
        Caption,        ///< drags the window; a double-click maximizes it
        MaximizeButton, ///< the GUI's maximize button, so the OS can offer its snap layouts
    };

    /// A window's state (IWindowBackend::SetWindowMode). Any window can go fullscreen.
    enum class WindowMode : uint8_t
    {
        Normal,               ///< as placed (restored)
        Minimized,
        Maximized,
        Fullscreen,           ///< borderless, covering its monitor at the desktop resolution
        ExclusiveFullscreen,  ///< takes the monitor over, in its desktop display mode
    };

    /// A window's rendering state: made by GraphicsContext::CreateWindowContext for each
    /// OS window, attached to it with IWindowBackend::AttachContextToWindow, and destroyed
    /// by GraphicsContext::DestroyWindowContext. The backend doesn't own it.
    class IWindowContext
    {
    public:
        virtual ~IWindowContext() = default;

        /// The GUI window's ID; the backend passes it with the window's input.
        virtual ID             GetID() const = 0;
        /// The window it was created for.
        virtual OSWindowHandle GetWindowHandle() const = 0;

        /// Whether the window has WindowFlags::Transparent.
        virtual bool IsTransparent() const = 0;
        /// A frame is recorded (drawn and submitted), then presented. RenderWindowFrame does
        /// both; a host loop records every window before presenting any.
        virtual void RenderWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing) = 0;
        /// Records and submits a frame without presenting it; PresentWindowFrame presents.
        /// The default does both, through RenderWindowFrame.
        virtual void RecordWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect) { RenderWindowFrame(graphicsContext, rect, false); }
        /// May wait for vsync; nothing to do for a render-to-image window.
        virtual void PresentWindowFrame() {}
        /// A transparent window the OS composites from a CPU copy (a layered window on
        /// Windows): shows its last frame again at screenPos (screen pixels). The default does nothing.
        virtual void UpdateTransparentWindow(int2 /*screenPos*/) {}

        /// The last rendered frame, BGRA8 with tightly packed rows; bgra holds width x
        /// height x 4 bytes of the window's pixel size. False when it can't be read.
        virtual bool ReadPixels(std::span<uint8_t> /*bgra*/) { return false; }

    };

    /// Makes and manages the OS windows and delivers their input; GUIApplication::SetWindowBackend
    /// sets one, SDLWindowBackend is the default. Positions and sizes are physical pixels.
    /// NullWindowBackend is the minimal implementation.
    class IWindowBackend
    {
    public:
        IWindowBackend() {}
        
        virtual ~IWindowBackend() {}

        /// Creates a window whose client area is `size` pixels, placed by the OS. `parentOSHandle`
        /// is the owner window, or nullptr for a top-level one. Returns nullptr on failure.
        virtual OSWindowHandle CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle) = 0;
        /// Creates a window whose client area is `rect` (screen pixels); otherwise as the size
        /// overload.
        virtual OSWindowHandle CreateWindow(std::string_view title, IntRect rect, WindowFlags windowFlags, OSWindowHandle parentOSHandle) = 0;
        /// Queues the window for destruction: the next PollEvents passes its context to cleanup,
        /// then destroys it. False for a handle that isn't one of the backend's windows.
        virtual bool DestroyWindow(OSWindowHandle) = 0;


        /// Where the window's input goes (see IPlatformInputHandler); not owned, nullptr for none.
        virtual void AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inputHandler) = 0;
        /// Stores the window's context (not owned), for GetWindowContext, for the ID passed with
        /// the window's input, and for PollEvents' cleanup. nullptr clears it.
        virtual void AttachContextToWindow(OSWindowHandle window, IWindowContext* context) = 0;

        /// Clears the context pointer stored in the OS window, so PollEvents' cleanup
        /// callback gets nullptr instead of double-freeing a context the caller destroys.
        virtual void DetachWindowContext(OSWindowHandle) = 0;

        /// True while the handle is a live window of this backend; false once it was destroyed
        /// (by code or the user) and for nullptr.
        virtual bool IsWindow(OSWindowHandle) = 0;

        /// The context attached with AttachContextToWindow, or nullptr.
        virtual IWindowContext* GetWindowContext(OSWindowHandle) = 0;

        /// Raises the window above the others and asks the OS for the input focus.
        virtual void BringToFront(OSWindowHandle) = 0;
        /// The backend's window under the mouse, or nullptr.
        virtual OSWindowHandle GetWindowUnderMouse() = 0;
        /// The window with the keyboard focus, or nullptr when none of the backend's windows has it.
        virtual OSWindowHandle GetActiveWindow() = 0;
        /// Goes up whenever keyboard focus moves between windows, or to or from
        /// another application; open popups are checked against it.
        virtual uint32_t GetFocusChangeCount() const = 0;

        /// Handles the OS's pending events, delivering input to the attached handlers. Then the
        /// windows destroyed since the last call (DestroyWindow, or closed by the OS) go: cleanup
        /// gets each handle and its context while the window still exists. Runs in UpdateFrame,
        /// and again in RecordFrame.
        virtual void PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) = 0;
        /// The mouse position in screen pixels (SDL: as of the last PollEvents).
        virtual int2 GetScreenMousePos() = 0;
        /// The mouse buttons held down, wherever the mouse is (SDL: as of the last PollEvents).
        virtual MouseButtons GetMouseButtons() = 0;
        /// The OS's double-click time in milliseconds, or 0 where the platform has none.
        virtual float GetDoubleClickMilliseconds() { return 0.0f; }
        /// The color of a screen pixel, anywhere on the desktop (an eyedropper);
        /// nothing where the platform can't read the screen.
        virtual std::optional<color4b> ReadScreenPixel(int2 /*screenPos*/) { return std::nullopt; }
        /// Whether the OS asked to close a WindowFlags::ConfirmClose window since last asked.
        virtual bool TakeCloseRequest(OSWindowHandle /*window*/) { return false; }
        
        /// Shows the system cursor for that shape; MouseCursor::None hides the cursor. The
        /// library sets it once per frame.
        virtual void SetCursor(MouseCursor cursor) = 0;

        /// Where text is being typed in the window (window coordinates), so the
        /// IME places its candidate list next to it.
        virtual void SetTextInputArea(OSWindowHandle window, IntRect rect) = 0;

        /// The UI scale the OS asks for in this window: pixel density times the
        /// monitor's content scale (1.5 on a 150% display). 0 when unknown.
        virtual float GetWindowDisplayScale(OSWindowHandle window) = 0;

        /// The window's render target: OS position (for OS compositing) and size in
        /// physical pixels.
        virtual IntRect GetWindowPixelRect(OSWindowHandle window) = 0;

        /// Presents for a graphics API that swaps through the window (OpenGL). The library
        /// doesn't call it: a Vulkan window context presents itself.
        virtual void SwapBuffers(OSWindowHandle) = 0;

        /// Client area in pixels; {0,0,0,0} while minimized.
        virtual IntRect GetWindowRect(OSWindowHandle) = 0;
        /// The client area's top-left in screen pixels.
        virtual int2 GetWindowPos(OSWindowHandle) = 0;
        /// The client area's size in pixels; {0,0} while minimized.
        virtual int2 GetWindowSize(OSWindowHandle) = 0;

        /// The OS's own window handle, passed to GraphicsContext::CreateWindowContext (HWND on
        /// Windows, NSWindow on macOS, an X11 window or Wayland surface on Linux); nullptr without one.
        virtual void* GetNativeHandle(OSWindowHandle) = 0;

        /// Moves and resizes the window so its client area is `clientRect` (screen pixels); the
        /// backend accounts for the frame. True on success.
        virtual bool SetWindowRect(OSWindowHandle, const IntRect& clientRect) = 0;
        /// Shows or hides the window; hiding doesn't destroy it.
        virtual void SetWindowVisible(OSWindowHandle, bool visible) = 0;
        /// Cloaks a shown window: it takes frames but stays invisible, so it can be drawn
        /// before it appears. False when the backend can't (then the window is hidden until
        /// drawn instead).
        virtual bool SetWindowCloaked(OSWindowHandle, bool /*cloaked*/) { return false; }

        /// Every monitor, the primary first.
        virtual std::vector<MonitorInfo> GetMonitors() const = 0;
        /// Index into GetMonitors of the monitor holding the screen point `pos`; 0 (the
        /// primary) when no monitor holds it, -1 without monitors.
        virtual int GetMonitorAt(int2 pos) const = 0;
        /// Index into GetMonitors of the window's monitor; -1 for an unknown window.
        virtual int GetMonitorFromWindow(OSWindowHandle handle) const = 0;

        /// A millisecond clock (SDL: since SDL started).
        virtual uint32_t GetTicks() const = 0;

        /// Sets the window's state: minimized, maximized, restored or fullscreen. The default
        /// does nothing.
        virtual void       SetWindowMode(OSWindowHandle, WindowMode) {}
        /// The window's state as the OS has it now; WindowMode::Normal by default.
        virtual WindowMode GetWindowMode(OSWindowHandle) const { return WindowMode::Normal; }

        /// The user's preferred language as a BCP 47 tag ("ja", "en-US"), or empty
        /// when the platform doesn't say.
        virtual std::string GetPreferredLanguage() const { return {}; }

        /// The OS mouse cursor's size in pixels (its accessibility size at the display's
        /// scale), or 0 when the platform doesn't say.
        virtual int32_t GetCursorSize() const { return 0; }

        /// The graphics context the windows are made for and painted with (during an
        /// OS live resize). GUIApplication::Initialize sets it before initializing the context.
        virtual void SetGraphicsContext(GraphicsContext* graphicsContext) { m_graphicsContext = graphicsContext; }

        /// Sets `cb`, called as soon as any window moves or resizes, even inside the OS modal
        /// move / resize loop (before the next frame), so it must be safe outside the frame.
        void SetWindowMovedCallback(std::function<void()> cb) { m_onWindowMoved = std::move(cb); }

        /// Classifies points (screen pixels) of CustomTitleBar windows for the OS. Called
        /// from inside PollEvents and the OS's modal move/resize loop.
        void SetHitTestCallback(std::function<WindowHitArea(OSWindowHandle, int2 screenPos)> cb) { m_hitTest = std::move(cb); }
        /// What a point (screen pixels) of the window is, from the hit-test callback;
        /// WindowHitArea::Client without one. For backends, from their OS hit testing.
        WindowHitArea HitTestWindow(OSWindowHandle window, int2 screenPos) const
        {
            return m_hitTest ? m_hitTest(window, screenPos) : WindowHitArea::Client;
        }

    protected:
        /// Set by SetWindowMovedCallback; a backend calls it when a window moves or resizes.
        std::function<void()> m_onWindowMoved;
        /// Set by SetHitTestCallback; a backend asks it through HitTestWindow.
        std::function<WindowHitArea(OSWindowHandle, int2)> m_hitTest;
        /// Set by SetGraphicsContext; not owned.
        GraphicsContext*      m_graphicsContext = nullptr;

    };
    /// @}
} // namespace tachyon
