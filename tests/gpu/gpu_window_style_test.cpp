// Win32 styles of real windows. Its own file: windows.h stays out of the other tests.
#include "gpu_test_harness.h"

#include <gui_controls/gui_application_internal.h>

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
    using WF = tachyon::WindowFlags;
    for (const WF flags : { WF::Borderless, WF::Borderless | WF::Resizable, WF::Borderless | WF::PopupMenu })
    {
        const tachyon::OSWindowHandle window =
            windowBackend->CreateWindow("style-test", tachyon::IntRect{ -3000, -3000, 200, 100 }, flags | WF::Hidden, nullptr);
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

// A drag ghost is a transparent (layered) window: what it renders is read back for
// UpdateLayeredWindow. Off-screen, so nothing shows.
TEST_F(GpuTest, ATransparentWindowReadsBackWhatItRenders)
{
    using WF = tachyon::WindowFlags;
    const WF flags = WF::Borderless | WF::Utility | WF::SkipTaskbar | WF::AlwaysOnTop | WF::Transparent | WF::AllowHighDPI;
    const tachyon::int2 size{ 64, 64 };
    const tachyon::OSWindowHandle window =
        windowBackend->CreateWindow("ghost-test", tachyon::IntRect{ -3000, -3000, size.x, size.y }, flags, nullptr);
    ASSERT_NE(window, nullptr);
    tachyon::ID id{};
    id.value.dynamicParts.mode      = tachyon::IDMode::Static;
    id.value.dynamicParts.hashValue = 777;
    auto* ctx = tachyon::vulkan::VulkanWindowContext::Create(id, windowBackend->GetNativeHandle(window), window, size,
                                                                flags, &vk->deviceContext, [] {});
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(ctx->needsReadback);

    for (int frame = 0; frame < 3; ++frame)
        RenderOneFrameWithDot(ctx, size);
    vk->deviceContext.WaitIdle();

    size_t opaque = 0;
    for (size_t i = 3; i < ctx->cachedBitmap.size(); i += 4)
        if (ctx->cachedBitmap[i] > 0) ++opaque;
    EXPECT_GT(opaque, 0u) << "the dot reached the layered window's bitmap";

    ctx->Destroy();
    delete ctx;
    windowBackend->DestroyWindow(window);
}

namespace
{
    // An app whose only window is a cursor ghost, as a drag shows one; off-screen.
    class GhostApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID   ghostID = tachyon::ID::Invalid();
        tachyon::int2 cursor{ -3000, -3000 };
        void OnGUI() override
        {
            if (BeginCursorGhostWindow(ghostID, cursor))
                AddGhostIcon(theme.addIcon);
            EndGhostWindow();
        }
    };
}

// The drag ghost of a real app: its OS window is shown, by the cursor, with what it draws.
// The icon is a single-color one (the demo's file icons are), which takes the ghost icon's
// color; a transparent one hid it.
TEST(GpuGhostWindow, ACursorGhostWindowIsShownWithItsIcon)
{
    GhostApp app;
    if (!app.Initialize())
    {
        app.Cleanup();
        GTEST_SKIP() << "no Vulkan device or window system available";
    }
    app.ghostID = app.idManager.CreateStaticID();
    for (int frame = 0; frame < 4 && app.UpdateFrame(); ++frame)
    {
        ASSERT_TRUE(app.RecordFrame());
        app.PresentFrame();
    }

    const tachyon::OSWindowHandle handle = app.windowManager->GetWindowHandle(app.ghostID);
    ASSERT_NE(handle, nullptr) << "the ghost has an OS window";
    const HWND hwnd = static_cast<HWND>(app.windowBackend->GetNativeHandle(handle));
    ASSERT_NE(hwnd, nullptr);
    EXPECT_TRUE(IsWindowVisible(hwnd)) << "the ghost is shown";
    EXPECT_NE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED, 0);

    RECT r{};
    GetWindowRect(hwnd, &r);
    EXPECT_GT(r.left, -3000) << "offset from the cursor";
    EXPECT_LT(r.left, -2900);
    EXPECT_GT(r.right - r.left, 0);

    auto* ctx = static_cast<tachyon::vulkan::VulkanWindowContext*>(app.windowManager->GetWindowContext(app.ghostID));
    ASSERT_NE(ctx, nullptr);
    size_t opaque = 0;
    for (size_t i = 3; i < ctx->cachedBitmap.size(); i += 4)
        if (ctx->cachedBitmap[i] > 0) ++opaque;
    EXPECT_GT(opaque, 0u) << "the ghost's icon and background reached the layered window";
    app.Cleanup();
}
#endif
