#pragma once

#include <gui_core/windows/gui_window_backend.h>

#include <memory>

namespace tachyon
{
    /// @addtogroup windows
    /// @{

    /// Adds windows that render to an image (WindowFlags::RenderToImage) to another
    /// window backend. They have no OS window: the backend keeps their rect and
    /// attached context, and everything else goes to the wrapped backend. The host
    /// feeds their input; SetPointer and SetFocus say where it goes.
    class ImageWindowBackend final : public IWindowBackend
    {
    public:
        explicit ImageWindowBackend(IWindowBackend& osBackend);
        ~ImageWindowBackend() override;

        /// True when `window` is one of this backend's image windows.
        bool IsImageWindow(OSWindowHandle window) const;

        /// The pointer is over an image window, at this screen position with these
        /// buttons held; it is then the window under the mouse. Null window: the
        /// pointer is back on the OS windows.
        void SetPointer(OSWindowHandle window, int2 screenPos, MouseButtons buttons);
        /// The image window with keyboard focus, reported as the active window. Null:
        /// the OS decides again.
        void SetFocus(OSWindowHandle window);

        void SetGraphicsContext(GraphicsContext* graphicsContext) override { IWindowBackend::SetGraphicsContext(graphicsContext); m_os.SetGraphicsContext(graphicsContext); }
        OSWindowHandle CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle) override;
        OSWindowHandle CreateWindow(std::string_view title, IntRect rect, WindowFlags windowFlags, OSWindowHandle parentOSHandle) override;
        bool DestroyWindow(OSWindowHandle window) override;
        void AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inputHandler) override;
        void AttachContextToWindow(OSWindowHandle window, IWindowContext* context) override;
        void DetachWindowContext(OSWindowHandle window) override;
        bool IsWindow(OSWindowHandle window) override;
        IWindowContext* GetWindowContext(OSWindowHandle window) override;
        void BringToFront(OSWindowHandle window) override;
        OSWindowHandle GetWindowUnderMouse() override;
        OSWindowHandle GetActiveWindow() override;
        uint32_t GetFocusChangeCount() const override { return m_os.GetFocusChangeCount() + m_focusChanges; }
        void PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) override;
        int2 GetScreenMousePos() override;
        std::optional<color4b> ReadScreenPixel(int2 screenPos) override { return m_os.ReadScreenPixel(screenPos); }
        bool TakeCloseRequest(OSWindowHandle window) override { return m_os.TakeCloseRequest(window); }
        MouseButtons GetMouseButtons() override;
        void SetCursor(MouseCursor cursor) override;
        void SetTextInputArea(OSWindowHandle window, IntRect rect) override;
        float GetWindowDisplayScale(OSWindowHandle window) override;
        IntRect GetWindowPixelRect(OSWindowHandle window) override;
        void SwapBuffers(OSWindowHandle window) override;
        IntRect GetWindowRect(OSWindowHandle window) override;
        int2 GetWindowPos(OSWindowHandle window) override;
        int2 GetWindowSize(OSWindowHandle window) override;
        void* GetNativeHandle(OSWindowHandle window) override;
        bool SetWindowRect(OSWindowHandle window, const IntRect& clientRect) override;
        void SetWindowVisible(OSWindowHandle window, bool visible) override;
        void       SetWindowMode(OSWindowHandle window, WindowMode mode) override { if (!IsImageWindow(window)) m_os.SetWindowMode(window, mode); }
        WindowMode GetWindowMode(OSWindowHandle window) const override { return IsImageWindow(window) ? WindowMode::Normal : m_os.GetWindowMode(window); }
        std::vector<MonitorInfo> GetMonitors() const override;
        int GetMonitorAt(int2 pos) const override;
        int GetMonitorFromWindow(OSWindowHandle window) const override;
        uint32_t GetTicks() const override;
        std::string GetPreferredLanguage() const override;
        int32_t     GetCursorSize() const override;

    private:
        struct ImageWindow;
        ImageWindow* Find(OSWindowHandle window) const;

        IWindowBackend& m_os;
        struct State;
        std::unique_ptr<State> m_state;
        uint32_t m_focusChanges = 0;   ///< SetFocus moving the focus
    };
    /// @}
}
