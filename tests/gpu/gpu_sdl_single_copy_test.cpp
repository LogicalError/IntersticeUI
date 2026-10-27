// One SDL in the process. With two, each SDL call goes to either copy: SDL started in
// one creates no windows in the other, and events pushed to one queue are flushed from
// the other.
#include <gtest/gtest.h>

#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <SDL3/SDL.h>

#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleA(const char* moduleName);
#endif

namespace
{
    TEST(SDLSingleCopy, OnlyOneSDLLibraryIsLoaded)
    {
#ifdef _WIN32
        const bool release = GetModuleHandleA("SDL3.dll") != nullptr;
        const bool debug   = GetModuleHandleA("SDL3d.dll") != nullptr;
        EXPECT_FALSE(release && debug)
            << "SDL3.dll and SDL3d.dll are both loaded: two SDLs in one process";
#else
        GTEST_SKIP() << "checks the Windows DLL names";
#endif
    }

    // The SDL the backend starts is the one the host sees.
    TEST(SDLSingleCopy, TheHostSeesTheSDLTheBackendStarted)
    {
        if (SDL_WasInit(SDL_INIT_VIDEO))
            GTEST_SKIP() << "SDL is already running";
        tachyon::SDLWindowBackend backend;
        if (!SDL_WasInit(SDL_INIT_VIDEO))
            GTEST_FAIL() << "the backend started SDL, but not the SDL this program calls";
        SDL_Window* window = SDL_CreateWindow("single SDL", 64, 64, SDL_WINDOW_HIDDEN);
        if (window == nullptr)
            GTEST_SKIP() << "no window available in this environment: " << SDL_GetError();
        SDL_DestroyWindow(window);
    }
}
