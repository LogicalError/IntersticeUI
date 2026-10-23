#include <gui_core/windows/gui_window_sync.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/text/gui_text_system.h>
#include <gui_core/windows/gui_os_window_manager.h>
#include <gui_core/windows/gui_window_registry.h>

#include <assert.h>

namespace interstice
{
    namespace
    {
        WindowFlags ElementFlagsToWindowFlags(ElementFlags elementFlags)
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
            if ((elementFlags & ElementFlags::WindowCloseWithParent) != ElementFlags::None)
                windowFlags |= WindowFlags::CloseWithParent;

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
    }

    void SyncWindowsBeforeLayout(OSWindowManager& windowManager, const WindowRegistry& windowRegistry,
                                 ElementStorage& elementStorage)
    {
        windowManager.ReleaseUndeclaredWindows([&](ID id) { return windowRegistry.Contains(id); });

        for (const auto& window : windowManager.AllWindows())
        {
            if (window.state != WindowState::Alive)
                continue;

            // Only user-controlled windows sync back; popup menus are UserControlled for
            // interaction but code-positioned (anchored), so they're skipped too.
            if ((window.flags & WindowFlags::UserControlled) == WindowFlags::None ||
                (window.flags & WindowFlags::PopupMenu) != WindowFlags::None)
                continue;

            ElementRef elementRef = windowRegistry.GetWindowRef(window.id);
            if (!elementStorage.IsValidRef(elementRef))
                continue;

            // The OS rect overrides the code-given one: after creation the user controls it.
            ElementRect& elementRect = elementStorage.GetElementRectUnsafe(elementRef);
            if ((window.flags & WindowFlags::Resizable) == WindowFlags::Resizable)
            {
                auto sc = LayoutOverride::FixedSize(window.rect.w, window.rect.h);
                elementRect.constraints.width  = sc.width;
                elementRect.constraints.height = sc.height;
            }
            if ((window.flags & WindowFlags::Moveable) == WindowFlags::Moveable)
            {
                auto pc = LayoutOverride{}.Position(window.rect.x, window.rect.y);
                elementRect.constraints.x = pc.x;
                elementRect.constraints.y = pc.y;
            }
        }
    }

    void SyncWindowsAfterLayout(OSWindowManager& windowManager, const WindowRegistry& windowRegistry,
                                ElementStorage& elementStorage, ITextSystem* textSystem, OnGuiFunc onGui)
    {
        for (const auto& windowEntry : windowRegistry.Windows())
        {
            if (windowEntry.id == ID::Invalid())
                continue;

            const ElementRef& winRef = windowEntry.windowRef;
            if (!elementStorage.IsValidRef(winRef))
                continue;

            ID parentID = ID::Invalid();
            if (windowEntry.parentWindowEntry != WindowRegistry::kInvalidIndex)
                parentID = windowRegistry.GetWindowID(windowEntry.parentWindowEntry);

            const Element&     winElement = elementStorage.GetElementUnsafe(winRef);
            const ElementRect& winRect    = elementStorage.GetElementRectUnsafe(winRef);
            std::string_view   title      = winElement.textID != TextID::Invalid ? textSystem->GetText(winElement.textID) : "";

            // Create or recycle the OS window.
            [[maybe_unused]]
            auto addResult = windowManager.GetOrCreateWindow(
                                windowEntry.id, title, winRect.rect, onGui,
                                ElementFlagsToWindowFlags(winElement.flags), parentID, winElement.monitorIndex);
            assert(addResult == AddWindowResult::SuccessCreate ||
                   addResult == AddWindowResult::SuccessRecycle ||
                   addResult == AddWindowResult::WindowUserClosed);
        }
    }
}
