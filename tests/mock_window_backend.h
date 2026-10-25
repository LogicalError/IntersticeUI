#pragma once

#include <gmock/gmock.h>

#include <gui_core/windows/gui_window_backend.h>

#include <functional>
#include <string_view>
#include <vector>

namespace GUITest
{

class MockWindowBackend : public interstice::IWindowBackend
{
public:
    MockWindowBackend() {}

    MOCK_METHOD(interstice::OSWindowHandle, CreateWindow,
        (std::string_view title, interstice::int2 size, interstice::WindowFlags flags, interstice::OSWindowHandle parentOSHandle),
        (override));
    MOCK_METHOD(interstice::OSWindowHandle, CreateWindow,
        (std::string_view title, interstice::IntRect rect, interstice::WindowFlags flags, interstice::OSWindowHandle parentOSHandle),
        (override));
    MOCK_METHOD(void, AttachContextToWindow,
        (interstice::OSWindowHandle window, interstice::IWindowContext* context),
        (override));
    MOCK_METHOD(void, AttachInputHandlerToWindow,
        (interstice::OSWindowHandle window, interstice::IPlatformInputHandler* inputHandler),
        (override));
    MOCK_METHOD(interstice::IWindowContext*, GetWindowContext, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(bool, DestroyWindow, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(void, DetachWindowContext, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(bool, IsWindow, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(void, BringToFront, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(uint32_t, GetFocusChangeCount, (), (const, override));
    MOCK_METHOD(interstice::OSWindowHandle, GetWindowUnderMouse, (), (override));
    MOCK_METHOD(interstice::OSWindowHandle, GetActiveWindow, (), (override));
    MOCK_METHOD(void, PollEvents,
        (std::function<void(interstice::OSWindowHandle, interstice::IWindowContext*)>), (override));
    MOCK_METHOD(interstice::int2, GetScreenMousePos, (), (override));
    MOCK_METHOD(std::optional<interstice::color4b>, ReadScreenPixel, (interstice::int2), (override));
    MOCK_METHOD(bool, TakeCloseRequest, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(interstice::MouseButtons, GetMouseButtons, (), (override));
    MOCK_METHOD(void, SwapBuffers, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(interstice::IntRect, GetWindowRect, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(interstice::int2, GetWindowPos, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(interstice::int2, GetWindowSize, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(bool, SetWindowRect, (interstice::OSWindowHandle, const interstice::IntRect&), (override));
    MOCK_METHOD(void, SetWindowVisible, (interstice::OSWindowHandle, bool), (override));

    MOCK_METHOD(std::vector<interstice::MonitorInfo>, GetMonitors, (), (const, override));
    MOCK_METHOD(int, GetMonitorAt, (interstice::int2), (const, override));
    MOCK_METHOD(int, GetMonitorFromWindow, (interstice::OSWindowHandle handle), (const, override));

    MOCK_METHOD(uint32_t, GetTicks, (), (const, override));
    MOCK_METHOD(void*, GetNativeHandle, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(void, SetCursor, (interstice::MouseCursor), (override));
    MOCK_METHOD(void, SetTextInputArea, (interstice::OSWindowHandle, interstice::IntRect), (override));
    MOCK_METHOD(float, GetWindowDisplayScale, (interstice::OSWindowHandle), (override));
    MOCK_METHOD(interstice::IntRect, GetWindowPixelRect, (interstice::OSWindowHandle), (override));
};

} // namespace GUITest
