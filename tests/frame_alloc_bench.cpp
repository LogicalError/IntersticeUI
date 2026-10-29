// Heap allocations per frame: builds a UI of one control kind a few hundred times headless
// and prints the allocations each control costs per frame, against static text (which
// registers no event handler). Disabled by default; run with
//   gui_core_tests --gtest_also_run_disabled_tests --gtest_filter=*FrameAlloc*

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace
{
    // Counted only while a frame is measured, on the measuring thread.
    thread_local bool     g_counting    = false;
    thread_local uint64_t g_allocations = 0;
    thread_local uint64_t g_bytes       = 0;
    thread_local size_t   g_sizes[1024] {};   // allocations by size, up to 1023 bytes
}

// Counts every plain operator new of this test executable; the aligned forms aren't
// replaced and aren't counted.
void* operator new(std::size_t size)
{
    if (g_counting) { ++g_allocations; g_bytes += size; if (size < 1024) ++g_sizes[size]; }
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    struct FrameAllocations { double perFrame; double bytesPerFrame; std::map<size_t, double> bySize; };

    FrameAllocations Measure(HeadlessGUIApp& app, const std::function<void()>& controls)
    {
        auto draw = [&] {
            app.BeginRootWindow({ 1920, 1080 });
            controls();
            app.EndRootWindow();
        };
        for (int i = 0; i < 10; i++) app.Frame(draw);   // warm up: first-frame registrations

        constexpr int kFrames = 50;
        g_allocations = 0;
        g_bytes       = 0;
        std::fill(std::begin(g_sizes), std::end(g_sizes), size_t(0));
        g_counting    = true;
        for (int i = 0; i < kFrames; i++) app.Frame(draw);
        g_counting    = false;
        FrameAllocations a{ double(g_allocations) / kFrames, double(g_bytes) / kFrames, {} };
        for (size_t s = 0; s < 1024; ++s)
            if (g_sizes[s] != 0)
                a.bySize[s] = double(g_sizes[s]) / kFrames;
        return a;
    }

    TEST(FrameAllocBench, DISABLED_AllocationsPerControl)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        constexpr int kCount = 300;
        std::vector<std::string> fields(kCount, "text");
        std::vector<bool> flags(kCount, false);
        int value = 50;

        struct Kind { const char* name; std::function<void()> controls; };
        const Kind kinds[] = {
            { "nothing", [] {} },
            { "Text",    [&] { for (int i = 0; i < kCount; i++) app.Text("A line of static text"); } },
            { "Label",   [&] { for (int i = 0; i < kCount; i++) app.Label("Name"); } },
            { "Button",  [&] { for (int i = 0; i < kCount; i++) app.Button("Apply", [] {}); } },
            { "Toggle",  [&] { for (int i = 0; i < kCount; i++) { bool on = flags[size_t(i)]; app.Toggle(on, "On", [](bool) {}); } } },
            { "Field",   [&] { for (int i = 0; i < kCount; i++) app.Field(fields[size_t(i)]); } },
            { "Slider",  [&] { for (int i = 0; i < kCount; i++) app.Slider(value, 0, 100); } },
        };

        FrameAllocations base{};
        for (const Kind& kind : kinds)
        {
            const FrameAllocations a = Measure(app, kind.controls);
            if (std::string(kind.name) == "nothing")
                base = a;
            std::printf("FRAMEALLOC %-8s %8.1f allocations/frame (%6.2f per control), %9.0f bytes/frame; per control by size:",
                        kind.name, a.perFrame, (a.perFrame - base.perFrame) / kCount, a.bytesPerFrame);
            for (const auto& [size, count] : a.bySize)
            {
                const auto b = base.bySize.find(size);
                const double extra = count - (b != base.bySize.end() ? b->second : 0.0);
                if (extra / kCount >= 0.5)
                    std::printf(" %zu B x%.1f", size, extra / kCount);
            }
            std::printf("\n");
        }
        app.Cleanup();
    }
}
