#pragma once

#include <gmock/gmock.h>

#include <gui_core/windows/gui_window_backend.h>

#include <functional>
#include <string_view>
#include <vector>

namespace GUITest
{

class MockWindowBackend : public tachyon::IWindowBackend
{
public:
    MockWindowBackend() {}

    MOCK_METHOD(tachyon::OSWindowHandle, CreateWindow,
        (std::string_view title, tachyon::int2 size, tachyon::WindowFlags flags, tachyon::OSWindowHandle parentOSHandle),
        (override));
    MOCK_METHOD(tachyon::OSWindowHandle, CreateWindow,
        (std::string_view title, tachyon::IntRect rect, tachyon::WindowFlags flags, tachyon::OSWindowHandle parentOSHandle),
        (override));
    MOCK_METHOD(void, AttachContextToWindow,
        (tachyon::OSWindowHandle window, tachyon::IWindowContext* context),
        (override));
    MOCK_METHOD(void, AttachInputHandlerToWindow,
        (tachyon::OSWindowHandle window, tachyon::IPlatformInputHandler* inputHandler),
        (override));
    MOCK_METHOD(tachyon::IWindowContext*, GetWindowContext, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(bool, DestroyWindow, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(void, DetachWindowContext, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(bool, IsWindow, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(void, BringToFront, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(uint32_t, GetFocusChangeCount, (), (const, override));
    MOCK_METHOD(tachyon::OSWindowHandle, GetWindowUnderMouse, (), (override));
    MOCK_METHOD(tachyon::OSWindowHandle, GetActiveWindow, (), (override));
    MOCK_METHOD(void, PollEvents,
        (std::function<void(tachyon::OSWindowHandle, tachyon::IWindowContext*)>), (override));
    MOCK_METHOD(tachyon::int2, GetScreenMousePos, (), (override));
    MOCK_METHOD(std::optional<tachyon::color4b>, ReadScreenPixel, (tachyon::int2), (override));
    MOCK_METHOD(bool, TakeCloseRequest, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(tachyon::MouseButtons, GetMouseButtons, (), (override));
    MOCK_METHOD(void, SwapBuffers, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(tachyon::IntRect, GetWindowRect, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(tachyon::int2, GetWindowPos, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(tachyon::int2, GetWindowSize, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(bool, SetWindowRect, (tachyon::OSWindowHandle, const tachyon::IntRect&), (override));
    MOCK_METHOD(void, SetWindowVisible, (tachyon::OSWindowHandle, bool), (override));
    MOCK_METHOD(void, SetWindowMode, (tachyon::OSWindowHandle, tachyon::WindowMode), (override));
    MOCK_METHOD(tachyon::WindowMode, GetWindowMode, (tachyon::OSWindowHandle), (const, override));

    MOCK_METHOD(std::vector<tachyon::MonitorInfo>, GetMonitors, (), (const, override));
    MOCK_METHOD(int, GetMonitorAt, (tachyon::int2), (const, override));
    MOCK_METHOD(int, GetMonitorFromWindow, (tachyon::OSWindowHandle handle), (const, override));

    MOCK_METHOD(uint32_t, GetTicks, (), (const, override));
    MOCK_METHOD(int32_t, GetCursorSize, (), (const, override));
    MOCK_METHOD(void*, GetNativeHandle, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(void, SetCursor, (tachyon::MouseCursor), (override));
    MOCK_METHOD(void, SetTextInputArea, (tachyon::OSWindowHandle, tachyon::IntRect), (override));
    MOCK_METHOD(float, GetWindowDisplayScale, (tachyon::OSWindowHandle), (override));
    MOCK_METHOD(tachyon::IntRect, GetWindowPixelRect, (tachyon::OSWindowHandle), (override));
};

} // namespace GUITest
