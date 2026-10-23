#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cassert>
#include <span>
#include <functional>
#include <limits>
#include <string>

#include <gui_core/frame/gui_frame_clock.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/text/gui_clipboard.h>
#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/text/gui_text_system.h>
#include <gui_core/text/gui_text_atlas_manager.h>

#include <gui_core/windows/gui_os_window_manager.h>
#include <gui_core/windows/gui_window_registry.h>
#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/windows/gui_image_window_backend.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_manager.h>
#include <gui_core/input/gui_command_manager.h>
#include <gui_core/input/gui_input_handler_func.h>
#include <gui_core/input/gui_input_handler_broadcaster.h>

#include <gui_core/layout/gui_layout_engine.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/graphics/gui_custom_draw_context.h>

#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_tab_chain_manager.h>

#include <gui_core/frame/gui_frame_pipeline.h>

#include <gui_core/gui_id_manager.h>

#include <gui_controls/gui_transition_manager.h>
#include <gui_controls/gui_type_dimensions.h>
#include <gui_controls/gui_default_handlers.h>
#include <gui_controls/gui_tooltip_manager.h>
#include <gui_controls/gui_menu_manager.h>
#include <gui_controls/gui_dock_node.h>
#include <gui_controls/gui_table_helpers.h>
#include <gui_controls/gui_theme.h>

#include <gui_controls/gui_widget_state_types.h>
#include <gui_controls/gui_node_view_types.h>
#include <gui_controls/gui_table_types.h>
#include <gui_controls/gui_tree_grid_types.h>
#include <gui_controls/gui_docking_types.h>

#include <gui_controls/subsystems/gui_subsystem_state.h>
#include <gui_controls/subsystems/gui_label_group_subsystem.h>
#include <gui_controls/subsystems/gui_button_group_subsystem.h>
#include <gui_controls/subsystems/gui_color_picker_subsystem.h>
#include <gui_controls/subsystems/gui_gradient_editor_subsystem.h>
#include <gui_controls/subsystems/gui_reorderable_subsystem.h>
#include <gui_controls/subsystems/gui_ui_cache_subsystem.h>

namespace interstice
{
    void VERIFY(ElementStorageResult result);
    void VERIFY(RegistryResult result);

    // WindowUserClosed is accepted too: it's the expected outcome when a window can't be reopened.
    void VERIFY(AddWindowResult result);
    void VERIFY(RequestCloseResult result);


    class GUIApplication
    {
    public:
        std::unique_ptr<InputManager>     inputManager;
        std::unique_ptr<LayoutEngine>     layoutEngine;
        std::unique_ptr<OSWindowManager>  windowManager;
        std::unique_ptr<TextAtlasManager> textAtlasManager;
        std::unique_ptr<IClipboard>       clipboard;          // from SetClipboard

        ITextSystem*        textSystem      { nullptr };
        IWindowBackend*     windowBackend   { nullptr };   // SetWindowBackend's windows plus render-to-image windows
        GraphicsContext*    graphicsContext { nullptr };   // the backend from SetGraphicsBackend
        GUIRenderer*        guiRenderer     { nullptr };

        IDManager           idManager{};
        GUIStateManager     guiStateManager{};
        TransitionManager   transitionManager{};
        TabChainManager     tabChainManager{};

        MenuManager         menuManager{};
        TooltipManager      tooltipManager{};

    private:
        // Per-frame stage pipeline. Declared before the subsystems, which register
        // callbacks in it, so they are destroyed first.
        FramePipeline       m_frame{};

        std::unique_ptr<SliderFieldTrackSubsystem> m_sliderFieldTrack;
        std::unique_ptr<MenuRectsSubsystem>        m_menuRects;
        std::unique_ptr<LabelGroupSubsystem>       m_labelGroup;
        std::unique_ptr<ButtonGroupSubsystem>      m_buttonGroup;
        std::unique_ptr<ColorPickerSubsystem>      m_colorPickerSub;
        std::unique_ptr<GradientEditorSubsystem>   m_gradientEditorSub;
        std::unique_ptr<RenameSubsystem>           m_renameSub;
        std::unique_ptr<ReorderableSubsystem>      m_reorderable;
        std::unique_ptr<DockTabDragSubsystem>      m_dockTabDragSub;
        ToolbarWindowDragSubsystem                 m_toolbarWindowDrag{};
        std::unique_ptr<UICacheManager>            m_uiCache;

    public:
        GradientManager     gradientManager{};
        StyleManager        styleManager { gradientManager };

        ElementStorage      elementStorage { styleManager, &guiStateManager.GetElementDataStore() };
        WindowRegistry      windowRegistry {};

        CommandManager      commandManager {};

        InputHandlerBroadcaster inputBroadcaster {};

        OnGuiFunc           onGui           {};

        GUITheme theme{};


        bool applicationQuit = false;
        float frameTime = 0.0f;
        // CPU work per frame, excluding the vsync stall in present.
        float frameBuildTime = 0.0f;

        // Rolling per-frame timing history in ms, recorded only while profiling is enabled.
        struct FrameTimingSample { float buildMs = 0.0f; float totalMs = 0.0f; };
        static constexpr int kFrameHistoryCount = 240;   // ~4s @ 60fps

        // Copies the ring chronologically (oldest first) into out.
        void GetFrameTimingHistory(std::span<FrameTimingSample> out) const;


        void CreatePanel(std::unordered_map<interstice::ID, interstice::Panel>& panels, ID id, std::string title, interstice::IconID icon,
                         std::function<void()> onPanelGUI,
                         std::string panelType = {},
                         std::function<void()> onEnable = {},
                         std::function<void()> onDisable = {});

        interstice::ID CreatePanel(std::unordered_map<interstice::ID, interstice::Panel>& panels, std::string title, interstice::IconID icon,
                            std::function<void()> onPanelGUI,
                            std::string panelType = {},
                            std::function<void()> onEnable = {},
                            std::function<void()> onDisable = {})
        {
            ID id = idManager.CreateStaticID();
            CreatePanel(panels, id, std::move(title), icon, std::move(onPanelGUI),
                        std::move(panelType), std::move(onEnable), std::move(onDisable));
            return id;
        }


        // Panel lifecycle. A panel is enabled iff it is the selectedTab of a leaf reachable
        // from layout.root; the docking enables the visible ones each frame.
        // DisableAllPanels brackets disruptive operations (theme reload, preset swap).
        void DisableAllPanels(DockingLayout& layout);

        // One OS window per floating toolbar. Closed windows are erased and their IDs released.
        void EmitFloatingToolbarWindows(std::vector<struct FloatingToolbarItem>& floatingItems,
                                        const struct FloatingToolbarOptions* options = nullptr);

        // Dock leaf whose tab bar contains screenPos, plus its owning layout (needed by
        // TransferPanel for cross-layout drops). {nullptr, nullptr} on miss.
        DockLeafResult FindDockLeafAtScreenPos(interstice::int2 screenPos);

        // Hit-tests leaf content areas; falls back to tab bars with zone=Center.
        DockDropResult FindDockDropTargetAtScreenPos(interstice::int2 screenPos);

        // Call from OnGUI after the main docking container. Empty windows are closed.
        void EmitFloatingDockWindows();

        // Closes the window for good: its panels are disabled and destroyed.
        void CloseFloatingDockWindow(size_t index);

        // Close the panel at `tabIndex` of a docked leaf (the tab's Close action).
        void CloseDockedPanel(DockingLayout& layout, DockNode& leaf, int tabIndex);

        // Moves a panel from sourceNode/sourceLayout to targetLayout, fixing up selectedTab.
        static void TransferPanel(interstice::ID panelID, interstice::DockNode* sourceNode,
                                  DockingLayout* sourceLayout,
                                  DockingLayout* targetLayout);

                virtual ~GUIApplication() = default;

        // What Initialize sets up besides the core systems. All on by default; a host
        // that does one itself turns it off.
        struct InitializeOptions
        {
            bool loadStyles             = true;  // load the theme (InitializeStyles)
            bool commandShortcuts       = true;  // route keys to commandManager's shortcuts
            bool repaintWhileResizing   = true;  // onGui = OnFrame, unless onGui is already set
            bool closeMenusOnWindowMove = true;  // open menus close when a window moves or resizes
        };

        virtual bool Initialize(const InitializeOptions& options = {});
        virtual void Cleanup();

        // Loads m_themePath at m_globalFontScale. False when the theme can't be opened.
        bool InitializeStyles();

        // Called after every successful theme/font reload (and by subclasses at startup) to
        // re-register styles/icons the reload wiped. Reset any bookkeeping of stale StyleRefs.
        virtual void OnStylesReloaded() {}

        // Deferred to the start of the next frame; safe from event handlers.
        // fontScale multiplies every CSS font-size.
        void RequestReloadTheme(std::string_view themePath, float fontScale);
        const std::string& GetThemePath() const { return m_themePath; }

        // The folder the resources load from: compiled shaders, default fonts and
        // relative theme paths. Empty (the default) is the working directory; a
        // package user passes INTERSTICE_RESOURCE_DIR, which the package defines.
        // Call before Initialize.
        void SetResourceRoot(std::string_view root) { m_resourceRoot = std::string(root); }
        const std::string& GetResourceRoot() const { return m_resourceRoot; }

        // Input for render-to-image windows (WindowFlags::RenderToImage), which get
        // none from the OS. Positions are in the window's pixels. The window is under
        // the pointer from a mouse event until InjectMouseLeave, and has keyboard
        // focus from any injected input until InjectFocus(windowID, false).
        void InjectMouseMove(ID windowID, int2 pos, KeyModifier mods = KeyModifier::None);
        void InjectMouseButton(ID windowID, int2 pos, MouseButtons button, bool down, KeyModifier mods = KeyModifier::None);
        void InjectMouseScroll(ID windowID, int2 pos, float2 offset, KeyModifier mods = KeyModifier::None);
        void InjectMouseLeave(ID windowID);
        void InjectFocus(ID windowID, bool focused);
        void InjectKey(ID windowID, KeyCode key, KeyAction action, KeyModifier mods = KeyModifier::None);
        void InjectText(ID windowID, std::string_view utf8);

        // The last rendered frame of a render-to-image window: BGRA8, tightly packed
        // rows, bgra holding width × height × 4 bytes of its pixel size.
        bool ReadWindowPixels(ID windowID, std::span<uint8_t> bgra);

        // The graphics backend Initialize uses; without one it creates a
        // vulkan::VulkanGraphicsContext. Set it up through its own API first, for
        // example SetExternalDevice to render on the host's device. Owned until
        // Cleanup. Call before Initialize.
        void SetGraphicsBackend(std::unique_ptr<GraphicsContext> backend) { m_graphicsBackend = std::move(backend); }

        // The OS windows and their input; Initialize wraps it to add render-to-image
        // windows. Without one it creates an SDLWindowBackend (SDL builds only).
        // Owned until Cleanup. Call before Initialize.
        void SetWindowBackend(std::unique_ptr<IWindowBackend> backend) { m_osWindowBackend = std::move(backend); }
        // Copy and paste for text fields. Defaults to SDL's clipboard, or a
        // LocalClipboard without SDL. Owned until Cleanup. Call before Initialize.
        void SetClipboard(std::unique_ptr<IClipboard> clipboardImpl) { clipboard = std::move(clipboardImpl); }

        // Host memory functions for what can be routed: Vulkan objects, VMA,
        // vk-bootstrap and SDL (only when the GUI starts SDL). See AllocationHooks
        // for how long they must live. Call before Initialize.
        void SetAllocationHooks(const AllocationHooks& hooks) { m_allocationHooks = hooks; }

        // The CSS rule files loaded after the palette, in order; relative paths are
        // next to the palette file. Defaults to the library's "styles.css". An app
        // appends its own and finds their classes with theme.GetStyle. Call before
        // Initialize, or follow with RequestReloadTheme.
        void SetStyleFiles(std::span<const std::string_view> files);
        float GetGlobalFontScale() const { return m_globalFontScale; }

        // Extra UI zoom on top of the main window's display scale (1 = follow the
        // OS). Styles are registered at their product; may be called before Initialize.
        void  SetUIScale(float scale);
        float GetUIScale() const { return m_uiScale; }

        // Builds the UI; called each frame.
        virtual void OnGUI() = 0;

        bool Run();

        // ---- Event handler factories ----
        //
        // Event handlers run at END OF FRAME (input dispatch), not during OnGUI. Captures must
        // outlive the frame: user-owned pointers, GUIApplication members and copies are fine;
        // transient construction state (m_treeState, ...) and OnGUI stack locals are not.
        //
        interstice::EventHandler MakeToggleHandler(bool& toggle);
        interstice::EventHandler MakeButtonHandler(std::function<void()> onClick);
        interstice::EventHandler MakeLabelHandler();

        interstice::ElementRef RegisterElement(interstice::Element&& element, LayoutOverride&& constraints);
        interstice::ElementRef RegisterElement(interstice::Element&& element, std::string_view text, LayoutOverride&& constraints);


        interstice::ElementRef RegisterControl(interstice::Element&& element, LayoutOverride&& constraints);
        interstice::ElementRef RegisterControl(interstice::Element&& element, std::string_view text, LayoutOverride&& constraints);
        interstice::ElementRef RegisterControl(interstice::Element&& element, std::function<TextID(interstice::Element& element)> createTextID, LayoutOverride&& constraints);

        // Reusable interactive menu-item row: an optional fixed icon slot, a grow:1
        // label, and an optional trailing text (keyboard shortcut or submenu arrow).
        // The row container carries the event handler and the hover/focus/selected
        // highlight; its children are non-interactive so the whole row reads as one.
        // `reserveIcon` keeps the icon slot present even when `icon` is Invalid so
        // labels line up across a menu that mixes icon and icon-less items.
        interstice::ElementRef MenuItemRow(ID id, interstice::EventHandler handler, StyleRef rowStyle,
                                    bool reserveIcon, IconID icon, std::string_view text,
                                    std::string_view trailing, StyleRef trailingStyle,
                                    LayoutOverride&& constraints);

        void BeginIndeterminate() { guiStateManager.Construction().PushIndeterminate(); }
        void EndIndeterminate()   { guiStateManager.Construction().PopIndeterminate(); }

        bool        Button(ID id, IconID icon, std::string_view text, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
        inline bool Button(ID id, IconID icon, std::string_view text, std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(id,                            icon,            text,               std::move(onClick), theme.controls->button, std::move(constraints)); }
        inline bool Button(ID id, IconID icon,                        std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {}) { return Button(id,                            icon,            std::string_view{}, std::move(onClick), style,                  std::move(constraints)); }
        inline bool Button(ID id, IconID icon,                        std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(id,                            icon,            std::string_view{}, std::move(onClick), theme.controls->button, std::move(constraints)); }
        inline bool Button(ID id,              std::string_view text, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {}) { return Button(id,                            IconID::Invalid, text,               std::move(onClick), style,                  std::move(constraints)); }
        inline bool Button(ID id,              std::string_view text, std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(id,                            IconID::Invalid, text,               std::move(onClick), theme.controls->button, std::move(constraints)); }
        inline bool Button(       IconID icon, std::string_view text, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {}) { return Button(idManager.AllocateDynamicID(), icon,            text,               std::move(onClick), style,                  std::move(constraints)); }
        inline bool Button(       IconID icon, std::string_view text, std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(idManager.AllocateDynamicID(), icon,            text,               std::move(onClick), theme.controls->button, std::move(constraints)); }
        inline bool Button(       IconID icon,                        std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {}) { return Button(idManager.AllocateDynamicID(), icon,            std::string_view{}, std::move(onClick), style,                  std::move(constraints)); }
        inline bool Button(       IconID icon,                        std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(idManager.AllocateDynamicID(), icon,            std::string_view{}, std::move(onClick), theme.controls->button, std::move(constraints)); }
        inline bool Button(                    std::string_view text, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {}) { return Button(idManager.AllocateDynamicID(), IconID::Invalid, text,               std::move(onClick), style,                  std::move(constraints)); }
        inline bool Button(                    std::string_view text, std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(idManager.AllocateDynamicID(), IconID::Invalid, text,               std::move(onClick), theme.controls->button, std::move(constraints)); }

        // Icon button for a command; the tooltip shows its name and shortcut, e.g. "Undo (Ctrl+Z)".
        bool        CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
        inline bool CommandButton(CommandID command, LayoutOverride&& constraints = {}) { return CommandButton(command, theme.controls->button, std::move(constraints)); }

        bool        Toggle(ID id, bool value, IconID icon, std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {});
        inline bool Toggle(ID id, bool value, IconID icon, std::string_view text, std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            text,               onToggle, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(ID id, bool value, IconID icon,                        std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            std::string_view{}, onToggle, style,                  std::move(constraints)); }
        inline bool Toggle(ID id, bool value, IconID icon,                        std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            std::string_view{}, onToggle, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(ID id, bool value,              std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(id,                            value, IconID::Invalid, text,               onToggle, style,                  std::move(constraints)); }
        inline bool Toggle(ID id, bool value,              std::string_view text, std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(id,                            value, IconID::Invalid, text,               onToggle, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(       bool value, IconID icon, std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            text,               onToggle, style,                  std::move(constraints)); }
        inline bool Toggle(       bool value, IconID icon, std::string_view text, std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            text,               onToggle, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(       bool value, IconID icon,                        std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            std::string_view{}, onToggle, style,                  std::move(constraints)); }
        inline bool Toggle(       bool value, IconID icon,                        std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            std::string_view{}, onToggle, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(       bool value,              std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, IconID::Invalid, text,               onToggle, style,                  std::move(constraints)); }
        inline bool Toggle(       bool value,              std::string_view text, std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, IconID::Invalid, text,               onToggle, theme.controls->toggle, std::move(constraints)); }
        
        inline bool Toggle(ID id, bool& value, IconID icon, std::string_view text, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            text,               [&value](bool newValue){ value = newValue; }, style,                  std::move(constraints)); }
        inline bool Toggle(ID id, bool& value, IconID icon, std::string_view text,                 LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            text,               [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(ID id, bool& value, IconID icon,                        StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            std::string_view{}, [&value](bool newValue){ value = newValue; }, style,                  std::move(constraints)); }
        inline bool Toggle(ID id, bool& value, IconID icon,                                        LayoutOverride&& constraints = {}) { return Toggle(id,                            value, icon,            std::string_view{}, [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(ID id, bool& value,              std::string_view text, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(id,                            value, IconID::Invalid, text,               [&value](bool newValue){ value = newValue; }, style,                  std::move(constraints)); }
        inline bool Toggle(ID id, bool& value,              std::string_view text,                 LayoutOverride&& constraints = {}) { return Toggle(id,                            value, IconID::Invalid, text,               [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(       bool& value, IconID icon, std::string_view text, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            text,               [&value](bool newValue){ value = newValue; }, style,                  std::move(constraints)); }
        inline bool Toggle(       bool& value, IconID icon, std::string_view text,                 LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            text,               [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(       bool& value, IconID icon,                        StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            std::string_view{}, [&value](bool newValue){ value = newValue; }, style,                  std::move(constraints)); }
        inline bool Toggle(       bool& value, IconID icon,                                        LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, icon,            std::string_view{}, [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }
        inline bool Toggle(       bool& value,              std::string_view text, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, IconID::Invalid, text,               [&value](bool newValue){ value = newValue; }, style,                  std::move(constraints)); }
        inline bool Toggle(       bool& value,              std::string_view text,                 LayoutOverride&& constraints = {}) { return Toggle(idManager.AllocateDynamicID(), value, IconID::Invalid, text,               [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }

        bool        BeginMenu(ID id, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints = {});
        inline bool BeginMenu(ID id, std::string_view text, LayoutOverride&& constraints = {}) { return BeginMenu(id, text, theme.controls->menuTrigger, theme.menuStyleRef, theme.menuItemRowStyleRef, std::move(constraints)); }
        inline bool BeginMenu(       std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints = {}) { return BeginMenu(idManager.AllocateDynamicID(), text, buttonStyle, menuStyle, itemStyle, std::move(constraints)); }
        inline bool BeginMenu(       std::string_view text, LayoutOverride&& constraints = {}) { return BeginMenu(idManager.AllocateDynamicID(), text, theme.controls->menuTrigger, theme.menuStyleRef, theme.menuItemRowStyleRef, std::move(constraints)); }
        // Optional icon, label, optional shortcut. The icon column is reserved if any sibling has an icon.
        bool        MenuItem(ID id, IconID icon, std::string_view text, std::string_view shortcut, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});

        inline bool MenuItem(ID id, std::string_view text, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}) { return MenuItem(id, IconID::Invalid, text, std::string_view{}, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        inline bool MenuItem(       std::string_view text, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}) { return MenuItem(idManager.AllocateDynamicID(), IconID::Invalid, text, std::string_view{}, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }

        // onClick is required so MenuItem(text, nullptr) stays unambiguous.
        inline bool MenuItem(ID id, std::string_view text, std::string_view shortcut, std::function<void()> onClick, LayoutOverride&& constraints = {}) { return MenuItem(id, IconID::Invalid, text, shortcut, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        inline bool MenuItem(       std::string_view text, std::string_view shortcut, std::function<void()> onClick, LayoutOverride&& constraints = {}) { return MenuItem(idManager.AllocateDynamicID(), IconID::Invalid, text, shortcut, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }

        inline bool MenuItem(IconID icon, std::string_view text, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}) { return MenuItem(idManager.AllocateDynamicID(), icon, text, std::string_view{}, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        inline bool MenuItem(IconID icon, std::string_view text, std::string_view shortcut, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}) { return MenuItem(idManager.AllocateDynamicID(), icon, text, shortcut, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        inline bool MenuItem(ID id, IconID icon, std::string_view text, std::string_view shortcut, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}) { return MenuItem(id, icon, text, shortcut, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }

        // Menu item for a command: icon, label, shortcut and callback come from the command.
        bool MenuItem(CommandID command);
        void EndMenu();

        // Menu bar: place BeginMenu triggers inside. Left/Right move between triggers,
        // Down enters the dropdown, Up at its first item returns to the trigger.
        bool        BeginMenuBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginMenuBar(ID id, LayoutOverride&& constraints = {}) { return BeginMenuBar(id, theme.menuBarStyleRef, std::move(constraints)); }
        inline bool BeginMenuBar(       LayoutOverride&& constraints = {}) { return BeginMenuBar(idManager.AllocateDynamicID(), theme.menuBarStyleRef, std::move(constraints)); }
        void EndMenuBar();

        // Divider inside a menu or context menu.
        void MenuSeparator();

        // Tooltip: simple text (optionally with icon). Refers to the last registered element.
        void SetTooltip(ID targetID, std::string_view text);
        void SetTooltip(ID targetID, IconID icon, std::string_view text);
        inline void SetTooltip(std::string_view text) { SetTooltip(elementStorage.GetLastAddedID(), text); }
        inline void SetTooltip(IconID icon, std::string_view text) { SetTooltip(elementStorage.GetLastAddedID(), icon, text); }

        // Tooltip: arbitrary content. Refers to the last registered element.
        bool BeginTooltip(ID targetID);
        inline bool BeginTooltip() { return BeginTooltip(elementStorage.GetLastAddedID()); }
        void EndTooltip();

        // Context menu: opens on right-click of the target element.
        bool BeginContextMenu(ID targetID);
        inline bool BeginContextMenu() { return BeginContextMenu(elementStorage.GetLastAddedID()); }
        void EndContextMenu();

        // Label group: Labels/DragLabels inside share one width, resizable by dragging
        // any label's right edge. Widths in CSS pixels.
        void        BeginLabelGroup(ID id, int32_t defaultWidth = 120, int32_t minWidth = 40, int32_t maxWidth = 500, bool resizable = true);
        inline void BeginLabelGroup(int32_t defaultWidth = 120, int32_t minWidth = 40, int32_t maxWidth = 500, bool resizable = true) { BeginLabelGroup(idManager.AllocateDynamicID(), defaultWidth, minWidth, maxWidth, resizable); }
        void        EndLabelGroup();

        // Button / toggle group: Buttons / Field(bool) inside are restyled with
        // .first-/.mid-/.last-* so they share borders. A single item keeps its plain style.
        void        BeginButtonGroup(LayoutOverride&& constraints = {});
        void        EndButtonGroup();
        void        BeginToggleGroup(LayoutOverride&& constraints = {});
        void        EndToggleGroup();
        // Area: scrollable container for grouping any content.
        bool        BeginArea(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginArea(ID id,                 LayoutOverride&& constraints = {}) { return BeginArea(id, theme.collectionStyleRef, std::move(constraints)); }
        inline bool BeginArea(       StyleRef style, LayoutOverride&& constraints = {}) { return BeginArea(idManager.AllocateDynamicID(), style, std::move(constraints)); }
        inline bool BeginArea(                       LayoutOverride&& constraints = {}) { return BeginArea(idManager.AllocateDynamicID(), theme.collectionStyleRef, std::move(constraints)); }
        void EndArea();

        // Cached scope: a grouping container (like BeginArea) whose rendered output is
        // retained. On a hit the body is skipped and the cached primitives are drawn.
        // Call EndCached only on a miss (or use the Cached() lambda form):
        //     if (app.BeginCached(id)) { ...build...; app.EndCached(); }
        //
        // `id` must be STABLE across frames (static, or salted dynamic). Bump `signature`
        // (or call Invalidate(id)) when the content depends on data the cache can't observe.
        // A scope rebuilds on size change and when input reaches it (cursor, focus).
        bool        BeginCached(ID id, StyleRef style, uint64_t signature = 0, LayoutOverride&& constraints = {});
        inline bool BeginCached(ID id, uint64_t signature = 0, LayoutOverride&& constraints = {}) { return BeginCached(id, theme.collectionStyleRef, signature, std::move(constraints)); }
        void        EndCached();
        template<class Fn> inline void Cached(ID id, uint64_t signature, Fn&& fn) { if (BeginCached(id, signature)) { fn(); EndCached(); } }
        template<class Fn> inline void Cached(ID id, Fn&& fn)                      { if (BeginCached(id, 0u))        { fn(); EndCached(); } }
        // Rebuild id's cached scope (and enclosing ones) next time. Safe from event handlers.
        void        Invalidate(ID id);

        // Enables the retained-render cache (on by default). When off, every scope rebuilds.
        void        SetDrawCacheEnabled(bool enabled) { if (m_uiCache) m_uiCache->SetDrawCacheEnabled(enabled); }

        // Invalidates a docked panel's cached content by panel id. Safe from event handlers.
        void        InvalidatePanel(ID panelId);

        // Undecorated flex containers; the Wrap variants wrap onto further lines/columns.
        bool        BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginHorizontal(ID id,                 LayoutOverride&& constraints = {}) { return BeginHorizontal(id, theme.emptyHorzStyleRef, std::move(constraints)); }
        inline bool BeginHorizontal(       StyleRef style, LayoutOverride&& constraints = {}) { return BeginHorizontal(idManager.AllocateDynamicID(), style, std::move(constraints)); }
        inline bool BeginHorizontal(                       LayoutOverride&& constraints = {}) { return BeginHorizontal(idManager.AllocateDynamicID(), theme.emptyHorzStyleRef, std::move(constraints)); }
        void EndHorizontal();

        bool        BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginHorizontalWrap(ID id,                 LayoutOverride&& constraints = {}) { return BeginHorizontalWrap(id, theme.emptyHorzWrapStyleRef, std::move(constraints)); }
        inline bool BeginHorizontalWrap(       StyleRef style, LayoutOverride&& constraints = {}) { return BeginHorizontalWrap(idManager.AllocateDynamicID(), style, std::move(constraints)); }
        inline bool BeginHorizontalWrap(                       LayoutOverride&& constraints = {}) { return BeginHorizontalWrap(idManager.AllocateDynamicID(), theme.emptyHorzWrapStyleRef, std::move(constraints)); }
        void EndHorizontalWrap();

        bool        BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginVertical(ID id,                 LayoutOverride&& constraints = {}) { return BeginVertical(id, theme.emptyVertStyleRef, std::move(constraints)); }
        inline bool BeginVertical(       StyleRef style, LayoutOverride&& constraints = {}) { return BeginVertical(idManager.AllocateDynamicID(), style, std::move(constraints)); }
        inline bool BeginVertical(                       LayoutOverride&& constraints = {}) { return BeginVertical(idManager.AllocateDynamicID(), theme.emptyVertStyleRef, std::move(constraints)); }
        void EndVertical();

        bool        BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginVerticalWrap(ID id,                 LayoutOverride&& constraints = {}) { return BeginVerticalWrap(id, theme.emptyVertWrapStyleRef, std::move(constraints)); }
        inline bool BeginVerticalWrap(       StyleRef style, LayoutOverride&& constraints = {}) { return BeginVerticalWrap(idManager.AllocateDynamicID(), style, std::move(constraints)); }
        inline bool BeginVerticalWrap(                       LayoutOverride&& constraints = {}) { return BeginVerticalWrap(idManager.AllocateDynamicID(), theme.emptyVertWrapStyleRef, std::move(constraints)); }
        void EndVerticalWrap();

        // Toolbar scope: a wrap container that swaps theme.controls to the toolbar styles,
        // so default-style widgets inside use their toolbar variants.
        void BeginToolbar(bool horizontal = true);
        void EndToolbar();

        // ---- Scrollbars ----
        // Call from INSIDE the scrollable container `containerID`: the bar overlays
        // its border box, unaffected by its scroll offset. Paints nothing without
        // overflow, so it can always be emitted.
        void VerticalScrollbar  (ID scrollBarID, ID containerID);
        void VerticalScrollbar  (ID containerID) { (void)idManager.PushParent(containerID); const ID barID = idManager.AllocateDynamicID("vertical_scrollbar"); (void)idManager.PopParent(); VerticalScrollbar(barID, containerID); }
        void HorizontalScrollbar(ID scrollBarID, ID containerID);
        void HorizontalScrollbar(ID containerID) { (void)idManager.PushParent(containerID); const ID barID = idManager.AllocateDynamicID("horizontal_scrollbar"); (void)idManager.PopParent(); HorizontalScrollbar(barID, containerID); }

        // Both bars plus a corner cap; each hides itself without overflow. `lastInnerSize`
        // (the container's last-frame inner size) trims the bars around the cap.
        void ScrollbarOverlay(ID containerID,
                              int2 lastInnerSize = int2{0, 0},
                              ID vertScrollBarID = ID::Invalid(),
                              ID horzScrollBarID = ID::Invalid(),
                              bool emitVert = true,
                              bool emitHorz = true);

        // ---- Reorderable Group: drag-to-reorder container ----
        // onReorder(fromIndex, toIndex) fires on drop. isWrapped allows multi-line layout.
        // onDragOut(index, screenPos): an item was released outside the group.
        // dragVisualizer(index, screenPos): each frame while dragging outside the group,
        // typically to draw a ghost window; suppresses the in-place cross-axis offset.
        bool        BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped, std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style, LayoutOverride&& constraints = {}, std::function<void(int, interstice::int2)> onDragOut = nullptr, std::function<void(int, interstice::int2)> dragVisualizer = nullptr);
        inline bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style, LayoutOverride&& constraints = {})
        { return BeginReorderableGroup(id, direction, false, std::move(onReorder), style, std::move(constraints)); }
        inline bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder, LayoutOverride&& constraints = {})
        {
            return BeginReorderableGroup(id, direction, false, std::move(onReorder),
                direction == ReorderDirection::Horizontal ? theme.horzReorderableGroupStyleRef : theme.vertReorderableGroupStyleRef,
                std::move(constraints));
        }
        inline bool BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped, std::function<void(int fromIndex, int toIndex)> onReorder, LayoutOverride&& constraints = {})
        {
            StyleRef style;
            if (isWrapped)
                style = direction == ReorderDirection::Horizontal ? theme.horzReorderableGroupWrapStyleRef : theme.vertReorderableGroupWrapStyleRef;
            else
                style = direction == ReorderDirection::Horizontal ? theme.horzReorderableGroupStyleRef : theme.vertReorderableGroupStyleRef;
            return BeginReorderableGroup(id, direction, isWrapped, std::move(onReorder), style, std::move(constraints));
        }
        void EndReorderableGroup();

        void        BeginReorderableItem(ID id, StyleRef style, LayoutOverride&& constraints = {});
        inline void BeginReorderableItem(ID id, LayoutOverride&& constraints = {}) { BeginReorderableItem(id, theme.reorderableItemStyleRef, std::move(constraints)); }
        inline void BeginReorderableItem(StyleRef style, LayoutOverride&& constraints = {}) { BeginReorderableItem(idManager.AllocateDynamicID(), style, std::move(constraints)); }
        inline void BeginReorderableItem(LayoutOverride&& constraints = {}) { BeginReorderableItem(idManager.AllocateDynamicID(), theme.reorderableItemStyleRef, std::move(constraints)); }
        void EndReorderableItem();

        // Non-interactive drag handle for use inside BeginReorderableItem; clicks reach the item.
        void EmitDragHandle();

        // ---- ListView: scrollable list with selectable items ----
        bool        BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style, LayoutOverride&& constraints = {});
        inline bool BeginListView(ID id, int* selectedIndex, int itemCount, StyleRef style, LayoutOverride&& constraints = {}) { return BeginListView(id, selectedIndex, itemCount, nullptr, style, std::move(constraints)); }
        inline bool BeginListView(ID id, int* selectedIndex, int itemCount, LayoutOverride&& constraints = {}) { return BeginListView(id, selectedIndex, itemCount, nullptr, theme.listViewStyleRef, std::move(constraints)); }
        inline bool BeginListView(       int* selectedIndex, int itemCount, LayoutOverride&& constraints = {}) { return BeginListView(idManager.AllocateDynamicID(), selectedIndex, itemCount, nullptr, theme.listViewStyleRef, std::move(constraints)); }
        inline bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, LayoutOverride&& constraints = {}) { return BeginListView(id, selectedIndex, itemCount, dragDrop, theme.listViewStyleRef, std::move(constraints)); }
        inline bool BeginListView(       int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, LayoutOverride&& constraints = {}) { return BeginListView(idManager.AllocateDynamicID(), selectedIndex, itemCount, dragDrop, theme.listViewStyleRef, std::move(constraints)); }
        void EndListView();

        int FindListItemIndex(interstice::ID id) const;

        bool        ListItem(ID id, std::string_view text, IconID icon = IconID::Invalid, StyleRef style = {}, LayoutOverride&& constraints = {});
        inline bool ListItem(ID id, std::string_view text, LayoutOverride&& constraints = {}) { return ListItem(id, text, IconID::Invalid, theme.listItemStyleRef, std::move(constraints)); }
        inline bool ListItem(       std::string_view text, IconID icon, LayoutOverride&& constraints = {}) { return ListItem(idManager.AllocateDynamicID(), text, icon, theme.listItemStyleRef, std::move(constraints)); }
        inline bool ListItem(       std::string_view text, LayoutOverride&& constraints = {}) { return ListItem(idManager.AllocateDynamicID(), text, IconID::Invalid, theme.listItemStyleRef, std::move(constraints)); }


        // Recomputes skipCount / ancestorLineMask / isLastSibling from row depths.
        // Call after any structural change. O(n²) worst case.
        static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);

        // Writes the flat indices of visible rows (honoring isOpen) to visibleIdx.
        static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows,
                                           std::vector<int>& visibleIdx);

        // Single-column virtualized tree view (a VirtualTable with one TreeCell column).
        // A non-empty header shows the header row.
        void VirtualTreeView(ID id,
                             std::vector<TreeViewRow>& rows,
                             std::vector<int>& visibleIdx,
                             TableState& state,
                             StyleRef style = {},
                             LayoutOverride&& constraints = {},
                             std::string_view header = {});

        // The tree row a drag is over (drives the drop indicator), or Invalid.
        ID TreeDropTargetRow() const { return m_treeDropState.targetRowID; }

        // Items emitted in between render as selected.
        void BeginSelected() { guiStateManager.Construction().PushSelected(); }
        void EndSelected()   { guiStateManager.Construction().PopSelected(); }

        // ---- GridView: 2D icon grid (like file explorer) ----
        bool        BeginGridView(ID id, int* selectedIndex, StyleRef style, MultiSelectState* multiSelect = nullptr, ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {});
        inline bool BeginGridView(ID id, int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr, ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {}) { return BeginGridView(id, selectedIndex, theme.gridViewStyleRef, multiSelect, dragDrop, std::move(constraints)); }
        void EndGridView();

        // renameBuffer (optional) enables in-place rename: F2, Ctrl+Shift+Click or a slow
        // re-click opens an editor. Enter commits; Escape or focus loss cancels.
        bool        GridViewItem(ID id, std::string_view text, IconID icon = IconID::Invalid,
                                 interstice::ElementState extraState = interstice::ElementState::None,
                                 StyleRef itemStyle = {},
                                 LayoutOverride&& constraints = {},
                                 std::string* renameBuffer = nullptr);
        inline bool GridViewItem(std::string_view text, IconID icon = IconID::Invalid, LayoutOverride&& constraints = {})
            { return GridViewItem(idManager.AllocateDynamicID(), text, icon, interstice::ElementState::None, {}, std::move(constraints)); }


        // generateItem(index, itemID) must emit exactly one cell, typically GridViewItem(itemID, ...).
        // itemID is a stable ranged sub-ID (index = id.value.subRangeParts.index), used for
        // selection and drag payloads. Cells are cellSize, with a 4px gap between columns.
        void VirtualGridView(ID id, GridViewVirtualState& vstate,
                             int itemCount, int2 cellSize,
                             const std::function<void(int index, ID itemID)>& generateItem,
                             int* selectedIndex = nullptr,
                             MultiSelectState* multiSelect = nullptr,
                             ItemDragDropConfig* dragDrop = nullptr,
                             StyleRef scrollStyle = {},
                             StyleRef rowStyle = {},
                             LayoutOverride&& constraints = {});

        // ---- Table: row × column data grid ----
        //
        // Columns are described each frame; widths, selection and sort state persist in
        // the user-owned TableState. Rows use BeginRow/EndRow with Cell(text) or Cell(lambda).
        // Cells are recorded and drained by EndTable through the VirtualTable emit path.
        //
        bool  BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount,
                         TableState& state, StyleRef style,
                         LayoutOverride&& constraints = {});
        inline bool BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount,
                               TableState& state, LayoutOverride&& constraints = {})
            { return BeginTable(id, columns, columnCount, state, theme.tableStyleRef, std::move(constraints)); }
        template <size_t N>
        inline bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state,
                               StyleRef style, LayoutOverride&& constraints = {})
            { return BeginTable(id, columns, N, state, style, std::move(constraints)); }
        template <size_t N>
        inline bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state,
                               LayoutOverride&& constraints = {})
            { return BeginTable(id, columns, N, state, theme.tableStyleRef, std::move(constraints)); }
        void EndTable();

        void BeginRow();
        void EndRow();

        // Cells auto-advance to the next column.
        void Cell(std::string_view text);

        // Custom-content cell: `emit` runs inside the cell during EndTable, so its
        // captures must live until then.
        void Cell(std::function<void()> emit);

        // Tree-style first column: [indent][arrow or spacer][optional icon][text].
        //
        //   depth              horizontal indent (pixels = depth * theme.treeTableIndent).
        //   isOpen             null for leaves (no arrow). Non-null toggles on click.
        //   icon               optional leading icon rendered between arrow and text.
        //   ancestorLineMask   bit d set iff the ancestor at depth d+1 has a next sibling
        //                      (its guide line passes through this row at column d).
        //   isLastSibling      true → own-depth L-tick stops at row center.
        //   renameBuffer       optional; enables in-place rename (F2 / Ctrl+Shift+Click / slow re-click).
        //   reserveExpander    false drops the arrow slot for leaves (for flat trees).
        //
        // Arrow clicks toggle without selecting; other clicks select the row.
        void TreeCell(std::string_view text, int depth, bool* isOpen,
                      IconID icon = IconID::Invalid,
                      uint32_t ancestorLineMask = 0,
                      bool isLastSibling = true,
                      std::string* renameBuffer = nullptr,
                      bool reserveExpander = true);

        // Virtual-scrolled table: emits only the visible rows, under one ranged ID.
        // Frozen columns get their own body section (left / mid / right), scroll-synced.
        //
        //   columns       : each carries its own generateCell(rowIndex), so reordering
        //                   the array reorders headers and cells together. A C array
        //                   converts to the span.
        //   rowHeight     : row-height floor; 0 uses the style's natural row height.
        //   showHeaders   : false omits the header row (and with it reorder, resize and sort).
        //   cellStyle     : body-cell style override (default theme.tableCellStyleRef);
        //                   the row height follows it.
        void VirtualTable(ID id, TableState& state,
                          std::span<VirtualTableColumn> columns,
                          int totalRowCount,
                          int32_t rowHeight = 0,
                          StyleRef style = {},
                          LayoutOverride&& constraints = {},
                          bool showHeaders = true,
                          StyleRef cellStyle = {},
                          bool columnSeparators = true);

        // ---- Breadcrumb: a path such as Graphs > Terrain material ----
        // Segments are separated by chevrons; the last is the current one. Clicking another
        // segment calls onSelect(index) and returns true that frame.
        bool        Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style, LayoutOverride&& constraints = {});
        inline bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, LayoutOverride&& constraints = {}) { return Breadcrumb(id, segments, std::move(onSelect), theme.breadcrumbStyleRef, std::move(constraints)); }
        inline bool Breadcrumb(       std::span<const std::string> segments, std::function<void(int)> onSelect, LayoutOverride&& constraints = {}) { return Breadcrumb(idManager.AllocateDynamicID(), segments, std::move(onSelect), theme.breadcrumbStyleRef, std::move(constraints)); }

        // ---- Node view: self-contained graph editor ----
        // All state lives in `view`, so several views can coexist.
        void RenderNodeView(NodeView& view);

        // Text property body aligned by the row's sockets: left ⇒ left, right ⇒ right,
        // both ⇒ centre. Call inside NodeProperty::renderBody.
        void NodeText(std::string_view text);

        // ---- TabBar: reorderable tabs ----
        bool BeginTabBar(ID id, int* selectedIndex,
                         std::function<void(int, int)> onReorder,
                         std::function<void(int)> onClose = nullptr,
                         std::function<void(int, interstice::int2)> onDragOut = nullptr,
                         std::function<void(int, interstice::int2)> dragVisualizer = nullptr,
                         StyleRef style = {},
                         LayoutOverride&& constraints = {});
        void EndTabBar();

        bool Tab(ID id, std::string_view text, IconID icon = IconID::Invalid, LayoutOverride&& constraints = {});
        inline bool Tab(std::string_view text, IconID icon = IconID::Invalid, LayoutOverride&& constraints = {})
            { return Tab(idManager.AllocateDynamicID(), text, icon, std::move(constraints)); }

        // ---- DockingContainer ----
        void BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints = {});
        void EndDockingContainer();

        bool        Field(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
        inline bool Field(       bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {}) { return Field(idManager.AllocateDynamicID(), value, text, style, std::move(constraints)); }
        inline bool Field(ID id, bool& value, std::string_view text,                 LayoutOverride&& constraints = {}) { return Field(id,                            value, text, theme.controls->toggle, std::move(constraints)); }
        inline bool Field(       bool& value, std::string_view text,                 LayoutOverride&& constraints = {}) { return Field(idManager.AllocateDynamicID(), value, text, theme.controls->toggle, std::move(constraints)); }

        bool        Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints = {});
        inline bool Field(       std::string& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints = {}) { return Field(idManager.AllocateDynamicID(), value, textEditBehavior, style, std::move(constraints)); }
        inline bool Field(ID id, std::string& value, StyleRef style, LayoutOverride&& constraints = {}) { return Field(id, value, TextEditBehavior::ContinuousUpdate, style, std::move(constraints)); }
        inline bool Field(       std::string& value, StyleRef style, LayoutOverride&& constraints = {}) { return Field(idManager.AllocateDynamicID(), value, TextEditBehavior::ContinuousUpdate, style, std::move(constraints)); }
        inline bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}) { return Field(id,                            value, textEditBehavior, theme.controls->textbox, std::move(constraints)); }
        inline bool Field(       std::string& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}) { return Field(idManager.AllocateDynamicID(), value, textEditBehavior, theme.controls->textbox, std::move(constraints)); }
        inline bool Field(ID id, std::string& value, LayoutOverride&& constraints = {}) { return Field(id,                            value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, std::move(constraints)); }
        inline bool Field(       std::string& value, LayoutOverride&& constraints = {}) { return Field(idManager.AllocateDynamicID(), value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, std::move(constraints)); }

        // A Field with a leading icon inside its border.
        bool        SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior = TextEditBehavior::ContinuousUpdate, LayoutOverride&& constraints = {});
        inline bool SearchField(       std::string& value, IconID icon, TextEditBehavior textEditBehavior = TextEditBehavior::ContinuousUpdate, LayoutOverride&& constraints = {}) { return SearchField(idManager.AllocateDynamicID(), value, icon, textEditBehavior, std::move(constraints)); }




        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, TextEditBehavior textEditBehavior, StyleRef style, NumericFieldConfig<ComponentT> config = {}, LayoutOverride&& constraints = {})
        {
            if (guiStateManager.Construction().IsIndeterminate())
            {
                RegisterControl(interstice::Element { .id = id, .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                    [this](interstice::Element& element) { return textSystem->RegisterText(element.id, "\xe2\x80\x94", element.styleRef, element.state); },
                    std::move(constraints));
                return false;
            }

			if constexpr (TypeDimensions<T>::dimensions == 1)
            {
                // Inside a node row, fill the body after the label so the node stays grid-aligned.
                if (nodeRowFieldMinWidth > 0 && !constraints.HasFixedWidth())
                    constraints.FlexGrow(1).MinWidth(nodeRowFieldMinWidth);
                bool modified = false;
                RegisterControl(interstice::Element { .id = id, .eventHandler = MakeNumericalBoxHandler(guiStateManager, value, config), .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                                [this, &modified, &value, textEditBehavior, config](interstice::Element& element) { return textSystem->RegisterEditableNumber(element.id, value, textEditBehavior, element.styleRef, element.state, modified, config); },
                                std::move(constraints));
                guiStateManager.SetModified(modified);
                return modified;
            } else
            {
                bool modified = false;
                // Width 0 + flex-grow: the group fills remaining space after the label.
                constraints.Width(0);
                RegisterElement(interstice::Element{ .id = id, .eventHandler = MakeMultiDimDragHandler<T>(guiStateManager, value, config), .styleRef = theme.fieldElementGroupStyleRef }, std::move(constraints));
                VERIFY(elementStorage.BeginChildren());
                {
					for (int i = 0; i < TypeDimensions<T>::dimensions; i++)
                    {
                        DragLabel(TypeDimensions<T>::names[i], 
                            LayoutOverride::FixedWidth(styleManager.Px(28)));
                        modified = Field(TypeDimensions<T>::GetComponent(value, i), textEditBehavior, style, config, LayoutOverride::Auto()
                            .MinWidth(styleManager.Px(40))) || modified;
                    }
                }
                VERIFY(elementStorage.EndChildren());
                return modified;
            }
        }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, TextEditBehavior textEditBehavior, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}) { return Field<T>(id, value, textEditBehavior, theme.controls->textbox, config, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}) { return Field<T>(id, value, textEditBehavior, theme.controls->textbox, {}, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, LayoutOverride&& constraints = {}) { return Field<T>(id, value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, {}, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, StyleRef style, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, textEditBehavior, style, config, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(T& value, StyleRef style, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, TextEditBehavior::ContinuousUpdate, style, config, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, textEditBehavior, style, {}, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, textEditBehavior, theme.controls->textbox, config, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(T& value, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, config, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, textEditBehavior, theme.controls->textbox, {}, std::move(constraints)); }
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, LayoutOverride&& constraints = {}) { return Field<T>(idManager.AllocateDynamicID(), value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, {}, std::move(constraints)); }
        

        // Reorderable list of fields with remove buttons and a trailing add button.
        // groupID must be stable so reorder state survives across frames.
        template<typename T>
        void Field(ID groupID, std::vector<T>& items, StyleRef style, LayoutOverride layoutOverride = {})
        {
            if (BeginArea(style, std::move(layoutOverride)))
            {
                auto onReorder = [&items](int from, int to)
                {
                    if (from < 0 || from >= (int)items.size()) return;
                    if (to   < 0 || to   >= (int)items.size()) return;
                    T moved = std::move(items[from]);
                    items.erase(items.begin() + from);
                    items.insert(items.begin() + to, std::move(moved));
                };

                if (BeginReorderableGroup(groupID, ReorderDirection::Vertical,
                                          std::move(onReorder),
                                          interstice::LayoutOverride{}.FlexGrow(1)))
                {
                    for (int i = 0; i < (int)items.size(); i++)
                    {
                        BeginReorderableItem(idManager.AllocateDynamicID(),
                                             interstice::LayoutOverride{}.FlexGrow(1));
                        {
                            EmitDragHandle();
                            Field(items[i], interstice::LayoutOverride{}.FlexGrow(1));
                            Button(theme.removeIcon, [&items, i]() { items.erase(items.begin() + i); },
                                theme.iconButtonStyleRef, interstice::LayoutOverride{}.NoGrow());
                        }
                        EndReorderableItem();
                    }
                    EndReorderableGroup();
                }
                Button(theme.addIcon, [&items]() { items.push_back({}); },
                       theme.iconButtonEndStyleRef, interstice::LayoutOverride{}.NoGrow());
                EndArea();
            }
        }
        
        template<typename T>
        void Field(ID groupID, std::vector<T>& items, LayoutOverride layoutOverride = {}) { Field(groupID, items, theme.collectionAreaStyleRef, layoutOverride); }

        // Key-value list (std::string keys, V values) with add/remove. Each row
        // is "[key] : [value] [remove]" glued onto one line via KeepWithNext. Not
        // reorderable — add a reorderable variant when an application needs it.
        template<typename V>
        void Field(std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride layoutOverride = {})
        {
            if (BeginArea(style, std::move(layoutOverride)))
            {
                for (int i = 0; i < (int)items.size(); i++)
                {
                    Field(items[i].first,  interstice::LayoutOverride{}.FlexGrow(1).KeepWithNext());
                    Text(":",              interstice::LayoutOverride{}.NoGrow()   .KeepWithNext());
                    Field(items[i].second, interstice::LayoutOverride{}.FlexGrow(1).KeepWithNext());
                    Button(theme.removeIcon, [&items, i]() { items.erase(items.begin() + i); },
                        theme.iconButtonStyleRef, interstice::LayoutOverride{}.NoGrow());
                }
                Button(theme.addIcon, [&items]() { items.push_back({ std::string{}, V{} }); },
                    theme.iconButtonEndStyleRef, interstice::LayoutOverride{}.NoGrow());
                EndArea();
            }
        }
        template<typename V>
        void Field(std::vector<std::pair<std::string, V>>& items, LayoutOverride layoutOverride = {}) { Field(items, theme.collectionAreaStyleRef, layoutOverride); }

        
		// Key-value list over an unordered_map. Row order can change when the map rehashes;
        // use the vector-of-pairs version for stable rows.
        template<typename V>
        void Field(std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride layoutOverride = {})
        {
            if (BeginArea(style, std::move(layoutOverride)))
            {
                // Map keys are const: edit a copy, committed on Enter/blur, and re-key
                // after the loop (renaming onto an existing key is ignored).
                std::string renameFrom, renameTo;
                bool        rename = false;
                for (auto& pair : items)
                {
                    std::string key = pair.first;
                    if (Field(key, TextEditBehavior::None, interstice::LayoutOverride{}.FlexGrow(1).KeepWithNext()) && key != pair.first)
                    {
                        renameFrom = pair.first;
                        renameTo   = key;
                        rename     = true;
                    }
                    Text(":", interstice::LayoutOverride{}.NoGrow().KeepWithNext());
                    Field(pair.second, interstice::LayoutOverride{}.FlexGrow(1).KeepWithNext());
                    Button(theme.removeIcon, [&items, k = pair.first]() { items.erase(k); },
                        theme.iconButtonStyleRef, interstice::LayoutOverride{}.NoGrow());
                }
                if (rename && !items.contains(renameTo))
                {
                    auto node = items.extract(renameFrom);
                    node.key() = renameTo;
                    items.insert(std::move(node));
                }
                Button(theme.addIcon, [&items]() { items.emplace(std::string{}, V{}); },
                    theme.iconButtonEndStyleRef, interstice::LayoutOverride{}.NoGrow());
                EndArea();
            }
        }
        template<typename V>
        void Field(std::unordered_map<std::string, V>& items, LayoutOverride layoutOverride = {}) { Field(items, theme.collectionAreaStyleRef, layoutOverride); }


        // ---- ColorField: swatch + color picker popup ----
        bool ColorField(interstice::color4f& value, bool allowHDR = false, LayoutOverride&& constraints = {});
        bool ColorField(interstice::color4b& value, LayoutOverride&& constraints = {});

        // ---- GradientField: gradient swatch + editor popup ----
        bool GradientField(interstice::Gradient& value, LayoutOverride&& constraints = {});

        // ---- SliderField: DragLabel + horizontal slider track + numeric textbox ----
        bool        SliderField(ID id, float& value, float minVal, float maxVal,
                                std::string_view label, LayoutOverride&& constraints = {});
        inline bool SliderField(float& value, float minVal, float maxVal,
                                std::string_view label, LayoutOverride&& constraints = {}) { return SliderField(idManager.AllocateDynamicID(), value, minVal, maxVal, label, std::move(constraints)); }

        // Integer variant: snaps to whole numbers.
        bool        SliderField(ID id, int& value, int minVal, int maxVal,
                                std::string_view label, LayoutOverride&& constraints = {});
        inline bool SliderField(int& value, int minVal, int maxVal,
                                std::string_view label, LayoutOverride&& constraints = {}) { return SliderField(idManager.AllocateDynamicID(), value, minVal, maxVal, label, std::move(constraints)); }

        // Groove + knob without value text; drag-only, integer-valued.
        bool        Slider(ID id, int& value, int minVal, int maxVal, LayoutOverride&& constraints = {});
        inline bool Slider(int& value, int minVal, int maxVal, LayoutOverride&& constraints = {}) { return Slider(idManager.AllocateDynamicID(), value, minVal, maxVal, std::move(constraints)); }



        void        DragLabel(ID id, std::string_view title, StyleRef style, LayoutOverride&& constraints = {});
        inline void DragLabel(ID id, std::string_view title,                 LayoutOverride&& constraints = {}) { DragLabel(id, title, theme.dragLabelStyleRef, std::move(constraints)); }
        inline void DragLabel(       std::string_view title, StyleRef style, LayoutOverride&& constraints = {}) { DragLabel(idManager.AllocateDynamicID(), title, style, std::move(constraints)); }
        inline void DragLabel(       std::string_view title,                 LayoutOverride&& constraints = {}) { DragLabel(idManager.AllocateDynamicID(), title, theme.dragLabelStyleRef, std::move(constraints)); }

        void        Label(ID id, std::string_view title, StyleRef style, LayoutOverride&& constraints = {});
        inline void Label(ID id, std::string_view title,                 LayoutOverride&& constraints = {}) { Label(id, title, theme.labelStyleRef, std::move(constraints)); }
        inline void Label(       std::string_view title, StyleRef style, LayoutOverride&& constraints = {}) { Label(idManager.AllocateDynamicID(), title, style, std::move(constraints)); }
        inline void Label(       std::string_view title,                 LayoutOverride&& constraints = {}) { Label(idManager.AllocateDynamicID(), title, theme.labelStyleRef, std::move(constraints)); }

        void        Text(ID id, std::string_view title, StyleRef style, LayoutOverride&& constraints = {});
        inline void Text(ID id, std::string_view title,                 LayoutOverride&& constraints = {}) { Text(id, title, theme.staticTextStyleRef, std::move(constraints)); }
        inline void Text(       std::string_view title, StyleRef style, LayoutOverride&& constraints = {}) { Text(idManager.AllocateDynamicID(), title, style, std::move(constraints)); }
        inline void Text(       std::string_view title,                 LayoutOverride&& constraints = {}) { Text(idManager.AllocateDynamicID(), title, theme.staticTextStyleRef, std::move(constraints)); }

        // Read-only rich text with links, mouse selection and Ctrl+C. `onLinkActivate` (optional)
        // gets a clicked link's contentId. Pass a STABLE id so selection survives across frames.
        void SelectableText(ID id, const std::span<ContentRun>& runs,
                            std::function<void(intptr_t contentId)> onLinkActivate,
                            StyleRef style, LayoutOverride&& constraints = {});
        inline void SelectableText(ID id, const std::span<ContentRun>& runs, StyleRef style, LayoutOverride&& constraints = {})
        { SelectableText(id, runs, {}, style, std::move(constraints)); }

        // Draws `image` (from graphicsContext->RegisterImage) stretched over the element's box.
        void        Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
        inline void Image(ID id, ImageIndex image,                 LayoutOverride&& constraints = {}) { Image(id, image, theme.imageStyleRef, std::move(constraints)); }
        inline void Image(       ImageIndex image, StyleRef style, LayoutOverride&& constraints = {}) { Image(idManager.AllocateDynamicID(), image, style, std::move(constraints)); }
        inline void Image(       ImageIndex image,                 LayoutOverride&& constraints = {}) { Image(idManager.AllocateDynamicID(), image, theme.imageStyleRef, std::move(constraints)); }

        // Aspect-preserving: sizes the element to the largest sourceSize-aspect rect that fits
        // the constraints. sourceSize is the image's pixel size.
        void        Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
        inline void Image(ID id, ImageIndex image, uint2 sourceSize,                 LayoutOverride&& constraints = {}) { Image(id, image, sourceSize, theme.imageStyleRef, std::move(constraints)); }
        inline void Image(       ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {}) { Image(idManager.AllocateDynamicID(), image, sourceSize, style, std::move(constraints)); }
        inline void Image(       ImageIndex image, uint2 sourceSize,                 LayoutOverride&& constraints = {}) { Image(idManager.AllocateDynamicID(), image, sourceSize, theme.imageStyleRef, std::move(constraints)); }

        bool BeginGhostWindow(ID windowLayoutID, int2 position)
        {
            return BeginGhostWindow(windowLayoutID, position, int2{ styleManager.Px(28), styleManager.Px(28) });
        }

        // `position` is the top-left in screen space; for drag previews use BeginCursorGhostWindow.
        bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size, StyleRef styleRef)
        {
            if (windowManager->GetWindowState(windowLayoutID) == WindowState::Closing)
            {
                return false;
            }

            auto&& constraints = LayoutOverride::FixedSize(size.x, size.y).Position(position.x, position.y);

            ElementFlags elementFlags = interstice::ElementFlags::WindowNoCaption | interstice::ElementFlags::WindowIsTransparent;
            return BeginWindow(windowLayoutID, std::move(constraints), styleRef, elementFlags);
        }

        bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size)
        {
            return BeginGhostWindow(windowLayoutID, position, size, theme.ghostWindowStyleRef);
        }

        // Sized to its content.
        bool BeginGhostWindow(ID windowLayoutID, int2 position, StyleRef styleRef)
        {
            if (windowManager->GetWindowState(windowLayoutID) == WindowState::Closing)
                return false;
            auto&& constraints = LayoutOverride{}.Position(position.x, position.y);
            ElementFlags elementFlags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsTransparent;
            return BeginWindow(windowLayoutID, std::move(constraints), styleRef, elementFlags);
        }

        // Ghost offset from the cursor so the preview doesn't sit under the OS cursor.
        int2 CursorGhostOffset() const { const int32_t o = styleManager.Px(14); return int2{ o, o }; }
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset());
        }
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos, int2 size, StyleRef styleRef)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset(), size, styleRef);
        }
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos, int2 size)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset(), size);
        }
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos, StyleRef styleRef)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset(), styleRef);
        }

        void AddGhostIcon(IconID icon)
        {
            auto dragGhostIconID = idManager.AllocateDynamicID();
            interstice::Element ghostIcon
            {
                .id       = dragGhostIconID,
                .textID   = TextID::Invalid,
                .iconID   = icon,
                .styleRef = theme.ghostIconStyleRef,
            };

            auto ghostIconRes = elementStorage.Add(std::move(ghostIcon),
                                                   interstice::LayoutOverride::FixedSize(styleManager.Px(28), styleManager.Px(28)));
            VERIFY(ghostIconRes.status);
            VERIFY(windowRegistry.Add(ghostIconRes.ref));
        }

        void EndGhostWindow()
        {
            EndWindow();
        }

        bool BeginWindow(ID windowLayoutID, std::string_view title, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, title, LayoutOverride{}, theme.windowStyleRef, flags, monitorIndex);
        }

        bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, title, LayoutOverride{}, styleRef, flags, monitorIndex);
        }

        bool BeginWindow(ID windowLayoutID, std::string_view title, LayoutOverride&& constraints, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, title, std::move(constraints), theme.windowStyleRef, flags, monitorIndex);
        }


        bool BeginWindow(ID windowLayoutID, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, LayoutOverride{}, theme.windowStyleRef, flags, monitorIndex);
        }

        bool BeginWindow(ID windowLayoutID, StyleRef styleRef, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, LayoutOverride{}, styleRef, flags, monitorIndex);
        }

        bool BeginWindow(ID windowLayoutID, LayoutOverride&& constraints, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, std::move(constraints), theme.windowStyleRef, flags, monitorIndex);
        }

        bool BeginWindow(ID windowLayoutID, LayoutOverride&& constraints, StyleRef styleRef, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, "", std::move(constraints), styleRef, flags, monitorIndex);
        }


        bool BeginWindow(ID windowLayoutID, std::string_view title, LayoutOverride&& constraints, StyleRef styleRef, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            assert(windowLayoutID != ID::Invalid());

            // Closed by the user since last frame.
            if (windowManager->GetWindowState(windowLayoutID) == WindowState::Closing)
            {
                return false;
            }

            if (monitorIndex == -1)
            {
                if (constraints.x.type == PositionType::Fixed &&
                    constraints.y.type == PositionType::Fixed)
                {
                    monitorIndex = windowManager->GetMonitorIndexAt(interstice::int2{ .x = constraints.x.near, .y = constraints.y.near });
                } else
                    monitorIndex = windowManager->GetPrimaryMonitorIndex();
            }

            Element windowElement
            {
                .id           = windowLayoutID,
                .textID       = (title.size() == 0) ? TextID::Invalid : textSystem->RegisterText(windowLayoutID, title, styleRef, interstice::ElementState::None),
                .styleRef     = styleRef,
                .monitorIndex = (int16_t)monitorIndex,
                .flags        = ElementFlags::IsWindow | flags
            };

            auto addResult = elementStorage.Add(std::move(windowElement),
                                                std::move(constraints));
            VERIFY(addResult.status);
            windowRegistry.BeginWindowScope(windowLayoutID, addResult.ref);
            VERIFY(elementStorage.BeginChildren());

            tabChainManager.BeginWindow(windowLayoutID);
            [[maybe_unused]] bool pushed = idManager.PushParent(windowLayoutID);
			assert(pushed); // windows are top-level, so this can't fail

            m_windowIdScopeStack.push_back(pushed);
            activeWindowID = windowLayoutID;
            return true;
        }

        void EndWindow()
        {
            tabChainManager.EndWindow();
            if (!m_windowIdScopeStack.empty() && m_windowIdScopeStack.back())
                (void)idManager.PopParent();
            if (!m_windowIdScopeStack.empty())
                m_windowIdScopeStack.pop_back();
            VERIFY(elementStorage.EndChildren());
            windowRegistry.EndWindowScope();
            activeWindowID = ID::Invalid();
            guiStateManager.Labels().ClearPendingLabel();
        }

        void AddBox(ID id, int2 size, StyleRef styleRef, TextID textID = TextID::Invalid)
        {
            Element boxElement {};
            boxElement.id = id;
            boxElement.flags = ElementFlags::None;
            boxElement.styleRef = styleRef;
            boxElement.textID = textID;
            boxElement.state = guiStateManager.GetCurrentState(id);

            auto childB1 = elementStorage.Add(std::move(boxElement), LayoutOverride::FixedSize(size));
            VERIFY(childB1.status);
            VERIFY(windowRegistry.Add(childB1.ref));
        }

        void AddBox(int2 size, StyleRef styleRef, TextID textID = TextID::Invalid)
        {
            AddBox(idManager.AllocateDynamicID(), size, styleRef, textID);
        }

        void AddBox(ID id, int2 size, StyleRef styleRef, std::string_view text)
        {
            ElementState state = guiStateManager.GetCurrentState(id);
            auto childB1 = elementStorage.Add(Element
                                              {
                                                  .id = id,
                                                  .textID = textSystem->RegisterText(id, text, styleRef, state),
                                                  .styleRef = styleRef,
                                                  .flags = ElementFlags::None,
                                                  .state = state
                                              },
                                              LayoutOverride::FixedSize(size));
            VERIFY(childB1.status);
            VERIFY(windowRegistry.Add(childB1.ref));
        }

        void AddBox(ID id, int2 size, StyleRef styleRef, IconID iconID, std::string_view text)
        {
            ElementState state = guiStateManager.GetCurrentState(id);
            auto childB1 = elementStorage.Add(Element
                                              {
                                                  .id = id,
                                                  .textID = textSystem->RegisterText(id, iconID, text, styleRef, state),
                                                  .styleRef = styleRef,
                                                  .flags = ElementFlags::None,
                                                  .state = state
                                              },
                                              LayoutOverride::FixedSize(size));
            VERIFY(childB1.status);
            VERIFY(windowRegistry.Add(childB1.ref));
        }

        void AddBox(ID id, StyleRef styleRef, IconID iconID, std::string_view text)
        {
            ElementState state = guiStateManager.GetCurrentState(id);
            auto childB1 = elementStorage.Add(Element
                                              {
                                                  .id = id,
                                                  .textID = textSystem->RegisterText(id, iconID, text, styleRef, state),
                                                  .styleRef = styleRef,
                                                  .flags = ElementFlags::None,
                                                  .state = state
                                              });
            VERIFY(childB1.status);
            VERIFY(windowRegistry.Add(childB1.ref));
        }

        void AddBox(StyleRef styleRef, std::string_view text)
        {
            AddBox(idManager.AllocateDynamicID(), styleRef, IconID::Invalid, text);
        }

        void AddBox(int2 size, StyleRef styleRef, std::string_view text)
        {
            AddBox(idManager.AllocateDynamicID(), size, styleRef, text);
        }

        // ------------------------------------------------------------------
        // Settings panel
        // ------------------------------------------------------------------
        // Two-pane settings panel: a page tree, a sizer, and the selected page's content.
        // Owns only selection and tree width; page state belongs to the application.
        struct SettingsPanelState
        {
            struct Page
            {
                interstice::ID               id;
                std::string           name;
                std::function<void()> render;
            };

            std::vector<Page> pages;
            interstice::ID           selectedID = interstice::ID::Invalid();
            int               treeWidth  = 200; // CSS pixels

            // Tree-view backing state, rebuilt per frame; not part of the API.
            struct Internal
            {
                std::vector<TreeViewRow> rows;
                std::vector<int>         visibleIdx;
                TableState               tableState;
            } internal;

            // `id` must be stable. The first page added is selected initially.
            interstice::ID AddPage(interstice::ID id, std::string_view name, 
                            std::function<void()> render)
            {
                pages.push_back({ id, std::string(name), std::move(render) });
                if (selectedID == interstice::ID::Invalid()) selectedID = id;
                return id;
            }
        };

        // Renders into the current container; the caller provides the outer panel.
        void RenderSettingsPanel(SettingsPanelState& state);

    private:
        // GUIApplicationInternals (gui_application_internal.h) re-exposes these to
        // the library's own code and the tests.
        friend class GUIApplicationInternals;

        // Cached scopes being rebuilt (BeginCached misses), innermost last.
        std::vector<ID>                            m_cacheRecordStack;

        // This frame's input as seen by cached scopes; refreshed lazily once per frame.
        bool     m_keyboardPending = false;
        bool     m_tabPending      = false;
        uint64_t m_inputFlagsFrame = 0;
        void     RefreshCachedInputFlags();

        // Registers a swatch claim with the enclosing cached scopes so hit frames can keep it alive.
        void NoteSwatchClaim(ID swatchID);

        // Panel id → its dock content (cached scope) id, for InvalidatePanel.
        std::unordered_map<interstice::ID, interstice::ID>       m_panelContentIDs;

        // Save-stack for nested BeginToolbar/EndToolbar.
        std::vector<const ControlStyles*> controlStylesStack;

        ID tooltipWindowID     { ID::Invalid() };
        ID contextMenuWindowID { ID::Invalid() };
        ID contextMenuTargetID { ID::Invalid() }; // element the context menu is attached to
        ID tabDragGhostWindowID{ ID::Invalid() }; // ghost window for dragged dock tab / drop overlay

        FrameTimingSample frameTimingHistory[kFrameHistoryCount] = {};
        int               frameTimingHead = 0;           // next write slot

        // Table drag state that persists across frames (defs in gui_table_types.h).
        ColumnDragState m_columnDrag{};
        TreeDropState   m_treeDropState{};

        // ---- ListView ID->index map (shared by every list view; cleared once per frame) ----
        // Filled by ListItem when dragDrop is on; valid in end-of-frame handlers via FindListItemIndex().
        std::unordered_map<interstice::ID, int> m_listItemIndexByID;


        // Transient construction state (defs in gui_tree_grid_types.h), valid only between
        // Begin*/End*. Handlers run later, so they must capture pointers, never read these.
        TreeViewState m_treeState{};
        GridViewState m_gridViewState;

        // GridView rename lookup, shared by every grid view; cleared before construction.
        std::unordered_map<interstice::ID, GridItemRenameInfo> m_gridItemRenameByID;


        // Transient state of the VirtualTable being emitted (def in gui_table_types.h).
        std::optional<TableEmit> m_tableEmit;
        // Emit states of outer tables while a table is built inside a cell, innermost last.
        std::vector<std::optional<TableEmit>> m_outerTableEmits;

        int CurrentCellCol() const
        {
            const int c = m_tableEmit->currentCol;
            assert(c < int(m_tableEmit->columnCount));
            return c;
        }

        TabBarState m_tabBarState;

        // Enables the panels reachable from layout.root; called by BeginDockingContainer each frame.
        void EnableVisiblePanels(DockingLayout& layout);

        // Transient, valid only between BeginDockingContainer / EndDockingContainer
        // (def in gui_docking_types.h); handlers must capture pointers, not read this.
        DockingConstructionState m_dockingState;

        // Docking layouts emitted this frame (cleared at frame start); used by
        // SyncDockingScrollOffsets and cross-dock drop hit-testing.
        std::vector<DockingLayout*> m_dockingLayoutsThisFrame{};

        // Toolbar docks emitted this frame, walked by SyncToolbarDockRects (type in controls_toolbar_dock.h).
        std::vector<struct ToolbarDockState*> m_toolbarDocksThisFrame{};

        // After input dispatch: stores each selected panel's scroll offset so it survives a tab switch.
        void SyncDockingScrollOffsets();

        // Snapshots each toolbar dock side's screen rect for next frame's drag hit-testing.
        void SyncToolbarDockRects();

        // Uses last frame's tab-bar rects, so it works during construction.
        interstice::DockNode* FindDockLeafTabBarCachedAtScreenPos(interstice::int2 screenPos);

        std::vector<std::unique_ptr<FloatingDockWindow>> m_floatingDockWindows;

        // Debug asserts for Begin*/End* stacks left unbalanced at frame end. Also used by the headless tests.
        void RegisterFrameBalanceChecks();

        // Rebuilds the styles when a reload is pending or the main window's
        // display scale changed. Runs before the managers begin their frame.
        void ApplyPendingStyleReload();

        void OnRender(float elapsedTimeSecs);
        void OnFrame();

        // Change via RequestReloadTheme so the reload happens between frames.
        std::string m_resourceRoot;
        std::unique_ptr<IWindowBackend> m_osWindowBackend;     // wrapped by windowBackend
        ImageWindowBackend*  m_imageWindows    { nullptr };    // == windowBackend
        MouseButtons         m_injectedButtons { MouseButtons::None };
        int2 InjectedScreenPos(ID windowID, int2 pos);
        AllocationHooks m_allocationHooks;
        std::unique_ptr<GraphicsContext> m_graphicsBackend;
        MouseButtons m_lastPhysicalButtons { MouseButtons::None };  // for SynthesizeMissingMouseEvents
        std::string ResourcePath(std::string_view path) const;   // relative paths resolve against m_resourceRoot
        std::string m_themePath     { "themes/dark.css" };
        std::vector<std::string> m_styleFiles { "styles.css" };
        float       m_globalFontScale { 1.0f };
        float       m_uiScale { 1.0f };
        float       m_displayScale { 1.0f };
        FrameClock  m_frameClock;   // animation time, vsync-paced
        bool        m_themeReloadPending { false };

        interstice::ElementRef RegisterElementInternal(interstice::Element&& element, LayoutOverride&& constraints, bool updateState = true);

        // Invalidates the scopes under the cursor and, on key input, the focused one.
        // Call after hit-testing, before dispatch.
        void        InvalidateCachedScopesForInput(ID overID);

        // Internal: called by the docking each frame.
        void        NotePanelContentID(ID panelId, ID contentID) { m_panelContentIDs[panelId] = contentID; }

        // Set by RenderNodeView around each renderBody; Start elsewhere.
        TextAlign nodeRowTextAlign = TextAlign::Start;

        // When > 0, a Field() in a node row grows to fill the body with this minimum width.
        // Set by RenderNodeView around each renderBody; 0 elsewhere.
        int32_t nodeRowFieldMinWidth = 0;

        void BeginDockingFrame();
        void EndDockingFrame();

        ID activeWindowID { ID::Invalid() };
        std::vector<bool> m_windowIdScopeStack;

        // The node view's breadcrumb, zoom controls and minimap, over its canvas.
        void EmitNodeViewOverlays(NodeView& view, ID canvasID);

        // Popup content for ColorField / GradientField.
        void EmitColorPicker(interstice::color4f& outColor, bool allowHDR);
        void EmitGradientEditor(interstice::Gradient& outGradient);
        // Horizontal gradient bar with a triangle indicator.
        void GradientSlider(interstice::ID id, float& value, float minVal, float maxVal,
                            std::string_view label,
                            interstice::VisualColorStyleIndex gradientStyle,
                            interstice::VisualColorStyleIndex triangleStyle,
                            LayoutOverride&& constraints = {});

        // Column-boundary sizers for [colStart, colEnd), anchored to each header cell's right edge.
        // skipDragged leaves out the dragged column's sizer; EmitDraggedColumnOnTop emits it last.
        void EmitColumnSizers(TableEmit& emit, int colStart, int colEnd,
                              bool skipDragged = false);
        // 1px column dividers for headerless tables, which have no sizers.
        void EmitColumnSeparators(TableEmit& emit, int colStart, int colEnd);
        // Full-height strips for columns with columnBackground: above the row backdrops, below the cells.
        void EmitColumnBackgrounds(TableEmit& emit, int colStart, int colEnd);

        // followTail, run from the body section's post-layout hook (final viewport/content height).
        void UpdateTableFollowTail(TableState& state, int32_t viewportH, int32_t contentH);
        void ScrollTableSectionsToBottom(TableState& state);
        // 1px separator between adjacent frozen sections.
        void EmitTableSectionSeparator();

        // Adds column c's reorder-drag X offset while a drag is live.
        interstice::LayoutOverride ApplyColumnDragOffsetX(interstice::LayoutOverride lo,
                                                   const TableEmit& emit, int c) const
        {
            if (emit.dragActive)
                lo = std::move(lo).PositionX(interstice::PositionConstraintAxis::Relative(emit.columns[c].internal.dragOffsetX));
            return lo;
        }
        // Mirrors Y across the left/mid/right bodies and X to the mid header; absent sections have invalid IDs.
        void MirrorTableScroll(TableState* state);
        // `gridCol` is section-local. `sectionMin/Max` are the inclusive global columns
        // the reorder drag may move this header within.
        void EmitTableHeaderCell(TableEmit& emit, interstice::ID tableID, int c, int16_t gridCol,
                                 bool isStretchTrack, int sectionMin, int sectionMax,
                                 int effectiveSortCol, IconID sortIcon);
        // Header cells go into row 0 of the section's body grid, StickyY-pinned to the top.
        void EmitTableHeaderSection(TableEmit& emit, interstice::ID tableID,
                                    int colStart, int colEnd, bool lastColIsStretch,
                                    int effectiveSortCol, IconID sortIcon);
        // Stretch for Stretch columns and the middle section's last column; fitContent
        // forces Fixed (fit-content sections have no leftover space to absorb).
        GridTrack ComputeSectionColumnTrack(const TableEmit& emit, int c, int colEnd,
                                            bool isMiddle, bool fitContent = false) const;
        // Follow with the row loop and CloseTableBodySection. `sampleViewport` records the
        // viewport size for next frame (primary section only).
        void OpenTableBodySection(TableEmit& emit, interstice::ID sectionID, interstice::StyleRef sectionStyle,
                                  interstice::LayoutOverride&& layout, bool sampleViewport);
        // Emits sizers and the section's H scrollbar (the rightmost section also gets the
        // shared V scrollbar). suppressHorzScrollbar is for fit-content sections.
        void CloseTableBodySection(TableEmit& emit,
                                   interstice::ID sectionBodyID, bool emitVerticalScrollbar,
                                   bool suppressHorzScrollbar = false);
        // Row loop for an open body section: spacers for off-screen rows, backdrop + cells for visible ones.
        void EmitVirtualBodyRows(TableEmit& emit,
                                 int colStart, int colEnd,
                                 int totalRowCount, int32_t actualRowHeight,
                                 int firstVisible, int lastVisible,
                                 interstice::ID rowsRangeID,
                                 bool isLastSection);

        // Emits the dragged column (cells, header, sizer) last so it paints on top.
        // Only when emit.dragActive and draggedCol is in [colStart, colEnd).
        void EmitDraggedColumnOnTop(TableEmit& emit, interstice::ID tableID,
                                    int colStart, int colEnd,
                                    int firstVisible, int lastVisible,
                                    interstice::ID rowsRangeID,
                                    bool lastColIsStretch,
                                    int effectiveSortCol, IconID sortIcon);
        // Cell emitters for Cell() / TreeCell(), into the open body section.
        void EmitDirectCellText(const VirtualTableColumn& column, TableEmit& emit,
                                interstice::ID cellID, interstice::ElementState cellState, interstice::ElementFlags cellFlags,
                                interstice::StyleRef cellStyleRef, interstice::EventHandler cellHandler,
                                interstice::TextID textID,
                                int colIdx, int colStart, int16_t gridRow);
        void EmitDirectCellCustom(const VirtualTableColumn& column, TableEmit& emit,
                                  interstice::ID cellID, interstice::ElementState cellState, interstice::ElementFlags cellFlags,
                                  interstice::StyleRef cellStyleRef, interstice::EventHandler cellHandler,
                                  bool cellSelected,
                                  std::function<void()> emit_fn,
                                  int colIdx, int colStart, int16_t gridRow);
        void EmitDirectCellTree(const VirtualTableColumn& column, TableEmit& emit,
                                interstice::ID cellID, interstice::ElementState cellState, interstice::ElementFlags cellFlags,
                                interstice::StyleRef cellStyleRef, interstice::EventHandler cellHandler,
                                bool cellSelected,
                                interstice::TextID textID,
                                int treeDepth, bool* treeIsOpen, IconID treeIcon,
                                uint32_t treeAncestorMask, bool treeIsLastSibling,
                                std::string* treeRenameBuffer, bool treeRenameActive,
                                interstice::ID treeBodyID, bool reserveExpander,
                                int colIdx, int colStart, int16_t gridRow);
        // One element spanning [colStart, colEnd) at gridRow.
        void EmitDirectRowBackdrop(interstice::ID rowID, int16_t gridRow,
                                   int colStart, int colEnd,
                                   interstice::ElementState backdropState,
                                   interstice::ElementFlags backdropFlags,
                                   interstice::StyleRef rowStyle,
                                   interstice::EventHandler backdropHandler,
                                   TableState* tableState,
                                   bool isLastSection);
        // Invisible row of `height` pixels at gridRow.
        void EmitDirectRowSpacer(int16_t gridRow,
                                 int colStart, int colEnd, int32_t height);
        // Row-mode backdrop MouseDown handler, shared by all rows (the row comes from the event id):
        // selection, double-click toggle, rename start.
        interstice::EventHandler BuildRowBackdropHandler(TableState* state,
                                                  interstice::ID containerID);
        // Focus on click, scroll routing to the middle body, keyboard navigation.
        interstice::EventHandler BuildTableContainerHandler(TableState* state,
                                                     interstice::ID containerID);

        // Tree/list item construction helpers (OnGUI only, not from handlers).
        bool IsTreeItemSelected(interstice::ID id, int itemIndex) const;
        interstice::TextID TreeItemRegisterText(interstice::ID id, std::string_view text, IconID icon, StyleRef style, interstice::ElementState state);
        void TreeItemRowCustomDraw(const interstice::ElementRef& rowRef, bool hasArrow, interstice::ID rowID);
        interstice::EventHandler MakeTreeRowDragDropHandler(interstice::ID rowID, bool rowHasChildren, ItemDragDropConfig* dd, bool* isOpen = nullptr);
        bool TreeItemRenameCheck(interstice::ID bodyID, std::string* renameBuffer, StyleRef textStyle);

        struct TreeClickState { interstice::ID lastClickID = interstice::ID::Invalid(); int64_t lastClickTimeNanos = 0; };

    };

} // namespace interstice
