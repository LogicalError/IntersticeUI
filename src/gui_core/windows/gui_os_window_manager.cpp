#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/graphics/gui_graphics_context.h>

#include <gui_core/text/gui_text_system.h>

#include <gui_core/windows/gui_window_registry.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include <unordered_set>
#include <assert.h>

namespace interstice
{
    static WindowFlags ElementFlagsToWindowFlags(ElementFlags elementFlags)
    {
        WindowFlags windowFlags = WindowFlags::AllowHighDPI;
        const bool isPopup       = (elementFlags & ElementFlags::WindowIsPopup) != ElementFlags::None;
        const bool isModal       = (elementFlags & ElementFlags::WindowIsModal) != ElementFlags::None;
        const bool noCaption     = (elementFlags & ElementFlags::WindowNoCaption) != ElementFlags::None;
        const bool isTransparent = (elementFlags & ElementFlags::WindowIsTransparent) != ElementFlags::None;
        //const bool isCentered  = (elementFlags & ElementFlags::WindowIsCentered) != ElementFlags::None;
        const bool isMoveable    = (elementFlags & ElementFlags::WindowCanMove) != ElementFlags::None;
        const bool isResizable   = (elementFlags & ElementFlags::WindowCanResize) != ElementFlags::None;
        const bool hasShadow     = (elementFlags & ElementFlags::Shadows) != ElementFlags::None;
        const bool notFocusable  = (elementFlags & ElementFlags::WindowNotFocusable) != ElementFlags::None;
        const bool renderToImage = (elementFlags & ElementFlags::WindowRenderToImage) != ElementFlags::None;

        if (isResizable)   windowFlags |= WindowFlags::Resizable;
        if (isMoveable)    windowFlags |= WindowFlags::Moveable;
        if (isTransparent) windowFlags |= WindowFlags::Transparent;
        if (isModal)       windowFlags |= WindowFlags::Modal;
        if (hasShadow)     windowFlags |= WindowFlags::Shadow;
        if (notFocusable)  windowFlags |= WindowFlags::NotFocusable;
        if (renderToImage) windowFlags |= WindowFlags::RenderToImage;

        if (noCaption)
        {
            // Borderless/utility windows usually live on-top and aren't shown in taskbar.
            // Transparent flag should only be used for special layered windows (e.g., drag ghosts).
            windowFlags |= WindowFlags::Borderless | WindowFlags::Utility | WindowFlags::SkipTaskbar | WindowFlags::AlwaysOnTop;
        }

        if (isPopup)
        {
            // Popup menus: keep on top and out of taskbar, but do not block other windows.
            windowFlags |= WindowFlags::PopupMenu | WindowFlags::AlwaysOnTop | WindowFlags::SkipTaskbar | WindowFlags::Utility;
        }
        return windowFlags;
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
            assert(false && "BeginFrame called while previous frame has not ended (no EndFrame called)");
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

        // Popup menus close when another (non-child) window becomes active.
        {
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
            assert(false && "EndFrame called without BeginFrame called first");
            return false;
        }

        // Refresh our monitor list because we might have monitors added/removed
        // or resolution changed.
        RefreshMonitors();

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
            //      invalidates child windows — Update sees a null context
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
            assert(false && "Tried to call Update inside of Begin/EndFrame");
            return false;
        }
        windowBackendManager->PollEvents([this](interstice::OSWindowHandle handle, IWindowContext* context)
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
       
    bool OSWindowManager::PreLayoutSync(const WindowRegistry &windowRegistry,
                                           ElementStorage &elementStorage)
    {
        if (!insideBeginEnd)
        {
            assert(false && "PreLayoutSync called without BeginFrame called first");
            return false;
        }
        
        // Windows not declared this frame lose their user-closed flag, so the app can reopen them later.
        for (auto it = userClosedWindows.begin(); it != userClosedWindows.end(); )
        {
            if (!windowRegistry.Contains(*it))
            {
                it = userClosedWindows.erase(it);
            } else
            {
                ++it;
            }
        }

        for (int32_t window_index = 0; window_index < static_cast<int32_t>(windows.size()); window_index++)
        {
            auto& window = windows[static_cast<size_t>(window_index)];
            if (window.state == WindowState::Closing)
                continue;

            // Hidden windows are handled in GetOrCreateWindow (called from PostLayoutSync
            // after layout has computed the correct rect). Skip them here.
            if (window.state == WindowState::Hidden)
                continue;

            // Close windows that are managed by OSWindowManager but no longer
            // present in registry
            if (!windowRegistry.Contains(window.id))
            {
                // For popup/menu windows, hide instead of destroy to avoid expensive
                // Vulkan swapchain teardown on every open/close cycle.
                if ((window.flags & WindowFlags::PopupMenu) != WindowFlags::None)
                {
                    window.state = WindowState::Hidden;
                    windowBackendManager->SetWindowVisible(window.GetWindowHandle(), false);
                    continue;
                }

                // RequestClose marks them for destruction (handled in EndFrame)
                // Allow reopening since this is a programmatic close, not user-initiated
                RequestCloseAllowReopen(window.id);
                continue;
            }

            // Window exists in registry and is alive


            // Only user-controlled windows sync back; popup menus are UserControlled for
            // interaction but code-positioned (anchored), so they're skipped too.
            if ((window.flags & WindowFlags::UserControlled) == WindowFlags::None ||
                (window.flags & WindowFlags::PopupMenu) != WindowFlags::None)
                continue;

            ElementRef elementRef = windowRegistry.GetWindowRef(window.id);
            if (!elementStorage.IsValidRef(elementRef))
                continue;
            
            // Window is user-controlled, so we need to sync its element position and size with the OS window.

            // Update element's client rect (element-storage
            // expects client-area rects in screen coords)
            ElementRect& elementRect = elementStorage.GetElementRectUnsafe(elementRef);

            // lock size (in layout) by setting sizeConstraints so layout won't resize it
            if ((window.flags & WindowFlags::Resizable) == WindowFlags::Resizable)
            {
                // Overwrites the code given size of the window, 
                // which is fine since after creation it's user controlled
                auto sc = LayoutOverride::FixedSize(window.rect.w, window.rect.h);
                elementRect.constraints.width = sc.width;
                elementRect.constraints.height = sc.height;
            }

            // lock position (in layout) by setting positionConstraints so layout won't move it
            if ((window.flags & WindowFlags::Moveable) == WindowFlags::Moveable)
            {
                // Overwrites the code given position of the window,
                // which is fine since after creation it's user controlled
                auto pc = LayoutOverride{}.Position(window.rect.x, window.rect.y);
                elementRect.constraints.x = pc.x;
                elementRect.constraints.y = pc.y;
            }
        }
        return true;
    }

    bool OSWindowManager::PostLayoutSync(const WindowRegistry& windowRegistry,
                                         ElementStorage&       elementStorage,
                                         ITextSystem*          textSystem,
                                         OnGuiFunc             onGui)
    {
        if (!insideBeginEnd)
        {
            assert(false && "PostLayoutSync called without BeginFrame called first");
            return false;
        }

        for (const auto &windowEntry : windowRegistry.Windows())
        {
            if (windowEntry.id == ID::Invalid())
                continue;

            const ElementRef& winRef = windowEntry.windowRef;            
            if (elementStorage.IsValidRef(winRef) == false)
                continue;

            ID parentID = ID::Invalid();
            if (windowEntry.parentWindowEntry != WindowRegistry::kInvalidIndex)
            {
                parentID = windowRegistry.GetWindowID(
                    windowEntry.parentWindowEntry);
            }
            
            const Element&     winElement = elementStorage.GetElementUnsafe(winRef);
            const ElementRect& winRect    = elementStorage.GetElementRectUnsafe(winRef);
            
            std::string_view title = winElement.textID != TextID::Invalid ? textSystem->GetText(winElement.textID) : "";

            ChildPolicy childPolicy = (winElement.flags & ElementFlags::WindowCloseWithParent) != ElementFlags::None ?
				                        ChildPolicy::Close : ChildPolicy::Detach;
            WindowFlags windowFlags = ElementFlagsToWindowFlags(winElement.flags);

            int monitorIndex = winElement.monitorIndex;
            IntRect clientRect = winRect.rect;
            
            // Create or recycle existing window
            [[maybe_unused]]
            auto addResult = GetOrCreateWindow(
                                windowEntry.id, title, clientRect, onGui,
                                childPolicy, windowFlags, parentID, monitorIndex);
            assert(addResult == AddWindowResult::SuccessCreate ||
                   addResult == AddWindowResult::SuccessRecycle ||
                   addResult == AddWindowResult::WindowUserClosed);
        }
        return true;
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
                                           interstice::OnGuiFunc onGui,
                                           ChildPolicy childPolicy, WindowFlags windowFlags,
                                           ID parentID, int monitorIndex)
    {
        if (!insideBeginEnd) 
        {
            assert(false && "Tried to create window outside of Begin/EndFrame");
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
                // Reposition/resize before showing — the menu may have moved
                if (existing_window->GetWindowHandle() != nullptr)
                {
                    windowBackendManager->SetWindowRect(existing_window->GetWindowHandle(), rect);
                    existing_window->lastRect = rect;
                    existing_window->rect = rect;
                }
                windowBackendManager->SetWindowVisible(existing_window->GetWindowHandle(), true);
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
            existing_window->childPolicy = childPolicy;
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
        
        interstice::OSWindowHandle osHandle = windowBackendManager->CreateWindow(title, rect, windowFlags,
                parentWindow != nullptr ? parentWindow->GetWindowHandle() : nullptr);
        if (osHandle == nullptr)
            return AddWindowResult::FailedToCreateOSWindow;

        IWindowContext* windowContext = nullptr;
        if (graphicsContext != nullptr)
        {
            void* nativeWindowHandle = windowBackendManager->GetNativeHandle(osHandle);
            // The render target takes the backend's pixel size of the window.
            const IntRect pixelRect = windowBackendManager->GetWindowPixelRect(osHandle);
            windowContext =
                graphicsContext->CreateWindowContext(nativeWindowHandle, osHandle,
                                                     id, pixelRect.size, windowFlags, std::move(onGui));
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
            .childPolicy  = childPolicy,
            .flags        = windowFlags,
            .parentID     = parentID,
            .rect         = rect,
            .monitorIndex = monitorIndex,
            .displayScale = QueryDisplayScale(osHandle),
            .sequence     = m_nextSequence++,
            .osHandle     = osHandle,
            .context      = windowContext
        };

        window.parentID = ID::Invalid();
        windows.push_back(std::move(window));
        LinkToParent(idx, parentID);
        return AddWindowResult::SuccessCreate;
    }

    bool OSWindowManager::SetWindowRect(ID id, const IntRect &clientRect)
    {
        if (!insideBeginEnd)
        {
            assert(false && "SetWindowRect called outside of Begin/EndFrame");
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
            assert(false && "Tried to create window outside of Begin/EndFrame");
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
            assert(false && "Tried to create window outside of Begin/EndFrame");
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
            switch (child_window.childPolicy)
            {
                case ChildPolicy::Close:
                {
                    auto result = RequestClose(child_index, detach_children);
                    if (result != RequestCloseResult::WindowAlreadyClosed &&
                        result != RequestCloseResult::Success)
                        return result;
                    break;
                }

                case ChildPolicy::Detach:
                {
                    detach_children.push_back(child_id);
                    break;
                }
            }
            child_id = child_window.nextSiblingID;
            children_processed++;
        }
        return RequestCloseResult::Success;
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

} // namespace interstice