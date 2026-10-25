#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/memory/gui_allocation_hooks.h>

#include <vector>
#include <unordered_set>
#include <unordered_map>

struct SDL_Window;
struct SDL_Cursor;
union SDL_Event;

namespace interstice
{
    class GraphicsContext;

    class SDLWindowBackend : public IWindowBackend
    {
    public:
        struct Options
        {
            // SDL's memory functions too, which are process-wide. Used only when
            // this backend starts SDL and SDL holds no memory yet.
            const AllocationHooks* hooks = nullptr;
            // PollEvents empties SDL's event queue. A host that runs SDL's event loop
            // itself turns this off; the GUI's windows still get their events while
            // the host pumps (SDL_PollEvent / SDL_PumpEvents).
            bool pumpEvents = true;
        };

        explicit SDLWindowBackend(const Options& options = {});
        ~SDLWindowBackend() override;

        // Windows are created for graphicsContext->GetAPI(); for a VulkanGraphicsContext
        // this also installs its SDL surface factory.
        void SetGraphicsContext(GraphicsContext* graphicsContext) override;

        // IWindowBackend
        OSWindowHandle CreateWindow(std::string_view title, int2 size, interstice::WindowFlags windowFlags, OSWindowHandle parentOSHandle) override;
        OSWindowHandle CreateWindow(std::string_view title, IntRect rect, interstice::WindowFlags windowFlags, OSWindowHandle parentOSHandle) override;


        void AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inputHandler) override;
		void AttachContextToWindow(OSWindowHandle window, IWindowContext* context) override;

        bool DestroyWindow(OSWindowHandle) override;
        void DetachWindowContext(OSWindowHandle) override;
        bool IsWindow(OSWindowHandle) override;

        IWindowContext* GetWindowContext(OSWindowHandle) override;

        void BringToFront(OSWindowHandle) override;
        OSWindowHandle GetWindowUnderMouse() override;
        OSWindowHandle GetActiveWindow() override;
        uint32_t GetFocusChangeCount() const override { return m_focusChanges; }

        void PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) override;
        int2 GetScreenMousePos() override;
        std::optional<color4b> ReadScreenPixel(int2 screenPos) override;
        bool TakeCloseRequest(OSWindowHandle window) override;
        MouseButtons GetMouseButtons() override;
        
        void SetCursor(MouseCursor cursor) override;
        void SetTextInputArea(OSWindowHandle window, IntRect rect) override;
        float GetWindowDisplayScale(OSWindowHandle window) override;
        IntRect GetWindowPixelRect(OSWindowHandle window) override;

        void SwapBuffers(OSWindowHandle) override;

        IntRect GetWindowRect(OSWindowHandle) override;
        int2 GetWindowPos(OSWindowHandle) override;
        int2 GetWindowSize(OSWindowHandle) override;

        bool SetWindowRect(OSWindowHandle, const IntRect& clientRect) override;
        void SetWindowVisible(OSWindowHandle, bool visible) override;
        void       SetWindowMode(OSWindowHandle, WindowMode) override;
        WindowMode GetWindowMode(OSWindowHandle) const override;

        std::vector<MonitorInfo> GetMonitors() const override;
        int GetMonitorAt(int2 pos) const override;
        int GetMonitorFromWindow(OSWindowHandle handle) const override;

        // Platform-specific operations
        uint32_t GetTicks() const override;
        std::string GetPreferredLanguage() const override;
        
        void* GetNativeHandle(OSWindowHandle) override;

        // Helpers
        static SDL_Window* ToSDL(OSWindowHandle h) { return reinterpret_cast<SDL_Window*>(h); }
        static OSWindowHandle FromSDL(SDL_Window* w) { return reinterpret_cast<OSWindowHandle>(w); }

    private:
        void UpdateMouseState();

        Options m_options;

        std::unordered_set<SDL_Window*> m_windows;
        // Track modal windows in creation order to support nested modals.
        std::vector<SDL_Window*> m_modalWindows;
        // store last polled mouse state
        int2 m_mousePos;

        // OS window coordinates per physical pixel (1 / pixel density; 1 on Windows).
        float OsPerPixel(SDL_Window* window) const;
    public:
        // A position inside `window` (OS window coordinates) in screen pixels.
        int2  WindowPointToPixels(SDL_Window* window, float x, float y) const;
    private:
        MouseButtons m_mouseButtons = MouseButtons::None;

        // track windows scheduled for destruction when received from events
        std::unordered_set<SDL_Window*> m_pendingDestroy;
        std::unordered_set<SDL_Window*> m_closeRequests;   // ConfirmClose windows the OS asked to close
        bool m_sdlInitialized = false;
        bool m_ownsSDL        = false; // we started SDL, so we shut it down
        bool m_routesSDLMemory = false; // SDL allocates through the allocation hooks
		bool m_cursorVisible = true;
        // One SDL cursor per system shape, created on first use; indexed by SDL_SystemCursor.
        static constexpr int kSystemCursorSlots = 32;
        SDL_Cursor* m_systemCursors[kSystemCursorSlots] = {};
        // Per-window SDL event watch (userdata = the window); removed when the window is destroyed.
        bool (*m_eventWatch)(void*, SDL_Event*) = nullptr;
        // Live resize, window moves, close requests and modality; sees every event, drops none.
        static bool BackendEventWatch(void* userdata, SDL_Event* event);
        uint32_t m_focusChanges = 0;
        bool m_checkFocusLeftApp = false;  // a window lost the focus: see whether the app did (exclusive fullscreen)
        bool m_suppressLiveResize = false; // suppress the watch's RenderWindowFrame during programmatic window changes
        SDL_Window* m_lastFocusableWindow = nullptr; // last window that can take focus (for rerouting tooltip input)
        
        SDL_Window* GetTopModalWindowSDL() const;
    public:
        // The modal window that blocks the event's window (one of ours), or null.
        SDL_Window* ModalBlockerFor(const SDL_Event* event) const;
    private:
    };

} // namespace interstice
