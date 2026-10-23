#include <gui_core/windows/gui_image_window_backend.h>

#include <gui_core/windows/gui_window_flags.h>
#include <gui_core/input/gui_input_enums.h>

#include <algorithm>
#include <vector>

namespace interstice
{
    struct ImageWindowBackend::ImageWindow
    {
        IntRect          rect { 0, 0, 0, 0 };
        IWindowContext*  context = nullptr;
    };

    struct ImageWindowBackend::State
    {
        std::vector<std::unique_ptr<ImageWindow>> windows;   // the handle is the ImageWindow's address
        ImageWindow*                              pointerWindow = nullptr;
        int2                                      pointerPos{};
        MouseButtons                              pointerButtons = MouseButtons::None;
        ImageWindow*                              focusWindow   = nullptr;
    };

    ImageWindowBackend::ImageWindowBackend(IWindowBackend& osBackend)
        : m_os(osBackend), m_state(std::make_unique<State>())
    {
    }

    ImageWindowBackend::~ImageWindowBackend() = default;

    ImageWindowBackend::ImageWindow* ImageWindowBackend::Find(OSWindowHandle window) const
    {
        for (const auto& w : m_state->windows)
            if (reinterpret_cast<OSWindowHandle>(w.get()) == window)
                return w.get();
        return nullptr;
    }

    bool ImageWindowBackend::IsImageWindow(OSWindowHandle window) const { return window != nullptr && Find(window) != nullptr; }

    void ImageWindowBackend::SetPointer(OSWindowHandle window, int2 screenPos, MouseButtons buttons)
    {
        m_state->pointerWindow  = Find(window);
        m_state->pointerPos     = screenPos;
        m_state->pointerButtons = buttons;
    }

    void ImageWindowBackend::SetFocus(OSWindowHandle window)
    {
        ImageWindow* focus = Find(window);
        if (focus != m_state->focusWindow)
            ++m_focusChanges;
        m_state->focusWindow = focus;
    }


    OSWindowHandle ImageWindowBackend::CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle)
    {
        return CreateWindow(title, IntRect{ 0, 0, size.x, size.y }, windowFlags, parentOSHandle);
    }

    OSWindowHandle ImageWindowBackend::CreateWindow(std::string_view title, IntRect rect, WindowFlags windowFlags, OSWindowHandle parentOSHandle)
    {
        if ((windowFlags & WindowFlags::RenderToImage) == WindowFlags::None)
            return m_os.CreateWindow(title, rect, windowFlags, IsImageWindow(parentOSHandle) ? nullptr : parentOSHandle);
        auto window = std::make_unique<ImageWindow>();
        window->rect = rect;
        const auto handle = reinterpret_cast<OSWindowHandle>(window.get());
        m_state->windows.push_back(std::move(window));
        return handle;
    }

    bool ImageWindowBackend::DestroyWindow(OSWindowHandle window)
    {
        ImageWindow* w = Find(window);
        if (w == nullptr)
            return m_os.DestroyWindow(window);
        if (m_state->pointerWindow == w)
            m_state->pointerWindow = nullptr;
        if (m_state->focusWindow == w)
            m_state->focusWindow = nullptr;
        std::erase_if(m_state->windows, [w](const auto& p) { return p.get() == w; });
        return true;
    }

    void ImageWindowBackend::AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inputHandler)
    {
        // An image window's input comes from the host, not from OS events.
        if (!IsImageWindow(window)) m_os.AttachInputHandlerToWindow(window, inputHandler);
    }

    void ImageWindowBackend::AttachContextToWindow(OSWindowHandle window, IWindowContext* context)
    {
        if (ImageWindow* w = Find(window)) w->context = context;
        else m_os.AttachContextToWindow(window, context);
    }

    void ImageWindowBackend::DetachWindowContext(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) w->context = nullptr;
        else m_os.DetachWindowContext(window);
    }

    bool ImageWindowBackend::IsWindow(OSWindowHandle window) { return IsImageWindow(window) || m_os.IsWindow(window); }

    IWindowContext* ImageWindowBackend::GetWindowContext(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->context;
        return m_os.GetWindowContext(window);
    }

    void ImageWindowBackend::BringToFront(OSWindowHandle window) { if (!IsImageWindow(window)) m_os.BringToFront(window); }

    OSWindowHandle ImageWindowBackend::GetWindowUnderMouse()
    {
        if (m_state->pointerWindow != nullptr)
            return reinterpret_cast<OSWindowHandle>(m_state->pointerWindow);
        return m_os.GetWindowUnderMouse();
    }

    OSWindowHandle ImageWindowBackend::GetActiveWindow()
    {
        if (m_state->focusWindow != nullptr)
            return reinterpret_cast<OSWindowHandle>(m_state->focusWindow);
        return m_os.GetActiveWindow();
    }

    void ImageWindowBackend::PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) { m_os.PollEvents(std::move(cleanup)); }

    int2 ImageWindowBackend::GetScreenMousePos()
    {
        return m_state->pointerWindow != nullptr ? m_state->pointerPos : m_os.GetScreenMousePos();
    }

    MouseButtons ImageWindowBackend::GetMouseButtons()
    {
        return m_state->pointerWindow != nullptr ? m_state->pointerButtons : m_os.GetMouseButtons();
    }
    void ImageWindowBackend::SetCursor(MouseCursor cursor) { m_os.SetCursor(cursor); }

    void ImageWindowBackend::SetTextInputArea(OSWindowHandle window, IntRect rect)
    {
        if (!IsImageWindow(window)) m_os.SetTextInputArea(window, rect);
    }

    float ImageWindowBackend::GetWindowDisplayScale(OSWindowHandle window)
    {
        return IsImageWindow(window) ? 1.0f : m_os.GetWindowDisplayScale(window);
    }

    IntRect ImageWindowBackend::GetWindowPixelRect(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect;
        return m_os.GetWindowPixelRect(window);
    }

    void ImageWindowBackend::SwapBuffers(OSWindowHandle window) { if (!IsImageWindow(window)) m_os.SwapBuffers(window); }

    IntRect ImageWindowBackend::GetWindowRect(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect;
        return m_os.GetWindowRect(window);
    }

    int2 ImageWindowBackend::GetWindowPos(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect.position;
        return m_os.GetWindowPos(window);
    }

    int2 ImageWindowBackend::GetWindowSize(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect.size;
        return m_os.GetWindowSize(window);
    }

    void* ImageWindowBackend::GetNativeHandle(OSWindowHandle window)
    {
        return IsImageWindow(window) ? nullptr : m_os.GetNativeHandle(window);
    }

    bool ImageWindowBackend::SetWindowRect(OSWindowHandle window, const IntRect& clientRect)
    {
        if (ImageWindow* w = Find(window))
        {
            w->rect = clientRect;
            return true;
        }
        return m_os.SetWindowRect(window, clientRect);
    }

    void ImageWindowBackend::SetWindowVisible(OSWindowHandle window, bool visible)
    {
        if (!IsImageWindow(window)) m_os.SetWindowVisible(window, visible);
    }

    std::vector<MonitorInfo> ImageWindowBackend::GetMonitors() const { return m_os.GetMonitors(); }
    int ImageWindowBackend::GetMonitorAt(int2 pos) const { return m_os.GetMonitorAt(pos); }

    int ImageWindowBackend::GetMonitorFromWindow(OSWindowHandle window) const
    {
        return IsImageWindow(window) ? 0 : m_os.GetMonitorFromWindow(window);
    }

    uint32_t ImageWindowBackend::GetTicks() const { return m_os.GetTicks(); }
    std::string ImageWindowBackend::GetPreferredLanguage() const { return m_os.GetPreferredLanguage(); }
}
