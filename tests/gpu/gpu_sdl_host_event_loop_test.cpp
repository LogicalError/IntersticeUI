// SDLWindowBackend inside a host that uses SDL itself: the host's event filter
// and event queue are left alone.
#include <gtest/gtest.h>

#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <SDL3/SDL.h>

namespace
{
    bool SDLCALL HostFilter(void*, SDL_Event*) { return true; }

    class SDLHostEventLoop : public ::testing::Test
    {
    protected:
        Uint32 hostEvent = 0;

        void SetUp() override
        {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                GTEST_SKIP() << "no video: " << SDL_GetError();
            hostEvent = SDL_RegisterEvents(1);
            SDL_SetEventFilter(&HostFilter, this);
        }

        void TearDown() override
        {
            if (!SDL_WasInit(SDL_INIT_VIDEO))
                return;
            SDL_SetEventFilter(nullptr, nullptr);
            SDL_FlushEvent(hostEvent);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }

        void PushHostEvent() const
        {
            SDL_Event event{};
            event.type = hostEvent;
            ASSERT_TRUE(SDL_PushEvent(&event));
        }

        int QueuedHostEvents() const
        {
            SDL_Event event{};
            return SDL_PeepEvents(&event, 1, SDL_PEEKEVENT, hostEvent, hostEvent);
        }
    };

    TEST_F(SDLHostEventLoop, KeepsTheHostsEventFilter)
    {
        {
            interstice::SDLWindowBackend backend;
            SDL_EventFilter filter = nullptr;
            void* userdata = nullptr;
            ASSERT_TRUE(SDL_GetEventFilter(&filter, &userdata));
            EXPECT_EQ(filter, &HostFilter);
            EXPECT_EQ(userdata, this);
        }
        SDL_EventFilter filter = nullptr;
        void* userdata = nullptr;
        ASSERT_TRUE(SDL_GetEventFilter(&filter, &userdata));
        EXPECT_EQ(filter, &HostFilter) << "still there after the backend is gone";
    }

    TEST_F(SDLHostEventLoop, LeavesTheQueueToAHostThatPumpsEvents)
    {
        interstice::SDLWindowBackend::Options options;
        options.pumpEvents = false;
        interstice::SDLWindowBackend backend(options);

        PushHostEvent();
        backend.PollEvents(nullptr);
        EXPECT_EQ(QueuedHostEvents(), 1) << "the host's event is still queued";
    }

    TEST_F(SDLHostEventLoop, EmptiesTheQueueWhenItRunsTheEventLoop)
    {
        interstice::SDLWindowBackend backend;

        PushHostEvent();
        backend.PollEvents(nullptr);
        EXPECT_EQ(QueuedHostEvents(), 0);
    }
}
