#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include <assert.h>

#include <gui_core/diagnostics/gui_logging.h>

namespace tachyon
{
    namespace
    {
        constexpr Diagnostics::LogCategory kLogWindows = Diagnostics::MakeLogCategory("windows");
    }

    void OSWindowManager::DestroyWindowContexts()
    {
        for (Window& window : windows)
        {
            if (window.context == nullptr || graphicsContext == nullptr)
                continue;
            windowBackendManager->AttachContextToWindow(window.osHandle, nullptr);
            graphicsContext->DestroyWindowContext(window.context);
            window.context = nullptr;
        }
    }

    bool OSWindowManager::RecreateWindowContexts()
    {
        bool all = true;
        for (Window& window : windows)
        {
            if (window.context != nullptr || window.osHandle == nullptr || graphicsContext == nullptr
                || window.state != WindowState::Alive)
                continue;
            const IntRect pixelRect = windowBackendManager->GetWindowPixelRect(window.osHandle);
            window.context = graphicsContext->CreateWindowContext(windowBackendManager->GetNativeHandle(window.osHandle),
                                                                  window.osHandle, window.id, pixelRect.size,
                                                                  window.flags, window.onGui);
            if (window.context == nullptr)
            {
                all = false;
                continue;
            }
            windowBackendManager->AttachContextToWindow(window.osHandle, window.context);
        }
        return all;
    }

    OSWindowManager::OSWindowManager(GraphicsContext* graphicsContext, IWindowBackend* backendManager)
        : graphicsContext(graphicsContext)
        , windowBackendManager(backendManager)
    {
        assert(windowBackendManager);
        windows.reserve(64);
    }

    OSWindowManager::OSWindowManager(GraphicsContext* graphicsContext, IWindowBackend* backendManager,
                                     IPlatformInputHandler* inputHandler, StyleManager& styleManager, GradientManager& gradientManager)
        : OSWindowManager(graphicsContext, backendManager)
    {
        this->inputHandler = inputHandler;
        this->styleManager = &styleManager;
        this->gradientManager = &gradientManager;
    }


    bool OSWindowManager::BeginFrame() 
    {
        if (insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: BeginFrame called while the previous frame has not ended (no EndFrame)");
            return false;
        }

        insideBeginEnd = true;

        // Refresh rects from the OS so layout isn't a frame behind during live resize.
        for (auto& window : windows)
        {
            if (window.state == WindowState::Closing || window.state == WindowState::Hidden)
                continue;
            window.lastRect = window.rect;
            window.rect = windowBackendManager->GetWindowRect(window.GetWindowHandle());
            window.monitorIndex = windowBackendManager->GetMonitorFromWindow(window.GetWindowHandle());
            window.displayScale = QueryDisplayScale(window.GetWindowHandle());
        }

        // Popup menus close when another (non-child) window becomes active; checked
        // when the focus moved, so a popup that hasn't taken focus yet stays open.
        if (const uint32_t focusChanges = windowBackendManager->GetFocusChangeCount();
            focusChanges != m_focusChangeCount)
        {
            m_focusChangeCount = focusChanges;
            ID activeWindow = GetActiveWindow();
            int32_t activeIndex = activeWindow == ID::Invalid() ? - 1 : IndexOf(activeWindow);
            for (int32_t window_index = 0; window_index < static_cast<int32_t>(windows.size()); ++window_index)
            {
                auto &w = windows[static_cast<size_t>(window_index)];
                if (w.state != WindowState::Alive) continue;
                if ((w.flags & WindowFlags::PopupMenu) == WindowFlags::None) continue;

                // Not-focusable popups (e.g. tooltips) intentionally never take
                // focus, so the active-window check would always close them.
                if ((w.flags & WindowFlags::NotFocusable) != WindowFlags::None) continue;

                // If the active window is the popup itself, do nothing.
                if (activeWindow == w.id) continue;

                // If the active window is a child (descendant) of the popup,
                // keep the popup open.
                bool is_descendant = false;
                int32_t cur = activeIndex;
                while (cur != -1)
                {
                    if (windows[static_cast<size_t>(cur)].id == w.id)
                    {
                        is_descendant = true;
                        break;
                    }
                    ID parent = windows[static_cast<size_t>(cur)].parentID;
                    cur = IndexOf(parent);
                }

                if (!is_descendant)
                {
                    // Hide rather than destroy (as for popups the app stops declaring):
                    // reopening reuses the window and its swapchain.
                    w.state = WindowState::Hidden;
                    windowBackendManager->SetWindowVisible(w.GetWindowHandle(), false);
                }
            }
        }

        // Detect OS-close (user manually closed the window)
        for (int32_t window_index = 0; window_index < static_cast<int32_t>(windows.size()); window_index++)
        {
            if (IsNotAlive(window_index))
                continue;

            auto& window = windows[static_cast<size_t>(window_index)];
            if (windowBackendManager->IsWindow(window.GetWindowHandle()))
                continue;

            // Track that this window was closed by the user
            userClosedWindows.insert(window.id);

            // Also closes children, per their policy
            [[maybe_unused]]
            auto result = RequestClose(window_index);
            assert(result == RequestCloseResult::Success);
        }
        return true;
    }

    bool OSWindowManager::EndFrame() 
    {
        if (!insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: EndFrame called without BeginFrame first");
            return false;
        }

        // Refresh our monitor list because we might have monitors added/removed
        // or resolution changed.
        RefreshMonitors();

        // New popups show after this frame's present once they've been drawn enough.
        for (auto& window : windows)
        {
            if (window.framesHidden == 0)
                continue;
            const bool show = window.state != WindowState::Alive || --window.framesHidden == 0;
            if (!show)
                continue;
            window.framesHidden = 0;
            if (window.GetWindowHandle() == nullptr)
                continue;
            if (std::exchange(window.cloaked, false))
                windowBackendManager->SetWindowCloaked(window.GetWindowHandle(), false);
            if (window.state == WindowState::Alive)   // a closed one shows when it's reopened
            {
                windowBackendManager->SetWindowVisible(window.GetWindowHandle(), true);
                RaisePopup(window);
            }
        }

        // Remove windows after all processing is done
        for (int32_t window_index = 0; window_index < static_cast<int32_t>(windows.size()); )
        {
            auto& window = windows[static_cast<size_t>(window_index)];
            if (window.state != WindowState::Closing)
            {
                window_index++;
                continue;
            }

            // Destroy the context here, while its native window still exists.
            // We cannot rely on the 'Update' cleanup callback because:
            //  (a) SDL_SetWindowParent means destroying a parent SDL window
            //      invalidates child windows - Update sees a null context
            //      for the child and skips it.
            //  (b) If this is the last window, bQuit is set and Update
            //      never runs again.
            //  (c) DestroyWindow only defers SDL destruction; the callback
            //      only fires on the next Update call.
            {
                OSWindowHandle handle = window.GetWindowHandle();
                bool isWindowAlive = handle != nullptr && windowBackendManager->IsWindow(handle);

                if (window.context != nullptr && graphicsContext != nullptr)
                {
                    if (isWindowAlive)
                        windowBackendManager->DetachWindowContext(handle);
                    graphicsContext->DestroyWindowContext(window.context);
                }

                if (isWindowAlive)
                    windowBackendManager->DestroyWindow(handle);
            }

            // Safe to remove window now
            RemoveWindow(window_index);

            // No increment: the last window was moved into this slot.
        }

        insideBeginEnd = false;
        return true;
    }

    void OSWindowManager::DestroyAllWindowContexts()
    {
        if (graphicsContext == nullptr) return;
        for (auto& window : windows)
        {
            if (window.context != nullptr)
            {
                graphicsContext->DestroyWindowContext(window.context);
                window.context  = nullptr;
                window.osHandle = nullptr;
            }
        }
    }

    bool OSWindowManager::Update()
    {
        if (insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: Update called inside BeginFrame/EndFrame");
            return false;
        }
        windowBackendManager->PollEvents([this](tachyon::OSWindowHandle handle, IWindowContext* context)
            {
                // Destroy the window context before its window is destroyed
                if (context != nullptr)
                {
                    if (graphicsContext != nullptr)
                    {
                        graphicsContext->DestroyWindowContext(context);
                    }
                    // Null out the Window struct's context so EndFrame won't destroy it again.
                    int32_t idx = IndexOf(handle);
                    if (idx >= 0)
                    {
                        // The handle is about to be nulled, so drop its map entry now;
                        // RemoveWindow would otherwise erase the wrong key and leave it stale.
                        HandleToWindowIndex.erase(handle);
                        windows[idx].context  = nullptr;
                        windows[idx].osHandle = nullptr;
                    }
                    context = nullptr;
                }
            });
        return true;
    }
       
    void OSWindowManager::ReleaseUndeclaredWindows(const std::function<bool(ID)>& declared)
    {
        if (!insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: ReleaseUndeclaredWindows called outside BeginFrame/EndFrame");
            return;
        }

        // Windows not declared this frame lose their user-closed flag, so the app can reopen them later.
        std::erase_if(userClosedWindows, [&](ID id) { return !declared(id); });

        for (auto& window : windows)
        {
            // Hidden windows come back through GetOrCreateWindow, after layout has their rect.
            if (window.state != WindowState::Alive || declared(window.id))
                continue;

            // Popups are hidden rather than destroyed, which would tear down their
            // swapchain on every open/close cycle.
            if ((window.flags & WindowFlags::PopupMenu) != WindowFlags::None)
            {
                window.state = WindowState::Hidden;
                windowBackendManager->SetWindowVisible(window.GetWindowHandle(), false);
                continue;
            }

            // A programmatic close, not the user's, so the window may be reopened.
            RequestCloseAllowReopen(window.id);
        }
    }

    IntRect OSWindowManager::GetMonitorArea(int monitorIndex)
    {
        if (monitors.size() == 0) 
        {
            RefreshMonitors();
            if (monitors.size() == 0) return IntRect{};
        }
        if (monitorIndex < 0 ||
            monitorIndex >= static_cast<int>(monitors.size()))
        {
            monitorIndex = primaryMonitorIndex;
        }
        return monitors[monitorIndex].workArea;
    }

    IntRect OSWindowManager::CenterInMonitor(int monitorIndex, int width, int height)
    {
        const IntRect workArea = GetMonitorArea(monitorIndex);
        int x = workArea.x + (workArea.w - width) / 2;
        int y = workArea.y + (workArea.h - height) / 2;
        return { x, y, width, height };
    }

    AddWindowResult
        OSWindowManager::GetOrCreateWindow(ID id, std::string_view title, IntRect rect,
                                           tachyon::OnGuiFunc onGui,
                                           WindowFlags windowFlags,
                                           ID parentID, int monitorIndex)
    {
        if (!insideBeginEnd) 
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: a window was declared outside BeginFrame/EndFrame");
            return AddWindowResult::CalledOutsideOfFrame;
        }
        if (id == ID::Invalid()) return AddWindowResult::InvalidID;
        
        // Don't recreate windows that were manually closed by the user
        if (userClosedWindows.find(id) != userClosedWindows.end())
        {
            return AddWindowResult::WindowUserClosed;
        }
        
        if (auto* existing_window = FindWindow(id))
        {
            // Revive hidden windows (popup/menu windows kept alive for fast reopen)
            if (existing_window->state == WindowState::Hidden)
            {
                existing_window->state = WindowState::Alive;
                // Reposition/resize before showing - the menu may have moved
                if (existing_window->GetWindowHandle() != nullptr)
                {
                    windowBackendManager->SetWindowRect(existing_window->GetWindowHandle(), rect);
                    existing_window->lastRect = rect;
                    existing_window->rect = rect;
                }
                windowBackendManager->SetWindowVisible(existing_window->GetWindowHandle(), true);
                RaisePopup(*existing_window);
            }

            // this is safe since the windows list cannot be reallocated while we have
            // a pointer to an existing window. (this assumes no multi-threaded access)
            const ptrdiff_t existingIndex = existing_window - &windows[0];
            if (existingIndex < 0 || existingIndex >= static_cast<int32_t>(windows.size()))
                return AddWindowResult::IndexOutOfBounds;

            // A window already closing this frame is left to finish closing.
            if (existing_window->state == WindowState::Closing)
                return AddWindowResult::SuccessRecycle;

            if (existing_window->parentID != parentID)
            {
                RemoveWindowFromSiblingList(int32_t(existingIndex));
                LinkToParent(int32_t(existingIndex), parentID);
            }
            existing_window->flags = windowFlags;
            existing_window->monitorIndex = monitorIndex;

            // For code-driven windows or popup/menu windows (which are code-positioned
            // via anchoring despite being UserControlled), apply the rect from layout.
            bool isCodePositioned = (windowFlags & WindowFlags::UserControlled) == WindowFlags::None
                                 || (windowFlags & WindowFlags::PopupMenu) != WindowFlags::None;
            if (isCodePositioned && existing_window->rect != rect)
            {
                if (existing_window->GetWindowHandle() != nullptr)
                {
                    windowBackendManager->SetWindowRect(existing_window->GetWindowHandle(), rect);
                }
                existing_window->rect = rect;
                existing_window->lastRect = rect;
            }

            return AddWindowResult::SuccessRecycle;
        }

        Window* parentWindow = FindWindow(parentID);

        // A new popup shows once it has been drawn a few times: shown at once, it is an empty
        // frame until its first presents reach the screen, and its first layout lacks what it
        // learns from being open (a menu's icon column). Meanwhile it is shown cloaked where
        // the backend can, since what's presented to a hidden window doesn't show when it
        // appears. Reopening reuses the window, drawn.
        const bool newPopup = (windowFlags & WindowFlags::PopupMenu) != WindowFlags::None;
        constexpr int32_t kNewPopupFramesHidden = 4;

        tachyon::OSWindowHandle osHandle = windowBackendManager->CreateWindow(title, rect,
                newPopup ? windowFlags | WindowFlags::Hidden : windowFlags,
                parentWindow != nullptr ? parentWindow->GetWindowHandle() : nullptr);
        if (osHandle == nullptr)
            return AddWindowResult::FailedToCreateOSWindow;
        const bool cloaked = newPopup && windowBackendManager->SetWindowCloaked(osHandle, true);
        if (cloaked)
            windowBackendManager->SetWindowVisible(osHandle, true);

        IWindowContext* windowContext = nullptr;
        if (graphicsContext != nullptr)
        {
            void* nativeWindowHandle = windowBackendManager->GetNativeHandle(osHandle);
            // The render target takes the backend's pixel size of the window.
            const IntRect pixelRect = windowBackendManager->GetWindowPixelRect(osHandle);
            windowContext =
                graphicsContext->CreateWindowContext(nativeWindowHandle, osHandle,
                                                     id, pixelRect.size, windowFlags, onGui);
            if (windowContext == nullptr)
            {
                windowBackendManager->DestroyWindow(osHandle);
                return AddWindowResult::FailedToCreateOSWindow;
            }
            windowBackendManager->AttachContextToWindow(osHandle, windowContext);
            windowBackendManager->AttachInputHandlerToWindow(osHandle, inputHandler);
        }

        int32_t idx = static_cast<int32_t>(windows.size());
        IDToWindowIndex[id] = idx;
        HandleToWindowIndex[osHandle] = idx;

        Window window =
        {
            .id           = id,
            .flags        = windowFlags,
            .parentID     = parentID,
            .rect         = rect,
            .monitorIndex = monitorIndex,
            .displayScale = QueryDisplayScale(osHandle),
            .sequence     = m_nextSequence++,
            .framesHidden = newPopup ? kNewPopupFramesHidden : 0,
            .cloaked      = cloaked,
            .osHandle     = osHandle,
            .context      = windowContext,
            .onGui        = std::move(onGui)
        };

        window.parentID = ID::Invalid();
        windows.push_back(std::move(window));
        LinkToParent(idx, parentID);
        if (!newPopup)
            RaisePopup(windows.back());   // a new popup is raised when it shows
        return AddWindowResult::SuccessCreate;
    }

    bool OSWindowManager::SetWindowRect(ID id, const IntRect &clientRect)
    {
        if (!insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: SetWindowRect called outside BeginFrame/EndFrame");
            return false;
        }

        int32_t index = IndexOf(id);
        if (index == -1) return false;

        Window &window = windows[static_cast<size_t>(index)];

        // If we have an OS handle, ask the backend to apply the client-area rect.
        if (window.GetWindowHandle() != nullptr)
        {
            bool ok = windowBackendManager->SetWindowRect(window.GetWindowHandle(), clientRect);
            if (!ok) return false;
        }

        // Set rect and lastRect so layout sees the applied rect as stable and doesn't override it.
        window.rect = clientRect;
        window.lastRect = clientRect;

        // Update monitor index according to backend (if available)
        if (window.GetWindowHandle() != nullptr)
        {
            window.monitorIndex = windowBackendManager->GetMonitorFromWindow(window.GetWindowHandle());
        }

        return true;
    }

    RequestCloseResult OSWindowManager::RequestClose(int32_t index)
    {
        if (!insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: RequestClose called outside BeginFrame/EndFrame");
            return RequestCloseResult::CalledOutsideFrame;
        }
        if (index < 0 || index >= static_cast<int32_t>(windows.size()))
            return RequestCloseResult::IndexOutOfBounds;

        Window& window = windows[index];
        if (window.state != WindowState::Alive)
            return RequestCloseResult::WindowAlreadyClosed;

        std::vector<ID> detach_children;
        detach_children.reserve(windows.size());
        auto result = RequestClose(index, detach_children);
        if (result != RequestCloseResult::Success)
            return result;

        for (ID child_id : detach_children)
        {
            int32_t child_index = IndexOf(child_id);
            if (child_index == -1) 
            { 
                assert(false && "child id returned invalid index");
                return RequestCloseResult::UnknownError;
            }
            RemoveWindowFromSiblingList(child_index);
        }

        RemoveWindowFromSiblingList(index);
        return RequestCloseResult::Success;
    }

    RequestCloseResult OSWindowManager::RequestClose(int32_t index, std::vector<ID>& detach_children)
    {
        if (!insideBeginEnd)
        {
            Diagnostics::ReportMisuse(kLogWindows, "OSWindowManager: RequestClose called outside BeginFrame/EndFrame");
            return RequestCloseResult::CalledOutsideFrame;
        }
        if (index < 0 || index >= static_cast<int32_t>(windows.size()))
            return RequestCloseResult::IndexOutOfBounds;

        Window& window = windows[index];
        if (window.state != WindowState::Alive && window.state != WindowState::Hidden)
            return RequestCloseResult::WindowAlreadyClosed;

        // Show hidden windows before closing so the OS window is visible for cleanup
        if (window.state == WindowState::Hidden)
            windowBackendManager->SetWindowVisible(window.GetWindowHandle(), true);

        window.state = WindowState::Closing;

        // Apply child policy using child count to prevent infinite loop
        int32_t children_processed = 0;
        for (ID child_id = window.firstChildID; child_id != ID::Invalid(); )
        {
            // Prevent infinite loop in case of corrupted sibling list
            if (children_processed >= window.childCount)
            {
                assert(false && "Corrupted child sibling list detected - more children than childCount");
                return RequestCloseResult::UnknownError;
            }

            int32_t child_index = IndexOf(child_id);
            if (child_index == -1)
                break;
            Window& child_window = windows[child_index];
            if ((child_window.flags & WindowFlags::CloseWithParent) != WindowFlags::None)
            {
                auto result = RequestClose(child_index, detach_children);
                if (result != RequestCloseResult::WindowAlreadyClosed &&
                    result != RequestCloseResult::Success)
                    return result;
            }
            else
                detach_children.push_back(child_id);
            child_id = child_window.nextSiblingID;
            children_processed++;
        }
        return RequestCloseResult::Success;
    }

    // A popup takes focus when it opens, so a focus change elsewhere closes it.
    void OSWindowManager::RaisePopup(const Window& window)
    {
        const bool focusablePopup = (window.flags & WindowFlags::PopupMenu) != WindowFlags::None
                                 && (window.flags & (WindowFlags::NotFocusable | WindowFlags::Hidden)) == WindowFlags::None;
        if (focusablePopup && window.GetWindowHandle() != nullptr)
            windowBackendManager->BringToFront(window.GetWindowHandle());
    }

    const OSWindowManager::Window* OSWindowManager::MainWindow() const
    {
        const Window* main = nullptr;
        for (const Window& window : windows)
        {
            if (window.state == WindowState::Closing || window.parentID != ID::Invalid())
                continue;
            if (main == nullptr || window.sequence < main->sequence)
                main = &window;
        }
        return main;
    }

    float OSWindowManager::MainRefreshRate() const
    {
        const std::vector<MonitorInfo> current = monitors.empty() ? windowBackendManager->GetMonitors() : monitors;
        const Window* main = MainWindow();
        if (main != nullptr && main->monitorIndex >= 0 && main->monitorIndex < static_cast<int32_t>(current.size()))
            return current[static_cast<size_t>(main->monitorIndex)].refreshRate;
        for (const MonitorInfo& monitor : current)
            if (monitor.isPrimary)
                return monitor.refreshRate;
        return 0.0f;
    }

    float OSWindowManager::MainDisplayScale() const
    {
        if (const Window* main = MainWindow())
            return main->displayScale;
        const std::vector<MonitorInfo> current = monitors.empty() ? windowBackendManager->GetMonitors() : monitors;
        for (const MonitorInfo& monitor : current)
            if (monitor.isPrimary && monitor.contentScale > 0.0f)
                return monitor.contentScale;
        return 1.0f;
    }

    float OSWindowManager::QueryDisplayScale(OSWindowHandle handle) const
    {
        const float scale = windowBackendManager->GetWindowDisplayScale(handle);
        return scale > 0.0f ? scale : 1.0f;
    }

    void OSWindowManager::LinkToParent(int32_t window_index, ID parentID)
    {
        const int32_t parent_index = IndexOf(parentID);
        if (parent_index == -1)
            return;
        Window& window = windows[static_cast<size_t>(window_index)];
        Window& parent = windows[static_cast<size_t>(parent_index)];
        window.parentID      = parentID;
        window.nextSiblingID = parent.firstChildID;
        parent.firstChildID  = window.id;
        parent.childCount++;
    }

    void OSWindowManager::RemoveWindowFromSiblingList(int32_t window_index)
    {
        auto& self_window = windows[window_index];
        ID parent_id = self_window.parentID;
        if (parent_id == ID::Invalid())
            return;

        int32_t parent_index = IndexOf(parent_id);
        if (parent_index == -1) 
        { 
            assert(false && "parent id returned invalid index"); 
            return; 
        }

        ID window_id = self_window.id;
        ID old_siblingID = self_window.nextSiblingID;
        self_window.parentID = ID::Invalid();
        self_window.nextSiblingID = ID::Invalid();
        auto& parentWindow = windows[parent_index];

        // Decrement parent's child count
        if (parentWindow.childCount > 0)
            parentWindow.childCount--;

        // Check if it's the first child
        if (parentWindow.firstChildID == window_id)
        {
            // Bypass the removed window
            parentWindow.firstChildID = old_siblingID;
        } else
        {
            // Find window in the sibling list to fix the link, using childCount to prevent infinite loop
            int32_t siblings_checked = 0;
            ID child_id = parentWindow.firstChildID;
            while (child_id != ID::Invalid())
            {
                // Prevent infinite loop in case of corrupted sibling list
                if (siblings_checked >= parentWindow.childCount)
                {
                    assert(false && "Corrupted sibling list detected - more siblings than childCount");
                    break;
                }

                int32_t child_index = IndexOf(child_id);
                if (child_index == -1)
                {
                    assert(false && "child id returned invalid index");
                    return;
                }

                ID next_sibling = windows[child_index].nextSiblingID;
                if (next_sibling == window_id)
                {
                    // Bypass the removed window
                    windows[child_index].nextSiblingID = old_siblingID;
                    break;
                }
                child_id = next_sibling;
                siblings_checked++;
            }
        }
    }

    void OSWindowManager::ClearParentIDInChildren(int32_t window_index)
    {
        auto& window = windows[window_index];

        // Use childCount to prevent infinite loop
        int32_t children_processed = 0;
        ID child_id = window.firstChildID;
        while (child_id != ID::Invalid())
        {
            // Prevent infinite loop in case of corrupted sibling list
            if (children_processed >= window.childCount)
            {
                assert(false && "Corrupted child sibling list detected - more children than childCount");
                break;
            }

            int32_t child_index = IndexOf(child_id);
            if (child_index == -1) 
            { 
                assert(false && "child id returned invalid index"); 
                return; 
            }
            windows[child_index].parentID = ID::Invalid();
            child_id = windows[child_index].nextSiblingID;
            children_processed++;
        }
    }

    void OSWindowManager::RemoveWindow(int32_t window_index)
    {
        if (window_index < 0 || window_index >= static_cast<int32_t>(windows.size()))
            return;

        {
            ID idToRemove = windows[window_index].id;
            OSWindowHandle handleToRemove = windows[window_index].GetWindowHandle();
                
            // Cleanup Hierarchy
            ClearParentIDInChildren(window_index);
            RemoveWindowFromSiblingList(window_index);
                
            // Erase from maps
            IDToWindowIndex.erase(idToRemove);
            HandleToWindowIndex.erase(handleToRemove);
        }

        // If we're not the last window, move the last window into our slot
        int32_t last_index = static_cast<int32_t>(windows.size()) - 1;
        if (window_index != last_index)
        {
            // Move last window into our slot
            windows[window_index] = std::move(windows[last_index]);
                
            // Update maps to point to the new location of the 'last' window
            auto& last_window = windows[window_index]; // new location of last window
            IDToWindowIndex[last_window.id] = window_index;
            if (last_window.GetWindowHandle() != nullptr)
                HandleToWindowIndex[last_window.GetWindowHandle()] = window_index;
        }

        // Remove last window
        windows.pop_back();
    }
    
    ID OSWindowManager::GetTopModalWindow() const
    {
        ID topModalID = ID::Invalid();
        
        // Iterate in creation order and remember the last alive modal window.
        for (const auto &window : windows)
        {
            if (window.state != WindowState::Alive)
            {
                continue;
            }
            
            if ((window.flags & WindowFlags::Modal) != WindowFlags::None)
            {
                topModalID = window.id;
            }
        }
        
        return topModalID;
    }

} // namespace tachyon