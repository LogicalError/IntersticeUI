#include <gui_controls/gui_application.h>
#include <gui_core/graphics/gui_pixel_snap.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <gui_core/diagnostics/gui_profiler.h>
#include <gui_core/style/gui_style_sheet.h>
#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/windows/gui_window_sync.h>
#if INTERSTICE_SDL
#include <gui_core/windows/sdl/gui_sdl_clipboard.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>
#endif
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace interstice
{
    namespace
    {
        constexpr Diagnostics::LogCategory kLogApp = Diagnostics::MakeLogCategory("app");

        std::vector<ID> CollectPropagatedAncestors(const ElementStorage& storage, ID leafID)
        {
            std::vector<ID> result;
            if (leafID == ID::Invalid()) return result;
            ElementRef ref = storage.FindElementRefByID(leafID);
            if (!ref.IsValid()) return result;
            ref = storage.GetParentRef(ref);
            while (ref.IsValid())
            {
                const Element* el = storage.GetElementPtr(ref);
                if (!el) break;
                if ((el->flags & ElementFlags::PropagatesDescendantState) != ElementFlags::None)
                    result.push_back(el->id);
                ref = storage.GetParentRef(ref);
            }
            return result;
        }

        void FireEffectiveSetTransitions(
            const std::vector<ID>& oldSet, const std::vector<ID>& newSet,
            ElementState bit,
            ElementStorage& storage, TransitionManager& tm, float elapsedSecs)
        {
            auto contains = [](const std::vector<ID>& v, ID id) {
                for (ID x : v) if (x == id) return true;
                return false;
            };
            for (ID id : oldSet)
            {
                if (contains(newSet, id)) continue;
                ElementRef ref = storage.FindElementRefByID(id);
                if (!ref.IsValid()) continue;
                ElementState oldS = storage.GetElementUnsafe(ref).state;
                tm.OnStateChange(id, oldS, oldS & ~bit, elapsedSecs);
            }
            for (ID id : newSet)
            {
                if (contains(oldSet, id)) continue;
                ElementRef ref = storage.FindElementRefByID(id);
                if (!ref.IsValid()) continue;
                ElementState oldS = storage.GetElementUnsafe(ref).state;
                tm.OnStateChange(id, oldS, oldS | bit, elapsedSecs);
            }
        }
    }

    void VERIFY(ElementStorageResult result)
    {
        if (result == ElementStorageResult::Success) return;
        const char* message = "unknown ElementStorageResult";
        switch (result)
        {
        case ElementStorageResult::Success:              return;
        case ElementStorageResult::NotConstructing:      message = "ElementStorage: operation called outside BeginConstruction()/EndConstruction()"; break;
        case ElementStorageResult::MissingAdd:           message = "ElementStorage: operation requires a prior Add() (no element to attach children to)"; break;
        case ElementStorageResult::MissingBeginChildren: message = "ElementStorage: EndChildren() called without a matching BeginChildren()"; break;
        case ElementStorageResult::MissingEndChildren:   message = "ElementStorage: EndConstruction() reached with an unclosed BeginChildren() scope — a matching EndChildren() is missing"; break;
        case ElementStorageResult::DuplicateCall:        message = "ElementStorage: method called twice in a row (scope already open / already begun)"; break;
        case ElementStorageResult::InvalidStyle:         message = "ElementStorage: an invalid style was used"; break;
        case ElementStorageResult::UnknownProblem:       message = "ElementStorage: unknown problem (should not happen)"; break;
        }
        // A construction mistake (e.g. unbalanced Begin/End) is reported, not fatal: the
        // storage recovers. Once per kind, as it likely repeats every frame.
        static bool reported[int(ElementStorageResult::UnknownProblem) + 1] = {};
        const int kind = int(result);
        if (kind >= 0 && kind < int(std::size(reported)) && !std::exchange(reported[kind], true))
            INTERSTICE_LOG_ERROR(kLogApp, "VERIFY failed: {}", message);
    }

    void VERIFY(RegistryResult result)
    {
        if (result == RegistryResult::Success) return;
        const char* message = "unknown RegistryResult";
        switch (result)
        {
        case RegistryResult::Success:        return;
        case RegistryResult::InvalidRef:     message = "WindowRegistry: invalid element reference"; break;
        case RegistryResult::NoActiveWindow: message = "WindowRegistry: no active window scope (call BeginWindowScope() first)"; break;
        }
        INTERSTICE_LOG_ERROR(kLogApp, "VERIFY failed: {}", message);
        assert(false && "RegistryResult is not Success");
    }

    // WindowUserClosed is accepted too: it's the expected outcome when a window can't be reopened.
    void VERIFY(AddWindowResult result)
    {
        if (result == AddWindowResult::SuccessCreate ||
            result == AddWindowResult::SuccessRecycle ||
            result == AddWindowResult::WindowUserClosed) return;
        const char* message = "unknown AddWindowResult";
        switch (result)
        {
            case AddWindowResult::SuccessCreate:
            case AddWindowResult::SuccessRecycle:
            case AddWindowResult::WindowUserClosed:      return;
            case AddWindowResult::CalledOutsideOfFrame:  message = "OSWindowManager: AddWindow called outside BeginFrame()/EndFrame()"; break;
            case AddWindowResult::IndexOutOfBounds:      message = "OSWindowManager: window index out of bounds (internal error)"; break;
            case AddWindowResult::FailedToCreateOSWindow:message = "OSWindowManager: OS-level window creation failed"; break;
            case AddWindowResult::InvalidID:             message = "OSWindowManager: AddWindow called with an invalid ID"; break;
        }
        INTERSTICE_LOG_ERROR(kLogApp, "VERIFY failed: {}", message);
        assert(false && "AddWindowResult is not a success variant");
    }

    void VERIFY(RequestCloseResult result)
    {
        if (result == RequestCloseResult::Success) return;
        const char* message = "unknown RequestCloseResult";
        switch (result)
        {
            case RequestCloseResult::Success:             return;
            case RequestCloseResult::CalledOutsideFrame:  message = "OSWindowManager: RequestClose called outside BeginFrame()/EndFrame()"; break;
            case RequestCloseResult::IndexOutOfBounds:    message = "OSWindowManager: window index out of bounds (internal error)"; break;
            case RequestCloseResult::WindowAlreadyClosed: message = "OSWindowManager: window was already closed"; break;
            case RequestCloseResult::UnknownError:        message = "OSWindowManager: unknown error while closing window"; break;
        }
        INTERSTICE_LOG_ERROR(kLogApp, "VERIFY failed: {}", message);
        assert(false && "RequestCloseResult is not Success");
    }


    bool GUIApplication::Initialize(const InitializeOptions& options)
    {
        if (!m_osWindowBackend)
        {
#if INTERSTICE_SDL
            SDLWindowBackend::Options sdlOptions;
            sdlOptions.hooks  = m_allocationHooks.IsSet() ? &m_allocationHooks : nullptr;
            m_osWindowBackend = std::make_unique<SDLWindowBackend>(sdlOptions);
#else
            INTERSTICE_LOG_ERROR(kLogApp, "no window backend: built without SDL, so call SetWindowBackend first");
            return false;
#endif
        }
        if (!clipboard)
        {
#if INTERSTICE_SDL
            clipboard = std::make_unique<SDLClipboard>();
#else
            clipboard = std::make_unique<LocalClipboard>();
#endif
        }

        textSystem       = new interstice::SkribidiTextSystem();
        inputManager     = std::make_unique<interstice::InputManager>();
        inputBroadcaster = interstice::InputHandlerBroadcaster({ inputManager.get(), textSystem });
        if (!m_graphicsBackend)
            m_graphicsBackend = std::make_unique<vulkan::VulkanGraphicsContext>();
        graphicsContext  = m_graphicsBackend.get();
        graphicsContext->SetAllocationHooks(m_allocationHooks);
        m_imageWindows   = new interstice::ImageWindowBackend(*m_osWindowBackend);
        windowBackend    = m_imageWindows;
        windowBackend->SetGraphicsContext(graphicsContext);
        guiRenderer      = graphicsContext->guiRenderer;
        layoutEngine     = std::make_unique<interstice::LayoutEngine>(*textSystem, guiStateManager.GetScrollManagement());
        windowManager    = std::make_unique<interstice::OSWindowManager>(graphicsContext, windowBackend, &inputBroadcaster, styleManager, gradientManager);

        graphicsContext->SetShaderDirectory(ResourcePath("shaders_compiled"));
        textSystem->SetFontDirectory(ResourcePath("fonts"));
        if (!options.fallbackFonts.empty())
            textSystem->SetFallbackFontDirectory(ResourcePath(options.fallbackFonts));

        if (!graphicsContext->Initialize(windowBackend))
        {
            INTERSTICE_LOG_ERROR(kLogApp, "failed to initialize the graphics context");
            return false;
        }

        textAtlasManager.reset(graphicsContext->CreateTextAtlasManager());
        if (!textAtlasManager || !textAtlasManager->Initialize(graphicsContext, graphicsContext->GetImageSet()))
        {
            INTERSTICE_LOG_ERROR(kLogApp, "failed to initialize the text atlas manager");
            return false;
        }

        if (!textSystem->Initialize(graphicsContext, &styleManager, textAtlasManager.get(), &guiStateManager, clipboard.get()))
        {
            INTERSTICE_LOG_ERROR(kLogApp, "failed to initialize the text system");
            return false;
        }
        // No localization yet, so the OS language is the best guess for the UI's.
        textSystem->SetLanguage(windowBackend->GetPreferredLanguage());

        tooltipWindowID      = idManager.CreateStaticID();
        contextMenuWindowID  = idManager.CreateStaticID();
        tabDragGhostWindowID = idManager.CreateStaticID();

        m_sliderFieldTrack = std::make_unique<SliderFieldTrackSubsystem>();
        m_menuRects        = std::make_unique<MenuRectsSubsystem>();
        m_labelGroup       = std::make_unique<LabelGroupSubsystem>(m_frame, elementStorage, guiStateManager, theme);
        m_buttonGroup      = std::make_unique<ButtonGroupSubsystem>(m_frame, elementStorage, styleManager);
        m_colorPickerSub    = std::make_unique<ColorPickerSubsystem>(m_frame);
        m_gradientEditorSub = std::make_unique<GradientEditorSubsystem>(m_frame);
        m_renameSub         = std::make_unique<RenameSubsystem>();
        m_reorderable       = std::make_unique<ReorderableSubsystem>(m_frame);
        m_dockTabDragSub    = std::make_unique<DockTabDragSubsystem>();
        m_uiCache           = std::make_unique<UICacheManager>(m_frame, elementStorage, guiStateManager);

        RegisterFrameBalanceChecks();

        // Keyed by item ID, so every list/grid view in a frame shares them; end-of-frame
        // handlers read them, so they're only reset when the next frame's construction starts.
        m_frame.OnPreConstruct([this](FrameContext&)
        {
            m_listItemIndexByID.clear();
            m_gridItemRenameByID.clear();
        });

        if (options.loadStyles && !InitializeStyles())
            return false;

        // Global handlers run after focused-element handlers, so a focused control
        // that consumes a key takes priority over shortcuts.
        if (options.commandShortcuts)
            inputManager->PushGlobalHandler(commandManager.MakeHandler());

        // Windows repaint inside the OS's modal resize loop.
        if (options.repaintWhileResizing && !onGui)
            onGui = [this]() { OnFrame(); };

        m_osWindowBackend->SetHitTestCallback([this](OSWindowHandle window, int2 screenPos) {
            return TitleBarHitTest(windowManager->GetWindowID(window), screenPos);
        });

        // Fires inside the OS modal drag/resize loop, before the next frame.
        if (options.closeMenusOnWindowMove)
            m_osWindowBackend->SetWindowMovedCallback([this]() {
                if (menuManager.IsOpen())
                    menuManager.CloseAll(guiStateManager, *windowManager);
            });
        return true;
    }

    void GUIApplication::RegisterFrameBalanceChecks()
    {
        m_frame.OnEndFrame([this](FrameContext&)
        {
            if (!controlStylesStack.empty())
            {
                assert(false && "GUIApplication: unbalanced BeginToolbar/EndToolbar at end of frame");
                controlStylesStack.clear();
            }
            if (!m_windowIdScopeStack.empty())
            {
                assert(false && "GUIApplication: unbalanced BeginWindow/EndWindow at end of frame");
                m_windowIdScopeStack.clear();
            }
            if (m_treeState.containerID != ID::Invalid())
            {
                assert(false && "GUIApplication: unbalanced BeginListView/BeginTreeView/EndListView at end of frame");
                m_treeState = {};
            }
            if (m_gridViewState.containerID != ID::Invalid())
            {
                assert(false && "GUIApplication: unbalanced BeginGridView/EndGridView at end of frame");
                m_gridViewState = {};
            }
            if (m_tabBarState.barID != ID::Invalid())
            {
                assert(false && "GUIApplication: unbalanced BeginTabBar/EndTabBar at end of frame");
                m_tabBarState = {};
            }
            if (m_dockingState.containerID != ID::Invalid())
            {
                assert(false && "GUIApplication: unbalanced BeginDockingContainer/EndDockingContainer at end of frame");
                m_dockingState = {};
            }
            if (!menuManager.IsBalanced())
            {
                assert(false && "GUIApplication: unbalanced BeginMenu/EndMenu at end of frame");
                menuManager.currentDepth = -1;
            }
        });
    }

    void GUIApplication::Cleanup()
    {
        gradientManager.ReleaseAll(graphicsContext);
        layoutEngine.reset();
        inputManager.reset();
        delete textSystem; textSystem = nullptr;

        // Window contexts need a live device, and their native windows (owned by
        // the backend, deleted last) must outlive them.
        if (windowManager != nullptr)
            windowManager->DestroyAllWindowContexts();
        windowManager.reset();

        guiRenderer = nullptr;
        // The atlas registered a cleanup lambda on the device's deletion queue, so it must outlive Cleanup().
        graphicsContext->Cleanup();
        textAtlasManager.reset();
        m_graphicsBackend.reset(); graphicsContext = nullptr;
        delete windowBackend; windowBackend = nullptr; m_imageWindows = nullptr;
        m_osWindowBackend.reset();
        clipboard.reset();
    }

    bool GUIApplication::InitializeStyles()
    {
        m_displayScale = windowManager ? windowManager->MainDisplayScale() : 1.0f;
        styleManager.SetUIScale(m_displayScale * m_uiScale);
        theme.InitializeDefaultStyles(styleManager, textSystem);
        const float fontScale = m_globalFontScale > 0.0f ? m_globalFontScale : 1.0f;
        if (theme.LoadTheme(styleManager, textSystem, ResourcePath(m_themePath), m_styleFiles, fontScale))
        {
            // After LoadTheme, so .table-cell / .tree-table-cell are resolved.
            if (textSystem) theme.ComputeRowMetrics(styleManager, *textSystem);
            return true;
        }
        INTERSTICE_LOG_ERROR(kLogApp, "could not load theme {}", m_themePath);
        return false;
    }

    void GUIApplication::CreatePanel(std::unordered_map<interstice::ID, interstice::Panel>& panels, ID id, std::string title, interstice::IconID icon,
                                     std::function<void()> onPanelGUI,
                                     std::string panelType,
                                     std::function<void()> onEnable,
                                     std::function<void()> onDisable)
    {
        panels[id] = Panel{
            .id         = id,
            .title      = std::move(title),
            .icon       = icon,
            .panelType  = std::move(panelType),
            .onPanelGUI = std::move(onPanelGUI),
            .onEnable   = std::move(onEnable),
            .onDisable  = std::move(onDisable),
        };
    }

    int2 GUIApplication::InjectedScreenPos(ID windowID, int2 pos)
    {
        const int2 screenPos = windowManager->GetWindowRect(windowID).position + pos;
        const OSWindowHandle window = windowManager->GetWindowHandle(windowID);
        m_imageWindows->SetPointer(window, screenPos, m_injectedButtons);
        m_imageWindows->SetFocus(window);
        return screenPos;
    }

    void GUIApplication::InjectMouseMove(ID windowID, int2 pos, KeyModifier mods)
    {
        inputBroadcaster.OnMouseMove(windowID, InjectedScreenPos(windowID, pos), m_injectedButtons, mods);
    }

    void GUIApplication::InjectMouseButton(ID windowID, int2 pos, MouseButtons button, bool down, KeyModifier mods)
    {
        m_injectedButtons = down ? (m_injectedButtons | button) : (m_injectedButtons & ~button);
        inputBroadcaster.OnMouseButton(windowID, InjectedScreenPos(windowID, pos), m_injectedButtons, mods,
                                       down ? MouseAction::MouseDown : MouseAction::MouseUp);
    }

    void GUIApplication::InjectMouseScroll(ID windowID, int2 pos, float2 offset, KeyModifier mods)
    {
        inputBroadcaster.OnMouseScroll(windowID, InjectedScreenPos(windowID, pos), offset, mods);
    }

    void GUIApplication::InjectMouseLeave(ID windowID)
    {
        if (windowManager->GetWindowHandle(windowID) == windowBackend->GetWindowUnderMouse())
            m_imageWindows->SetPointer(nullptr, {}, MouseButtons::None);
    }

    void GUIApplication::InjectFocus(ID windowID, bool focused)
    {
        const OSWindowHandle window = windowManager->GetWindowHandle(windowID);
        if (focused)
            m_imageWindows->SetFocus(window);
        else if (window == windowBackend->GetActiveWindow())
            m_imageWindows->SetFocus(nullptr);
    }

    void GUIApplication::InjectKey(ID windowID, KeyCode key, KeyAction action, KeyModifier mods)
    {
        m_imageWindows->SetFocus(windowManager->GetWindowHandle(windowID));
        inputBroadcaster.OnKeyboard(windowID, key, 0, mods, action);
    }

    void GUIApplication::InjectText(ID windowID, std::string_view utf8)
    {
        // One KeyChar per codepoint, as the OS backend sends typed text.
        m_imageWindows->SetFocus(windowManager->GetWindowHandle(windowID));
        for (size_t i = 0; i < utf8.size(); )
        {
            const auto lead = static_cast<unsigned char>(utf8[i]);
            const int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
            if (length == 0 || i + length > utf8.size())
                return;   // malformed: stop rather than send garbage
            uint32_t codepoint = length == 1 ? lead : lead & (0x7Fu >> length);
            for (int k = 1; k < length; ++k)
                codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
            inputBroadcaster.OnKeyboard(windowID, static_cast<KeyCode>(codepoint), 0, KeyModifier::None, KeyAction::KeyChar);
            i += size_t(length);
        }
    }

    bool GUIApplication::ReadWindowPixels(ID windowID, std::span<uint8_t> bgra)
    {
        IWindowContext* context = windowManager ? windowManager->GetWindowContext(windowID) : nullptr;
        return context != nullptr && context->ReadPixels(bgra);
    }

    std::string GUIApplication::ResourcePath(std::string_view path) const
    {
        const std::filesystem::path p(path);
        return m_resourceRoot.empty() || p.is_absolute() ? p.string() : (std::filesystem::path(m_resourceRoot) / p).string();
    }

    void GUIApplication::SetStyleFiles(std::span<const std::string_view> files)
    {
        m_styleFiles.assign(files.begin(), files.end());
    }

    void GUIApplication::RequestReloadTheme(std::string_view themePath, float fontScale)
    {
        // Callers (e.g. a dragged slider) may call this every frame; skip no-ops.
        if (themePath == m_themePath && fontScale == m_globalFontScale) return;
        m_themePath = std::string(themePath);
        m_globalFontScale = fontScale;
        m_themeReloadPending = true;
    }

    void GUIApplication::ApplyPendingStyleReload()
    {
        // Runs before BeginFrame so style state is consistent for the whole frame.
        if (windowManager && windowManager->MainDisplayScale() != m_displayScale)
            m_themeReloadPending = true; // the main window moved to a monitor with another scale
        if (!m_themeReloadPending)
            return;
        m_themeReloadPending = false;

        // Tear down panel resources before their styles disappear; m_dockingLayoutsThisFrame
        // still holds last frame's layouts here. EnableVisiblePanels rebuilds them next frame.
        for (DockingLayout* layout : m_dockingLayoutsThisFrame)
            if (layout) DisableAllPanels(*layout);

        styleManager.ClearAll();
        if (textSystem) textSystem->ClearCache();
        if (!InitializeStyles())
            INTERSTICE_LOG_ERROR(kLogApp, "theme reload failed for {}; styles stay cleared", m_themePath);
        else
            OnStylesReloaded();

        // Captured draws reference the old styles and glyph atlas.
        if (m_uiCache) m_uiCache->InvalidateAll();
        if (guiRenderer) guiRenderer->ClearScopeCaptures();
    }

    bool GUIApplication::Run()
    {
        applicationQuit = false;
        while (!applicationQuit)
        {
            auto frameStart = std::chrono::steady_clock::now();

            INTERSTICE_PROFILE_FRAME_BEGIN("Frame");

            // Pump input right before the build, after the previous present.
            {
                INTERSTICE_PROFILE_SCOPE("Input pump");
                windowManager->Update();
            }

            // Build: construction, layout, input dispatch and draw recording (CPU only).
            auto buildStart = std::chrono::steady_clock::now();
            {
                INTERSTICE_PROFILE_SCOPE("Build");
                graphicsContext->BeginFrame();
                OnFrame();
                graphicsContext->EndFrame();
            }
            auto buildEnd = std::chrono::steady_clock::now();
            frameBuildTime = std::chrono::duration<float>(buildEnd - buildStart).count();

            // Uploads must run between build and render: the render consumes what this build requested
            // (windows created/resized by OnFrame, glyphs, styles, gradient pages).
            {
                INTERSTICE_PROFILE_SCOPE("Resource upload");
                windowManager->Update();
                textSystem->Update();
                graphicsContext->PrepareFrame(styleManager, gradientManager, static_cast<float>(m_frameClock.Seconds()));
            }
            {
                INTERSTICE_PROFILE_SCOPE("Render");
                windowManager->ForEachAliveWindowContext
                    ([&](IWindowContext* context, IntRect rect)
                    {
                        context->RenderWindowFrame(graphicsContext, rect, false);
                    });
            }

            // frameTime includes any vsync wait; frameBuildTime is just the CPU build.
            INTERSTICE_PROFILE_FRAME_END();
            frameTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - frameStart).count();

            // In milliseconds; frozen while profiling is off.
            if (Diagnostics::IsProfilingEnabled())
            {
                frameTimingHistory[frameTimingHead] = { frameBuildTime * 1000.0f, frameTime * 1000.0f };
                frameTimingHead = (frameTimingHead + 1) % kFrameHistoryCount;
            }
        }
        return true;
    }

    void GUIApplication::GetFrameTimingHistory(std::span<FrameTimingSample> out) const
    {
        const int n = int(out.size()) < kFrameHistoryCount ? int(out.size()) : kFrameHistoryCount;
        for (int i = 0; i < n; ++i)
            out[size_t(i)] = frameTimingHistory[(frameTimingHead + i) % kFrameHistoryCount];
    }

    void GUIApplication::SetUIScale(float scale)
    {
        scale = scale > 0.0f ? scale : 1.0f;
        if (scale == m_uiScale) return;
        m_uiScale = scale;
        m_themeReloadPending = true; // styles are registered at the UI scale
    }

    void GUIApplication::OnRender(float elapsedTimeSecs)
    {
        // Per-depth render state, indexed by ElementRef.depth (parent = depth - 1).
        // Fields are in different spaces; each name says which.
        struct RenderDepthState
        {
            IntRect  clipRectScreen       {};  // scissor for children, SCREEN space (window-local)
            float3x2 contentTransformAccum{};  // accumulated content transforms, applied around transformPivotLayout
            int2     transformPivotLayout {};  // innerRect.position of the nearest ancestor with a non-identity contentTransform
            bool     hasTransform         = false;  // contentTransformAccum != identity
            float4   pixelRect            {};  // this element's border box in window pixels, before snapping
            IntRect  snappedPixelRect     {};  // ...and snapped (children snap relative to it; gui_pixel_snap.h)
        };
        constexpr uint32_t kMaxRenderDepth = 64;
        RenderDepthState depthStates[kMaxRenderDepth]{};



        for (auto& windowEntry : windowRegistry.Windows())
        {
            auto& windowRef = windowEntry.windowRef;
            auto& windowElement = elementStorage.GetElementUnsafe(windowRef);

            if (!guiRenderer->BeginWindow(windowElement.id))
                continue;

            guiRenderer->SetTransform(interstice::float3x2::Identity());

            // Element state plus OS window state: Focused = OS-active window, Hover = window under the mouse.
            ElementState windowState = guiStateManager.GetCurrentState(windowElement.id);
            auto* osWindowManager = windowManager.get();
            if (osWindowManager->GetActiveWindow() == windowElement.id)
                windowState |= ElementState::Focused;
            if (osWindowManager->GetWindowUnderMouse() == windowElement.id)
                windowState |= ElementState::Hover;

            auto  windowBoxStyleIndex = windowElement.styleRef.visualBoxStyleIndex;
            auto& windowElementRect   = elementStorage.GetElementRectUnsafe(windowRef);
            int2  offset              = windowElementRect.rect.position;

            // Window background
            IntRect windowRect    = windowElementRect.rect;
            windowRect.position   = { 0, 0 };
            IntRect windowClipRect = windowElementRect.innerRect - offset;
            const float4  windowPixelRect   = PixelBounds(float3x2::Identity(), windowRect);
            const IntRect windowSnappedRect = SnapAbsolute(windowPixelRect);
            guiRenderer->RenderBox(windowRect, windowRect, windowBoxStyleIndex, windowState, VisualColorStyleIndex{}, ImageIndex::Invalid,
                                   windowSnappedRect);

            // Windows reset ancestor transforms, so LAYOUT == SCREEN space at window depth.
            const uint32_t windowDepth = windowRef.depth;
            assert(windowDepth < kMaxRenderDepth && "Window hierarchy too deep for render state array");
            depthStates[windowDepth].clipRectScreen        = windowClipRect;
            depthStates[windowDepth].contentTransformAccum = windowElementRect.contentTransform;
            depthStates[windowDepth].transformPivotLayout  = windowElementRect.innerRect.position - offset;
            depthStates[windowDepth].hasTransform          = !windowElementRect.contentTransform.IsIdentity();
            depthStates[windowDepth].pixelRect             = windowPixelRect;
            depthStates[windowDepth].snappedPixelRect      = windowSnappedRect;

            const auto& nodes = windowEntry.members;
            // While capturing, a cached scope's children render into its capture (until captureEndIdx).
            bool     capturing     = false;
            uint32_t captureEndIdx = 0;
            interstice::ID  captureScopeId = interstice::ID::Invalid();
            for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); )
            {
                const interstice::WindowRegistry::RenderNode& node = nodes[i];
                if (capturing && i >= captureEndIdx)
                {
                    guiRenderer->EndScopeCapture(captureScopeId);
                    textSystem->EndAtlasRecord();
                    capturing = false;
                }
                const ElementRef childRef  = node.ref;
                const uint32_t   depth     = childRef.depth;
                assert(depth > 0 && depth < kMaxRenderDepth);

                auto& childElementRect = elementStorage.GetElementRectUnsafe(childRef);
                auto  layoutStyle      = childElementRect.layoutStyle;
                assert(layoutStyle != nullptr && "All elements must have a layout style at this point");

                RenderDepthState& parentState = depthStates[depth - 1];

                // LAYOUT space (window-local, pre-transform); apply the parent's content
                // transform to get screen-space rects.
                IntRect rectLayout          = childElementRect.rect          - offset;
                IntRect innerRectLayout     = childElementRect.innerRect     - offset;
                IntRect contentBoundsLayout = childElementRect.contentBounds - offset;

                // Display::None skips this element but still traverses its children.
                const bool displayNone = layoutStyle->display == interstice::Display::None;

                const int2 elementPosLayout{ rectLayout.x, rectLayout.y };

                if (!displayNone)
                {
                    // Cull the subtree if it misses the parent scissor (transformed to SCREEN space first).
                    IntRect cullCheckRectScreen = rectLayout;
                    if (parentState.hasTransform)
                    {
                        const float2 pf { (float)parentState.transformPivotLayout.x, (float)parentState.transformPivotLayout.y };
                        const float2 corners[4] = {
                            { (float)rectLayout.x,                  (float)rectLayout.y                  },
                            { (float)(rectLayout.x + rectLayout.w), (float)rectLayout.y                  },
                            { (float)rectLayout.x,                  (float)(rectLayout.y + rectLayout.h) },
                            { (float)(rectLayout.x + rectLayout.w), (float)(rectLayout.y + rectLayout.h) },
                        };
                        float minX = corners[0].x, minY = corners[0].y, maxX = corners[0].x, maxY = corners[0].y;
                        for (const float2& c : corners)
                        {
                            float2 tc = pf + parentState.contentTransformAccum.TransformPoint(c - pf);
                            if (tc.x < minX) minX = tc.x;
                            if (tc.y < minY) minY = tc.y;
                            if (tc.x > maxX) maxX = tc.x;
                            if (tc.y > maxY) maxY = tc.y;
                        }
                        cullCheckRectScreen = IntRect { (int)minX, (int)minY, (int)(maxX - minX), (int)(maxY - minY) };
                    }
                    IntRect isect = cullCheckRectScreen.Intersection(parentState.clipRectScreen);
                    if (isect.w == 0 || isect.h == 0)
                    {
                        // Still fire post-draw callbacks linked here: their owner may be visible.
                        auto& cullDraw = elementStorage.GetElementCustomDrawUnsafe(childRef);
                        for (uint32_t idx = cullDraw.postDrawHead; idx != UINT32_MAX; )
                        {
                            auto& entry     = elementStorage.GetPostDrawEntryUnsafe(idx);
                            auto& ownerDraw = elementStorage.GetElementCustomDrawUnsafe(entry.ownerRef);
                            guiRenderer->SetTransform(ownerDraw.afterDrawRenderTransform);
                            CustomDrawContext ctx{ *guiRenderer, elementStorage, entry.ownerRef,
                                                   ownerDraw.afterDrawElementRectLayout,
                                                   ownerDraw.afterDrawInnerRectLayout,
                                                   ownerDraw.afterDrawClipRectScreen,
                                                   ownerDraw.afterDrawRenderTransform, offset, textSystem,
                                                   ownerDraw.afterDrawPixelRect, ownerDraw.afterDrawSnappedRect };
                            entry.callback(ctx);
                            idx = entry.nextIndex;
                        }
                        i = node.subtreeSkip;
                        continue;
                    }
                }

                // Screen-space AABB of a layout rect under the parent's content transform.
                auto transformRectToScreen = [&](IntRect r) -> IntRect {
                    if (!parentState.hasTransform) return r;
                    const float2 pf { (float)parentState.transformPivotLayout.x, (float)parentState.transformPivotLayout.y };
                    const float2 corners[4] = {
                        { (float)r.x,             (float)r.y             },
                        { (float)(r.x + r.w),     (float)r.y             },
                        { (float)r.x,             (float)(r.y + r.h)     },
                        { (float)(r.x + r.w),     (float)(r.y + r.h)     },
                    };
                    float minX = corners[0].x, minY = corners[0].y, maxX = corners[0].x, maxY = corners[0].y;
                    for (const float2& c : corners)
                    {
                        float2 tc = pf + parentState.contentTransformAccum.TransformPoint(c - pf);
                        if (tc.x < minX) minX = tc.x;
                        if (tc.y < minY) minY = tc.y;
                        if (tc.x > maxX) maxX = tc.x;
                        if (tc.y > maxY) maxY = tc.y;
                    }
                    return IntRect{ (int)minX, (int)minY, (int)(maxX - minX), (int)(maxY - minY) };
                };

                // Children clip when either axis is non-visible (CSS: one paint box).
                const bool    anyClip              = (layoutStyle->overflowX != interstice::Overflow::Visible)
                                                  || (layoutStyle->overflowY != interstice::Overflow::Visible);
                IntRect       childClipRectScreen  = anyClip
                                                     ? parentState.clipRectScreen.Intersection(transformRectToScreen(innerRectLayout))
                                                     : parentState.clipRectScreen;
                float3x2 childContentTransformAccum  = parentState.contentTransformAccum * childElementRect.contentTransform;
                depthStates[depth].clipRectScreen        = childClipRectScreen;
                depthStates[depth].contentTransformAccum = childContentTransformAccum;
                // The pivot is sticky through identity-transform descendants, so an ancestor's
                // transform (e.g. node-view pan/zoom) applies consistently to nested content.
                depthStates[depth].transformPivotLayout  = childElementRect.contentTransform.IsIdentity()
                                                            ? parentState.transformPivotLayout
                                                            : innerRectLayout.position;
                depthStates[depth].hasTransform          = !childContentTransformAccum.IsIdentity();

                // LAYOUT -> SCREEN: T(pivot) * contentTransformAccum * T(-pivot), for the element's own box and text.
                float3x2 elementRenderTransform = float3x2::Identity();
                if (parentState.hasTransform)
                {
                    const int2& pivot = parentState.transformPivotLayout;
                    elementRenderTransform =
                        float3x2::Translate((float)pivot.x,  (float)pivot.y) *
                        parentState.contentTransformAccum *
                        float3x2::Translate((float)-pivot.x, (float)-pivot.y);
                }

                // The border box in window pixels, snapped relative to the parent's
                // snapped box, so insets and borders are the same pixels everywhere.
                const float4  elementPixelRect   = PixelBounds(elementRenderTransform, rectLayout);
                const IntRect elementSnappedRect = SnapToParent(parentState.pixelRect, parentState.snappedPixelRect, elementPixelRect);
                depthStates[depth].pixelRect        = elementPixelRect;
                depthStates[depth].snappedPixelRect = elementSnappedRect;

                if (!displayNone)
                {
                    guiRenderer->SetTransform(elementRenderTransform);

                    // The box clips to the parent scissor; content additionally clips to the padding box.
                    // innerRectLayout may carry a leaf-scroll offset, so the clip inner rect is
                    // recomputed from the border box to stay put while content scrolls.
                    IntRect parentClipRectScreen = parentState.clipRectScreen;
                    IntRect visualInnerRectLayout = layoutStyle->GetPaddingBoxContentRect(rectLayout);

                    IntRect elementRectScreen   = transformRectToScreen(rectLayout);
                    IntRect innerRectScreen     = transformRectToScreen(visualInnerRectLayout);
                    // Content clips to the padding box; custom draws clip to the border box
                    // (CSS background-clip: border-box).
                    IntRect contentClipRectScreen = parentState.clipRectScreen.Intersection(innerRectScreen);
                    IntRect borderClipRectScreen  = parentState.clipRectScreen.Intersection(elementRectScreen);

                    auto& childElement      = elementStorage.GetElementUnsafe(childRef);
                    auto  state             = childElement.state;
                    auto  baseBoxStyleIndex = childElement.styleRef.visualBoxStyleIndex;

                    // Windows get an OS-drawn shadow instead.
                    const bool wantsShadow = (childElement.flags & ElementFlags::Shadows) != ElementFlags::None;
                    const bool isWindow    = (childElement.flags & ElementFlags::IsWindow) != ElementFlags::None;
                    if (wantsShadow && !isWindow &&
                        theme.shadowColorStyle != VisualColorStyleIndex::Invalid)
                    {
                        const int kShadowOffsetPx = styleManager.Px(6);

                        // Offset in LAYOUT space so it scales with the content transform (zoom).
                        IntRect shadowRect = rectLayout;
                        shadowRect.x += kShadowOffsetPx;
                        shadowRect.y += kShadowOffsetPx;

                        const VisualBoxStyle* bs = styleManager.GetVisualBoxStyle(baseBoxStyleIndex, state);
                        const float baseRadius   = bs ? float(bs->borderRadius.topLeft) : 0.0f;
                        const float shadowRadius = baseRadius;

                        guiRenderer->RenderQuad(shadowRect, parentClipRectScreen,
                                                theme.shadowColorStyle, shadowRadius);
                    }

                    {
                        TransitionState ts = transitionManager.Resolve(childElement.id, baseBoxStyleIndex, state, elapsedTimeSecs);
                        // A mid-transition blend would freeze in the capture; rebuild until it settles.
                        if (capturing && ts.blendT < 1.0f)
                            m_uiCache->Invalidate(captureScopeId);
                        // rect is LAYOUT space (mapped by SetTransform), the scissor is SCREEN space.
                        guiRenderer->RenderBox(rectLayout, parentClipRectScreen, ts.fromStyleIndex, ts.toStyleIndex, ts.blendT,
                                               childElement.colorStyleIndex, childElement.backgroundImage, elementSnappedRect);
                    }

                    // RenderText handles plain text, rich text with inline icons, and TextEditor rendering.
                    if (childElement.textID != TextID::Invalid)
                    {
                        IntRect textRectLayout = innerRectLayout;
                        textRectLayout.size    = contentBoundsLayout.size;
                        // Rasterize glyphs at the transform's uniform scale rather than scaling in the shader.
                        const float textPixelScale = parentState.hasTransform
                            ? sqrtf(elementRenderTransform.r0.x * elementRenderTransform.r0.x +
                                    elementRenderTransform.r0.y * elementRenderTransform.r0.y)
                            : 1.f;
                        // Editors hit-test OS mouse positions: hand them the inverse of the
                        // ancestors' content transform (zoom/pan), in absolute coordinates.
                        float3x2 screenToLayout = float3x2::Identity();
                        if (parentState.hasTransform)
                        {
                            const float2 pivot{ float(parentState.transformPivotLayout.x + offset.x),
                                                float(parentState.transformPivotLayout.y + offset.y) };
                            screenToLayout = float3x2::Translate(pivot) * parentState.contentTransformAccum.Inverse()
                                           * float3x2::Translate(float2{ -pivot.x, -pivot.y });
                        }
                        textSystem->RenderText(childElement.textID, textRectLayout, childElementRect.innerRect, contentClipRectScreen,
                                               textPixelScale, screenToLayout);
                    }
                    else if (childElement.iconID != IconID::Invalid)
                    {
                        // -interstice-icon-offset-y: positive moves the icon up.
                        IntRect iconRectLayout = innerRectLayout;
                        if (childElementRect.layoutStyle)
                            iconRectLayout.y -= childElementRect.layoutStyle->iconOffsetY;
                        const VisualTextStyle* iconText = styleManager.GetVisualTextStyle(childElement.styleRef.visualTextStyleIndex, state);
                        textSystem->RenderIcon(childElement.iconID, iconRectLayout, contentClipRectScreen,
                                               iconText ? iconText->iconColorIndex : VisualColorStyleIndex::Invalid);
                    }

                    // Custom draws take inner-rect-local LAYOUT pixels, in the same content
                    // space as the element's children (own contentTransform included).
                    const float3x2 customDrawRenderTransform =
                        elementRenderTransform *
                        float3x2::Translate((float)visualInnerRectLayout.position.x,
                                            (float)visualInnerRectLayout.position.y) *
                        childElementRect.contentTransform;

                    // CustomDrawContext rects are ABSOLUTE layout space (to match GetElementRect);
                    // the render transform is window-local, which is fine for relative offsets.
                    const IntRect elementRectAbs     = childElementRect.rect;
                    const IntRect innerRectAbs       = layoutStyle->GetPaddingBoxContentRect(childElementRect.rect);

                    if (const auto* draw = elementStorage.GetCustomDraw(childRef))
                    {
                        if (draw->callback)
                        {
                            const bool customDrawAnyClip =
                                (layoutStyle->overflowX != interstice::Overflow::Visible) ||
                                (layoutStyle->overflowY != interstice::Overflow::Visible);
                            IntRect customDrawClipScreen = customDrawAnyClip
                                ? borderClipRectScreen : parentClipRectScreen;
                            guiRenderer->SetTransform(customDrawRenderTransform);
                            CustomDrawContext ctx{ *guiRenderer, elementStorage, childRef,
                                                   elementRectAbs, innerRectAbs, customDrawClipScreen,
                                                   customDrawRenderTransform, offset, textSystem,
                                                   elementPixelRect, elementSnappedRect };
                            draw->callback(ctx);
                        }
                    }

                    // Saved so AddCustomDrawAfter callbacks use this element's space wherever they fire.
                    {
                        auto& draw = elementStorage.GetElementCustomDrawUnsafe(childRef);
                        if (draw.afterHead != UINT32_MAX)
                        {
                            draw.afterDrawClipRectScreen    = borderClipRectScreen;
                            draw.afterDrawElementRectLayout = elementRectAbs;
                            draw.afterDrawInnerRectLayout   = innerRectAbs;
                            draw.afterDrawRenderTransform   = customDrawRenderTransform;
                            draw.afterDrawPixelRect         = elementPixelRect;
                            draw.afterDrawSnappedRect       = elementSnappedRect;
                        }
                    }
                }

                // Link afterHead onto the subtree's last node (also for display:none).
                // Prepended, so inner elements fire before outer ones.
                {
                    auto& draw = elementStorage.GetElementCustomDrawUnsafe(childRef);
                    if (draw.afterHead != UINT32_MAX)
                    {
                        const ElementRef lastRef = nodes[node.subtreeSkip - 1].ref;
                        auto& lastDraw = elementStorage.GetElementCustomDrawUnsafe(lastRef);
                        elementStorage.GetPostDrawEntryUnsafe(draw.afterTail).nextIndex = lastDraw.postDrawHead;
                        lastDraw.postDrawHead = draw.afterHead;
                    }
                }

                // Fire postDrawHead - each entry replays its OWNER's captured render transform.
                {
                    auto& draw = elementStorage.GetElementCustomDrawUnsafe(childRef);
                    for (uint32_t idx = draw.postDrawHead; idx != UINT32_MAX; )
                    {
                        auto& entry     = elementStorage.GetPostDrawEntryUnsafe(idx);
                        auto& ownerDraw = elementStorage.GetElementCustomDrawUnsafe(entry.ownerRef);
                        guiRenderer->SetTransform(ownerDraw.afterDrawRenderTransform);
                        CustomDrawContext ctx{ *guiRenderer, elementStorage, entry.ownerRef,
                                               ownerDraw.afterDrawElementRectLayout,
                                               ownerDraw.afterDrawInnerRectLayout,
                                               ownerDraw.afterDrawClipRectScreen,
                                               ownerDraw.afterDrawRenderTransform, offset, textSystem,
                                               ownerDraw.afterDrawPixelRect, ownerDraw.afterDrawSnappedRect };
                        entry.callback(ctx);
                        idx = entry.nextIndex;
                    }
                }

                // Retained-render cache: a rebuilt scope captures its children as they render;
                // a clean (empty) one blits its capture at the container's current position.
                const Element& cachedEl = elementStorage.GetElementUnsafe(childRef);
                if (!displayNone && !capturing && m_uiCache->IsDrawCacheEnabled() &&
                    (cachedEl.flags & ElementFlags::IsCachedScope) != ElementFlags::None)
                {
                    if (UICacheManager::Scope* scope = m_uiCache->Find(cachedEl.id);
                        scope && !scope->nonCacheable)
                    {
                        const int2 containerPos = (childElementRect.rect - offset).position;
                        if (scope->missedThisFrame)
                        {
                            // Rebuilt: capture the children as they render.
                            guiRenderer->BeginScopeCapture(cachedEl.id);
                            textSystem->BeginAtlasRecord(cachedEl.id);
                            capturing      = true;
                            captureEndIdx  = node.subtreeSkip;
                            captureScopeId = cachedEl.id;
                            scope->drawCapturePos = containerPos;
                            m_uiCache->RecordDrawCapture();
                        }
                        else if (guiRenderer->HasScopeCapture(cachedEl.id)
                                 && !textSystem->ReplayAtlasTouches(cachedEl.id))
                        {
                            // A glyph/icon the capture points at was evicted or moved: re-render.
                            m_uiCache->Invalidate(cachedEl.id);
                            i = node.subtreeSkip;
                            continue;
                        }
                        else if (guiRenderer->ReplayScopeCapture(cachedEl.id, containerPos - scope->drawCapturePos))
                        {
                            m_uiCache->RecordDrawReplay();
                            i = node.subtreeSkip;
                            continue;
                        }
                        else
                        {
                            // Capture dropped (e.g. theme reload) or taken at another scale: rebuild next frame.
                            m_uiCache->Invalidate(cachedEl.id);
                        }
                    }
                }

                i++;
            }

            if (capturing) // close a capture that ran to the end of the member list
            {
                guiRenderer->EndScopeCapture(captureScopeId);
                textSystem->EndAtlasRecord();
                capturing = false;
            }

            guiRenderer->EndWindow();
        }
    }


    void GUIApplication::OnFrame()
    {
        // Once per frame; shared by transitions and GPU push constants.
        m_frameClock.Tick(SteadyClockNowNanos(), windowManager ? windowManager->MainRefreshRate() : 0.0f);
        const float elapsedTimeSecs = static_cast<float>(m_frameClock.Seconds());

        FrameContext ctx{
            .app               = *this,
            .idManager         = idManager,
            .guiStateManager   = guiStateManager,
            .styleManager      = styleManager,
            .elementStorage    = elementStorage,
            .windowRegistry    = windowRegistry,
            .windowManager     = *windowManager,
            .transitionManager = transitionManager,
            .tabChainManager   = tabChainManager,
            .menuManager       = menuManager,
            .tooltipManager    = tooltipManager,
            .theme             = theme,
            .textSystem        = textSystem,
            .inputManager      = inputManager.get(),
            .windowBackend     = windowBackend,
            .elapsedTimeSecs   = elapsedTimeSecs,
        };

        m_frame.RunStage(FrameStage::PreFrame, ctx);

        ApplyPendingStyleReload();

        // *** BEGIN FRAME ***
        {
            INTERSTICE_PROFILE_SCOPE("BeginFrame");

            // On Windows, with a secondary top-level window open, the click that activates a
            // sibling never reaches SDL; synthesize it from the physical button state.
            // Must run before BeginFrame, whose popup auto-close marks popups as Closing.
            if (inputManager &&
                (windowManager->HasAliveSecondaryWindows() || m_lastPhysicalButtons != MouseButtons::None))
            {
                interstice::int2 mousePos = m_osWindowBackend->GetScreenMousePos();
                interstice::MouseButtons physButtons = m_osWindowBackend->GetMouseButtons();
                inputManager->SynthesizeMissingMouseEvents(mousePos, m_lastPhysicalButtons, physButtons);
                m_lastPhysicalButtons = physButtons;
            } else
                m_lastPhysicalButtons = MouseButtons::None;

            if (!idManager.BeginFrame())
            {
                INTERSTICE_LOG_ERROR(kLogApp, "IDManager::BeginFrame failed");
                return;
            }

            if (!windowManager->BeginFrame())
            {
                INTERSTICE_LOG_ERROR(kLogApp, "OSWindowManager::BeginFrame failed");
                return;
            }

            if (!guiStateManager.BeginFrame())
            {
                INTERSTICE_LOG_ERROR(kLogApp, "GUIStateManager::BeginFrame failed");
            }

            // Before construction: drives the active-flash window in GetCurrentState.
            guiStateManager.Interaction().SetFrameTime(elapsedTimeSecs);

            BeginDockingFrame();
            tabChainManager.BeginFrame(guiStateManager);
            menuManager.BeginFrame(guiStateManager, *windowManager);

            // Input that dismisses the tooltip.
            {
                bool hasMouseMove = false, hasMousePress = false, hasKeyboard = false, hasScroll = false;
                if (inputManager)
                {
                    for (const auto& e : inputManager->GetRecordedEvents())
                    {
                        if (e.type == EventType::MouseMove)   hasMouseMove = true;
                        if (e.type == EventType::MouseDown)    hasMousePress = true;
                        if (e.type == EventType::KeyDown)      hasKeyboard = true;
                        if (e.type == EventType::MouseScroll)  hasScroll = true;
                    }
                }
                tooltipManager.BeginFrame(guiStateManager.Interaction().GetHoveredID(), guiStateManager.Interaction().GetFocusedID(),
                    elapsedTimeSecs, hasMouseMove, hasMousePress, hasKeyboard, hasScroll,
                    menuManager.IsOpen());

                // Any mouse activity ends keyboard-nav hover suppression.
                if (hasMouseMove || hasMousePress || hasScroll)
                    guiStateManager.Interaction().ClearHoverSuppression();
            }

            textSystem->BeginFrame(windowBackend->GetScreenMousePos(),
                                   windowBackend->GetMouseButtons(),
                                   windowBackend->GetTicks());
            VERIFY(elementStorage.BeginConstruction());
            windowRegistry.Clear();

            guiStateManager.Cursor().Clear();
            m_frame.RunStage(FrameStage::PreConstruct, ctx);
        }
        // ***


        {
            INTERSTICE_PROFILE_SCOPE("OnGUI");
            OnGUI();
        }

        {
            INTERSTICE_PROFILE_SCOPE("EndFrame");
            m_frame.RunStage(FrameStage::PostConstruct, ctx);
            
            tooltipManager.EndFrame();

            // *** END FRAME *** 

            if (guiStateManager.Drag().IsDragging())
            {
                DragVisualContext visualContext;
                visualContext.screenPos = windowManager->GetScreenMousePos();
                visualContext.sourceID = guiStateManager.Drag().GetDraggedID();
                visualContext.targetID = guiStateManager.Drag().GetDragOverID();
                visualContext.isDropValid = guiStateManager.Drag().GetValidatedTargetID() != interstice::ID::Invalid();
                visualContext.leftMouseDown = (windowManager->GetMouseButtons() & interstice::MouseButtons::Left) != interstice::MouseButtons::None;
                visualContext.payloadType = guiStateManager.Drag().GetEventPayloadType();
                visualContext.payloadValue = guiStateManager.Drag().GetEventPayloadValue();
                visualContext.payload = &guiStateManager.Drag().GetPayload();

                guiStateManager.Drag().InvokeActiveVisualizer(visualContext);
            }

            VERIFY(elementStorage.EndConstruction());

            tabChainManager.EndFrame();

            textSystem->Update();

            // Apply user-moved/resized OS window rects before layout.
            SyncWindowsBeforeLayout(*windowManager, windowRegistry, elementStorage);

            m_frame.RunStage(FrameStage::PreLayout, ctx);

            layoutEngine->Layout(*windowManager, elementStorage);

            // AddPostLayout callbacks: layout is final, render hasn't started.
            for (const auto& entry : elementStorage.GetPostLayoutEntries())
            {
                const auto* er = elementStorage.GetElementRectPtr(entry.ownerRef);
                if (!er) continue;
                PostLayoutContext plCtx{ elementStorage, entry.ownerRef,
                                         er->rect, er->innerRect, er->contentTransform };
                entry.callback(plCtx);
            }

            m_frame.RunStage(FrameStage::PostLayout, ctx);

            windowRegistry.BuildRenderSkipLinks();

            // Create/update OS windows for layout-driven windows.
            SyncWindowsAfterLayout(*windowManager, windowRegistry, elementStorage, textSystem, onGui);
            
            m_frame.RunStage(FrameStage::PreInput, ctx);

            if (inputManager)
                inputManager->UpdatePreHover(guiStateManager);

            // Captured before EndFrame to detect state changes.
            const ID prevHoverID   = guiStateManager.Interaction().GetHoveredID();
            const ID prevActiveID  = guiStateManager.Interaction().GetActiveID();
            const ID prevFocusedID = guiStateManager.Interaction().GetFocusedID();
            std::vector<ID> prevHoverProp   = guiStateManager.Interaction().GetPropagatedHoverIDs();
            std::vector<ID> prevFocusedProp = guiStateManager.Interaction().GetPropagatedFocusedIDs();

            const interstice::int2 mousePosition = windowManager->GetScreenMousePos();
            FrameInput frameInput
            {
                .activeWindowID   = windowManager->GetActiveWindow(),
                .modalWindowID    = windowManager->GetTopModalWindow(),
                .windowUnderMouse = windowManager->GetWindowUnderMouse(),
                .mousePos         = mousePosition,
                .mouseButtons     = windowManager->GetMouseButtons(),
                .hitTest = [this](ID window, int2 pos)
                {
                    const ElementRef windowRef = windowRegistry.GetWindowRef(window);
                    return windowRef != ElementRef::Invalid() ? elementStorage.GetElementIDAt(windowRef, pos) : ID::Invalid();
                },
            };
            if (textSystem)
                frameInput.textDragTarget = [this](ID window, int2 pos, ID& target)
                { return textSystem->TryGetTextDragHoverTarget(window, pos, target); };

            const FrameHover frameHover   = guiStateManager.EndFrame(frameInput, idManager);
            const interstice::ID overID        = frameHover.overID;
            const interstice::ID hoverID       = frameHover.hoverID;
            const interstice::ID hoverWindowID = frameHover.hoverWindowID;

            // Start transitions for state changes. Hover/Focused diff the leaf plus its
            // PropagatesDescendantState ancestors, so moving within such an ancestor doesn't retrigger it.
            const ID newHoverID   = guiStateManager.Interaction().GetHoveredID();
            const ID newActiveID  = guiStateManager.Interaction().GetActiveID();
            const ID newFocusedID = guiStateManager.Interaction().GetFocusedID();

            std::vector<ID> newHoverProp   = CollectPropagatedAncestors(elementStorage, newHoverID);
            std::vector<ID> newFocusedProp = CollectPropagatedAncestors(elementStorage, newFocusedID);

            auto withLeaf = [](std::vector<ID> v, ID leaf) {
                if (leaf != ID::Invalid()) v.push_back(leaf);
                return v;
            };
            const std::vector<ID> prevHoverEff   = withLeaf(prevHoverProp,   prevHoverID);
            const std::vector<ID> newHoverEff    = withLeaf(newHoverProp,    newHoverID);
            const std::vector<ID> prevFocusedEff = withLeaf(prevFocusedProp, prevFocusedID);
            const std::vector<ID> newFocusedEff  = withLeaf(newFocusedProp,  newFocusedID);

            // Nothing under the cursor anymore (widget vanished, window blur): end suppression.
            if (newHoverID == ID::Invalid() && prevHoverID != ID::Invalid())
                guiStateManager.Interaction().ClearHoverSuppression();

            // No hover transitions while keyboard-navigating: a keyboard scroll moves
            // elements under a stationary cursor, which would otherwise animate them.
            if (!guiStateManager.Interaction().IsHoverSuppressed())
            {
                FireEffectiveSetTransitions(prevHoverEff, newHoverEff,
                                            ElementState::Hover,
                                            elementStorage, transitionManager, elapsedTimeSecs);
            }

            FireEffectiveSetTransitions(prevFocusedEff, newFocusedEff,
                                        ElementState::Focused,
                                        elementStorage, transitionManager, elapsedTimeSecs);

            if (prevActiveID != newActiveID)
            {
                if (prevActiveID != ID::Invalid())
                {
                    ElementRef ref = elementStorage.FindElementRefByID(prevActiveID);
                    if (ref != ElementRef::Invalid())
                    {
                        ElementState oldState = elementStorage.GetElementUnsafe(ref).state;
                        ElementState newState = oldState & ~ElementState::Active;
                        transitionManager.OnStateChange(prevActiveID, oldState, newState, elapsedTimeSecs);
                    }
                }
                if (newActiveID != ID::Invalid())
                {
                    ElementRef ref = elementStorage.FindElementRefByID(newActiveID);
                    if (ref != ElementRef::Invalid())
                    {
                        ElementState oldState = elementStorage.GetElementUnsafe(ref).state;
                        ElementState newState = oldState | ElementState::Active;
                        transitionManager.OnStateChange(newActiveID, oldState, newState, elapsedTimeSecs);
                    }
                }
            }

            guiStateManager.Interaction().SetPropagatedHoverIDs(std::move(newHoverProp));
            guiStateManager.Interaction().SetPropagatedFocusedIDs(std::move(newFocusedProp));
            
            MouseCursor textDragCursor = MouseCursor::Default;
            const bool hasTextDragCursorOverride =
                (textSystem != nullptr) &&
                textSystem->TryGetTextDragCursor(hoverWindowID, mousePosition, textDragCursor);

            if (guiStateManager.Drag().IsDragging())
            {
                guiStateManager.Cursor().Request(MouseCursor::Default);
            } else
            if (hasTextDragCursorOverride)
            {
                guiStateManager.Cursor().Request(textDragCursor);
            } else
            {
                if (hoverID != ID::Invalid())
                {
                    // Default arrow over selected text (it can be dragged).
                    if (textSystem && textSystem->IsTextEditor(hoverID) &&
                        textSystem->IsHoveringSelectedText(hoverID, hoverWindowID, mousePosition))
                    {
                        guiStateManager.Cursor().Request(MouseCursor::Default);
                    } else
                    {
                        ElementRef ref = elementStorage.FindElementRefByID(hoverID);
                        ElementRef cur = ref;
                        while (cur.IsValid())
                        {
                            ElementRect& rect = elementStorage.GetElementRectUnsafe(cur);

                            if (rect.layoutStyle != nullptr)
                            {
                                int2 pos = mousePosition - rect.innerRect.position;
                                if (textSystem->HitTestClickable(hoverID, pos.x, pos.y))
                                {
                                    guiStateManager.Cursor().Request(MouseCursor::Pointer);
                                } else
                                    if (rect.layoutStyle->cursor != MouseCursor::Default)
                                        guiStateManager.Cursor().Request(rect.layoutStyle->cursor);
                                break;
                            }

                            cur = elementStorage.GetParentRef(cur);
                        }
                    }
                }
            }


            menuManager.EndFrame(elementStorage, guiStateManager);

            windowBackend->SetCursor(guiStateManager.Cursor().Get());

            if (inputManager)
                menuManager.CloseOnInputOutside(*inputManager, elementStorage, guiStateManager);

            InvalidateCachedScopesForInput(overID);

            if (inputManager)
            {
                inputManager->Process(guiStateManager, elementStorage,
                    tabChainManager, textSystem, overID);
            }

            // Keep the IME candidate window next to the caret being typed at.
            if (textSystem)
                if (const auto caret = textSystem->GetCaretScreenRect())
                    windowManager->SetTextInputArea(frameInput.activeWindowID, *caret);

            m_frame.RunStage(FrameStage::PostInput, ctx);

            EndDockingFrame();
        }

        m_frame.RunStage(FrameStage::PreRender, ctx);

        {
            INTERSTICE_PROFILE_SCOPE("OnRender");
            OnRender(elapsedTimeSecs);
        }

        m_frame.RunStage(FrameStage::PostRender, ctx);

        transitionManager.Purge(elapsedTimeSecs);

        m_frame.RunStage(FrameStage::EndFrame, ctx);

        idManager.EndFrame();

        if (!windowManager->EndFrame())
            return;

        // Quit once every user-facing window has closed; hidden cached popups don't count.
        if (!windowManager->HasAliveMainWindow())
        {
            applicationQuit = true;
        }
    }
}