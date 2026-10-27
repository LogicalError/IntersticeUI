#include <gui_core/windows/null/gui_null_window_backend.h>

#include <gui_core/input/gui_input_enums.h>
#include <gui_core/windows/gui_window_flags.h>

namespace tachyon
{
    NullWindowBackend::NullWindowBackend(IntRect monitor)
        : m_monitor(monitor)
    {
    }

    OSWindowHandle NullWindowBackend::CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle)
    {
        // Centered on the monitor, like an OS would place it.
        const int2 position{ m_monitor.x + (m_monitor.w - size.x) / 2, m_monitor.y + (m_monitor.h - size.y) / 2 };
        return CreateWindow(title, IntRect{ .position = position, .size = size }, windowFlags, parentOSHandle);
    }

    OSWindowHandle NullWindowBackend::CreateWindow(std::string_view, IntRect rect, WindowFlags, OSWindowHandle)
    {
        const auto handle = reinterpret_cast<OSWindowHandle>(++m_nextHandle);
        m_windows.emplace(handle, Window{ .rect = rect });
        return handle;
    }

    bool NullWindowBackend::DestroyWindow(OSWindowHandle window)
    {
        if (!m_windows.contains(window))
            return false;
        // Like an OS backend: gone at the next PollEvents, which hands its context to cleanup.
        m_pendingDestroy.insert(window);
        return true;
    }

    void NullWindowBackend::AttachContextToWindow(OSWindowHandle window, IWindowContext* context)
    {
        if (auto it = m_windows.find(window); it != m_windows.end())
            it->second.context = context;
    }

    void NullWindowBackend::DetachWindowContext(OSWindowHandle window)
    {
        AttachContextToWindow(window, nullptr);
    }

    IWindowContext* NullWindowBackend::GetWindowContext(OSWindowHandle window)
    {
        const auto it = m_windows.find(window);
        return it != m_windows.end() ? it->second.context : nullptr;
    }

    void NullWindowBackend::PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup)
    {
        for (OSWindowHandle window : m_pendingDestroy)
        {
            const auto it = m_windows.find(window);
            if (it == m_windows.end())
                continue;
            if (cleanup != nullptr)
                cleanup(window, it->second.context);
            m_windows.erase(it);
        }
        m_pendingDestroy.clear();
    }

    MouseButtons NullWindowBackend::GetMouseButtons()
    {
        return MouseButtons::None;
    }

    IntRect NullWindowBackend::GetWindowRect(OSWindowHandle window)
    {
        const auto it = m_windows.find(window);
        return it != m_windows.end() ? it->second.rect : IntRect{};
    }

    bool NullWindowBackend::SetWindowRect(OSWindowHandle window, const IntRect& clientRect)
    {
        const auto it = m_windows.find(window);
        if (it == m_windows.end())
            return false;
        it->second.rect = clientRect;
        return true;
    }

    std::vector<MonitorInfo> NullWindowBackend::GetMonitors() const
    {
        return { MonitorInfo{ .rect = m_monitor, .workArea = m_monitor, .isPrimary = true } };
    }

    uint32_t NullWindowBackend::GetTicks() const
    {
        const auto elapsed = std::chrono::steady_clock::now() - m_start;
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
}
