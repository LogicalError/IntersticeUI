#pragma once

#include <gui_core/windows/gui_window_backend.h>

#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace interstice
{
    /// A window backend without an OS: windows are records with a rect, and no
    /// input arrives (inject it with GUIApplication::Inject*). For headless runs,
    /// builds without SDL, and as the minimal example of a window backend.
    class NullWindowBackend final : public IWindowBackend
    {
    public:
        /// `monitor` is the one monitor GetMonitors reports, in screen pixels.
        explicit NullWindowBackend(IntRect monitor = { 0, 0, 1920, 1080 });

        OSWindowHandle CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle) override;
        OSWindowHandle CreateWindow(std::string_view title, IntRect rect, WindowFlags windowFlags, OSWindowHandle parentOSHandle) override;
        bool DestroyWindow(OSWindowHandle window) override;
        void AttachInputHandlerToWindow(OSWindowHandle, IPlatformInputHandler*) override {}
        void AttachContextToWindow(OSWindowHandle window, IWindowContext* context) override;
        void DetachWindowContext(OSWindowHandle window) override;
        bool IsWindow(OSWindowHandle window) override { return m_windows.contains(window); }
        IWindowContext* GetWindowContext(OSWindowHandle window) override;
        void BringToFront(OSWindowHandle) override {}
        OSWindowHandle GetWindowUnderMouse() override { return nullptr; }
        OSWindowHandle GetActiveWindow() override { return nullptr; }
        uint32_t GetFocusChangeCount() const override { return 0; }
        void PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) override;
        int2 GetScreenMousePos() override { return {}; }
        MouseButtons GetMouseButtons() override;
        void SetCursor(MouseCursor) override {}
        void SetTextInputArea(OSWindowHandle, IntRect) override {}
        float GetWindowDisplayScale(OSWindowHandle) override { return 1.0f; }
        IntRect GetWindowPixelRect(OSWindowHandle window) override { return GetWindowRect(window); }
        void SwapBuffers(OSWindowHandle) override {}
        IntRect GetWindowRect(OSWindowHandle window) override;
        int2 GetWindowPos(OSWindowHandle window) override { return GetWindowRect(window).position; }
        int2 GetWindowSize(OSWindowHandle window) override { return GetWindowRect(window).size; }
        void* GetNativeHandle(OSWindowHandle) override { return nullptr; }
        bool SetWindowRect(OSWindowHandle window, const IntRect& clientRect) override;
        void SetWindowVisible(OSWindowHandle, bool) override {}
        std::vector<MonitorInfo> GetMonitors() const override;
        int GetMonitorAt(int2) const override { return 0; }
        int GetMonitorFromWindow(OSWindowHandle) const override { return 0; }
        uint32_t GetTicks() const override;

    private:
        struct Window
        {
            IntRect         rect;
            IWindowContext* context = nullptr;
        };

        IntRect                                        m_monitor;
        std::unordered_map<OSWindowHandle, Window>     m_windows;
        std::unordered_set<OSWindowHandle>             m_pendingDestroy;
        uintptr_t                                      m_nextHandle = 0;
        std::chrono::steady_clock::time_point          m_start = std::chrono::steady_clock::now();
    };
}
