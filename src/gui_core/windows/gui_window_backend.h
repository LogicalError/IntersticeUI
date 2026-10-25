#pragma once

#include <gui_core/gui_base_types.h>
#include <optional>

#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <span>


namespace interstice
{
    // requires gui/windows/gui_window_flags.h
    enum class WindowFlags : uint32_t;

    // requires gui/input/gui_input_enums.h
    enum class KeyModifier  : uint8_t;
    enum class MouseButtons : uint8_t;
    enum class KeyAction    : uint8_t;
    enum class MouseAction  : uint8_t;
    enum class KeyCode      : uint32_t;

    // requires gui/style/gui_style.h
    enum class MouseCursor : uint8_t;

    class IPlatformInputHandler; // requires gui/input/gui_platform_input_handler.h
	class GraphicsContext;		 // requires gui/graphics/gui_graphics_context.h
    struct ID;                   // requires gui/gui_id.h

    using OSWindowHandle = void*;
    
    // Window, monitor and mouse coordinates are physical pixels. The UI scale is
    // applied to styles, not coordinates (see MonitorInfo::contentScale).
    struct MonitorInfo
    {
        IntRect rect;       // Full monitor bounds (absolute virtual screen coords)
        IntRect workArea;   // Usable area (excludes taskbar/dock)
        bool isPrimary = false;
        float contentScale = 1.0f; // UI scale the user chose for this monitor (1.0 = 100%)
        float refreshRate  = 0.0f; // Hz; 0 when unknown
    };

    // What a point of a window with a custom title bar (WindowFlags::CustomTitleBar) is to the OS.
    enum class WindowHitArea : uint8_t
    {
        Client,         // the GUI handles it
        Caption,        // drags the window; a double-click maximizes it
        MaximizeButton, // the GUI's maximize button, so the OS can offer its snap layouts
    };

    class IWindowContext
    {
    public:
        virtual ID             GetID() const = 0;
        virtual OSWindowHandle GetWindowHandle() const = 0;

        virtual bool IsTransparent() const = 0;
        virtual void RenderWindowFrame(interstice::GraphicsContext* graphicsContext, interstice::IntRect rect, bool resizing) = 0;
        virtual void UpdateTransparentWindow(int2 /*screenPos*/) {}

        // The last rendered frame, BGRA8 with tightly packed rows; bgra holds width x
        // height x 4 bytes of the window's pixel size. False when it can't be read.
        virtual bool ReadPixels(std::span<uint8_t> /*bgra*/) { return false; }

    };

    class IWindowBackend
    {
    public:
        IWindowBackend() {}
        
        virtual ~IWindowBackend() {}

        // Sizes and rects in pixels; the client area, not the frame.
        virtual OSWindowHandle CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle) = 0;
        virtual OSWindowHandle CreateWindow(std::string_view title, IntRect rect, WindowFlags windowFlags, OSWindowHandle parentOSHandle) = 0;
        virtual bool DestroyWindow(OSWindowHandle) = 0;


        virtual void AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inputHandler) = 0;
        virtual void AttachContextToWindow(OSWindowHandle window, IWindowContext* context) = 0;

        // Clears the context pointer stored in the OS window, so PollEvents' cleanup
        // callback gets nullptr instead of double-freeing a context the caller destroys.
        virtual void DetachWindowContext(OSWindowHandle) = 0;

        // Returns true if the window handle is still valid,
        // false if it has been destroyed (by code or user) or is a nullptr
        virtual bool IsWindow(OSWindowHandle) = 0;

        virtual IWindowContext* GetWindowContext(OSWindowHandle) = 0;

        virtual void BringToFront(OSWindowHandle) = 0;
        // Returns the OS window handle that is considered to be under the mouse.
        virtual OSWindowHandle GetWindowUnderMouse() = 0;
        virtual OSWindowHandle GetActiveWindow() = 0;
        // Goes up whenever keyboard focus moves between windows, or to or from
        // another application; open popups are checked against it.
        virtual uint32_t GetFocusChangeCount() const = 0;

        virtual void PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) = 0;
        virtual int2 GetScreenMousePos() = 0;
        virtual MouseButtons GetMouseButtons() = 0;
        // The color of a screen pixel, anywhere on the desktop (an eyedropper);
        // nothing where the platform can't read the screen.
        virtual std::optional<color4b> ReadScreenPixel(int2 /*screenPos*/) { return std::nullopt; }
        // Whether the OS asked to close a WindowFlags::ConfirmClose window since last asked.
        virtual bool TakeCloseRequest(OSWindowHandle /*window*/) { return false; }
        
        virtual void SetCursor(MouseCursor cursor) = 0;

        // Where text is being typed in the window (window coordinates), so the
        // IME places its candidate list next to it.
        virtual void SetTextInputArea(OSWindowHandle window, IntRect rect) = 0;

        // The UI scale the OS asks for in this window: pixel density times the
        // monitor's content scale (1.5 on a 150% display). 0 when unknown.
        virtual float GetWindowDisplayScale(OSWindowHandle window) = 0;

        // The window's render target: OS position (for OS compositing) and size in
        // physical pixels.
        virtual IntRect GetWindowPixelRect(OSWindowHandle window) = 0;

        virtual void SwapBuffers(OSWindowHandle) = 0;

        // Client area in pixels; {0,0,0,0} while minimized.
        virtual IntRect GetWindowRect(OSWindowHandle) = 0;
        virtual int2 GetWindowPos(OSWindowHandle) = 0;
        virtual int2 GetWindowSize(OSWindowHandle) = 0;

        virtual void* GetNativeHandle(OSWindowHandle) = 0;

        // Request the backend to move/resize an existing window (client-area rect, pixels).
        // Backend is responsible for decorations and applying a client-area rect correctly.
        // Returns true on success.
        virtual bool SetWindowRect(OSWindowHandle, const IntRect& clientRect) = 0;
        virtual void SetWindowVisible(OSWindowHandle, bool visible) = 0;

        virtual std::vector<MonitorInfo> GetMonitors() const = 0;
        virtual int GetMonitorAt(int2 pos) const = 0;                // Returns monitor index containing the point
        virtual int GetMonitorFromWindow(OSWindowHandle handle) const = 0;  // Returns monitor index for the window

        // Timing - get current time in milliseconds
        virtual uint32_t GetTicks() const = 0;

        // The states the OS caption buttons give a window.
        virtual void MinimizeWindow(OSWindowHandle) {}
        virtual void MaximizeWindow(OSWindowHandle) {}
        virtual void RestoreWindow(OSWindowHandle) {}
        virtual bool IsWindowMaximized(OSWindowHandle) const { return false; }

        // The user's preferred language as a BCP 47 tag ("ja", "en-US"), or empty
        // when the platform doesn't say.
        virtual std::string GetPreferredLanguage() const { return {}; }

        // The graphics context the windows are made for and painted with (during an
        // OS live resize). GUIApplication::Initialize sets it before initializing the context.
        virtual void SetGraphicsContext(GraphicsContext* graphicsContext) { m_graphicsContext = graphicsContext; }

        // Set a callback invoked immediately when any window is moved/resized.
        // Fires during the OS modal loop (before the next frame), so the
        // callback must be safe to call outside the normal frame lifecycle.
        void SetWindowMovedCallback(std::function<void()> cb) { m_onWindowMoved = std::move(cb); }

        // Classifies points (screen pixels) of CustomTitleBar windows for the OS. Called
        // from inside PollEvents and the OS's modal move/resize loop.
        void SetHitTestCallback(std::function<WindowHitArea(OSWindowHandle, int2 screenPos)> cb) { m_hitTest = std::move(cb); }
        WindowHitArea HitTestWindow(OSWindowHandle window, int2 screenPos) const
        {
            return m_hitTest ? m_hitTest(window, screenPos) : WindowHitArea::Client;
        }

    protected:
        std::function<void()> m_onWindowMoved;
        std::function<WindowHitArea(OSWindowHandle, int2)> m_hitTest;
        GraphicsContext*      m_graphicsContext = nullptr;

    };
} // namespace interstice
