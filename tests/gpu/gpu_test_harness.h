#pragma once

// Real-device Vulkan test harness. Boots a VulkanGraphicsContext with the
// validation layers configured to RECORD messages (instead of just printing),
// plus synchronization-validation, so a test can assert that a code path is
// validation-clean. Tests are SKIPPED when no suitable device/display exists.
//
// vk_mem_alloc.h (declarations only) provides the VMA types the internal headers
// below depend on.
#include <vk_mem_alloc.h>
#include <gui_core/graphics/vulkan/vk_types.h>

#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace GpuTestHarness
{
    // Thread-safe collector for validation-layer messages. The validation
    // layers may call the debug callback from worker threads, hence the mutex.
    struct ValidationSink
    {
        std::mutex               mutex;
        std::vector<std::string> errors;    // ERROR severity (incl. sync-validation)
        std::vector<std::string> warnings;  // WARNING severity

        void Clear()
        {
            std::lock_guard<std::mutex> lock(mutex);
            errors.clear();
            warnings.clear();
        }

        bool HasErrors()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return !errors.empty();
        }

        std::string Dump()
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::string out;
            for (const auto& e : errors)   out += "  [error]   " + e + "\n";
            for (const auto& w : warnings) out += "  [warning] " + w + "\n";
            return out;
        }
    };

    inline VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
        VkDebugUtilsMessageTypeFlagsEXT             /*types*/,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*                                       userData)
    {
        auto* sink = static_cast<ValidationSink*>(userData);
        if (sink != nullptr && data != nullptr && data->pMessage != nullptr)
        {
            std::lock_guard<std::mutex> lock(sink->mutex);
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                sink->errors.emplace_back(data->pMessage);
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                sink->warnings.emplace_back(data->pMessage);
        }
        return VK_FALSE; // never abort the triggering call
    }

    class GpuTest : public ::testing::Test
    {
    protected:
        interstice::GraphicsContext*          graphicsContext = nullptr;
        interstice::SDLWindowBackend*         windowBackend   = nullptr;
        interstice::vulkan::VulkanGraphicsContext* vk              = nullptr;
        ValidationSink                 sink;

        // Minimal style/gradient managers so the render path (PrepareFrame) has
        // something to upload. Device-independent, so safe as plain members.
        interstice::GradientManager           gradientManager{ 256 };
        interstice::StyleManager              styleManager{ gradientManager };

        void SetUp() override
        {
            vk              = new interstice::vulkan::VulkanGraphicsContext();
            graphicsContext = vk;
            windowBackend   = new interstice::SDLWindowBackend();
            windowBackend->SetGraphicsContext(graphicsContext);

            interstice::vulkan::VulkanDeviceContext::InstanceDebugConfig cfg;
            cfg.callback             = &ValidationCallback;
            cfg.userData             = &sink;
            // Synchronization validation is the most crash-prone layer feature. Set
            // INTERSTICE_GPU_TEST_NO_SYNC_VALIDATION=1 to run with core validation only,
            // e.g. to tell a layer crash from a product bug.
            char*  noSync    = nullptr;
            size_t noSyncLen = 0;
            _dupenv_s(&noSync, &noSyncLen, "INTERSTICE_GPU_TEST_NO_SYNC_VALIDATION");
            cfg.enableSyncValidation = noSync == nullptr;
            free(noSync);
            vk->deviceContext.SetInstanceDebugConfig(cfg);

            if (!graphicsContext->Initialize(windowBackend))
            {
                Teardown();
                GTEST_SKIP() << "No suitable Vulkan device/display available";
            }

            ASSERT_FALSE(sink.HasErrors())
                << "Validation errors during device initialization:\n" << sink.Dump();

            // With zero styles PrepareFrame would upload a zero-size buffer, which
            // the driver rejects.
            styleManager.Register(interstice::Style{});
        }

        // ---- Swapchain tier: a real (hidden) window + VulkanWindowContext ----
        struct OwnedWindowContext
        {
            interstice::vulkan::VulkanWindowContext* context  = nullptr;
            interstice::OSWindowHandle          osHandle = nullptr;
        };
        std::vector<OwnedWindowContext> m_windowContexts;
        uint32_t                        m_nextWindowId = 7000; // monotonic — never reused

        // When set, TearDown asserts the validation layer reported no leaked
        // Vulkan objects at device destroy. Opt-in, so leak-sensitive tests can
        // require it without imposing it on tests that defer cleanup.
        bool m_checkLeaksAtTeardown = false;

        // Create an OS window + its Vulkan swapchain/window-context. Returns
        // nullptr (the test should SKIP) if no window/surface can be created.
        // Defaults to Hidden; pass WindowFlags::None when the test must actually
        // render/present (acquire+present need a presentable surface).
        interstice::vulkan::VulkanWindowContext* CreateHiddenWindowContext(
            interstice::int2 size = { 320, 240 },
            interstice::WindowFlags flags = interstice::WindowFlags::Hidden)
        {
            interstice::OSWindowHandle osHandle =
                windowBackend->CreateWindow("gpu-test", size, flags, nullptr);
            if (osHandle == nullptr) return nullptr;

            interstice::ID id{};
            id.value.dynamicParts.mode      = interstice::IDMode::Static;
            id.value.dynamicParts.hashValue = m_nextWindowId++;

            auto* ctx = interstice::vulkan::VulkanWindowContext::Create(
                id, windowBackend->GetNativeHandle(osHandle), osHandle, size,
                flags, &vk->deviceContext, [] {});
            if (ctx == nullptr) { windowBackend->DestroyWindow(osHandle); return nullptr; }

            m_windowContexts.push_back({ ctx, osHandle });
            return ctx;
        }

        // A render-to-image window context: no OS window and no swapchain; ReadPixels
        // returns its frames. For tests that need pixels, not a presented window.
        interstice::vulkan::VulkanWindowContext* CreateImageWindowContext(interstice::int2 size = { 320, 240 })
        {
            interstice::ID id{};
            id.value.dynamicParts.mode      = interstice::IDMode::Static;
            id.value.dynamicParts.hashValue = m_nextWindowId++;

            auto* ctx = interstice::vulkan::VulkanWindowContext::Create(
                id, nullptr, nullptr, size, interstice::WindowFlags::RenderToImage, &vk->deviceContext, [] {});
            if (ctx == nullptr) return nullptr;
            m_windowContexts.push_back({ ctx, nullptr });
            return ctx;
        }

        // Drive one full window frame that emits a single dot, running the real
        // upload + draw + present path. This allocates the window's per-target
        // GPU buffers, so window-destroy buffer reclamation can be observed.
        void RenderOneFrameWithDot(interstice::vulkan::VulkanWindowContext* ctx, interstice::int2 size)
        {
            graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
            interstice::GUIRenderer* r = graphicsContext->guiRenderer;
            r->Reset();
            if (r->BeginWindow(ctx->GetID()))
            {
                r->RenderDot(interstice::float2{ 8.0f, 8.0f },
                             interstice::IntRect{ 0, 0, size.x, size.y },
                             4.0f, interstice::color4f{ 1, 1, 1, 1 });
                r->EndWindow();
            }
            ctx->RenderWindowFrame(graphicsContext, interstice::IntRect{ 0, 0, size.x, size.y }, false);
        }

        void TearDown() override
        {
            // Destroy any window contexts the test created, before the device.
            for (auto& wc : m_windowContexts)
            {
                if (wc.context) { wc.context->Destroy(); delete wc.context; }
                if (wc.osHandle && windowBackend) windowBackend->DestroyWindow(wc.osHandle);
            }
            m_windowContexts.clear();

            // Gradient GPU resources must go before the device, or the atlas
            // images leak past allocator destroy.
            if (graphicsContext != nullptr)
                gradientManager.ReleaseAll(graphicsContext);

            // Leaks are reported at vkDestroyDevice (inside Cleanup); clear first
            // to isolate them from test-body noise.
            if (m_checkLeaksAtTeardown) sink.Clear();

            if (graphicsContext != nullptr)
                graphicsContext->Cleanup();

            if (m_checkLeaksAtTeardown)
                EXPECT_FALSE(sink.HasErrors())
                    << "Vulkan objects leaked (reported at device destroy):\n" << sink.Dump();

            Teardown();
        }

    private:
        void Teardown()
        {
            delete windowBackend;   windowBackend   = nullptr;
            delete graphicsContext; graphicsContext = nullptr;
            vk = nullptr;
        }
    };
}
