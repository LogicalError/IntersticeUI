#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>

#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/windows/gui_window_flags.h>

#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace interstice
{
    // Forward declarations
    class GraphicsContext;
    class IWindowBackend;
    class ImageSet;
    class StyleManager;
    class GraphicsContext;
    class GradientManager;

    enum class AddWindowResult : int8_t
    {
        SuccessCreate          =  0, // New window created
        SuccessRecycle         =  1, // Existing window returned
        CalledOutsideOfFrame   = -1, // Called outside of BeginFrame/EndFrame
        IndexOutOfBounds       = -2, // Internal error: window index out of bounds
        FailedToCreateOSWindow = -3, // OS window creation failed
        InvalidID              = -4, // Provided ID was invalid
        WindowUserClosed       = -5  // Window was manually closed by user and cannot be automatically reopened
    };

    enum class RequestCloseResult : int8_t
    {
        Success             =  0, // Window close requested 
        CalledOutsideFrame  = -1, // Called outside of BeginFrame/EndFrame
        IndexOutOfBounds    = -2, // Internal error: window index out of bounds
        WindowAlreadyClosed = -3, // Window was already closed
        UnknownError        = -4  // An Unknown error has occurred
    };

    enum class WindowState : uint8_t
    {
        NotRegistered,  // window has not been created yet (initial state after GetOrCreateWindow)
		Alive,          // normal
		Closing,        // close requested (this frame) or already closed
		Hidden          // OS window hidden but context alive (for fast reopen of popups/menus)
    };

    class OSWindowManager 
    {
    private:
        // ---------------- Window Data ----------------

        struct Window 
        {
            ID              id = ID::Invalid();

            WindowState     state         = WindowState::Alive;
            WindowFlags     flags         = WindowFlags::None;

            ID              parentID      = ID::Invalid();
            ID              firstChildID  = ID::Invalid();
            ID              nextSiblingID = ID::Invalid();
            int32_t         childCount    = 0;

            IntRect         rect{};             // client rect, pixels
            IntRect         lastRect{};         // persisted

            int32_t         monitorIndex  = -1;
            float           displayScale  = 1.0f; // the OS UI scale where the window is
            uint32_t        sequence      = 0;    // creation order

            OSWindowHandle  osHandle = nullptr;
            IWindowContext* context  = nullptr;

            OSWindowHandle  GetWindowHandle() const { return osHandle; }
        };

        std::vector<MonitorInfo> monitors;
        uint32_t m_nextSequence = 0;
        ID      m_textInputWindow = ID::Invalid();
        uint32_t m_focusChangeCount = 0;   // the backend's, when popups were last checked
        IntRect m_textInputArea {};
        int primaryMonitorIndex = 0;

        bool insideBeginEnd = false;

    protected:
        OSWindowManager() = default;

        // Application-owned; handed to each new window context for RenderFrame, so the
        // graphics context itself has no style/gradient dependency.
        StyleManager*    styleManager    = nullptr;
        GradientManager* gradientManager = nullptr;

        IPlatformInputHandler* inputHandler = nullptr;

    public:
        virtual ~OSWindowManager() = default;
        // 2-arg form for tests without real windows: no style/gradient managers, no context init.
        explicit OSWindowManager(GraphicsContext* graphicsContext, IWindowBackend* backend);
        OSWindowManager(GraphicsContext* graphicsContext, IWindowBackend* backendWindowManager,
                        IPlatformInputHandler* inputHandler, StyleManager& styleManager, GradientManager& gradientManager);


        // ---------------- Frame Lifecycle ---------------- 

        bool BeginFrame();

        bool EndFrame();

        // ---------------- Declared windows ----------------

        // Windows the app no longer declares (declared(id) is false): popups are
        // hidden for a fast reopen, the others closed, reopenable. Undeclared windows
        // also lose their user-closed flag. Call each frame, before layout.
        void ReleaseUndeclaredWindows(const std::function<bool(ID)>& declared);

        // ---------------- Input ----------------

        // Gates synthetic mouse events: a parented popup can make the OS swallow a click on its parent.
        bool HasAlivePopupWindows() const
        {
            for (const auto& w : windows)
                if (w.state == WindowState::Alive &&
                    (w.flags & WindowFlags::PopupMenu) != WindowFlags::None)
                    return true;
            return false;
        }

        // Any alive secondary top-level window (popup, tool window, drag ghost, ...).
        // Clicking another of our windows then can make SDL swallow the click, so
        // the input layer runs synthetic-event repair.
        bool HasAliveSecondaryWindows() const
        {
            const WindowFlags kSecondary = WindowFlags::PopupMenu
                                         | WindowFlags::Utility;
            for (const auto& w : windows)
                if (w.state == WindowState::Alive &&
                    (w.flags & kSecondary) != WindowFlags::None)
                    return true;
            return false;
        }

        // Hide a popup/menu OS window by ID. The window is cached (Hidden state)
        // for fast reopen, not destroyed. Safe to call outside the normal frame
        // lifecycle (e.g. during OS modal loops).
        void HidePopupWindow(ID id)
        {
            auto* w = FindWindow(id);
            if (w == nullptr || w->state != WindowState::Alive) return;
            w->state = WindowState::Hidden;
            windowBackendManager->SetWindowVisible(w->GetWindowHandle(), false);
        }

        // Places the IME candidate window at `screenRect` (e.g. the text caret) in
        // window `id`; only tells the OS when it changes.
        void SetTextInputArea(ID id, IntRect screenRect)
        {
            auto* w = FindWindow(id);
            if (w == nullptr || w->state != WindowState::Alive) return;
            const IntRect local{ screenRect.x - w->rect.x, screenRect.y - w->rect.y, screenRect.w, screenRect.h };
            if (id == m_textInputWindow && local == m_textInputArea) return;
            m_textInputWindow = id;
            m_textInputArea   = local;
            windowBackendManager->SetTextInputArea(w->GetWindowHandle(), local);
        }

        bool Update();

        // Destroy every window's IWindowContext. Call from the application's shutdown
        // path before tearing down the graphics device and the window backend.
        void DestroyAllWindowContexts();

        std::optional<color4b> ReadScreenPixel(int2 screenPos)
        {
            return windowBackendManager->ReadScreenPixel(screenPos);
        }

        int2 GetScreenMousePos()
        {
            return windowBackendManager->GetScreenMousePos();
        }

        MouseButtons GetMouseButtons()
        {
            return windowBackendManager->GetMouseButtons();
        }

        // ---------------- Window Creation ---------------- 

        inline bool IsNotAlive(ID id) const { return IsNotAlive(IndexOf(id)); }

        // NOTE: not multi-thread safe
        AddWindowResult
            GetOrCreateWindow(ID id, std::string_view title, IntRect rect,
                              OnGuiFunc onGui,
                              WindowFlags windowFlags = WindowFlags::DefaultFlags | WindowFlags::CloseWithParent,
                              ID parentID = ID::Invalid(), int monitorIndex = -1);

        // ---------------- Monitor State ---------------- 

        void RefreshMonitors()
        {
            monitors = windowBackendManager->GetMonitors();
            for (size_t i = 0; i < monitors.size(); ++i)
            {
                if (monitors[i].isPrimary)
                {
                    primaryMonitorIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        size_t GetMonitorCount() const
        {
            return monitors.size();
        }

        IntRect GetMonitorArea(int monitorIndex);
        IntRect CenterInMonitor(int monitorIndex, int width, int height);

        const std::vector<MonitorInfo>& Monitors() const { return monitors; }

        const MonitorInfo &GetMonitor(int index) const
        {
            return (index >= 0 && index < static_cast<int>(monitors.size()))
                       ? monitors[index]
                       : GetPrimaryMonitor();
        }

        int GetMonitorIndexAt(int2 pos) const
        {
            return windowBackendManager->GetMonitorAt(pos);
        }

        int GetPrimaryMonitorIndex() const
        {
            if (primaryMonitorIndex < 0 || primaryMonitorIndex >= monitors.size())
                return -1;
            return primaryMonitorIndex;
        }

        const MonitorInfo &GetPrimaryMonitor() const
        {
            static MonitorInfo kEmptyInfo{.isPrimary = true};
            if (primaryMonitorIndex < 0 || primaryMonitorIndex >= monitors.size())
                return kEmptyInfo; // no monitor connected
            return monitors[primaryMonitorIndex];
        }        

        // Request move/resize of an existing managed window (client-area coords).
        // Returns true on success. Must be called between BeginFrame()/EndFrame().
        bool SetWindowRect(ID id, const IntRect &clientRect);
                
        // ---------------- Window State ---------------- 

        RequestCloseResult RequestClose(ID id) { return RequestClose(IndexOf(id)); }

        // Closes a window on the user's behalf (e.g. its title bar's close button): like
        // closing it through the OS, it isn't reopened while the app keeps declaring it.
        RequestCloseResult RequestUserClose(ID id)
        {
            userClosedWindows.insert(id);
            return RequestClose(IndexOf(id));
        }
        
        // Programmatically close a window and allow it to be reopened
        // (unlike user-initiated closes which prevent automatic reopening)
        RequestCloseResult RequestCloseAllowReopen(ID id)
        {
            // Remove from user-closed set if present (allow reopening)
            userClosedWindows.erase(id);
            return RequestClose(IndexOf(id));
        }
        
        // Allow a window the user closed to be reopened programmatically.
        // The flag also clears when a window isn't declared for a frame
        // (ReleaseUndeclaredWindows), so stopping and resuming the declaration reopens it.
        void AllowReopenWindow(ID id)
        {
            userClosedWindows.erase(id);
        }
        
        // Check if a window was manually closed by the user
        bool WasUserClosed(ID id) const
        {
            return userClosedWindows.find(id) != userClosedWindows.end();
        }

        WindowState GetWindowState(ID id) const
        {
            int32_t index = IndexOf(id);
            return index == -1 ? WindowState::NotRegistered : windows[index].state;
		}

        bool HasBeenRegistered(ID id) const
        {
            int32_t index = IndexOf(id);
            return index != -1;
        }

        // The OS UI scale where the window is (see
        // IWindowBackend::GetWindowDisplayScale); 1 when unknown.
        float GetWindowDisplayScale(ID id) const
        {
            int32_t index = IndexOf(id);
            return index == -1 ? 1.0f : windows[index].displayScale;
        }

        // The display scale of the main window (the first top-level window still
        // open), or of the primary monitor before there is one.
        float MainDisplayScale() const;

        // Refresh rate in Hz of the monitor showing the main window, or of the
        // primary monitor before there is one; 0 when unknown.
        float MainRefreshRate() const;

        // Client rect in pixels.
        // Null when the window doesn't exist.
        IWindowContext* GetWindowContext(ID id) const
        {
            const int32_t index = IndexOf(id);
            return index == -1 ? nullptr : windows[index].context;
        }
        OSWindowHandle GetWindowHandle(ID id) const
        {
            const int32_t index = IndexOf(id);
            return index == -1 ? nullptr : windows[index].GetWindowHandle();
        }
        // Invalid for a handle this manager doesn't own.
        ID GetWindowID(OSWindowHandle handle) const
        {
            const int32_t index = IndexOf(handle);
            return index == -1 ? ID::Invalid() : windows[index].id;
        }

        void MinimizeWindow(ID id) { if (OSWindowHandle h = GetWindowHandle(id)) windowBackendManager->MinimizeWindow(h); }
        void MaximizeWindow(ID id) { if (OSWindowHandle h = GetWindowHandle(id)) windowBackendManager->MaximizeWindow(h); }
        void RestoreWindow(ID id)  { if (OSWindowHandle h = GetWindowHandle(id)) windowBackendManager->RestoreWindow(h); }
        bool IsWindowMaximized(ID id) const
        {
            const OSWindowHandle h = GetWindowHandle(id);
            return h != nullptr && windowBackendManager->IsWindowMaximized(h);
        }

        IntRect GetWindowRect(ID id) const
        {
            int32_t index = IndexOf(id);
            return index == -1 ? IntRect{} : windows[index].rect;
        }

        int2 GetWindowSize(ID id) const
        {
            int32_t index = IndexOf(id);
            return index == -1 ? int2 {} : windows[index].rect.size;
        }

        int2 GetWindowPosition(ID id) const
        {
            int32_t index = IndexOf(id);
            return index == -1 ? int2 {} : windows[index].rect.position;
        }

        // ---------------- Focus & Z Order ---------------- 

        void BringToFront(ID id) 
        {
            int32_t index = IndexOf(id);
            if (index == -1) return;
            windowBackendManager->BringToFront(windows[index].GetWindowHandle());
        }

        ID GetWindowUnderMouse() const
        {
            OSWindowHandle handle = windowBackendManager->GetWindowUnderMouse();
            if (handle == nullptr) return ID::Invalid(); // No active window
            int32_t index = IndexOf(handle);
            if (index == -1) return ID::Invalid();
            return windows[index].id;
        }

        ID GetActiveWindow() const
        {
            OSWindowHandle handle = windowBackendManager->GetActiveWindow();
            if (handle == nullptr)
            {
                return ID::Invalid(); // No active window
            }
            int32_t index = IndexOf(handle);
            if (index == -1)
            {
                // No active window, or active window not managed by us
                return ID::Invalid();
            }
            return windows[index].id;
        }
        
        // ---------------- Queries ---------------- 
        
        const size_t WindowCount() const { return windows.size(); }
        const std::vector<Window> &AllWindows() const { return windows; }

        // Any alive non-popup, non-tooltip window. The run loop exits without one, so
        // cached popups and tooltips don't keep the application open.
        bool HasAliveMainWindow() const
        {
            const WindowFlags auxFlags = WindowFlags::PopupMenu | WindowFlags::NotFocusable;
            for (const auto& w : windows)
                if (w.state == WindowState::Alive &&
                    (w.flags & auxFlags) == WindowFlags::None)
                    return true;
            return false;
        }
        
        // Returns the ID of the top-most modal window (last created, still alive),
        // or ID::Invalid() if there are no alive modal windows.
        ID GetTopModalWindow() const;

        bool WasWindowUserClosed(ID id) const
        {
            return userClosedWindows.find(id) != userClosedWindows.end();
        }

        // Calls func for each live window's context with its render-target rect
        // (OS position, size in physical pixels).
        void ForEachAliveWindowContext(const std::function<void(IWindowContext*, IntRect)>& func)
        {
            for (const auto& w : windows)
            {
                if (w.state == WindowState::Alive && w.context != nullptr)
                {
                    func(w.context, windowBackendManager->GetWindowPixelRect(w.GetWindowHandle()));
                }
            }
        }

    protected:
        IWindowBackend*  windowBackendManager = nullptr;
        GraphicsContext* graphicsContext = nullptr;

        std::vector<Window> windows;
        std::unordered_map<ID, int32_t> IDToWindowIndex;
        std::unordered_map<OSWindowHandle, int32_t> HandleToWindowIndex;
        
        // Track windows that were manually closed by the user (not through code)
        // to prevent them from being automatically reopened
        std::unordered_set<ID> userClosedWindows;

        // ---------------- Internal ---------------- 

        Window* FindWindow(ID id)
        {
            int32_t index = IndexOf(id);
            if (index == -1) return nullptr;
            return &windows[index];
        }

        Window* FindWindow(OSWindowHandle handle)
        {
            int32_t index = IndexOf(handle);
            if (index == -1) return nullptr;
            return &windows[index];
        }

        int32_t IndexOf(ID id) const
        {
            if (id == ID::Invalid()) return -1;
            auto it = IDToWindowIndex.find(id);
            if (it == IDToWindowIndex.end()) return -1;
            return it->second;
        }

        int32_t IndexOf(OSWindowHandle handle) const
        {
            auto it = HandleToWindowIndex.find(handle);
            if (it == HandleToWindowIndex.end()) return -1;
            return it->second;
        }

        inline bool IsNotAlive(int32_t index) const
        {
            return index < 0 || index >= static_cast<int32_t>(windows.size()) ||
                    windows[index].state != WindowState::Alive;
        }

        int GetMonitorIndexForWindow(ID id) const
        {
            int index = IndexOf(id);
            return (index != -1) ? windows[index].monitorIndex : -1;
        }

        RequestCloseResult RequestClose(int32_t index);
        RequestCloseResult RequestClose(int32_t index, std::vector<ID> &detach_children);
        void RemoveWindowFromSiblingList(int32_t window_index);
        float QueryDisplayScale(OSWindowHandle handle) const;
        // First top-level window still open; null when there is none.
        void RaisePopup(const Window& window);
        const Window* MainWindow() const;
        void LinkToParent(int32_t window_index, ID parentID);
        void ClearParentIDInChildren(int32_t window_index);
        void RemoveWindow(int32_t window_index);

    };
}