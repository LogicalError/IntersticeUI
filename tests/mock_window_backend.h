#pragma once

#include <gmock/gmock.h>

#include <gui_core/windows/gui_window_backend.h>

#include <functional>
#include <string_view>
#include <vector>

namespace GUITest
{

class MockWindowBackend : public GUI::IWindowBackend
{
public:
    MockWindowBackend() {}

    MOCK_METHOD(GUI::OSWindowHandle, CreateBootstrapWindow, (), (override));
    MOCK_METHOD(void, DestroyBootstrapWindow, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(GUI::OSWindowHandle, CreateWindow,
        (std::string_view title, GUI::int2 size, GUI::WindowFlags flags, GUI::OSWindowHandle parentOSHandle),
        (override));
    MOCK_METHOD(GUI::OSWindowHandle, CreateWindow,
        (std::string_view title, GUI::IntRect rect, GUI::WindowFlags flags, GUI::OSWindowHandle parentOSHandle),
        (override));
    MOCK_METHOD(void, AttachContextToWindow,
        (GUI::OSWindowHandle window, GUI::IWindowContext* context),
        (override));
    MOCK_METHOD(void, AttachInputHandlerToWindow,
        (GUI::OSWindowHandle window, GUI::IPlatformInputHandler* inputHandler),
        (override));
    MOCK_METHOD(GUI::IWindowContext*, GetWindowContext, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(bool, DestroyWindow, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(void, DetachWindowContext, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(bool, IsWindow, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(void, BringToFront, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(GUI::OSWindowHandle, GetWindowUnderMouse, (), (override));
    MOCK_METHOD(GUI::OSWindowHandle, GetActiveWindow, (), (override));
    MOCK_METHOD(void, PollEvents,
        (std::function<void(GUI::OSWindowHandle, GUI::IWindowContext*)>), (override));
    MOCK_METHOD(GUI::int2, GetScreenMousePos, (), (override));
    MOCK_METHOD(GUI::MouseButtons, GetMouseButtons, (), (override));
    MOCK_METHOD(void, SwapBuffers, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(GUI::IntRect, GetWindowRect, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(GUI::int2, GetWindowPos, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(GUI::int2, GetWindowSize, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(bool, SetWindowRect, (GUI::OSWindowHandle, const GUI::IntRect&), (override));
    MOCK_METHOD(void, SetWindowVisible, (GUI::OSWindowHandle, bool), (override));

    MOCK_METHOD(std::vector<GUI::MonitorInfo>, GetMonitors, (), (const, override));
    MOCK_METHOD(int, GetMonitorAt, (GUI::int2), (const, override));
    MOCK_METHOD(int, GetMonitorFromWindow, (GUI::OSWindowHandle handle), (const, override));

    MOCK_METHOD(uint32_t, GetTicks, (), (const, override));
    MOCK_METHOD(void*, GetNativeHandle, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(void, SetCursor, (GUI::MouseCursor), (override));
    MOCK_METHOD(void, SetTextInputArea, (GUI::OSWindowHandle, GUI::IntRect), (override));
    MOCK_METHOD(float, GetWindowDisplayScale, (GUI::OSWindowHandle), (override));
    MOCK_METHOD(GUI::IntRect, GetWindowPixelRect, (GUI::OSWindowHandle), (override));
};

} // namespace GUITest
