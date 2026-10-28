#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <cassert>
#include <source_location>
#include <span>
#include <functional>
#include <limits>
#include <string>
#include <utility>

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
#include <gui_controls/gui_content.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/gui_result.h>

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

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// Checks the result of an element-storage call (e.g. elementStorage.Add): a failure is
    /// reported as API misuse and the caller carries on.
    void VERIFY(ElementStorageResult result);
    /// As above, for windowRegistry calls.
    void VERIFY(RegistryResult result);

    /// WindowUserClosed is accepted too: it's the expected outcome when a window can't be reopened.
    void VERIFY(AddWindowResult result);
    /// Checks the result of closing a window: misuse (outside a frame, already closed) is
    /// reported; other failures assert.
    void VERIFY(RequestCloseResult result);


    // ---- MessageDialog: a modal question (GUIApplication::MessageDialog) ----
    /// The icon beside a MessageDialog's text.
    enum class MessageIcon : uint8_t { Warning, Quit, Question, Info, Error };
    /// How a MessageButton is styled: Primary for the main choice, Danger for a destructive one.
    enum class MessageButtonKind : uint8_t { Secondary, Primary, Danger };

    /// A button of a MessageDialog.
    struct MessageButton
    {
        /// A verb that says what happens ("Quit", "Keep mine"), never Yes / No. Its first
        /// letter is the Alt mnemonic.
        std::string_view  label;
        MessageButtonKind kind      = MessageButtonKind::Secondary;   ///< its look; see MessageButtonKind
        bool              alignLeft = false;   ///< the alternative to the main choice ("Don't save")
    };

    /// An affected item, listed in a well: a name, an optional color dot and note, and a
    /// checkbox when `checked` is set.
    struct MessageDialogItem
    {
        std::string_view name;             ///< e.g. a file name
        std::string_view meta;             ///< e.g. "12 edits"
        color4f          color {};         ///< alpha 0: no dot
        bool*            checked = nullptr;   ///< shows a checkbox bound to it when set
    };

    /// What a MessageDialog shows.
    struct MessageDialogDesc
    {
        MessageIcon                        icon = MessageIcon::Question;   ///< beside the text
        std::string_view                   title;      ///< a question: "Quit Test?"
        std::string_view                   message;    ///< one or two sentences on what happens
        std::span<const MessageButton>     buttons;    ///< MessageDialog returns an index into these
        int                                defaultButton = 0;    ///< focused at first; Enter
        int                                cancelButton  = -1;   ///< Esc and the close button; -1: the default one
        std::span<const MessageDialogItem> items;      ///< affected items, listed in a well
        bool*                              dontAskAgain = nullptr;   ///< shows "Don't ask again" when set
        std::string_view                   details;    ///< collapsible technical details (mono)
    };

    // Outside GUIApplication: GCC can't use a nested struct's member initializers in a
    // default argument of the class being defined.
    /// What GUIApplication::Initialize sets up besides the core systems. All on by default;
    /// a host that does one itself turns it off.
    struct GUIApplicationInitializeOptions
    {
        bool loadStyles             = true;  ///< load the theme (InitializeStyles)
        bool commandShortcuts       = true;  ///< route keys to commandManager's shortcuts
        bool repaintWhileResizing   = true;  ///< onGui = OnFrame, unless onGui is already set
        bool closeMenusOnWindowMove = true;  ///< open menus close when a window moves or resizes
        /// The optional fallback fonts (Noto: other scripts, emoji, symbols), relative to
        /// the resource root, e.g. "fonts/Noto-otf". Empty: only the built-in fonts.
        std::string_view fallbackFonts {};
    };

    /// The application: owns the windows, the systems and the frame loop. Subclass it, build
    /// the UI in OnGUI, and call Initialize, then Run (or the frame calls), then Cleanup.
    ///
    ///     class HelloApp : public tachyon::GUIApplication
    ///     {
    ///     public:
    ///         tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    ///         {
    ///             if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
    ///                 return result;
    ///             windowID = idManager.CreateStaticID();   // windows need stable IDs
    ///             return {};
    ///         }
    ///
    ///         void OnGUI() override   // every frame: describe the whole UI
    ///         {
    ///             if (BeginWindow(windowID, "Hello", tachyon::LayoutOverride{}.Size(360, 160)))
    ///                 Button("Greet", [this] { ++greetings; });
    ///             EndWindow();   // always, whatever BeginWindow returned
    ///         }
    ///
    ///     private:
    ///         tachyon::ID windowID = tachyon::ID::Invalid();
    ///         int         greetings = 0;
    ///     };
    ///
    /// Every control is a member function, grouped below by family.
    class GUIApplication
    {
    public:
        /// Routes input events to the elements; also holds global handlers. Created by Initialize.
        std::unique_ptr<InputManager>     inputManager;
        /// Lays out the elements each frame. Created by Initialize.
        std::unique_ptr<LayoutEngine>     layoutEngine;
        /// The OS windows and their render contexts. Created by Initialize.
        std::unique_ptr<OSWindowManager>  windowManager;
        /// The glyph atlas. Created by Initialize.
        std::unique_ptr<TextAtlasManager> textAtlasManager;
        std::unique_ptr<IClipboard>       clipboard;          ///< from SetClipboard

        /// Shapes, measures and edits text. Created by Initialize.
        ITextSystem*        textSystem      { nullptr };
        IWindowBackend*     windowBackend   { nullptr };   ///< SetWindowBackend's windows plus render-to-image windows
        GraphicsContext*    graphicsContext { nullptr };   ///< the backend from SetGraphicsBackend
        /// The graphics backend's renderer.
        GUIRenderer*        guiRenderer     { nullptr };

        /// Element IDs: static ones (CreateStaticID), call-site ones (AllocateDynamicID) and
        /// parent scopes (PushParent / PopParent).
        IDManager           idManager{};
        /// Per-element interaction state (hover, focus, clicks) and the construction stacks
        /// (disabled, selected).
        GUIStateManager     guiStateManager{};
        /// In-progress visual state transitions, per element.
        TransitionManager   transitionManager{};
        /// The Tab-key focus order, rebuilt each frame.
        TabChainManager     tabChainManager{};

        /// The open menus and the menu bar's triggers.
        MenuManager         menuManager{};
        /// When a tooltip shows (delaySeconds) and for which element.
        TooltipManager      tooltipManager{};

    private:
        /// Per-frame stage pipeline. Declared before the subsystems, which register
        /// callbacks in it, so they are destroyed first.
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
        /// @name State
        ///@{

        /// GPU pages for the gradients styles use.
        GradientManager     gradientManager{};
        /// The registered styles; Px converts CSS pixels to pixels at the UI scale.
        StyleManager        styleManager { gradientManager };

        /// This frame's element tree.
        ElementStorage      elementStorage { styleManager, &guiStateManager.GetElementDataStore() };
        /// The elements of each window, filled as the windows are built.
        WindowRegistry      windowRegistry {};

        /// Commands, their shortcuts and the undo history.
        CommandManager      commandManager {};

        /// Sends each platform input event to the input manager and the text system.
        InputHandlerBroadcaster inputBroadcaster {};

        /// Runs a frame from inside the OS's modal resize loop, so windows repaint while resized.
        /// Initialize sets it unless repaintWhileResizing is off or it is already set.
        OnGuiFunc           onGui           {};

        /// The loaded theme: the style refs, icons and metrics the controls use; theme.GetStyle
        /// finds a CSS class.
        GUITheme theme{};


        /// Set to true to end Run (UpdateFrame then returns false).
        bool applicationQuit = false;
        /// Internal: when the current frame started (set by UpdateFrame).
        std::chrono::steady_clock::time_point m_frameStart{};
        /// The last frame's duration in seconds, including any vsync wait.
        float frameTime = 0.0f;
        /// CPU work per frame, excluding the vsync stall in present.
        float frameBuildTime = 0.0f;

        /// Rolling per-frame timing history in ms, recorded only while profiling is enabled.
        struct FrameTimingSample { float buildMs = 0.0f; float totalMs = 0.0f; };
        static constexpr int kFrameHistoryCount = 240;   ///< ~4s @ 60fps

        /// Copies the ring chronologically (oldest first) into out.
        void GetFrameTimingHistory(std::span<FrameTimingSample> out) const;

        ///@}


        /// @name Panels and floating windows
        ///@{

        /// Adds a panel to `panels` (usually a DockingLayout's). onPanelGUI builds its content;
        /// onEnable / onDisable run when it becomes visible / hidden. panelType names its kind,
        /// which saved UI state matches panels by (else the title).
        void CreatePanel(std::unordered_map<tachyon::ID, tachyon::Panel>& panels, ID id, std::string title, tachyon::IconID icon,
                         std::function<void()> onPanelGUI,
                         std::string panelType = {},
                         std::function<void()> onEnable = {},
                         std::function<void()> onDisable = {});

        /// As above, with a new static ID, which it returns.
        tachyon::ID CreatePanel(std::unordered_map<tachyon::ID, tachyon::Panel>& panels, std::string title, tachyon::IconID icon,
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


        /// Panel lifecycle. A panel is enabled iff it is the selectedTab of a leaf reachable
        /// from layout.root; the docking enables the visible ones each frame.
        /// DisableAllPanels brackets disruptive operations (theme reload, preset swap).
        void DisableAllPanels(DockingLayout& layout);

        /// One OS window per floating toolbar. Closed windows are erased and their IDs released.
        void EmitFloatingToolbarWindows(std::vector<struct FloatingToolbarItem>& floatingItems,
                                        const struct FloatingToolbarOptions* options = nullptr);

        /// Dock leaf whose tab bar contains screenPos, plus its owning layout (needed by
        /// TransferPanel for cross-layout drops). {nullptr, nullptr} on miss.
        DockLeafResult FindDockLeafAtScreenPos(tachyon::int2 screenPos);

        /// Hit-tests leaf content areas; falls back to tab bars with zone=Center.
        DockDropResult FindDockDropTargetAtScreenPos(tachyon::int2 screenPos);

        /// Call from OnGUI after the main docking container. Empty windows are closed.
        void EmitFloatingDockWindows();
        /// Moves a docked panel into its own window at a screen position, as tearing its tab
        /// off does; the window's Dock button returns it. False if the layout hasn't the panel.
        bool FloatPanel(DockingLayout& layout, ID panelID, int2 screenPos);

        /// Closes the window for good: its panels are disabled and destroyed.
        void CloseFloatingDockWindow(size_t index);

        /// Closes the panel at `tabIndex` of a docked leaf, as the tab's Close action does.
        void CloseDockedPanel(DockingLayout& layout, DockNode& leaf, int tabIndex);

        /// Moves a panel from sourceNode/sourceLayout to targetLayout, fixing up selectedTab.
        static void TransferPanel(tachyon::ID panelID, tachyon::DockNode* sourceNode,
                                  DockingLayout* sourceLayout,
                                  DockingLayout* targetLayout);

        ///@}

        /// @name Setup and resources
        ///@{

                virtual ~GUIApplication() = default;

        /// See GUIApplicationInitializeOptions.
        using InitializeOptions = GUIApplicationInitializeOptions;

        /// Creates the backends not set before, the window manager, the text system and the
        /// subsystems, and loads the theme. An error when the graphics or text setup fails.
        virtual Result<void> Initialize(const InitializeOptions& options = {});
        /// Releases what Initialize created, including the backends set before it. Safe without
        /// Initialize.
        virtual void Cleanup();

        /// Loads the current theme at the current font scale; an error when it can't be opened.
        Result<void> InitializeStyles();

        /// Called after every successful theme/font reload (and by subclasses at startup) to
        /// re-register styles/icons the reload wiped. Reset any bookkeeping of stale StyleRefs.
        virtual void OnStylesReloaded() {}

        /// Deferred to the start of the next frame; safe from event handlers.
        /// fontScale multiplies every CSS font-size.
        void RequestReloadTheme(std::string_view themePath, float fontScale);
        /// How the last reload went; on an error the previous theme stays.
        const Result<void>& ThemeReloadResult() const { return m_themeReload; }
        const std::string& GetThemePath() const { return m_themePath; }

        /// The folder the resources load from: compiled shaders, default fonts and
        /// relative theme paths. Empty (the default) is the working directory; a
        /// package user passes TACHYON_RESOURCE_DIR, which the package defines.
        /// Call before Initialize.
        void SetResourceRoot(std::string_view root) { m_resourceRoot = std::string(root); }
        const std::string& GetResourceRoot() const { return m_resourceRoot; }

        ///@}

        /// @name Input for render-to-image windows
        ///@{

        /// Input for render-to-image windows (WindowFlags::RenderToImage), which get
        /// none from the OS. Positions are in the window's pixels. The window is under
        /// the pointer from a mouse event until InjectMouseLeave, and has keyboard
        /// focus from any injected input until InjectFocus(windowID, false).
        void InjectMouseMove(ID windowID, int2 pos, KeyModifier mods = KeyModifier::None);
        /// A button press (down) or release at pos.
        void InjectMouseButton(ID windowID, int2 pos, MouseButtons button, bool down, KeyModifier mods = KeyModifier::None);
        /// A wheel scroll by offset at pos.
        void InjectMouseScroll(ID windowID, int2 pos, float2 offset, KeyModifier mods = KeyModifier::None);
        /// The pointer left the window.
        void InjectMouseLeave(ID windowID);
        /// Gives the window keyboard focus, or takes it away.
        void InjectFocus(ID windowID, bool focused);
        /// A key event; typed text goes through InjectText.
        void InjectKey(ID windowID, KeyCode key, KeyAction action, KeyModifier mods = KeyModifier::None);
        /// Typed text, one character event per codepoint.
        void InjectText(ID windowID, std::string_view utf8);

        /// The last rendered frame of a render-to-image window: BGRA8, tightly packed
        /// rows, bgra holding width x height x 4 bytes of its pixel size.
        Result<void> ReadWindowPixels(ID windowID, std::span<uint8_t> bgra);

        ///@}

        /// @name Backends, styles and scale
        ///@{

        /// The graphics backend Initialize uses; without one it creates a
        /// vulkan::VulkanGraphicsContext. Set it up through its own API first, for
        /// example SetExternalDevice to render on the host's device. Owned until
        /// Cleanup. Call before Initialize.
        void SetGraphicsBackend(std::unique_ptr<GraphicsContext> backend) { m_graphicsBackend = std::move(backend); }

        /// The OS windows and their input; Initialize wraps it to add render-to-image
        /// windows. Without one it creates an SDLWindowBackend (SDL builds only).
        /// Owned until Cleanup. Call before Initialize.
        void SetWindowBackend(std::unique_ptr<IWindowBackend> backend) { m_osWindowBackend = std::move(backend); }
        /// Copy and paste for text fields. Defaults to SDL's clipboard, or a
        /// LocalClipboard without SDL. Owned until Cleanup. Call before Initialize.
        void SetClipboard(std::unique_ptr<IClipboard> clipboardImpl) { clipboard = std::move(clipboardImpl); }

        /// Host memory functions for what can be routed: Vulkan objects, VMA,
        /// vk-bootstrap and SDL (only when the GUI starts SDL). See AllocationHooks
        /// for how long they must live. Call before Initialize.
        void SetAllocationHooks(const AllocationHooks& hooks) { m_allocationHooks = hooks; }

        /// The CSS rule files loaded after the palette, in order; relative paths are
        /// next to the palette file. Defaults to the library's "styles.css". An app
        /// appends its own and finds their classes with theme.GetStyle. Call before
        /// Initialize, or follow with RequestReloadTheme.
        void SetStyleFiles(std::span<const std::string_view> files);
        float GetGlobalFontScale() const { return m_globalFontScale; }

        /// Extra UI zoom on top of the main window's display scale (1 follows the OS). Styles
        /// are registered at display scale x zoom. May be called before Initialize.
        void  SetUIScale(float scale);
        float GetUIScale() const { return m_uiScale; }

        ///@}

        /// @name The frame
        ///@{

        /// Builds the UI; called each frame.
        virtual void OnGUI() = 0;

        /// Loops over the frame calls below until the app quits; an error when the GPU device
        /// was lost and couldn't be made again (a host's device).
        Result<void> Run();

        /// One frame, for a host with its own main loop:
        ///
        ///     while (app.UpdateFrame()) { app.RecordFrame(); /* the host's own work */ app.PresentFrame(); }
        ///
        /// UpdateFrame pumps OS events and input (false once the app quits). RecordFrame builds
        /// the UI and records and submits every window; render-to-image windows are done then.
        /// A lost GPU device is made again first; an error when it can't be (a host's device).
        /// PresentFrame presents the OS windows and may wait for vsync.
        bool         UpdateFrame();
        /// See UpdateFrame.
        Result<void> RecordFrame();
        /// See UpdateFrame.
        void         PresentFrame();

        ///@}

        /// @name Event handlers and control building blocks
        ///@{

        // ---- Event handler factories ----
        //
        // Event handlers run at END OF FRAME (input dispatch), not during OnGUI. Captures must
        // outlive the frame: user-owned pointers, GUIApplication members and copies are fine;
        // transient construction state (m_treeState, ...) and OnGUI stack locals are not.
        //
        /// A handler that flips `toggle` on click.
        tachyon::EventHandler MakeToggleHandler(bool& toggle);
        /// A handler that calls onClick on click.
        tachyon::EventHandler MakeButtonHandler(std::function<void()> onClick);
        /// A label's handler: a click focuses the control the label belongs to.
        tachyon::EventHandler MakeLabelHandler();

        /// Adds an element, with its interaction state, to the current window; for custom elements.
        tachyon::ElementRef RegisterElement(tachyon::Element&& element, LayoutOverride&& constraints);
        /// As above, with `text` registered as its text.
        tachyon::ElementRef RegisterElement(tachyon::Element&& element, std::string_view text, LayoutOverride&& constraints);


        /// As RegisterElement, for a control: it joins the Tab order and needs a valid ID.
        tachyon::ElementRef RegisterControl(tachyon::Element&& element, LayoutOverride&& constraints);
        /// As above, with `text` registered as its text.
        tachyon::ElementRef RegisterControl(tachyon::Element&& element, std::string_view text, LayoutOverride&& constraints);
        /// As above; createTextID registers the text, given the element with its current state.
        tachyon::ElementRef RegisterControl(tachyon::Element&& element, std::function<TextID(tachyon::Element& element)> createTextID, LayoutOverride&& constraints);

        /// A menu row: an icon slot, the label and a trailing text (a shortcut or a submenu
        /// arrow). The row takes the input and the highlight. `reserveIcon` keeps the icon
        /// slot without an icon, so labels line up in a menu that mixes both.
        tachyon::ElementRef MenuItemRow(ID id, tachyon::EventHandler handler, StyleRef rowStyle,
                                    bool reserveIcon, IconID icon, std::string_view text,
                                    std::string_view trailing, StyleRef trailingStyle,
                                    LayoutOverride&& constraints, bool destructive = false);
        /// SearchField, also giving the ID of its text field (to focus it).
        bool SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior, LayoutOverride&& constraints, ID* outFieldID);
        /// Internal: the body of MenuItem and DestructiveMenuItem.
        bool MenuItemImpl(ID id, const GUIContent& content, std::string_view shortcut,
                          std::function<void()> onClick, StyleRef style, bool destructive, LayoutOverride&& constraints);
        /// The content's tooltip, if it has one, on the control `id`.
        void ShowContentTooltip(ID id, const GUIContent& content) { if (!content.tooltip.empty()) SetTooltip(id, content.tooltip); }

        /// Value fields emitted in between show a dash for mixed values (e.g. across a selection)
        /// and can't be edited.
        bool BeginIndeterminate() { guiStateManager.Construction().PushIndeterminate(); return true; }
        /// Closes BeginIndeterminate.
        void EndIndeterminate()   { guiStateManager.Construction().PopIndeterminate(); }

        ///@}

        /// @name Buttons and toggles
        ///@{

        /// Controls take (id, icon, text, ..., style, constraints), each part optional. A
        /// change is reported twice: input is handled after the frame it arrives in, where the
        /// callback runs; the return value is true in the next frame. Without an id, the
        /// control's ID comes from its call site.
        bool        Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's button style.
        inline bool Button(ID id, const GUIContent& content, std::function<void()> onClick,                 LayoutOverride&& constraints = {}) { return Button(id,  content, std::move(onClick), theme.controls->button, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Button(       const GUIContent& content, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Button(idManager.AllocateDynamicID(site), content, std::move(onClick), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's button style.
        inline bool Button(       const GUIContent& content, std::function<void()> onClick,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Button(idManager.AllocateDynamicID(site), content, std::move(onClick), theme.controls->button, std::move(constraints)); }

        /// Icon button for a command; the tooltip shows its name and shortcut, e.g. "Undo (Ctrl+Z)".
        bool        CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's button style.
        inline bool CommandButton(CommandID command, LayoutOverride&& constraints = {}) { return CommandButton(command, theme.controls->button, std::move(constraints)); }

        /// A checkbox (a box, checked with a tick) and its label.
        bool        Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, LayoutOverride&& constraints = {});

        /// An on/off switch (track and knob) with an optional label after it, e.g. "On".
        bool        Switch(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {});
        /// Switches `value` itself.
        inline bool Switch(ID id, bool& value, std::string_view text = {}, LayoutOverride&& constraints = {}) { return Switch(id,                            value, text, [&value](bool newValue){ value = newValue; }, theme.switchStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Switch(       bool& value, std::string_view text = {}, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Switch(idManager.AllocateDynamicID(site), value, text, [&value](bool newValue){ value = newValue; }, theme.switchStyleRef, std::move(constraints)); }

        /// A button that shows pressed while `value` is on; a click calls onToggle with the new value.
        bool        Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's toggle style.
        inline bool Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}) { return Toggle(id,  value, content, std::move(onToggle), theme.controls->toggle, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Toggle(       bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Toggle(idManager.AllocateDynamicID(site), value, content, std::move(onToggle), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's toggle style.
        inline bool Toggle(       bool value, const GUIContent& content, std::function<void(bool)> onToggle,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Toggle(idManager.AllocateDynamicID(site), value, content, std::move(onToggle), theme.controls->toggle, std::move(constraints)); }
        /// Toggles `value` itself.
        inline bool Toggle(ID id, bool& value, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {}) { return Toggle(id,  value, content, [&value](bool newValue){ value = newValue; }, style, std::move(constraints)); }
        /// As above, in the theme's toggle style.
        inline bool Toggle(ID id, bool& value, const GUIContent& content,                 LayoutOverride&& constraints = {}) { return Toggle(id,  value, content, [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Toggle(       bool& value, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Toggle(idManager.AllocateDynamicID(site), value, content, [&value](bool newValue){ value = newValue; }, style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's toggle style.
        inline bool Toggle(       bool& value, const GUIContent& content,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Toggle(idManager.AllocateDynamicID(site), value, content, [&value](bool newValue){ value = newValue; }, theme.controls->toggle, std::move(constraints)); }

        ///@}

        /// @name Menus and message dialogs
        ///@{

        /// A menu: a trigger button (in a menu bar or on its own), or a submenu item inside
        /// another menu. True while it is open: emit its items then. EndMenu is called either way.
        bool        BeginMenu(ID id, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints = {});
        /// As above, in the theme's menu styles.
        inline bool BeginMenu(ID id, std::string_view text, LayoutOverride&& constraints = {}) { return BeginMenu(id, text, theme.controls->menuTrigger, theme.menuStyleRef, theme.menuItemRowStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginMenu(       std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginMenu(idManager.AllocateDynamicID(site), text, buttonStyle, menuStyle, itemStyle, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's menu styles.
        inline bool BeginMenu(       std::string_view text, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginMenu(idManager.AllocateDynamicID(site), text, theme.controls->menuTrigger, theme.menuStyleRef, theme.menuItemRowStyleRef, std::move(constraints)); }
        /// A section's name between items, e.g. "View", lined up with the labels.
        void        MenuHeader(std::string_view text);
        /// A modal question with verb buttons, centered. Call it every frame while asking; it
        /// returns the index of the button chosen (once, on the frame after), else -1. Esc and
        /// the close button choose desc.cancelButton; Alt + a button's first letter chooses it.
        int         MessageDialog(ID id, const MessageDialogDesc& desc);
        /// Whether the user asked to close a window flagged WindowConfirmClose (its close
        /// button, Alt+F4, the OS quitting) since last asked. The window stays until the app
        /// closes it, e.g. after asking with a MessageDialog.
        bool        CloseRequested(ID windowID);

        /// A filter field for a big menu, first in it: it takes the keyboard when the menu
        /// opens, and while it holds text the items (and headers and separators) that
        /// don't contain it are left out.
        void        MenuFilter();

        /// Optional icon, label, optional shortcut. The icon column is reserved if any sibling has an icon.
        bool        MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, without a shortcut, in the theme's item style.
        inline bool MenuItem(ID id, const GUIContent& content, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}) { return MenuItem(id, content, std::string_view{}, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool MenuItem(       const GUIContent& content, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return MenuItem(idManager.AllocateDynamicID(site), content, std::string_view{}, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        /// With a shortcut; onClick is required so MenuItem(text, nullptr) stays unambiguous.
        inline bool MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick, LayoutOverride&& constraints = {}) { return MenuItem(id, content, shortcut, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool MenuItem(       const GUIContent& content, std::string_view shortcut, std::function<void()> onClick, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return MenuItem(idManager.AllocateDynamicID(site), content, shortcut, std::move(onClick), theme.menuItemRowStyleRef, std::move(constraints)); }

        /// Menu item for a command: icon, label, shortcut and callback come from the command.
        bool MenuItem(CommandID command);
        /// An item that destroys something (Delete, Discard), in the error color.
        bool        DestructiveMenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick, LayoutOverride&& constraints = {});
        /// As above, without a shortcut, with the call site as the ID.
        inline bool DestructiveMenuItem(       const GUIContent& content, std::function<void()> onClick, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return DestructiveMenuItem(idManager.AllocateDynamicID(site), content, std::string_view{}, std::move(onClick), std::move(constraints)); }
        /// Closes BeginMenu; called whether or not it returned true.
        void EndMenu();

        /// Menu bar: place BeginMenu triggers inside. Left/Right move between triggers,
        /// Down enters the dropdown, Up at its first item returns to the trigger.
        bool        BeginMenuBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's menu bar style.
        inline bool BeginMenuBar(ID id, LayoutOverride&& constraints = {}) { return BeginMenuBar(id, theme.menuBarStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginMenuBar(       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginMenuBar(idManager.AllocateDynamicID(site), theme.menuBarStyleRef, std::move(constraints)); }
        /// Closes BeginMenuBar.
        void EndMenuBar();

        ///@}

        /// @name Title bar, tooltips and context menus
        ///@{

        // ---- Title bar: a menu bar the window is dragged by ----
        // Whatever in it isn't a control is the caption: dragging it moves the window and a
        // double-click maximizes it. In a window with ElementFlags::WindowCustomTitleBar,
        // EndTitleBar adds the minimize, maximize / restore and close buttons.
        /// A menu bar the window is dragged by: what in it isn't a control is the caption
        /// (drag to move, double-click to maximize). In a window with
        /// ElementFlags::WindowCustomTitleBar, EndTitleBar adds the window buttons.
        bool        BeginTitleBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's title bar style.
        inline bool BeginTitleBar(ID id, LayoutOverride&& constraints = {}) { return BeginTitleBar(id, theme.titleBarStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginTitleBar(       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginTitleBar(idManager.AllocateDynamicID(site), theme.titleBarStyleRef, std::move(constraints)); }
        /// Closes BeginTitleBar, adding the window buttons in a custom-title-bar window.
        void        EndTitleBar();

        /// What the OS should do with a screen point of a window with a custom title bar.
        WindowHitArea TitleBarHitTest(ID windowID, int2 screenPos) const;

        /// Divider inside a menu or context menu.
        void MenuSeparator();

        /// A text tooltip on targetID, shown when the pointer rests on it.
        void SetTooltip(ID targetID, std::string_view text);
        /// As above, with an icon before the text.
        void SetTooltip(ID targetID, IconID icon, std::string_view text);
        /// As above, on the last registered element.
        inline void SetTooltip(std::string_view text) { SetTooltip(elementStorage.GetLastAddedID(), text); }
        /// As above, with an icon, on the last registered element.
        inline void SetTooltip(IconID icon, std::string_view text) { SetTooltip(elementStorage.GetLastAddedID(), icon, text); }

        /// A tooltip with any content on targetID: build it while this returns true.
        /// EndTooltip is called either way.
        bool BeginTooltip(ID targetID);
        /// As above, on the last registered element.
        inline bool BeginTooltip() { return BeginTooltip(elementStorage.GetLastAddedID()); }
        /// Closes BeginTooltip; called whether or not it returned true.
        void EndTooltip();

        /// Context menu: opens on right-click of the target element, below it.
        bool BeginContextMenu(ID targetID);
        /// As above, on the last registered element.
        inline bool BeginContextMenu() { return BeginContextMenu(elementStorage.GetLastAddedID()); }
        /// Closes BeginContextMenu; called whether or not it returned true.
        void EndContextMenu();
        /// Opens `targetID`'s context menu at a screen point, e.g. for something that isn't an
        /// element; BeginContextMenu(targetID) then builds it.
        void OpenContextMenu(ID targetID, int2 screenPos);

        ///@}

        /// @name Label, button and toggle groups
        ///@{

        /// Label group: Labels/DragLabels inside share one width, resizable by dragging
        /// any label's right edge. Widths in CSS pixels.
        bool        BeginLabelGroup(ID id, CssPx defaultWidth = 120, CssPx minWidth = 40, CssPx maxWidth = 500, bool resizable = true);
        /// As above, with the call site as the ID.
        inline bool BeginLabelGroup(CssPx defaultWidth = 120, CssPx minWidth = 40, CssPx maxWidth = 500, bool resizable = true, std::source_location site = std::source_location::current()) { return BeginLabelGroup(idManager.AllocateDynamicID(site), defaultWidth, minWidth, maxWidth, resizable); }
        /// Closes BeginLabelGroup.
        void        EndLabelGroup();

        /// Button / toggle group: Buttons / Field(bool) inside are restyled with
        /// .first-/.mid-/.last-* so they share borders. A single item keeps its plain style.
        bool        BeginButtonGroup(LayoutOverride&& constraints = {});
        /// Closes BeginButtonGroup.
        void        EndButtonGroup();
        /// The same for Toggles and Field(bool).
        bool        BeginToggleGroup(LayoutOverride&& constraints = {});
        /// Closes BeginToggleGroup.
        void        EndToggleGroup();

        ///@}
        /// @name Areas, caching and layout containers
        ///@{

        /// A container drawn in `style`, for grouping content; it scrolls when bounded and
        /// its style has overflow: auto.
        bool        BeginArea(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's area style.
        inline bool BeginArea(ID id,                 LayoutOverride&& constraints = {}) { return BeginArea(id, theme.collectionStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginArea(       StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginArea(idManager.AllocateDynamicID(site), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's area style.
        inline bool BeginArea(                       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginArea(idManager.AllocateDynamicID(site), theme.collectionStyleRef, std::move(constraints)); }
        /// Closes BeginArea.
        void EndArea();

        /// A container (like BeginArea) whose drawing is kept: while nothing changed it
        /// returns false, skipping the content, and the kept drawing is shown. EndCached is
        /// called either way. `id` must be the same every frame (for example a static ID). The content
        /// is rebuilt when its size changes, when input reaches it, when `signature`
        /// changes, or after Invalidate(id); change `signature` when it shows data the
        /// cache can't see change.
        bool        BeginCached(ID id, StyleRef style, uint64_t signature = 0, LayoutOverride&& constraints = {});
        /// As above, in the theme's area style.
        inline bool BeginCached(ID id, uint64_t signature = 0, LayoutOverride&& constraints = {}) { return BeginCached(id, theme.collectionStyleRef, signature, std::move(constraints)); }
        /// Closes BeginCached; called whether or not it returned true.
        void        EndCached();
        /// Rebuilds id's cached scope, and those around it, next frame. Safe from event handlers.
        void        Invalidate(ID id);

        /// Enables the retained-render cache (on by default). When off, every scope rebuilds.
        void        SetDrawCacheEnabled(bool enabled) { if (m_uiCache) m_uiCache->SetDrawCacheEnabled(enabled); }

        /// Invalidates a docked panel's cached content by panel id. Safe from event handlers.
        void        InvalidatePanel(ID panelId);

        /// Undecorated flex containers; the Wrap variants wrap onto further lines/columns.
        bool        BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's plain style.
        inline bool BeginHorizontal(ID id,                 LayoutOverride&& constraints = {}) { return BeginHorizontal(id, theme.emptyHorzStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginHorizontal(       StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginHorizontal(idManager.AllocateDynamicID(site), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's plain style.
        inline bool BeginHorizontal(                       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginHorizontal(idManager.AllocateDynamicID(site), theme.emptyHorzStyleRef, std::move(constraints)); }
        /// Closes BeginHorizontal.
        void EndHorizontal();

        /// A horizontal container whose children wrap onto further lines.
        bool        BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's plain style.
        inline bool BeginHorizontalWrap(ID id,                 LayoutOverride&& constraints = {}) { return BeginHorizontalWrap(id, theme.emptyHorzWrapStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginHorizontalWrap(       StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginHorizontalWrap(idManager.AllocateDynamicID(site), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's plain style.
        inline bool BeginHorizontalWrap(                       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginHorizontalWrap(idManager.AllocateDynamicID(site), theme.emptyHorzWrapStyleRef, std::move(constraints)); }
        /// Closes BeginHorizontalWrap.
        void EndHorizontalWrap();

        /// A vertical container.
        bool        BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's plain style.
        inline bool BeginVertical(ID id,                 LayoutOverride&& constraints = {}) { return BeginVertical(id, theme.emptyVertStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginVertical(       StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginVertical(idManager.AllocateDynamicID(site), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's plain style.
        inline bool BeginVertical(                       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginVertical(idManager.AllocateDynamicID(site), theme.emptyVertStyleRef, std::move(constraints)); }
        /// Closes BeginVertical.
        void EndVertical();

        /// A vertical container whose children wrap onto further columns.
        bool        BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's plain style.
        inline bool BeginVerticalWrap(ID id,                 LayoutOverride&& constraints = {}) { return BeginVerticalWrap(id, theme.emptyVertWrapStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginVerticalWrap(       StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginVerticalWrap(idManager.AllocateDynamicID(site), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's plain style.
        inline bool BeginVerticalWrap(                       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginVerticalWrap(idManager.AllocateDynamicID(site), theme.emptyVertWrapStyleRef, std::move(constraints)); }
        /// Closes BeginVerticalWrap.
        void EndVerticalWrap();

        /// Toolbar scope: a wrap container that swaps theme.controls to the toolbar styles,
        /// so default-style widgets inside use their toolbar variants.
        bool BeginToolbar(bool horizontal = true);
        /// Closes BeginToolbar.
        void EndToolbar();

        ///@}

        /// @name Scrollbars
        ///@{

        // ---- Scrollbars ----
        // Call from INSIDE the scrollable container `containerID`: the bar overlays
        // its border box, unaffected by its scroll offset. Paints nothing without
        // overflow, so it can always be emitted.
        /// A vertical scrollbar for containerID, called from inside it: it overlays the
        /// container's border box and draws nothing without overflow.
        void VerticalScrollbar  (ID scrollBarID, ID containerID);
        /// As above, with an ID derived from the container's.
        void VerticalScrollbar  (ID containerID) { (void)idManager.PushParent(containerID); const ID barID = idManager.AllocateDynamicID("vertical_scrollbar"); (void)idManager.PopParent(); VerticalScrollbar(barID, containerID); }
        /// As VerticalScrollbar, horizontal.
        void HorizontalScrollbar(ID scrollBarID, ID containerID);
        /// As above, with an ID derived from the container's.
        void HorizontalScrollbar(ID containerID) { (void)idManager.PushParent(containerID); const ID barID = idManager.AllocateDynamicID("horizontal_scrollbar"); (void)idManager.PopParent(); HorizontalScrollbar(barID, containerID); }

        /// Both bars plus a corner cap; each hides itself without overflow. `lastInnerSize`
        /// (the container's last-frame inner size) trims the bars around the cap.
        void ScrollbarOverlay(ID containerID,
                              int2 lastInnerSize = int2{0, 0},
                              ID vertScrollBarID = ID::Invalid(),
                              ID horzScrollBarID = ID::Invalid(),
                              bool emitVert = true,
                              bool emitHorz = true);

        ///@}

        /// @name Reorderable groups
        ///@{

        // ---- Reorderable Group: drag-to-reorder container ----
        // onReorder(fromIndex, toIndex) fires on drop. isWrapped allows multi-line layout.
        // onDragOut(index, screenPos): an item was released outside the group.
        // dragVisualizer(index, screenPos): each frame while dragging outside the group,
        // typically to draw a ghost window; suppresses the in-place cross-axis offset.
        /// A container of BeginReorderableItem scopes the user reorders by dragging.
        /// onReorder(from, to) runs on drop; isWrapped lets items wrap onto more lines.
        bool        BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped, std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style, LayoutOverride&& constraints = {}, std::function<void(int, tachyon::int2)> onDragOut = nullptr, std::function<void(int, tachyon::int2)> dragVisualizer = nullptr);
        /// As above, not wrapped.
        inline bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style, LayoutOverride&& constraints = {})
        { return BeginReorderableGroup(id, direction, false, std::move(onReorder), style, std::move(constraints)); }
        /// As above, not wrapped, in the theme's style for the direction.
        inline bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder, LayoutOverride&& constraints = {})
        {
            return BeginReorderableGroup(id, direction, false, std::move(onReorder),
                direction == ReorderDirection::Horizontal ? theme.horzReorderableGroupStyleRef : theme.vertReorderableGroupStyleRef,
                std::move(constraints));
        }
        /// As above, in the theme's style for the direction and wrapping.
        inline bool BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped, std::function<void(int fromIndex, int toIndex)> onReorder, LayoutOverride&& constraints = {})
        {
            StyleRef style;
            if (isWrapped)
                style = direction == ReorderDirection::Horizontal ? theme.horzReorderableGroupWrapStyleRef : theme.vertReorderableGroupWrapStyleRef;
            else
                style = direction == ReorderDirection::Horizontal ? theme.horzReorderableGroupStyleRef : theme.vertReorderableGroupStyleRef;
            return BeginReorderableGroup(id, direction, isWrapped, std::move(onReorder), style, std::move(constraints));
        }
        /// Closes BeginReorderableGroup.
        void EndReorderableGroup();

        /// One draggable item of a reorderable group; any content goes inside.
        bool        BeginReorderableItem(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's item style.
        inline bool BeginReorderableItem(ID id, LayoutOverride&& constraints = {}) { return BeginReorderableItem(id, theme.reorderableItemStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginReorderableItem(StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginReorderableItem(idManager.AllocateDynamicID(site), style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's item style.
        inline bool BeginReorderableItem(LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginReorderableItem(idManager.AllocateDynamicID(site), theme.reorderableItemStyleRef, std::move(constraints)); }
        /// Closes BeginReorderableItem.
        void EndReorderableItem();

        /// Non-interactive drag handle for use inside BeginReorderableItem; clicks reach the item.
        void EmitDragHandle();

        ///@}

        /// @name List and tree views
        ///@{

        // ---- ListView: scrollable list with selectable items ----
        /// Opens a list view of ListItems. selectedIndex (optional) holds the selection, which a
        /// click or Up / Down changes; itemCount bounds the keys. dragDrop (optional) lets items be dragged.
        bool        BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, without drag and drop.
        inline bool BeginListView(ID id, int* selectedIndex, int itemCount, StyleRef style, LayoutOverride&& constraints = {}) { return BeginListView(id, selectedIndex, itemCount, nullptr, style, std::move(constraints)); }
        /// As above, without drag and drop, in the theme's list style.
        inline bool BeginListView(ID id, int* selectedIndex, int itemCount, LayoutOverride&& constraints = {}) { return BeginListView(id, selectedIndex, itemCount, nullptr, theme.listViewStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginListView(       int* selectedIndex, int itemCount, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginListView(idManager.AllocateDynamicID(site), selectedIndex, itemCount, nullptr, theme.listViewStyleRef, std::move(constraints)); }
        /// As above, with drag and drop, in the theme's list style.
        inline bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, LayoutOverride&& constraints = {}) { return BeginListView(id, selectedIndex, itemCount, dragDrop, theme.listViewStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginListView(       int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginListView(idManager.AllocateDynamicID(site), selectedIndex, itemCount, dragDrop, theme.listViewStyleRef, std::move(constraints)); }
        /// Closes BeginListView.
        void EndListView();

        /// A list item's index by ID, for end-of-frame handlers; -1 if unknown. Filled only for
        /// lists with drag and drop.
        int FindListItemIndex(tachyon::ID id) const;

        /// One item of a list view, drawn selected when it is the selected index. True when clicked.
        bool        ListItem(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's item style.
        inline bool ListItem(ID id, const GUIContent& content, LayoutOverride&& constraints = {}) { return ListItem(id, content, theme.listItemStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool ListItem(       const GUIContent& content, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return ListItem(idManager.AllocateDynamicID(site), content, theme.listItemStyleRef, std::move(constraints)); }


        /// Recomputes skipCount / ancestorLineMask / isLastSibling from row depths.
        /// Call after any structural change. O(n^2) worst case.
        static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);

        /// Writes the flat indices of visible rows (honoring isOpen) to visibleIdx.
        static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows,
                                           std::vector<int>& visibleIdx);

        /// Single-column virtualized tree view (a VirtualTable with one TreeCell column).
        /// A non-empty header shows the header row.
        void VirtualTreeView(ID id,
                             std::vector<TreeViewRow>& rows,
                             std::vector<int>& visibleIdx,
                             TableState& state,
                             StyleRef style = {},
                             LayoutOverride&& constraints = {},
                             std::string_view header = {});

        /// The tree row a drag is over (drives the drop indicator), or Invalid.
        ID TreeDropTargetRow() const { return m_treeDropState.targetRowID; }

        /// Items emitted in between render as selected.
        bool BeginSelected() { guiStateManager.Construction().PushSelected(); return true; }
        /// Closes BeginSelected.
        void EndSelected()   { guiStateManager.Construction().PopSelected(); }
        /// Controls emitted in between are disabled: drawn dimmed, and they take no input.
        bool BeginDisabled() { guiStateManager.Construction().PushDisabled(); return true; }
        /// Closes BeginDisabled.
        void EndDisabled()   { guiStateManager.Construction().PopDisabled(); }

        ///@}

        /// @name Grid views
        ///@{

        // ---- GridView: 2D icon grid (like file explorer) ----
        /// Opens a grid view of GridViewItems. selectedIndex (optional) holds a single selection,
        /// multiSelect (optional) a multi-selection; dragDrop (optional) lets items be dragged.
        /// Arrow keys move the selection.
        bool        BeginGridView(ID id, int* selectedIndex, StyleRef style, MultiSelectState* multiSelect = nullptr, ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {});
        /// As above, in the theme's grid style.
        inline bool BeginGridView(ID id, int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr, ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {}) { return BeginGridView(id, selectedIndex, theme.gridViewStyleRef, multiSelect, dragDrop, std::move(constraints)); }
        /// Closes BeginGridView.
        void EndGridView();

        /// renameBuffer (optional) enables in-place rename: F2, Ctrl+Shift+Click or a slow
        /// re-click opens an editor. Enter commits; Escape or focus loss cancels.
        bool        GridViewItem(ID id, const GUIContent& content,
                                 tachyon::ElementState extraState = tachyon::ElementState::None,
                                 std::string* renameBuffer = nullptr,
                                 StyleRef itemStyle = {},
                                 LayoutOverride&& constraints = {});
        /// As above, without rename.
        inline bool GridViewItem(ID id, const GUIContent& content, LayoutOverride&& constraints)
            { return GridViewItem(id, content, tachyon::ElementState::None, nullptr, {}, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool GridViewItem(const GUIContent& content, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current())
            { return GridViewItem(idManager.AllocateDynamicID(site), content, tachyon::ElementState::None, nullptr, {}, std::move(constraints)); }

        /// Whether an inline rename editor (a grid item's, a tree row's) is open; an app that
        /// sorts by name re-sorts once it closes.
        bool IsRenaming() const { return m_renameSub && m_renameSub->isActive; }


        /// generateItem(index, itemID) must emit exactly one cell, typically GridViewItem(itemID, ...).
        /// itemID is a stable ranged sub-ID (index = id.value.subRangeParts.index), used for
        /// selection and drag payloads. Cells are cellSize, with a 4px gap between columns.
        void VirtualGridView(ID id, GridViewVirtualState& vstate,
                             int itemCount, int2 cellSize,
                             const std::function<void(int index, ID itemID)>& generateItem,
                             int* selectedIndex = nullptr,
                             MultiSelectState* multiSelect = nullptr,
                             ItemDragDropConfig* dragDrop = nullptr,
                             StyleRef scrollStyle = {},
                             StyleRef rowStyle = {},
                             LayoutOverride&& constraints = {});

        ///@}

        /// @name Tables
        ///@{

        // ---- Table: row x column data grid ----
        /// Opens a table. Columns are described each frame; widths, selection and sort state
        /// live in the caller's TableState. Rows are BeginRow / EndRow with Cell(text) or
        /// Cell(emit); EndTable builds them as a VirtualTable. `columns` must stay valid until
        /// EndTable.
        bool  BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount,
                         TableState& state, StyleRef style,
                         LayoutOverride&& constraints = {});
        /// As above, in the theme's table style.
        inline bool BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount,
                               TableState& state, LayoutOverride&& constraints = {})
            { return BeginTable(id, columns, columnCount, state, theme.tableStyleRef, std::move(constraints)); }
        /// As above, for a C array of columns.
        template <size_t N>
        inline bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state,
                               StyleRef style, LayoutOverride&& constraints = {})
            { return BeginTable(id, columns, N, state, style, std::move(constraints)); }
        /// As above, for a C array of columns, in the theme's table style.
        template <size_t N>
        inline bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state,
                               LayoutOverride&& constraints = {})
            { return BeginTable(id, columns, N, state, theme.tableStyleRef, std::move(constraints)); }
        /// Builds the recorded rows as a VirtualTable.
        void EndTable();

        /// Starts a row; its Cells fill the columns in order.
        bool BeginRow();
        /// Ends the row.
        void EndRow();

        /// Cells auto-advance to the next column.
        void Cell(std::string_view text);

        /// Custom-content cell: `emit` runs inside the cell during EndTable, so its
        /// captures must live until then.
        void Cell(std::function<void()> emit);

        /// Tree-style first column: [indent][arrow or spacer][optional icon][text].
        ///
        ///   depth              horizontal indent (pixels = depth * theme.treeTableIndent).
        ///   isOpen             null for leaves (no arrow). Non-null toggles on click.
        ///   icon               optional leading icon rendered between arrow and text.
        ///   ancestorLineMask   bit d set iff the ancestor at depth d+1 has a next sibling
        ///                      (its guide line passes through this row at column d).
        ///   isLastSibling      true -> own-depth L-tick stops at row center.
        ///   renameBuffer       optional; enables in-place rename (F2 / Ctrl+Shift+Click / slow re-click).
        ///   reserveExpander    false drops the arrow slot for leaves (for flat trees).
        ///
        /// Arrow clicks toggle without selecting; other clicks select the row.
        void TreeCell(std::string_view text, int depth, bool* isOpen,
                      IconID icon = IconID::Invalid,
                      uint32_t ancestorLineMask = 0,
                      bool isLastSibling = true,
                      std::string* renameBuffer = nullptr,
                      bool reserveExpander = true);

        /// Virtual-scrolled table: emits only the visible rows, under one ranged ID.
        /// Frozen columns get their own body section (left / mid / right), scroll-synced.
        ///
        ///   columns       : each carries its own generateCell(rowIndex), so reordering
        ///                   the array reorders headers and cells together. A C array
        ///                   converts to the span.
        ///   rowHeight     : row-height floor; 0 uses the style's natural row height.
        /// Headers, cell style and separators are TableState options.
        void VirtualTable(ID id, TableState& state,
                          std::span<VirtualTableColumn> columns,
                          int totalRowCount,
                          CssPx rowHeight = 0,
                          StyleRef style = {},
                          LayoutOverride&& constraints = {});

        ///@}

        /// @name Breadcrumb
        ///@{

        // ---- Breadcrumb ----
        /// A path such as Graphs > Terrain material: segments separated by chevrons, the last
        /// one current. Clicking another segment calls onSelect(index) and returns true.
        bool        Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's breadcrumb style.
        inline bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, LayoutOverride&& constraints = {}) { return Breadcrumb(id, segments, std::move(onSelect), theme.breadcrumbStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Breadcrumb(       std::span<const std::string> segments, std::function<void(int)> onSelect, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Breadcrumb(idManager.AllocateDynamicID(site), segments, std::move(onSelect), theme.breadcrumbStyleRef, std::move(constraints)); }

        ///@}

        /// @name Status bar
        ///@{

        // ---- Status bar ----
        /// A strip of short status texts, usually along a window's bottom edge. Give one
        /// StatusText FlexGrow(1) to push the texts after it to the right.
        bool        BeginStatusBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's status bar style.
        inline bool BeginStatusBar(ID id, LayoutOverride&& constraints = {}) { return BeginStatusBar(id, theme.statusBarStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool BeginStatusBar(       LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return BeginStatusBar(idManager.AllocateDynamicID(site), theme.statusBarStyleRef, std::move(constraints)); }
        /// Closes BeginStatusBar.
        void        EndStatusBar();
        /// A text in the status bar.
        void        StatusText(ID id, std::string_view text, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline void StatusText(       std::string_view text, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { StatusText(idManager.AllocateDynamicID(site), text, std::move(constraints)); }

        ///@}

        /// @name Command search
        ///@{

        // ---- Command search ----
        /// Finds and runs commandManager's commands: focused, it lists those whose description
        /// contains the query; Up / Down move through them, Enter or a click runs one, Escape
        /// leaves. `state` must outlive the frame. True when a command ran since the last call.
        bool        CommandSearch(ID id, CommandSearchState& state, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool CommandSearch(       CommandSearchState& state, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return CommandSearch(idManager.AllocateDynamicID(site), state, std::move(constraints)); }
        /// Puts the keyboard in the search next frame; bind it to a command such as Ctrl+K.
        static void FocusCommandSearch(CommandSearchState& state) { state.focusRequested = true; }

        ///@}

        /// @name Node view
        ///@{

        // ---- Node view: self-contained graph editor ----
        // All state lives in `view`, so several views can coexist.
        /// Builds `view` (canvas, nodes, wires and overlays) in the current container.
        void RenderNodeView(NodeView& view);

        /// Text property body aligned by the row's sockets: left => left, right => right,
        /// both => centre. Call inside NodeProperty::renderBody.
        void NodeText(std::string_view text);

        ///@}

        /// @name Tabs
        ///@{

        // ---- TabBar: reorderable tabs ----
        /// Opens a tab bar of Tabs, which wrap onto more rows when it is narrow. selectedIndex
        /// holds the selected tab; onReorder(from, to) runs on a drop, onClose(index) closes a tab
        /// (none when null); onDragOut and dragVisualizer as for BeginReorderableGroup.
        bool BeginTabBar(ID id, int* selectedIndex,
                         std::function<void(int, int)> onReorder,
                         std::function<void(int)> onClose = nullptr,
                         std::function<void(int, tachyon::int2)> onDragOut = nullptr,
                         std::function<void(int, tachyon::int2)> dragVisualizer = nullptr,
                         StyleRef style = {},
                         LayoutOverride&& constraints = {});
        /// Closes BeginTabBar.
        void EndTabBar();

        /// One tab of the tab bar; a click selects it. True when clicked.
        bool        Tab(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's tab style.
        inline bool Tab(ID id, const GUIContent& content, LayoutOverride&& constraints = {}) { return Tab(id, content, theme.tabStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Tab(       const GUIContent& content, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Tab(idManager.AllocateDynamicID(site), content, theme.tabStyleRef, std::move(constraints)); }

        ///@}

        /// @name Docking
        ///@{

        // ---- DockingContainer ----
        /// Builds `layout`'s docked panels (each one's onPanelGUI) in a container, enabling the
        /// visible panels first. Then call EmitFloatingDockWindows.
        bool BeginDockingContainer(ID id, DockingLayout* layout, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's dock style.
        inline bool BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints = {})
            { return BeginDockingContainer(id, layout, theme.dockContainerStyleRef, std::move(constraints)); }
        /// Closes BeginDockingContainer.
        void EndDockingContainer();

        ///@}

        /// @name Fields
        ///@{

        /// A bool as a Toggle labelled `text`.
        bool        Field(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool Field(       bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field(idManager.AllocateDynamicID(site), value, text, style, std::move(constraints)); }
        /// As above, in the theme's toggle style.
        inline bool Field(ID id, bool& value, std::string_view text,                 LayoutOverride&& constraints = {}) { return Field(id,                            value, text, theme.controls->toggle, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's toggle style.
        inline bool Field(       bool& value, std::string_view text,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field(idManager.AllocateDynamicID(site), value, text, theme.controls->toggle, std::move(constraints)); }

        /// A text field editing `value`; true when it changed. With ContinuousUpdate the value
        /// changes on every keystroke, else when the edit is committed.
        bool        Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool Field(       std::string& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field(idManager.AllocateDynamicID(site), value, textEditBehavior, style, std::move(constraints)); }
        /// As above, updating continuously.
        inline bool Field(ID id, std::string& value, StyleRef style, LayoutOverride&& constraints = {}) { return Field(id, value, TextEditBehavior::ContinuousUpdate, style, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Field(       std::string& value, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field(idManager.AllocateDynamicID(site), value, TextEditBehavior::ContinuousUpdate, style, std::move(constraints)); }
        /// As the first, in the theme's text box style.
        inline bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}) { return Field(id,                            value, textEditBehavior, theme.controls->textbox, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Field(       std::string& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field(idManager.AllocateDynamicID(site), value, textEditBehavior, theme.controls->textbox, std::move(constraints)); }
        /// As the first, updating continuously, in the theme's text box style.
        inline bool Field(ID id, std::string& value, LayoutOverride&& constraints = {}) { return Field(id,                            value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool Field(       std::string& value, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field(idManager.AllocateDynamicID(site), value, TextEditBehavior::ContinuousUpdate, theme.controls->textbox, std::move(constraints)); }

        /// A Field with a leading icon inside its border.
        bool        SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior = TextEditBehavior::ContinuousUpdate, LayoutOverride&& constraints = {})
        { return SearchField(id, value, icon, textEditBehavior, std::move(constraints), nullptr); }
        /// As above, with the call site as the ID.
        inline bool SearchField(       std::string& value, IconID icon, TextEditBehavior textEditBehavior = TextEditBehavior::ContinuousUpdate, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return SearchField(idManager.AllocateDynamicID(site), value, icon, textEditBehavior, std::move(constraints)); }




        /// A number field; for a vector type (TypeDimensions), a field per component, each after
        /// a colored axis label that drags it. config sets the drag step and the range. True when
        /// the value changed.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, TextEditBehavior textEditBehavior, StyleRef style, NumericFieldConfig<ComponentT> config = {}, LayoutOverride&& constraints = {})
        {
            if (guiStateManager.Construction().IsIndeterminate())
            {
                RegisterControl(tachyon::Element { .id = id, .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                    [this](tachyon::Element& element) { return textSystem->RegisterText(element.id, "\xe2\x80\x94", element.styleRef, element.state); },
                    std::move(constraints));
                return false;
            }

			if constexpr (TypeDimensions<T>::dimensions == 1)
            {
                // Inside a node row, fill the body after the label so the node stays grid-aligned.
                if (nodeRowFieldMinWidth > 0 && !constraints.HasFixedWidth())
                    constraints.FlexGrow(1).MinWidth(Pixels{ nodeRowFieldMinWidth });
                bool modified = false;
                RegisterControl(tachyon::Element { .id = id, .eventHandler = MakeNumericalBoxHandler(guiStateManager, value, config), .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                                [this, &modified, &value, textEditBehavior, config](tachyon::Element& element) {
                                    const TextID textID = textSystem->RegisterEditableNumber(element.id, value, textEditBehavior, element.styleRef, element.state, modified, config);
                                    if constexpr (std::is_floating_point_v<T>)
                                        if (std::isnan(value))
                                            textSystem->SetTextColor(textID, theme.nanTextColor);   // NaN reads as a warning
                                    return textID;
                                },
                                std::move(constraints));
                guiStateManager.SetModified(modified);
                return modified;
            } else
            {
                bool modified = false;
                // Width 0 + flex-grow: the group fills remaining space after the label.
                constraints.Width(0);
                RegisterElement(tachyon::Element{ .id = id, .eventHandler = MakeMultiDimDragHandler<T>(guiStateManager, value, config), .styleRef = theme.fieldElementGroupStyleRef }, std::move(constraints));
                VERIFY(elementStorage.BeginChildren());
                {
					// Each component: a colored axis label (drag it to change the value) attached
                    // to the component's field. A caller's own style replaces the attached field.
                    const StyleRef componentStyle = (style == theme.controls->number || style == theme.controls->textbox) ? theme.vectorComponentStyleRef : style;
                    for (int i = 0; i < TypeDimensions<T>::dimensions; i++)
                    {
                        DragLabel(TypeDimensions<T>::names[i], theme.axisLabelStyleRefs[i % 4],
                            LayoutOverride::FixedWidth(Pixels{ theme.axisLabelWidth }).KeepWithNext());   // wraps with its field
                        modified = Field(TypeDimensions<T>::GetComponent(value, i), textEditBehavior, componentStyle, config, LayoutOverride::Auto()
                            .MinWidth(40)) || modified;
                    }
                }
                VERIFY(elementStorage.EndChildren());
                return modified;
            }
        }
        /// As above, in the theme's number style.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, TextEditBehavior textEditBehavior, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}) { return Field<T>(id, value, textEditBehavior, theme.controls->number, config, std::move(constraints)); }
        /// As above, with the default config.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}) { return Field<T>(id, value, textEditBehavior, theme.controls->number, {}, std::move(constraints)); }
        /// As above, updating continuously.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(ID id, T& value, LayoutOverride&& constraints = {}) { return Field<T>(id, value, TextEditBehavior::ContinuousUpdate, theme.controls->number, {}, std::move(constraints)); }
        /// As the first, with the call site as the ID.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, StyleRef style, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, textEditBehavior, style, config, std::move(constraints)); }
        /// As the first, with the call site as the ID, updating continuously.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(T& value, StyleRef style, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, TextEditBehavior::ContinuousUpdate, style, config, std::move(constraints)); }
        /// As the first, with the call site as the ID and the default config.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, textEditBehavior, style, {}, std::move(constraints)); }
        /// As the first, with the call site as the ID, in the theme's number style.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, textEditBehavior, theme.controls->number, config, std::move(constraints)); }
        /// As the first, with the call site as the ID, updating continuously, in the theme's number style.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(T& value, NumericFieldConfig<ComponentT> config, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, TextEditBehavior::ContinuousUpdate, theme.controls->number, config, std::move(constraints)); }
        /// As the first, with the call site as the ID and the default config, in the theme's number style.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, TextEditBehavior textEditBehavior, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, textEditBehavior, theme.controls->number, {}, std::move(constraints)); }
        /// As the first, with the call site as the ID and the default config, updating continuously, in
        /// the theme's number style.
        template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
        inline bool Field(       T& value, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Field<T>(idManager.AllocateDynamicID(site), value, TextEditBehavior::ContinuousUpdate, theme.controls->number, {}, std::move(constraints)); }
        

        /// A number's type as a short tag: f32, f64, i8 ... u64 (empty for other types).
        template<typename T>
        static constexpr std::string_view TypeTag()
        {
            if constexpr (std::is_same_v<T, float>)         return "f32";
            else if constexpr (std::is_same_v<T, double>)   return "f64";
            else if constexpr (std::is_same_v<T, int8_t>)   return "i8";
            else if constexpr (std::is_same_v<T, int16_t>)  return "i16";
            else if constexpr (std::is_same_v<T, int32_t>)  return "i32";
            else if constexpr (std::is_same_v<T, int64_t>)  return "i64";
            else if constexpr (std::is_same_v<T, uint8_t>)  return "u8";
            else if constexpr (std::is_same_v<T, uint16_t>) return "u16";
            else if constexpr (std::is_same_v<T, uint32_t>) return "u32";
            else if constexpr (std::is_same_v<T, uint64_t>) return "u64";
            else if constexpr (std::is_same_v<T, std::string>) return "string";
            else return {};
        }

        /// A number field with its type after the value, e.g. "0.5  f32".
        template<typename T>
        bool TypedField(ID id, T& value, LayoutOverride&& constraints = {})
        {
            BeginHorizontal(idManager.AllocateDynamicID("typed_field"), theme.typedFieldStyleRef, std::move(constraints));
            const bool changed = Field(id, value, TextEditBehavior::ContinuousUpdate, theme.typedFieldInputStyleRef, {},
                                       LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("typed_field_type"), TypeTag<T>(), theme.typedFieldTypeStyleRef);
            EndHorizontal();
            return changed;
        }
        /// As above, with the call site as the ID.
        template<typename T>
        bool TypedField(T& value, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return TypedField(idManager.AllocateDynamicID(site), value, std::move(constraints)); }

        /// A reorderable list: each row a grip, its index, the field (numbers with their type)
        /// and a remove button; "Add item" after the rows. groupID must be stable so the
        /// reorder state survives across frames.
        template<typename T>
        void Field(ID groupID, std::vector<T>& items, StyleRef style, LayoutOverride layoutOverride = {}, std::source_location site = std::source_location::current())
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

                if (BeginReorderableGroup(groupID, ReorderDirection::Vertical, std::move(onReorder),
                                          theme.collectionListStyleRef, tachyon::LayoutOverride{}.FlexGrow(1)))
                {
                    for (int i = 0; i < (int)items.size(); i++)
                    {
                        BeginReorderableItem(idManager.AllocateDynamicID(site), theme.collectionRowStyleRef,
                                             tachyon::LayoutOverride{}.FlexGrow(1));
                        {
                            EmitDragHandle();
                            Text(idManager.AllocateDynamicID("list_index"), std::to_string(i), theme.collectionIndexStyleRef);
                            if constexpr (std::is_arithmetic_v<T>)
                                TypedField(items[i], tachyon::LayoutOverride{}.FlexGrow(1));
                            else
                                Field(items[i], tachyon::LayoutOverride{}.FlexGrow(1));
                            Button(theme.tabCloseIcon, [&items, i]() { items.erase(items.begin() + i); },
                                   theme.collectionRemoveStyleRef, tachyon::LayoutOverride{}.NoGrow());
                        }
                        EndReorderableItem();
                    }
                    EndReorderableGroup();
                }
                Button(idManager.AllocateDynamicID("list_add"), "+  Add item", [&items]() { items.push_back({}); },
                       theme.collectionAddStyleRef);
                EndArea();
            }
        }

        /// As above, in the theme's collection style.
        template<typename T>
        void Field(ID groupID, std::vector<T>& items, LayoutOverride layoutOverride = {}) { Field(groupID, items, theme.collectionAreaStyleRef, layoutOverride); }

        /// A key-value list (std::string keys, V values): a Key / Value header with the types,
        /// then a row per entry (key, value, remove) and "Add entry".
        template<typename V>
        void Field(std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride layoutOverride = {})
        {
            if (BeginArea(style, std::move(layoutOverride)))
            {
                CollectionHeader(TypeTag<V>());
                for (int i = 0; i < (int)items.size(); i++)
                {
                    BeginHorizontal(idManager.AllocateDynamicID("dictionary_row"), theme.collectionEntryStyleRef, tachyon::LayoutOverride{}.NoShrink());
                    Field(items[i].first,  tachyon::LayoutOverride{}.FlexGrow(1));
                    Field(items[i].second, TextEditBehavior::ContinuousUpdate, theme.collectionValueStyleRef, tachyon::LayoutOverride{}.FlexGrow(1));
                    Button(theme.tabCloseIcon, [&items, i]() { items.erase(items.begin() + i); },
                           theme.collectionRemoveStyleRef, tachyon::LayoutOverride{}.NoGrow());
                    EndHorizontal();
                }
                Button(idManager.AllocateDynamicID("dictionary_add"), "+  Add entry",
                       [&items]() { items.push_back({ std::string{}, V{} }); }, theme.collectionAddEntryStyleRef);
                EndArea();
            }
        }
        /// As above, in the theme's collection style.
        template<typename V>
        void Field(std::vector<std::pair<std::string, V>>& items, LayoutOverride layoutOverride = {}) { Field(items, theme.collectionAreaStyleRef, layoutOverride); }


		/// Key-value list over an unordered_map. Row order can change when the map rehashes;
        /// use the vector-of-pairs version for stable rows.
        template<typename V>
        void Field(std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride layoutOverride = {})
        {
            if (BeginArea(style, std::move(layoutOverride)))
            {
                CollectionHeader(TypeTag<V>());
                // Map keys are const: edit a copy, committed on Enter/blur, and re-key
                // after the loop (renaming onto an existing key is ignored).
                std::string renameFrom, renameTo;
                bool        rename = false;
                for (auto& pair : items)
                {
                    BeginHorizontal(idManager.AllocateDynamicID("dictionary_row"), theme.collectionEntryStyleRef, tachyon::LayoutOverride{}.NoShrink());
                    std::string key = pair.first;
                    if (Field(key, TextEditBehavior::None, tachyon::LayoutOverride{}.FlexGrow(1)) && key != pair.first)
                    {
                        renameFrom = pair.first;
                        renameTo   = key;
                        rename     = true;
                    }
                    Field(pair.second, TextEditBehavior::ContinuousUpdate, theme.collectionValueStyleRef, tachyon::LayoutOverride{}.FlexGrow(1));
                    Button(theme.tabCloseIcon, [&items, k = pair.first]() { items.erase(k); },
                           theme.collectionRemoveStyleRef, tachyon::LayoutOverride{}.NoGrow());
                    EndHorizontal();
                }
                if (rename && !items.contains(renameTo))
                {
                    auto node = items.extract(renameFrom);
                    node.key() = renameTo;
                    items.insert(std::move(node));
                }
                Button(idManager.AllocateDynamicID("dictionary_add"), "+  Add entry",
                       [&items]() { items.emplace(std::string{}, V{}); }, theme.collectionAddEntryStyleRef);
                EndArea();
            }
        }
        /// As above, in the theme's collection style.
        template<typename V>
        void Field(std::unordered_map<std::string, V>& items, LayoutOverride layoutOverride = {}) { Field(items, theme.collectionAreaStyleRef, layoutOverride); }

        /// A dictionary's header row: "Key string", "Value <type>".
        void CollectionHeader(std::string_view valueType);

        ///@}


        /// @name Color and gradient fields
        ///@{

        // ---- ColorField: swatch + color picker popup ----
        /// A color swatch that opens a color picker; true when the color changed. allowHDR lets
        /// the picker take brightness over 1.
        bool        ColorField(ID id, tachyon::color4f& value, bool allowHDR = false, LayoutOverride&& constraints = {});
        /// As above, for an 8-bit color.
        bool        ColorField(ID id, tachyon::color4b& value, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool ColorField(       tachyon::color4f& value, bool allowHDR = false, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return ColorField(idManager.AllocateDynamicID(site), value, allowHDR, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline bool ColorField(       tachyon::color4b& value, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current())                        { return ColorField(idManager.AllocateDynamicID(site), value, std::move(constraints)); }

        // ---- GradientField: gradient swatch + editor popup ----
        /// A gradient swatch that opens a gradient editor; true when the gradient changed.
        bool        GradientField(ID id, tachyon::Gradient& value, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool GradientField(       tachyon::Gradient& value, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return GradientField(idManager.AllocateDynamicID(site), value, std::move(constraints)); }

        ///@}

        /// @name Sliders
        ///@{

        // ---- SliderField: DragLabel + horizontal slider track + numeric textbox ----
        /// A label, a slider track and a number field for value in [minVal, maxVal]; true when
        /// it changed.
        bool        SliderField(ID id, float& value, float minVal, float maxVal,
                                std::string_view text, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool SliderField(float& value, float minVal, float maxVal,
                                std::string_view text, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return SliderField(idManager.AllocateDynamicID(site), value, minVal, maxVal, text, std::move(constraints)); }

        /// Integer variant: snaps to whole numbers.
        bool        SliderField(ID id, int& value, int minVal, int maxVal,
                                std::string_view text, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool SliderField(int& value, int minVal, int maxVal,
                                std::string_view text, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return SliderField(idManager.AllocateDynamicID(site), value, minVal, maxVal, text, std::move(constraints)); }

        /// Groove + knob without value text; drag-only, integer-valued.
        bool        Slider(ID id, int& value, int minVal, int maxVal, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline bool Slider(int& value, int minVal, int maxVal, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { return Slider(idManager.AllocateDynamicID(site), value, minVal, maxVal, std::move(constraints)); }



        ///@}

        /// @name Text, labels and images
        ///@{

        /// A label whose horizontal drag changes the value of the field after it; a click focuses
        /// that field and Escape cancels a drag. In a label group it takes the group's width.
        void        DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's drag label style.
        inline void DragLabel(ID id, std::string_view text,                 LayoutOverride&& constraints = {}) { DragLabel(id, text, theme.dragLabelStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline void DragLabel(       std::string_view text, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { DragLabel(idManager.AllocateDynamicID(site), text, style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's drag label style.
        inline void DragLabel(       std::string_view text,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { DragLabel(idManager.AllocateDynamicID(site), text, theme.dragLabelStyleRef, std::move(constraints)); }

        /// A label for the control after it: a click focuses that control. In a label group it
        /// takes the group's width.
        void        Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
        /// A label over a field taller than a row (a list): at the top, with a caption under
        /// it such as "3 items".
        void        Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints = {});
        /// As above, with the call site as the ID.
        inline void Label(       std::string_view text, std::string_view caption, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Label(idManager.AllocateDynamicID(site), text, caption, std::move(constraints)); }
        /// As the first, in the theme's label style.
        inline void Label(ID id, std::string_view text,                 LayoutOverride&& constraints = {}) { Label(id, text, theme.labelStyleRef, std::move(constraints)); }
        /// As the first, with the call site as the ID.
        inline void Label(       std::string_view text, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Label(idManager.AllocateDynamicID(site), text, style, std::move(constraints)); }
        /// As the first, with the call site as the ID, in the theme's label style.
        inline void Label(       std::string_view text,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Label(idManager.AllocateDynamicID(site), text, theme.labelStyleRef, std::move(constraints)); }

        /// Static text.
        void        Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's text style.
        inline void Text(ID id, std::string_view text,                 LayoutOverride&& constraints = {}) { Text(id, text, theme.staticTextStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline void Text(       std::string_view text, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Text(idManager.AllocateDynamicID(site), text, style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's text style.
        inline void Text(       std::string_view text,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Text(idManager.AllocateDynamicID(site), text, theme.staticTextStyleRef, std::move(constraints)); }

        /// Read-only rich text with links, mouse selection and Ctrl+C. `onLinkActivate` (optional)
        /// gets a clicked link's contentId. The ID must be the same every frame for the selection to last.
        void SelectableText(ID id, const std::span<ContentRun>& runs,
                            std::function<void(intptr_t contentId)> onLinkActivate,
                            StyleRef style, LayoutOverride&& constraints = {});
        /// As above, without link handling.
        inline void SelectableText(ID id, const std::span<ContentRun>& runs, StyleRef style, LayoutOverride&& constraints = {})
        { SelectableText(id, runs, {}, style, std::move(constraints)); }

        /// Draws `image` (from graphicsContext->RegisterImage) stretched over the element's box.
        void        Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's image style.
        inline void Image(ID id, ImageIndex image,                 LayoutOverride&& constraints = {}) { Image(id, image, theme.imageStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline void Image(       ImageIndex image, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Image(idManager.AllocateDynamicID(site), image, style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's image style.
        inline void Image(       ImageIndex image,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Image(idManager.AllocateDynamicID(site), image, theme.imageStyleRef, std::move(constraints)); }

        /// Aspect-preserving: sizes the element to the largest sourceSize-aspect rect that fits
        /// the constraints. sourceSize is the image's pixel size.
        void        Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
        /// As above, in the theme's image style.
        inline void Image(ID id, ImageIndex image, uint2 sourceSize,                 LayoutOverride&& constraints = {}) { Image(id, image, sourceSize, theme.imageStyleRef, std::move(constraints)); }
        /// As above, with the call site as the ID.
        inline void Image(       ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Image(idManager.AllocateDynamicID(site), image, sourceSize, style, std::move(constraints)); }
        /// As above, with the call site as the ID, in the theme's image style.
        inline void Image(       ImageIndex image, uint2 sourceSize,                 LayoutOverride&& constraints = {}, std::source_location site = std::source_location::current()) { Image(idManager.AllocateDynamicID(site), image, sourceSize, theme.imageStyleRef, std::move(constraints)); }

        ///@}

        /// @name Ghost windows
        ///@{

        /// A ghost window: caption-less and transparent, e.g. a drag preview; 28 x 28 CSS px.
        /// Close it with EndGhostWindow, whether or not it opened.
        bool BeginGhostWindow(ID windowLayoutID, int2 position)
        {
            return BeginGhostWindow(windowLayoutID, position, int2{ styleManager.Px(28), styleManager.Px(28) });
        }

        /// `position` is the top-left in screen space; for drag previews use BeginCursorGhostWindow.
        bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size, StyleRef styleRef)
        {
            if (windowManager->GetWindowState(windowLayoutID) == WindowState::Closing)
            {
                m_windowOpen.push_back(false);   // its EndGhostWindow is still called
                return false;
            }

            LayoutOverride constraints = LayoutOverride::FixedSize(size).Position(position.x, position.y);

            ElementFlags elementFlags = tachyon::ElementFlags::WindowNoCaption | tachyon::ElementFlags::WindowIsTransparent;
            return BeginWindow(windowLayoutID, styleRef, std::move(constraints), elementFlags);
        }

        /// As above, in the theme's ghost window style.
        bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size)
        {
            return BeginGhostWindow(windowLayoutID, position, size, theme.ghostWindowStyleRef);
        }

        /// Sized to its content.
        bool BeginGhostWindow(ID windowLayoutID, int2 position, StyleRef styleRef)
        {
            if (windowManager->GetWindowState(windowLayoutID) == WindowState::Closing)
            {
                m_windowOpen.push_back(false);   // its EndGhostWindow is still called
                return false;
            }
            LayoutOverride constraints = LayoutOverride{}.Position(position.x, position.y);
            ElementFlags elementFlags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsTransparent;
            return BeginWindow(windowLayoutID, styleRef, std::move(constraints), elementFlags);
        }

        /// Where a ghost sits from the cursor, clear of the OS cursor: 14 px for a 32 px cursor,
        /// in proportion for others, 14 CSS px when the backend can't tell the cursor's size.
        int2 CursorGhostOffset() const
        {
            const int32_t o = m_cursorSize > 0 ? (m_cursorSize * 14 + 16) / 32 : styleManager.Px(14);
            return int2{ o, o };
        }
        /// A ghost window at the cursor, offset by CursorGhostOffset; 28 x 28 CSS px.
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset());
        }
        /// As above, with a size and style.
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos, int2 size, StyleRef styleRef)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset(), size, styleRef);
        }
        /// As above, with a size.
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos, int2 size)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset(), size);
        }
        /// As above, with a style, sized to its content.
        bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos, StyleRef styleRef)
        {
            return BeginGhostWindow(windowLayoutID, cursorPos + CursorGhostOffset(), styleRef);
        }

        /// An icon in the open ghost window, 28 x 28 CSS px.
        void AddGhostIcon(IconID icon, std::source_location site = std::source_location::current())
        {
            auto dragGhostIconID = idManager.AllocateDynamicID(site);
            tachyon::Element ghostIcon
            {
                .id       = dragGhostIconID,
                .textID   = TextID::Invalid,
                .iconID   = icon,
                .styleRef = theme.ghostIconStyleRef,
            };

            auto ghostIconRes = elementStorage.Add(std::move(ghostIcon),
                                                   tachyon::LayoutOverride::FixedSize(28, 28));
            VERIFY(ghostIconRes.status);
            VERIFY(windowRegistry.Add(ghostIconRes.ref));
        }

        /// Closes BeginGhostWindow or BeginCursorGhostWindow.
        void EndGhostWindow()
        {
            EndWindow();
        }

        ///@}

        /// @name Windows
        ///@{

        /// As the full BeginWindow below, in the theme's window style.
        bool BeginWindow(ID windowLayoutID, std::string_view title, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, title, theme.windowStyleRef, LayoutOverride{}, flags, monitorIndex);
        }

        /// As the full BeginWindow below.
        bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, title, styleRef, LayoutOverride{}, flags, monitorIndex);
        }

        /// As the full BeginWindow below, in the theme's window style.
        bool BeginWindow(ID windowLayoutID, std::string_view title, LayoutOverride&& constraints, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, title, theme.windowStyleRef, std::move(constraints), flags, monitorIndex);
        }


        /// As the full BeginWindow below, without a title, in the theme's window style.
        bool BeginWindow(ID windowLayoutID, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, theme.windowStyleRef, LayoutOverride{}, flags, monitorIndex);
        }

        /// As the full BeginWindow below, without a title.
        bool BeginWindow(ID windowLayoutID, StyleRef styleRef, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, styleRef, LayoutOverride{}, flags, monitorIndex);
        }

        /// As the full BeginWindow below, without a title, in the theme's window style.
        bool BeginWindow(ID windowLayoutID, LayoutOverride&& constraints, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, theme.windowStyleRef, std::move(constraints), flags, monitorIndex);
        }

        /// As the full BeginWindow below, without a title.
        bool BeginWindow(ID windowLayoutID, StyleRef styleRef, LayoutOverride&& constraints, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            return BeginWindow(windowLayoutID, "", styleRef, std::move(constraints), flags, monitorIndex);
        }


        /// An OS window, made the first time its ID is seen; build its content while this
        /// returns true. `flags` (ElementFlags::Window*) set how it behaves; monitorIndex is the
        /// monitor a new window opens on (-1: the one at its fixed position, else the primary).
        /// EndWindow is called either way.
        bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, LayoutOverride&& constraints, ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered, int monitorIndex = -1)
        {
            const bool open = OpenWindow(windowLayoutID, title, styleRef, std::move(constraints), flags, monitorIndex);
            m_windowOpen.push_back(open);
            return open;
        }

        /// Internal: the body of BeginWindow; call BeginWindow.
        bool OpenWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, LayoutOverride&& constraints, ElementFlags flags, int monitorIndex)
        {
            if (windowLayoutID == ID::Invalid())
            {
                ReportMisuse("GUIApplication: BeginWindow with an invalid ID (not shown)");
                return false;
            }

            // Closed by the user since last frame.
            if (windowManager->GetWindowState(windowLayoutID) == WindowState::Closing
                || windowManager->WasUserClosed(windowLayoutID))
            {
                return false;
            }
            if (!m_restoredWindows.empty())
                ApplyRestoredWindow(windowLayoutID, constraints, flags);

            if (monitorIndex == -1)
            {
                if (constraints.x.type == PositionType::Fixed &&
                    constraints.y.type == PositionType::Fixed)
                {
                    monitorIndex = windowManager->GetMonitorIndexAt(tachyon::int2{ .x = constraints.x.near, .y = constraints.y.near });
                } else
                    monitorIndex = windowManager->GetPrimaryMonitorIndex();
            }

            Element windowElement
            {
                .id           = windowLayoutID,
                .textID       = (title.size() == 0) ? TextID::Invalid : textSystem->RegisterText(windowLayoutID, title, styleRef, tachyon::ElementState::None),
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

        /// Closes BeginWindow; called whether or not it returned true.
        void EndWindow()
        {
            if (m_windowOpen.empty())
            {
                ReportMisuse("GUIApplication: EndWindow without BeginWindow (ignored)");
                return;
            }
            const bool open = m_windowOpen.back();
            m_windowOpen.pop_back();
            if (open)
                CloseWindow();
        }

        ///@}

        /// A container's scope: its End runs when the scope leaves (or at End()). True when
        /// the container's content is to be built, as its Begin returned.
        ///
        ///     if (auto menu = app.Menu("File")) { ...items... }
        template<void (GUIApplication::*EndFn)()>
        class [[nodiscard]] Scope
        {
        public:
            /// Made by the scope functions below.
            Scope(GUIApplication& app, bool open) : m_app(&app), m_open(open) {}
            Scope(Scope&& other) noexcept : m_app(std::exchange(other.m_app, nullptr)), m_open(other.m_open) {}
            Scope(const Scope&)            = delete;
            Scope& operator=(const Scope&) = delete;
            Scope& operator=(Scope&&)      = delete;
            ~Scope() { End(); }

            /// Ends the container now instead of when the scope leaves.
            void End()
            {
                if (GUIApplication* app = std::exchange(m_app, nullptr))
                    (app->*EndFn)();
            }
            explicit operator bool() const { return m_open; }

        private:
            GUIApplication* m_app;
            bool            m_open;
        };

        /// @name Container scopes
        /// Scopes for the Begin / End pairs, taking the Begin's arguments.
        ///@{
        template<class... A> auto Window(A&&... a)           { return Scope<&GUIApplication::EndWindow>(*this, BeginWindow(std::forward<A>(a)...)); }
        template<class... A> auto GhostWindow(A&&... a)      { return Scope<&GUIApplication::EndGhostWindow>(*this, BeginGhostWindow(std::forward<A>(a)...)); }
        template<class... A> auto CursorGhostWindow(A&&... a){ return Scope<&GUIApplication::EndGhostWindow>(*this, BeginCursorGhostWindow(std::forward<A>(a)...)); }
        template<class... A> auto Menu(A&&... a)             { return Scope<&GUIApplication::EndMenu>(*this, BeginMenu(std::forward<A>(a)...)); }
        template<class... A> auto MenuBar(A&&... a)          { return Scope<&GUIApplication::EndMenuBar>(*this, BeginMenuBar(std::forward<A>(a)...)); }
        template<class... A> auto ContextMenu(A&&... a)      { return Scope<&GUIApplication::EndContextMenu>(*this, BeginContextMenu(std::forward<A>(a)...)); }
        template<class... A> auto Tooltip(A&&... a)          { return Scope<&GUIApplication::EndTooltip>(*this, BeginTooltip(std::forward<A>(a)...)); }
        template<class... A> auto Cached(A&&... a)           { return Scope<&GUIApplication::EndCached>(*this, BeginCached(std::forward<A>(a)...)); }
        template<class... A> auto Horizontal(A&&... a)       { return Scope<&GUIApplication::EndHorizontal>(*this, BeginHorizontal(std::forward<A>(a)...)); }
        template<class... A> auto Vertical(A&&... a)         { return Scope<&GUIApplication::EndVertical>(*this, BeginVertical(std::forward<A>(a)...)); }
        template<class... A> auto HorizontalWrap(A&&... a)   { return Scope<&GUIApplication::EndHorizontalWrap>(*this, BeginHorizontalWrap(std::forward<A>(a)...)); }
        template<class... A> auto VerticalWrap(A&&... a)     { return Scope<&GUIApplication::EndVerticalWrap>(*this, BeginVerticalWrap(std::forward<A>(a)...)); }
        template<class... A> auto Area(A&&... a)             { return Scope<&GUIApplication::EndArea>(*this, BeginArea(std::forward<A>(a)...)); }
        template<class... A> auto LabelGroup(A&&... a)       { return Scope<&GUIApplication::EndLabelGroup>(*this, BeginLabelGroup(std::forward<A>(a)...)); }
        template<class... A> auto ButtonGroup(A&&... a)      { return Scope<&GUIApplication::EndButtonGroup>(*this, BeginButtonGroup(std::forward<A>(a)...)); }
        template<class... A> auto ToggleGroup(A&&... a)      { return Scope<&GUIApplication::EndToggleGroup>(*this, BeginToggleGroup(std::forward<A>(a)...)); }
        template<class... A> auto Table(A&&... a)            { return Scope<&GUIApplication::EndTable>(*this, BeginTable(std::forward<A>(a)...)); }
        template<class... A> auto Row(A&&... a)              { return Scope<&GUIApplication::EndRow>(*this, BeginRow(std::forward<A>(a)...)); }
        template<class... A> auto ListView(A&&... a)         { return Scope<&GUIApplication::EndListView>(*this, BeginListView(std::forward<A>(a)...)); }
        template<class... A> auto GridView(A&&... a)         { return Scope<&GUIApplication::EndGridView>(*this, BeginGridView(std::forward<A>(a)...)); }
        template<class... A> auto TabBar(A&&... a)           { return Scope<&GUIApplication::EndTabBar>(*this, BeginTabBar(std::forward<A>(a)...)); }
        template<class... A> auto Toolbar(A&&... a)          { return Scope<&GUIApplication::EndToolbar>(*this, BeginToolbar(std::forward<A>(a)...)); }
        template<class... A> auto StatusBar(A&&... a)        { return Scope<&GUIApplication::EndStatusBar>(*this, BeginStatusBar(std::forward<A>(a)...)); }
        template<class... A> auto TitleBar(A&&... a)         { return Scope<&GUIApplication::EndTitleBar>(*this, BeginTitleBar(std::forward<A>(a)...)); }
        template<class... A> auto ReorderableGroup(A&&... a) { return Scope<&GUIApplication::EndReorderableGroup>(*this, BeginReorderableGroup(std::forward<A>(a)...)); }
        template<class... A> auto ReorderableItem(A&&... a)  { return Scope<&GUIApplication::EndReorderableItem>(*this, BeginReorderableItem(std::forward<A>(a)...)); }
        template<class... A> auto Disabled(A&&... a)         { return Scope<&GUIApplication::EndDisabled>(*this, BeginDisabled(std::forward<A>(a)...)); }
        template<class... A> auto Selected(A&&... a)         { return Scope<&GUIApplication::EndSelected>(*this, BeginSelected(std::forward<A>(a)...)); }
        template<class... A> auto Indeterminate(A&&... a)    { return Scope<&GUIApplication::EndIndeterminate>(*this, BeginIndeterminate(std::forward<A>(a)...)); }
        template<class... A> auto DockingContainer(A&&... a) { return Scope<&GUIApplication::EndDockingContainer>(*this, BeginDockingContainer(std::forward<A>(a)...)); }
        template<class... A> auto DockingFrame(A&&... a)     { return Scope<&GUIApplication::EndDockingFrame>(*this, BeginDockingFrame(std::forward<A>(a)...)); }
        ///@}

        /// @name Boxes
        ///@{

        /// Internal: the body of EndWindow for an open window; call EndWindow.
        void CloseWindow()
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

        /// A plain box of a fixed size in pixels, in a style; textID is optional.
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

        /// As above, with the call site as the ID.
        void AddBox(int2 size, StyleRef styleRef, TextID textID = TextID::Invalid, std::source_location site = std::source_location::current())
        {
            AddBox(idManager.AllocateDynamicID(site), size, styleRef, textID);
        }

        /// As above, with text.
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

        /// As above, with an icon and text.
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

        /// As above, sized to its content.
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

        /// A box with text, sized to its content, with the call site as the ID.
        void AddBox(StyleRef styleRef, std::string_view text, std::source_location site = std::source_location::current())
        {
            AddBox(idManager.AllocateDynamicID(site), styleRef, IconID::Invalid, text);
        }

        /// A fixed-size box with text, with the call site as the ID.
        void AddBox(int2 size, StyleRef styleRef, std::string_view text, std::source_location site = std::source_location::current())
        {
            AddBox(idManager.AllocateDynamicID(site), size, styleRef, text);
        }

        ///@}

        /// @name Settings panel
        ///@{

        // ------------------------------------------------------------------
        // Settings panel
        // ------------------------------------------------------------------
        // Two-pane settings panel: a page tree, a sizer, and the selected page's content.
        // Owns only selection and tree width; page state belongs to the application.
        /// What RenderSettingsPanel shows: its pages, the selected one and the tree width.
        struct SettingsPanelState
        {
            /// A page: a stable ID, its name in the tree, and render, which builds its content.
            struct Page
            {
                /// Stable across frames.
                tachyon::ID               id;
                /// Shown in the page tree.
                std::string           name;
                /// Builds the page's content while it is selected.
                std::function<void()> render;
            };

            /// Added with AddPage.
            std::vector<Page> pages;
            /// The page shown; a click in the tree changes it.
            tachyon::ID           selectedID = tachyon::ID::Invalid();
            /// The page tree's width; its sizer changes it.
            CssPx             treeWidth  = 200;

            /// Tree-view backing state, rebuilt per frame; not part of the API.
            struct Internal
            {
                std::vector<TreeViewRow> rows;
                std::vector<int>         visibleIdx;
                TableState               tableState;
            } internal;

            /// `id` must be stable. The first page added is selected initially.
            tachyon::ID AddPage(tachyon::ID id, std::string_view name, 
                            std::function<void()> render)
            {
                pages.push_back({ id, std::string(name), std::move(render) });
                if (selectedID == tachyon::ID::Invalid()) selectedID = id;
                return id;
            }
        };

        /// Renders into the current container; the caller provides the outer panel.
        void RenderSettingsPanel(SettingsPanelState& state);

        ///@}

        /// @name Saved UI state
        /// The UI's state as JSON text, which the host stores where and when it likes. Name
        /// what to keep once, by keys that stay the same between runs; the named objects
        /// must outlive the naming:
        ///
        ///     PersistWindow("main", mainWindowID);         // position, size, mode
        ///     PersistDocking("dock", dockingLayout);       // panels by type, else title
        ///     PersistTable("assets", tableState, columns); // column widths and order, by header
        ///     PersistToolbars("toolbars", toolbarSides);   // items by debugName
        ///     PersistValue("zoom", zoom);                  // an app setting, if it wants
        ///
        /// RestoreUIState applies saved text: windows when they are next declared, the rest
        /// at once. What it doesn't know (keys, panels, columns) is skipped.
        ///@{

        /// Keeps the window's position, size and mode.
        void PersistWindow(std::string key, ID windowID);
        /// Keeps the layout's dock tree; panels are matched by type, else by title.
        void PersistDocking(std::string key, DockingLayout& layout);
        /// Keeps the table's column widths and order, matching columns by header.
        void PersistTable(std::string key, TableState& state, std::span<VirtualTableColumn> columns);
        /// Keeps each side's toolbar items, by debugName.
        void PersistToolbars(std::string key, std::span<std::vector<ToolbarItem>, kToolbarSideCount> sides);
        /// Keeps an app value.
        void PersistValue(std::string key, bool& value);
        /// As above, for an int.
        void PersistValue(std::string key, int& value);
        /// As above, for a float.
        void PersistValue(std::string key, float& value);
        /// As above, for a string.
        void PersistValue(std::string key, std::string& value);
        /// The named state as JSON text.
        std::string  SaveUIState() const;
        /// Applies JSON text from SaveUIState. An error when it doesn't parse, isn't an object
        /// or was saved by a newer version.
        Result<void> RestoreUIState(std::string_view json);

        ///@}

    private:
        /// GUIApplicationInternals (gui_application_internal.h) re-exposes these to
        /// the library's own code and the tests.
        friend class GUIApplicationInternals;

        /// Cached scopes being rebuilt (BeginCached misses), innermost last.
        std::vector<ID>                            m_cacheRecordStack;

        /// This frame's input as seen by cached scopes; refreshed lazily once per frame.
        bool     m_keyboardPending = false;
        bool     m_tabPending      = false;
        uint64_t m_inputFlagsFrame = 0;
        void     RefreshCachedInputFlags();

        /// Registers a swatch claim with the enclosing cached scopes so hit frames can keep it alive.
        void NoteSwatchClaim(ID swatchID);
        /// The text of the label just before the current control (empty without one).
        std::string PendingLabelText() const;
        /// How color swatches draw a color (the theme's checkerboard and alpha bar).
        struct ColorSwatchLook SwatchLook() const;

        /// Panel id -> its dock content (cached scope) id, for InvalidatePanel.
        std::unordered_map<tachyon::ID, tachyon::ID>       m_panelContentIDs;

        /// Save-stack for nested BeginToolbar/EndToolbar.
        std::vector<const ControlStyles*> controlStylesStack;

        ID tooltipWindowID     { ID::Invalid() };
        ID contextMenuWindowID { ID::Invalid() };
        ID contextMenuTargetID { ID::Invalid() }; ///< element the context menu is attached to
        std::optional<int2> contextMenuScreenPos; ///< where OpenContextMenu put it; else below the target
        ID tabDragGhostWindowID{ ID::Invalid() }; ///< ghost window for dragged dock tab / drop overlay

        FrameTimingSample frameTimingHistory[kFrameHistoryCount] = {};
        int               frameTimingHead = 0;           ///< next write slot

        /// Table drag state that persists across frames (defs in gui_table_types.h).
        ColumnDragState m_columnDrag{};
        TreeDropState   m_treeDropState{};

        // ---- ListView ID->index map (shared by every list view; cleared once per frame) ----
        // Filled by ListItem when dragDrop is on; valid in end-of-frame handlers via FindListItemIndex().
        std::unordered_map<tachyon::ID, int> m_listItemIndexByID;


        /// Transient construction state (defs in gui_tree_grid_types.h), valid only between
        /// Begin*/End*. Handlers run later, so they must capture pointers, never read these.
        TreeViewState m_treeState{};
        GridViewState m_gridViewState;

        /// GridView rename lookup, shared by every grid view; cleared before construction.
        std::unordered_map<tachyon::ID, GridItemRenameInfo> m_gridItemRenameByID;


        /// Transient state of the VirtualTable being emitted (def in gui_table_types.h).
        std::optional<TableEmit> m_tableEmit;
        /// Emit states of outer tables while a table is built inside a cell, innermost last.
        std::vector<std::optional<TableEmit>> m_outerTableEmits;

        int CurrentCellCol() const
        {
            const int c = m_tableEmit->currentCol;
            assert(c < int(m_tableEmit->columnCount));
            return c;
        }

        TabBarState m_tabBarState;

        /// Enables the panels reachable from layout.root; called by BeginDockingContainer each frame.
        void EnableVisiblePanels(DockingLayout& layout);

        /// Transient, valid only between BeginDockingContainer / EndDockingContainer
        /// (def in gui_docking_types.h); handlers must capture pointers, not read this.
        DockingConstructionState m_dockingState;

        /// Docking layouts emitted this frame (cleared at frame start); used by
        /// SyncDockingScrollOffsets and cross-dock drop hit-testing.
        std::vector<DockingLayout*> m_dockingLayoutsThisFrame{};

        /// Toolbar docks emitted this frame, walked by SyncToolbarDockRects (type in controls_toolbar_dock.h).
        std::vector<struct ToolbarDockState*> m_toolbarDocksThisFrame{};

        /// After input dispatch: stores each selected panel's scroll offset so it survives a tab switch.
        void SyncDockingScrollOffsets();

        /// Snapshots each toolbar dock side's screen rect for next frame's drag hit-testing.
        void SyncToolbarDockRects();

        /// Uses last frame's tab-bar rects, so it works during construction.
        tachyon::DockNode* FindDockLeafTabBarCachedAtScreenPos(tachyon::int2 screenPos);

        std::vector<std::unique_ptr<FloatingDockWindow>> m_floatingDockWindows;

        /// Debug asserts for Begin*/End* stacks left unbalanced at frame end. Also used by the headless tests.
        void RegisterFrameHooks();   // per-frame stage hooks; Initialize (and test harnesses) call it
        void RegisterFrameBalanceChecks();
        /// API misuse (e.g. an End without its Begin): logged once, and the caller carries on.
        static void ReportMisuse(const char* message);
        /// The bodies of the conditional Begin / End pairs (the public ones keep the open stacks).
        bool OpenMenu(ID menuID, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints);
        void CloseMenu();
        bool OpenTooltip(ID targetID);
        bool OpenContextMenuWindow(ID targetID);
        bool OpenCached(ID id, StyleRef style, uint64_t signature, LayoutOverride&& constraints);
        static void ReportInvalidControlID(ID id);

        /// Rebuilds the styles when a reload is pending or the main window's
        /// display scale changed. Runs before the managers begin their frame.
        void ApplyPendingStyleReload();

        void OnRender(float elapsedTimeSecs);
        /// The box style's CSS shadows (and the focus ring when `ring`), blended as the box is.
        void RenderBoxShadows(IntRect rectLayout, IntRect clipRectScreen, const TransitionState& ts, bool ring);
        void OnFrame();
        Result<void> RecoverDevice();

        /// Change via RequestReloadTheme so the reload happens between frames.
        std::string m_resourceRoot;
        std::unique_ptr<IWindowBackend> m_osWindowBackend;     ///< wrapped by windowBackend
        ImageWindowBackend*  m_imageWindows    { nullptr };    ///< == windowBackend
        MouseButtons         m_injectedButtons { MouseButtons::None };
        int2 InjectedScreenPos(ID windowID, int2 pos);
        AllocationHooks m_allocationHooks;
        std::unique_ptr<GraphicsContext> m_graphicsBackend;
        MouseButtons m_lastPhysicalButtons { MouseButtons::None };  ///< for SynthesizeMissingMouseEvents
        std::string ResourcePath(std::string_view path) const;   // relative paths resolve against m_resourceRoot
        std::string m_themePath     { "themes/dark.css" };
        bool        m_initialized = false;   ///< Initialize succeeded; Cleanup clears it
        bool        m_inFrame     = false;   ///< inside OnFrame (a frame can't run inside another)
        Result<void> m_themeReload {};

        /// Saved UI state: what the app named, and windows waiting for their saved rect.
        struct PersistedWindow   { std::string key; ID target; };
        struct PersistedDock     { std::string key; DockingLayout* target; };
        struct PersistedTable    { struct Target { TableState* state; std::span<VirtualTableColumn> columns; }; std::string key; Target target; };
        struct PersistedToolbars { std::string key; std::span<std::vector<ToolbarItem>, kToolbarSideCount> target; };
        struct PersistedValue    { using Target = std::variant<bool*, int*, float*, std::string*>; std::string key; Target target; };
        struct RestoredWindow    { IntRect rect {}; WindowMode mode = WindowMode::Normal; bool hasPosition = false; bool rectDone = false; };
        std::vector<PersistedWindow>   m_persistWindows;
        std::vector<PersistedDock>     m_persistDocks;
        std::vector<PersistedTable>    m_persistTables;
        std::vector<PersistedToolbars> m_persistToolbars;
        std::vector<PersistedValue>    m_persistValues;
        std::unordered_map<ID, RestoredWindow> m_restoredWindows;
        void ApplyRestoredWindow(ID windowID, LayoutOverride& constraints, ElementFlags& flags);
        int32_t      m_cursorSize  = 0;   ///< the OS cursor's, in pixels (0: unknown); read with the styles
        /// Begin/End contract: whether each open BeginWindow / BeginMenu (and BeginContextMenu)
        /// / BeginTooltip / BeginCached scope is showing; its End pops it.
        std::vector<bool> m_windowOpen, m_menuOpen, m_tooltipOpen, m_cachedOpen;
        /// The theme that last loaded, for a reload that fails to fall back to.
        std::string m_loadedThemePath;
        float       m_loadedFontScale { 1.0f };
        std::vector<std::string> m_styleFiles { "styles.css" };
        float       m_globalFontScale { 1.0f };
        float       m_uiScale { 1.0f };
        float       m_displayScale { 1.0f };
        FrameClock  m_frameClock;   ///< animation time, vsync-paced
        bool        m_themeReloadPending { false };

        tachyon::ElementRef RegisterElementInternal(tachyon::Element&& element, LayoutOverride&& constraints, bool updateState = true);

        /// Invalidates the scopes under the cursor and, on key input, the focused one.
        /// Call after hit-testing, before dispatch.
        void        InvalidateCachedScopesForInput(ID overID);
        /// The PropagatesDescendantState ancestors of `leafID`'s element, innermost first.
        static std::vector<ID> CollectPropagatedAncestors(const ElementStorage& storage, ID leafID);

        /// Internal: called by the docking each frame.
        void        NotePanelContentID(ID panelId, ID contentID) { m_panelContentIDs[panelId] = contentID; }

        /// Set by RenderNodeView around each renderBody; Start elsewhere.
        TextAlign nodeRowTextAlign = TextAlign::Start;

        /// When > 0, a Field() in a node row grows to fill the body with this minimum width.
        /// Set by RenderNodeView around each renderBody; 0 elsewhere.
        int32_t nodeRowFieldMinWidth = 0;

        bool BeginDockingFrame();
        void EndDockingFrame();

        ID activeWindowID { ID::Invalid() };
        std::vector<bool> m_windowIdScopeStack;

        /// The node view's breadcrumb, zoom controls and minimap, over its canvas.
        void EmitNodeViewOverlays(NodeView& view, ID canvasID);

        /// Each window's title bar, from its last BeginTitleBar / EndTitleBar.
        struct TitleBarInfo { ID barID = ID::Invalid(); ID maximizeID = ID::Invalid(); std::vector<ID> captions; };
        std::unordered_map<ID, TitleBarInfo> m_titleBars;
        std::vector<ID> m_titleBarStack;    ///< bar IDs between BeginTitleBar and EndTitleBar
        std::vector<ID> m_titleBarCaptions; ///< controls in the open title bar whose background drags the window
        std::unordered_set<ID> m_closeRequests;   ///< ConfirmClose windows whose title-bar close was clicked
        std::vector<TextStyle> m_mnemonicStyles;  ///< this frame's underlined mnemonic letters (MessageDialog)
        /// Windows dimmed under a modal dialog, by window ID: fading in while wanted, out after.
        struct ScrimFade { float amount = 0.0f; bool wanted = false; VisualColorStyleIndex color = VisualColorStyleIndex::Invalid; };
        std::unordered_map<ID, ScrimFade> m_scrims;
        float m_scrimTime = -1.0f;
        void DrawScrims(float now);
        /// Render: AddCustomDrawAfter owners waiting for the walk to pass their subtree.
        struct PendingAfterDraw { uint32_t endIdx; ElementRef owner; };
        std::vector<PendingAfterDraw> m_pendingAfterDraws;
        /// A control in the open title bar whose own background is caption (e.g. a tab strip).
        void TitleBarCaption(ID id) { m_titleBarCaptions.push_back(id); }
        void DockFloatingWindow(FloatingDockWindow& fw);

        /// Popup content for ColorField / GradientField.
        void EmitColorPicker(tachyon::color4f& outColor, bool allowHDR);
        void EmitGradientEditor(tachyon::Gradient& outGradient);
        /// Horizontal gradient bar with a triangle indicator.
        void GradientSlider(tachyon::ID id, float& value, float minVal, float maxVal,
                            std::string_view label,
                            tachyon::VisualColorStyleIndex gradientStyle,
                            tachyon::VisualColorStyleIndex triangleStyle,
                            LayoutOverride&& constraints = {});

        /// Column-boundary sizers for [colStart, colEnd), anchored to each header cell's right edge.
        /// skipDragged leaves out the dragged column's sizer; EmitDraggedColumnOnTop emits it last.
        void EmitColumnSizers(TableEmit& emit, int colStart, int colEnd,
                              bool skipDragged = false);
        /// 1px column dividers for headerless tables, which have no sizers.
        void EmitColumnSeparators(TableEmit& emit, int colStart, int colEnd);
        /// Full-height strips for columns with columnBackground: above the row backdrops, below the cells.
        void EmitColumnBackgrounds(TableEmit& emit, int colStart, int colEnd);

        /// followTail, run from the body section's post-layout hook (final viewport/content height).
        void UpdateTableFollowTail(TableState& state, int32_t viewportH, int32_t contentH);
        void ScrollTableSectionsToBottom(TableState& state);
        /// 1px separator between adjacent frozen sections.
        void EmitTableSectionSeparator();

        /// Adds column c's reorder-drag X offset while a drag is live.
        tachyon::LayoutOverride ApplyColumnDragOffsetX(tachyon::LayoutOverride lo,
                                                   const TableEmit& emit, int c) const
        {
            if (emit.dragActive)
                lo = std::move(lo).PositionX(tachyon::PositionConstraintAxis::Relative(emit.columns[c].internal.dragOffsetX));
            return lo;
        }
        /// Mirrors Y across the left/mid/right bodies and X to the mid header; absent sections have invalid IDs.
        void MirrorTableScroll(TableState* state);
        /// `gridCol` is section-local. `sectionMin/Max` are the inclusive global columns
        /// the reorder drag may move this header within.
        void EmitTableHeaderCell(TableEmit& emit, tachyon::ID tableID, int c, int16_t gridCol,
                                 bool isStretchTrack, int sectionMin, int sectionMax,
                                 int effectiveSortCol, IconID sortIcon);
        /// Header cells go into row 0 of the section's body grid, StickyY-pinned to the top.
        void EmitTableHeaderSection(TableEmit& emit, tachyon::ID tableID,
                                    int colStart, int colEnd, bool lastColIsStretch,
                                    int effectiveSortCol, IconID sortIcon);
        /// Stretch for Stretch columns and the middle section's last column; fitContent
        /// forces Fixed (fit-content sections have no leftover space to absorb).
        GridTrack ComputeSectionColumnTrack(const TableEmit& emit, int c, int colEnd,
                                            bool isMiddle, bool fitContent = false) const;
        /// Follow with the row loop and CloseTableBodySection. `sampleViewport` records the
        /// viewport size for next frame (primary section only).
        void OpenTableBodySection(TableEmit& emit, tachyon::ID sectionID, tachyon::StyleRef sectionStyle,
                                  tachyon::LayoutOverride&& layout, bool sampleViewport);
        /// Emits sizers and the section's H scrollbar (the rightmost section also gets the
        /// shared V scrollbar). suppressHorzScrollbar is for fit-content sections.
        void CloseTableBodySection(TableEmit& emit,
                                   tachyon::ID sectionBodyID, bool emitVerticalScrollbar,
                                   bool suppressHorzScrollbar = false);
        /// Row loop for an open body section: spacers for off-screen rows, backdrop + cells for visible ones.
        void EmitVirtualBodyRows(TableEmit& emit,
                                 int colStart, int colEnd,
                                 int totalRowCount, int32_t actualRowHeight,
                                 int firstVisible, int lastVisible,
                                 tachyon::ID rowsRangeID,
                                 bool isLastSection);

        /// Emits the dragged column (cells, header, sizer) last so it paints on top.
        /// Only when emit.dragActive and draggedCol is in [colStart, colEnd).
        void EmitDraggedColumnOnTop(TableEmit& emit, tachyon::ID tableID,
                                    int colStart, int colEnd,
                                    int firstVisible, int lastVisible,
                                    tachyon::ID rowsRangeID,
                                    bool lastColIsStretch,
                                    int effectiveSortCol, IconID sortIcon);
        /// Cell emitters for Cell() / TreeCell(), into the open body section.
        void EmitDirectCellText(const VirtualTableColumn& column, TableEmit& emit,
                                tachyon::ID cellID, tachyon::ElementState cellState, tachyon::ElementFlags cellFlags,
                                tachyon::StyleRef cellStyleRef, tachyon::EventHandler cellHandler,
                                tachyon::TextID textID,
                                int colIdx, int colStart, int16_t gridRow);
        void EmitDirectCellCustom(const VirtualTableColumn& column, TableEmit& emit,
                                  tachyon::ID cellID, tachyon::ElementState cellState, tachyon::ElementFlags cellFlags,
                                  tachyon::StyleRef cellStyleRef, tachyon::EventHandler cellHandler,
                                  bool cellSelected,
                                  std::function<void()> emit_fn,
                                  int colIdx, int colStart, int16_t gridRow);
        void EmitDirectCellTree(const VirtualTableColumn& column, TableEmit& emit,
                                tachyon::ID cellID, tachyon::ElementState cellState, tachyon::ElementFlags cellFlags,
                                tachyon::StyleRef cellStyleRef, tachyon::EventHandler cellHandler,
                                bool cellSelected,
                                tachyon::TextID textID,
                                int treeDepth, bool* treeIsOpen, IconID treeIcon,
                                uint32_t treeAncestorMask, bool treeIsLastSibling,
                                std::string* treeRenameBuffer, bool treeRenameActive,
                                tachyon::ID treeBodyID, bool reserveExpander,
                                int colIdx, int colStart, int16_t gridRow);
        /// One element spanning [colStart, colEnd) at gridRow.
        void EmitDirectRowBackdrop(tachyon::ID rowID, int16_t gridRow,
                                   int colStart, int colEnd,
                                   tachyon::ElementState backdropState,
                                   tachyon::ElementFlags backdropFlags,
                                   tachyon::StyleRef rowStyle,
                                   tachyon::EventHandler backdropHandler,
                                   TableState* tableState,
                                   bool isLastSection);
        /// Invisible row of `height` pixels at gridRow.
        void EmitDirectRowSpacer(int16_t gridRow,
                                 int colStart, int colEnd, int32_t height);
        /// Row-mode backdrop MouseDown handler, shared by all rows (the row comes from the event id):
        /// selection, double-click toggle, rename start.
        tachyon::EventHandler BuildRowBackdropHandler(TableState* state,
                                                  tachyon::ID containerID);
        /// Focus on click, scroll routing to the middle body, keyboard navigation.
        tachyon::EventHandler BuildTableContainerHandler(TableState* state,
                                                     tachyon::ID containerID);

        /// Tree/list item construction helpers (OnGUI only, not from handlers).
        bool IsTreeItemSelected(tachyon::ID id, int itemIndex) const;
        tachyon::TextID TreeItemRegisterText(tachyon::ID id, std::string_view text, IconID icon, StyleRef style, tachyon::ElementState state);
        void TreeItemRowCustomDraw(const tachyon::ElementRef& rowRef, bool hasArrow, tachyon::ID rowID);
        tachyon::EventHandler MakeTreeRowDragDropHandler(tachyon::ID rowID, bool rowHasChildren, ItemDragDropConfig* dd, bool* isOpen = nullptr);
        bool TreeItemRenameCheck(tachyon::ID bodyID, std::string* renameBuffer, StyleRef textStyle);

        struct TreeClickState { tachyon::ID lastClickID = tachyon::ID::Invalid(); int64_t lastClickTimeNanos = 0; };

    };

    /// @}
} // namespace tachyon
