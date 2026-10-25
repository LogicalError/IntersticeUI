#include "gpu_test_harness.h"
#include <string>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/input/gui_platform_input_handler.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

using namespace GpuTestHarness;

// The window-moved callback is for the OS's own move/resize loop (it closes open menus).
// A window the app moves or resizes itself (SetWindowRect, SetWindowMode from a menu item)
// mustn't fire it in the middle of the frame: the menu would close under its own click.
TEST_F(GpuTest, ProgrammaticWindowChangesDontFireTheMovedCallback)
{
    const interstice::OSWindowHandle window =
        windowBackend->CreateWindow("moved-test", interstice::IntRect{ -3000, -3000, 200, 100 },
                                    interstice::WindowFlags::Hidden | interstice::WindowFlags::Resizable, nullptr);
    ASSERT_NE(window, nullptr);
    int moved = 0;
    windowBackend->SetWindowMovedCallback([&] { ++moved; });

    windowBackend->SetWindowRect(window, interstice::IntRect{ -2900, -2900, 300, 200 });
    windowBackend->SetWindowMode(window, interstice::WindowMode::Maximized);
    windowBackend->SetWindowMode(window, interstice::WindowMode::Normal);
    EXPECT_EQ(moved, 0);

    windowBackend->SetWindowMovedCallback(nullptr);
    windowBackend->DestroyWindow(window);
}

// SDL's own minimize-on-focus-loss would minimize an exclusive fullscreen window whenever a
// menu or dialog of the app opens; the backend turns it off (and minimizes only when the
// focus leaves the app).
TEST_F(GpuTest, ExclusiveFullscreenSurvivesTheAppsOwnPopups)
{
    const char* hint = SDL_GetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS);
    ASSERT_NE(hint, nullptr);
    EXPECT_STREQ(hint, "0");
}

// A zero-size (minimize) swapchain resize must still queue the old image views
// for deletion. A leak surfaces as validation errors at device destroy.
TEST_F(GpuTest, MinimizeResizeDoesNotLeakSwapchainResources)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 });
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    m_checkLeaksAtTeardown = true;

    for (int i = 0; i < 4; ++i)
    {
        ctx->ResizeSwapchain({ 0, 0 });     // minimize
        ctx->ResizeSwapchain({ 320, 240 }); // restore
    }
    // Leaks are reported in TearDown at device destroy.
}

// A minimize requested by RenderWindowFrame clears the live images, so the frame
// must stop before recording against null image handles (a driver crash).
TEST_F(GpuTest, MinimizeFrameStopsBeforeRecordingAndRestores)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 });
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    ctx->resize_requested = true;
    ctx->RenderWindowFrame(graphicsContext, interstice::IntRect{ 0, 0, 0, 0 }, false);
    EXPECT_TRUE(ctx->freeze_rendering);
    EXPECT_EQ(ctx->GetCurrentFrame().drawImage.image, VK_NULL_HANDLE);

    // The cleared image extent makes the restored size request a resize even
    // while frozen. That resize must recreate resources before recording.
    ctx->RenderWindowFrame(graphicsContext, interstice::IntRect{ 0, 0, 320, 240 }, false);
    EXPECT_FALSE(ctx->freeze_rendering);
    EXPECT_NE(ctx->GetLastFrame().drawImage.image, VK_NULL_HANDLE);
}

// Drives a real upload + draw + present frame to a window under sync-validation.
TEST_F(GpuTest, RenderFrameToWindowIsValidationClean)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    sink.Clear();
    RenderOneFrameWithDot(ctx, { 320, 240 });
    EXPECT_FALSE(sink.HasErrors())
        << "rendering a frame tripped validation/sync errors:\n" << sink.Dump();
}

// Several frames so FRAME_OVERLAP cycling, fence reuse, late acquire and
// per-slot upload-arena reuse run under sync-validation.
TEST_F(GpuTest, RenderMultipleFramesIsValidationClean)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    sink.Clear();
    for (int i = 0; i < int(interstice::vulkan::FRAME_OVERLAP); ++i)
        RenderOneFrameWithDot(ctx, { 320, 240 });

    std::array<VkBuffer, interstice::vulkan::FRAME_OVERLAP> arenaHandles{};
    std::array<size_t, interstice::vulkan::FRAME_OVERLAP> arenaCapacities{};
    for (size_t i = 0; i < interstice::vulkan::FRAME_OVERLAP; ++i)
    {
        arenaHandles[i] = ctx->state.frames[i].primitiveUploadBuffer.buffer;
        arenaCapacities[i] = ctx->state.frames[i].primitiveUploadCapacity;
        EXPECT_NE(arenaHandles[i], VK_NULL_HANDLE);
        EXPECT_GE(arenaCapacities[i], 64u * 1024u);
    }

    for (int i = 0; i < 6; ++i)
        RenderOneFrameWithDot(ctx, { 320, 240 });

    for (size_t i = 0; i < interstice::vulkan::FRAME_OVERLAP; ++i)
    {
        EXPECT_EQ(ctx->state.frames[i].primitiveUploadBuffer.buffer, arenaHandles[i]);
        EXPECT_EQ(ctx->state.frames[i].primitiveUploadCapacity, arenaCapacities[i]);
    }
    EXPECT_FALSE(sink.HasErrors())
        << "multi-frame render tripped validation/sync errors:\n" << sink.Dump();
}

TEST_F(GpuTest, PrimitiveUploadArenaGrowsAndThenStaysStable)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    // Warm both frame slots with the minimum arena.
    RenderOneFrameWithDot(ctx, { 320, 240 });
    RenderOneFrameWithDot(ctx, { 320, 240 });
    const VkBuffer oldHandle = ctx->state.frames[0].primitiveUploadBuffer.buffer;
    const size_t oldCapacity = ctx->state.frames[0].primitiveUploadCapacity;

    graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
    interstice::GUIRenderer* r = graphicsContext->guiRenderer;
    r->Reset();
    if (r->BeginWindow(ctx->GetID()))
    {
        for (int i = 0; i < 2048; ++i)
            r->RenderDot(interstice::float2{ float(i % 320), float((i / 320) % 240) },
                         interstice::IntRect{ 0, 0, 320, 240 }, 2.0f,
                         interstice::color4f{ 1, 1, 1, 1 });
        r->EndWindow();
    }
    ctx->RenderWindowFrame(graphicsContext, interstice::IntRect{ 0, 0, 320, 240 }, false);

    auto& grownSlot = ctx->state.frames[0];
    EXPECT_NE(grownSlot.primitiveUploadBuffer.buffer, oldHandle);
    EXPECT_GT(grownSlot.primitiveUploadCapacity, oldCapacity);
    const VkBuffer grownHandle = grownSlot.primitiveUploadBuffer.buffer;

    // Cycle through slot 1 and back to slot 0 with a small frame. The grown
    // allocation should be retained rather than shrinking or reallocating.
    RenderOneFrameWithDot(ctx, { 320, 240 });
    RenderOneFrameWithDot(ctx, { 320, 240 });
    EXPECT_EQ(ctx->state.frames[0].primitiveUploadBuffer.buffer, grownHandle);
    EXPECT_FALSE(sink.HasErrors())
        << "upload-arena growth tripped validation/sync errors:\n" << sink.Dump();
}

// Present mode: prefer MAILBOX (vsync, no tearing, lowest latency) with a third
// image to render ahead; otherwise FIFO double-buffered. Either way the image
// count stays minimal - not vk-bootstrap's larger default - to keep present-queue
// latency down.
TEST_F(GpuTest, SwapchainUsesLowLatencyPresentMode)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    const size_t count = ctx->swapchain.swapchainImages.size();
    if (ctx->swapchain.presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
    {
        EXPECT_EQ(count, 3u) << "MAILBOX needs a third image to render ahead";
    }
    else
    {
        EXPECT_EQ(ctx->swapchain.presentMode, VK_PRESENT_MODE_FIFO_KHR)
            << "expected MAILBOX or the guaranteed FIFO fallback";
        EXPECT_EQ(count, 2u) << "FIFO fallback should stay double-buffered";
    }
}

// Destroying a window must reclaim its render-target GPU buffers rather than
// hold them until shutdown; a leak grows the VMA allocation count per cycle.
TEST_F(GpuTest, WindowDestroyReclaimsRenderTargetBuffers)
{
    auto allocCount = [&] {
        VmaTotalStatistics s{};
        vmaCalculateStatistics(vk->deviceContext.allocator, &s);
        return s.total.statistics.allocationCount;
    };

    uint32_t afterFirst = 0;
    const int kCycles = 6;
    for (int i = 0; i < kCycles; ++i)
    {
        auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
        if (ctx == nullptr)
            GTEST_SKIP() << "no window/swapchain available in this environment";
        RenderOneFrameWithDot(ctx, { 320, 240 });

        const interstice::ID windowID = ctx->GetID();
        graphicsContext->DestroyWindowContext(ctx);   // deletes ctx; the native window stays tracked
        m_windowContexts.back().context = nullptr;

        auto* renderer = static_cast<interstice::PrimitiveRenderer*>(graphicsContext->guiRenderer);
        EXPECT_EQ(renderer->idTargets.find(windowID), renderer->idTargets.end());
        vk->deviceContext.Flush();     // drain the frame deletion queues

        if (i == 0) afterFirst = allocCount();
    }

    EXPECT_LE(allocCount(), afterFirst)
        << "render-target buffers accumulate across window create/destroy cycles "
           "(held until shutdown instead of freed on window destroy)";
}

// Cursor changes run every frame; the backend must reuse one SDL cursor per
// shape rather than creating (and leaking) a new one on every call.
TEST_F(GpuTest, SetCursorReusesSystemCursors)
{
    windowBackend->SetCursor(interstice::MouseCursor::Text);
    SDL_Cursor* text = SDL_GetCursor();
    ASSERT_NE(text, nullptr);

    windowBackend->SetCursor(interstice::MouseCursor::Default);
    windowBackend->SetCursor(interstice::MouseCursor::Text);
    EXPECT_EQ(SDL_GetCursor(), text);
}

// Presentation waits aren't covered by any fence, so the semaphore a present
// waits on must belong to the swapchain image, not to a frame slot that comes
// back around while that present may still be pending.
TEST_F(GpuTest, PresentSemaphoresArePerSwapchainImage)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    m_checkLeaksAtTeardown = true;
    sink.Clear();
    for (int i = 0; i < 6; ++i)
        RenderOneFrameWithDot(ctx, { 320, 240 });
    ctx->ResizeSwapchain({ 400, 300 });
    for (int i = 0; i < 3; ++i)
        RenderOneFrameWithDot(ctx, { 400, 300 });

    const auto& sc = ctx->swapchain;
    ASSERT_EQ(sc.presentSemaphores.size(), sc.swapchainImages.size());
    for (size_t i = 0; i < sc.presentSemaphores.size(); ++i)
    {
        EXPECT_NE(sc.presentSemaphores[i], VK_NULL_HANDLE);
        for (size_t j = i + 1; j < sc.presentSemaphores.size(); ++j)
            EXPECT_NE(sc.presentSemaphores[i], sc.presentSemaphores[j]);
    }
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}

// Mouse events must carry the modifier state at the time of the event, not the
// state from the last key event our windows happened to receive (e.g. Ctrl
// pressed while another app had focus, then a Ctrl-click into ours).
TEST_F(GpuTest, MouseEventsCarryCurrentModifiers)
{
    struct Recorder : interstice::IPlatformInputHandler
    {
        interstice::KeyModifier lastMods = interstice::KeyModifier::None;
        int              presses  = 0;
        void OnKeyboard(interstice::ID, interstice::KeyCode, int, interstice::KeyModifier, interstice::KeyAction) override {}
        void OnMouseMove(interstice::ID, interstice::int2, interstice::MouseButtons, interstice::KeyModifier) override {}
        void OnMouseButton(interstice::ID, interstice::int2, interstice::MouseButtons, interstice::KeyModifier mods, interstice::MouseAction) override
        {
            lastMods = mods;
            presses++;
        }
        void OnMouseScroll(interstice::ID, interstice::int2, interstice::float2, interstice::KeyModifier) override {}
        void OnTextComposition(interstice::ID, std::string_view, int32_t) override {}
    } recorder;

    interstice::OSWindowHandle window = windowBackend->CreateWindow("gpu-test-mods", interstice::int2{ 200, 100 },
                                                             interstice::WindowFlags::Hidden, nullptr);
    if (window == nullptr)
        GTEST_SKIP() << "no window available in this environment";
    windowBackend->AttachInputHandlerToWindow(window, &recorder);

    SDL_SetModState(SDL_KMOD_LCTRL);
    SDL_Event down{};
    down.type             = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.windowID  = SDL_GetWindowID(interstice::SDLWindowBackend::ToSDL(window));
    down.button.button    = SDL_BUTTON_LEFT;
    down.button.down      = true;
    SDL_PushEvent(&down);
    SDL_SetModState(SDL_KMOD_NONE);

    windowBackend->DetachWindowContext(window);
    windowBackend->AttachInputHandlerToWindow(window, nullptr);
    windowBackend->DestroyWindow(window);

    ASSERT_EQ(recorder.presses, 1);
    EXPECT_NE(recorder.lastMods & interstice::KeyModifier::Control, interstice::KeyModifier::None);
}

// IME composition (SDL_EVENT_TEXT_EDITING) reaches the window's input handler
// with its text and caret, so the focused editor can show it.
TEST_F(GpuTest, TextEditingEventsReachTheInputHandler)
{
    struct Recorder : interstice::IPlatformInputHandler
    {
        std::string text;
        int32_t     cursor = -1;
        int         calls  = 0;
        void OnKeyboard(interstice::ID, interstice::KeyCode, int, interstice::KeyModifier, interstice::KeyAction) override {}
        void OnMouseMove(interstice::ID, interstice::int2, interstice::MouseButtons, interstice::KeyModifier) override {}
        void OnMouseButton(interstice::ID, interstice::int2, interstice::MouseButtons, interstice::KeyModifier, interstice::MouseAction) override {}
        void OnMouseScroll(interstice::ID, interstice::int2, interstice::float2, interstice::KeyModifier) override {}
        void OnTextComposition(interstice::ID, std::string_view t, int32_t c) override { text = t; cursor = c; ++calls; }
    } recorder;

    interstice::OSWindowHandle window = windowBackend->CreateWindow("gpu-test-ime", interstice::int2{ 200, 100 },
                                                             interstice::WindowFlags::Hidden, nullptr);
    if (window == nullptr)
        GTEST_SKIP() << "no window available in this environment";
    windowBackend->AttachInputHandlerToWindow(window, &recorder);

    static char composition[] = "\xE3\x81\x8B"; // U+304B, one codepoint
    SDL_Event edit{};
    edit.type          = SDL_EVENT_TEXT_EDITING;
    edit.edit.windowID = SDL_GetWindowID(interstice::SDLWindowBackend::ToSDL(window));
    edit.edit.text     = composition;
    edit.edit.start    = 1;
    edit.edit.length   = 0;
    SDL_PushEvent(&edit);
    SDL_FlushEvent(SDL_EVENT_TEXT_EDITING);

    windowBackend->AttachInputHandlerToWindow(window, nullptr);
    windowBackend->DestroyWindow(window);

    ASSERT_EQ(recorder.calls, 1);
    EXPECT_EQ(recorder.text, composition);
    EXPECT_EQ(recorder.cursor, 1);
}

// A window context torn down right after presenting must wait for its own
// in-flight frames itself; destroying fences, semaphores and command pools the
// GPU still uses is a validation error (and a driver crash in practice).
TEST_F(GpuTest, WindowContextDestroyRightAfterPresentIsValidationClean)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    for (int i = 0; i < 3; ++i)
        RenderOneFrameWithDot(ctx, { 320, 240 });
    sink.Clear();

    ctx->Destroy();
    delete ctx;
    m_windowContexts.back().context = nullptr;

    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}

extern "C" __declspec(dllimport) int __stdcall IsWindow(void* hwnd);

// A window closed through the backend hands its context to the cleanup callback
// while the native window still exists: the surface and swapchain must be
// destroyed before the window they belong to.
TEST_F(GpuTest, WindowCleanupRunsBeforeTheNativeWindowIsDestroyed)
{
    interstice::OSWindowHandle os = windowBackend->CreateWindow("gpu-test-close", interstice::int2{ 200, 100 },
                                                         interstice::WindowFlags::Hidden, nullptr);
    if (os == nullptr)
        GTEST_SKIP() << "no window available in this environment";
    void* native = windowBackend->GetNativeHandle(os);
    ASSERT_NE(native, nullptr);

    ASSERT_TRUE(windowBackend->DestroyWindow(os));
    bool cleanedUp = false;
    bool nativeAliveDuringCleanup = false;
    windowBackend->PollEvents([&](interstice::OSWindowHandle handle, interstice::IWindowContext*)
        {
            if (handle != os) return;
            cleanedUp = true;
            nativeAliveDuringCleanup = IsWindow(native) != 0;
        });

    EXPECT_TRUE(cleanedUp);
    EXPECT_TRUE(nativeAliveDuringCleanup);
    EXPECT_EQ(IsWindow(native), 0) << "the window is still destroyed afterwards";
}

// When no swapchain image becomes available in time (the compositor holds them
// all), the frame is skipped instead of aborting the process.
TEST_F(GpuTest, AcquireTimeoutSkipsTheFrameInsteadOfAborting)
{
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";
    RenderOneFrameWithDot(ctx, { 320, 240 });

    // Hold every image the swapchain will hand out, so the next acquire times out.
    const VkDevice device = vk->deviceContext.device;
    std::vector<VkFence> fences;
    for (size_t i = 0; i < ctx->swapchain.swapchainImages.size(); ++i)
    {
        VkFenceCreateInfo info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateFence(device, &info, nullptr, &fence), VK_SUCCESS);
        uint32_t image = 0;
        const VkResult r = vkAcquireNextImageKHR(device, ctx->swapchain.swapchain, 0, VK_NULL_HANDLE, fence, &image);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { vkDestroyFence(device, fence, nullptr); break; }
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        fences.push_back(fence);
    }
    ASSERT_FALSE(fences.empty());
    sink.Clear();

    for (int i = 0; i < 2; ++i)
    {
        graphicsContext->BeginFrame();
        RenderOneFrameWithDot(ctx, { 320, 240 });
    }

    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
    for (VkFence fence : fences)
        vkDestroyFence(device, fence, nullptr);
}

// A host that started SDL itself keeps it running after the GUI's window
// backend goes away; the backend only shuts down an SDL it started.
TEST(SDLWindowBackendOwnership, LeavesAHostStartedSDLRunning)
{
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    {
        interstice::SDLWindowBackend backend;
    }
    EXPECT_NE(SDL_WasInit(SDL_INIT_VIDEO), 0u);
    SDL_Quit();
}

// SDL has one process-wide event filter; the backend leaves it to the host.
TEST(SDLWindowBackendOwnership, InstallsNoEventFilter)
{
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    {
        interstice::SDLWindowBackend backend;
    }
    SDL_EventFilter filter   = nullptr;
    void*           userdata = nullptr;
    EXPECT_FALSE(SDL_GetEventFilter(&filter, &userdata)) << "a filter is still installed";
    SDL_Quit();
}

// SDL destroys a window's children with it. A child the window manager keeps
// (Detach policy) must survive its parent closing; one it closes goes on its own.
TEST_F(GpuTest, ChildWindowSurvivesItsParentBeingDestroyed)
{
    interstice::OSWindowHandle parent = windowBackend->CreateWindow("gpu-test-parent", interstice::int2{ 300, 200 },
                                                             interstice::WindowFlags::Hidden, nullptr);
    if (parent == nullptr)
        GTEST_SKIP() << "no window available in this environment";
    interstice::OSWindowHandle child = windowBackend->CreateWindow("gpu-test-child", interstice::int2{ 150, 100 },
                                                            interstice::WindowFlags::Hidden, parent);
    ASSERT_NE(child, nullptr);
    void* childNative = windowBackend->GetNativeHandle(child);

    ASSERT_TRUE(windowBackend->DestroyWindow(parent));
    windowBackend->PollEvents([](interstice::OSWindowHandle, interstice::IWindowContext*) {});

    EXPECT_FALSE(windowBackend->IsWindow(parent));
    EXPECT_TRUE(windowBackend->IsWindow(child));
    EXPECT_NE(IsWindow(childNative), 0) << "the child's native window went with its parent";

    ASSERT_TRUE(windowBackend->DestroyWindow(child));
    windowBackend->PollEvents([](interstice::OSWindowHandle, interstice::IWindowContext*) {});
    EXPECT_EQ(IsWindow(childNative), 0);
}

// The SDL backend reports the real display scale for windows and monitors.
TEST_F(GpuTest, BackendReportsDisplayScales)
{
    interstice::OSWindowHandle window = windowBackend->CreateWindow("gpu-test-scale", interstice::int2{ 200, 100 },
                                                             interstice::WindowFlags::Hidden, nullptr);
    if (window == nullptr)
        GTEST_SKIP() << "no window available in this environment";
    SDL_Window* sdlWindow = interstice::SDLWindowBackend::ToSDL(window);
    EXPECT_FLOAT_EQ(windowBackend->GetWindowDisplayScale(window), SDL_GetWindowDisplayScale(sdlWindow));
    EXPECT_GT(windowBackend->GetWindowDisplayScale(window), 0.0f);

    const auto monitors = windowBackend->GetMonitors();
    ASSERT_FALSE(monitors.empty());
    EXPECT_FLOAT_EQ(monitors[0].contentScale, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    windowBackend->DestroyWindow(window);
}

// A device that can't be brought up (here: no surface) makes Initialize return
// false instead of terminating the process.
TEST(GpuInitFailure, MissingSurfaceReturnsFalseInsteadOfTerminating)
{
    auto* vk = new interstice::vulkan::VulkanGraphicsContext();
    interstice::GraphicsContext* graphicsContext = vk;
    auto* windowBackend = new interstice::SDLWindowBackend();
    windowBackend->SetGraphicsContext(graphicsContext);
    vk->SetSurfaceFactory([](VkInstance, const VkAllocationCallbacks*, interstice::OSWindowHandle) { return VkSurfaceKHR(VK_NULL_HANDLE); });

    EXPECT_FALSE(graphicsContext->Initialize(windowBackend));
    graphicsContext->Cleanup(); // shutdown paths run it even after a failed start

    delete windowBackend;
    delete graphicsContext;
}

// Frame build time is the CPU work from one present returning to the next frame
// blocking on its swapchain image, so it includes recording this frame's
// commands (here: uploading many primitives) and not the previous frame's present.
TEST_F(GpuTest, FrameBuildTimeIncludesRecordingTheFrame)
{
    const interstice::int2 size{ 320, 240 };
    auto* ctx = CreateHiddenWindowContext(size, interstice::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    double addSeconds = 0.0, renderSeconds = 0.0;
    for (int frame = 0; frame < 4; ++frame)
    {
        graphicsContext->BeginFrame();
        graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
        interstice::GUIRenderer* r = graphicsContext->guiRenderer;
        r->Reset();
        const auto addStart = std::chrono::steady_clock::now();
        if (r->BeginWindow(ctx->GetID()))
        {
            for (int i = 0; i < 100000; ++i)
                r->RenderDot(interstice::float2{ float(i % 300), float(i / 300 % 220) },
                             interstice::IntRect{ 0, 0, size.x, size.y }, 2.0f, interstice::color4f{ 1, 1, 1, 1 });
            r->EndWindow();
        }
        const auto renderStart = std::chrono::steady_clock::now();
        ctx->RenderWindowFrame(graphicsContext, interstice::IntRect{ 0, 0, size.x, size.y }, false);
        const auto renderEnd = std::chrono::steady_clock::now();
        addSeconds    = std::chrono::duration<double>(renderStart - addStart).count();
        renderSeconds = std::chrono::duration<double>(renderEnd - renderStart).count();
    }

    const double build = graphicsContext->GetLastFrameBuildSeconds();
    EXPECT_GE(build - addSeconds, renderSeconds * 0.5)
        << "build " << build * 1000.0 << " ms, adding primitives " << addSeconds * 1000.0
        << " ms, recording + presenting " << renderSeconds * 1000.0 << " ms";
}
