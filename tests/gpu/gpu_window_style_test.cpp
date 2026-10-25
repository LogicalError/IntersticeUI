// Win32 styles of real windows. Its own file: windows.h stays out of the other tests.
#include "gpu_test_harness.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#undef CreateWindow

using namespace GpuTestHarness;

// #5: a borderless window loses SDL's caption style, which would make DWM draw its frame
// (shadow, rounded corners, border) on every borderless window, menus and tooltips too.
TEST_F(GpuTest, BorderlessWindowsGetNoSystemFrame)
{
    using WF = interstice::WindowFlags;
    for (const WF flags : { WF::Borderless, WF::Borderless | WF::Resizable, WF::Borderless | WF::PopupMenu })
    {
        const interstice::OSWindowHandle window =
            windowBackend->CreateWindow("style-test", interstice::IntRect{ -3000, -3000, 200, 100 }, flags | WF::Hidden, nullptr);
        ASSERT_NE(window, nullptr);
        const HWND hwnd = static_cast<HWND>(windowBackend->GetNativeHandle(window));
        ASSERT_NE(hwnd, nullptr);

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        EXPECT_NE(style & WS_POPUP, 0) << "flags " << uint32_t(flags);
        EXPECT_NE(style & WS_CAPTION, WS_CAPTION) << "flags " << uint32_t(flags);
        EXPECT_EQ(style & WS_THICKFRAME, 0) << "flags " << uint32_t(flags);
        BOOL ncRendering = TRUE;
        DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED, &ncRendering, sizeof(ncRendering));
        EXPECT_FALSE(ncRendering) << "DWM draws a frame on a borderless window, flags " << uint32_t(flags);

        windowBackend->DestroyWindow(window);
    }
}
#endif
