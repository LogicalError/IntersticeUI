#include "headless_gui_app.h"

#include <gui_core/input/gui_input_manager.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>

#include <gui_core/gui_id.h>

using ::testing::NiceMock;
using ::testing::Return;

namespace GUITest
{

HeadlessGUIApp::HeadlessGUIApp(std::string themePath)
    : m_themePath(std::move(themePath))
{
    m_rootWindowID.value.dynamicParts.mode      = GUI::IDMode::Static;
    m_rootWindowID.value.dynamicParts.hashValue = 0xBEEF;
}

HeadlessGUIApp::~HeadlessGUIApp()
{
    Cleanup();
}

bool HeadlessGUIApp::Initialize(const InitializeOptions& options)
{
    using ::testing::_;

    // One fake 1920x1080 monitor; layout queries GetMonitorArea for absolute
    // elements and auto-centered windows.
    GUI::MonitorInfo monitor{};
    monitor.workArea   = GUI::IntRect{ 0, 0, 1920, 1080 };
    monitor.isPrimary  = true;
    std::vector<GUI::MonitorInfo> monitors{ monitor };
    ON_CALL(mockBackend, GetMonitors()).WillByDefault(Return(monitors));
    ON_CALL(mockBackend, GetMonitorAt(_)).WillByDefault(Return(0));
    ON_CALL(mockBackend, GetMonitorFromWindow(_)).WillByDefault(Return(0));
    ON_CALL(mockBackend, GetScreenMousePos()).WillByDefault(Return(GUI::int2{0,0}));
    ON_CALL(mockBackend, GetMouseButtons()).WillByDefault(Return(GUI::MouseButtons::None));
    // Popup/tooltip window creation returns a stable non-null sentinel so
    // OSWindowManager registers the window; GetWindowRect returns the monitor
    // area so layout doesn't collapse popups.
    static int dummyHandleStorage = 0;
    GUI::OSWindowHandle fakeHandle = &dummyHandleStorage;
    ON_CALL(mockBackend, CreateWindow(_, ::testing::An<GUI::int2>(), _, _))
        .WillByDefault(Return(fakeHandle));
    ON_CALL(mockBackend, CreateWindow(_, ::testing::An<GUI::IntRect>(), _, _))
        .WillByDefault(Return(fakeHandle));
    ON_CALL(mockBackend, IsWindow(_)).WillByDefault(Return(true));
    ON_CALL(mockBackend, GetWindowRect(_))
        .WillByDefault(Return(GUI::IntRect{ 0, 0, 200, 300 }));
    ON_CALL(mockBackend, GetWindowPos(_)).WillByDefault(Return(GUI::int2{ 0, 0 }));
    ON_CALL(mockBackend, GetWindowSize(_)).WillByDefault(Return(GUI::int2{ 200, 300 }));
    ON_CALL(mockBackend, GetTicks()).WillByDefault(Return(0u));

    // Slots GUIApplication's Initialize() normally fills. StubTextSystem measures
    // text by character count; no font backend.
    textSystem    = &ownedTextSystem;
    windowBackend = &mockBackend;
    inputManager  = std::make_unique<GUI::InputManager>();
    layoutEngine  = std::make_unique<GUI::LayoutEngine>(*textSystem, guiStateManager.GetScrollManagement());
    // Menus, tooltips and popups reach the mock-backed OSWindowManager through this.
    windowManager = std::make_unique<GUI::OSWindowManager>(nullptr, &mockBackend);

    // Static window IDs Initialize() reserves for tooltip / context-menu /
    // tab-drag-ghost popups; widgets using them assert otherwise.
    tooltipWindowID      = idManager.CreateStaticID();
    contextMenuWindowID  = idManager.CreateStaticID();
    tabDragGhostWindowID = idManager.CreateStaticID();

    // Subsystems, as Initialize() creates them after the manager wiring; widgets
    // dereference these.
    m_sliderFieldTrack  = std::make_unique<GUI::SliderFieldTrackSubsystem>();
    m_menuRects         = std::make_unique<GUI::MenuRectsSubsystem>();
    m_labelGroup        = std::make_unique<GUI::LabelGroupSubsystem>(m_frame, elementStorage, guiStateManager, theme);
    m_buttonGroup       = std::make_unique<GUI::ButtonGroupSubsystem>(m_frame, elementStorage, styleManager);
    m_colorPickerSub    = std::make_unique<GUI::ColorPickerSubsystem>(m_frame);
    m_gradientEditorSub = std::make_unique<GUI::GradientEditorSubsystem>(m_frame);
    m_renameSub         = std::make_unique<GUI::RenameSubsystem>();
    m_reorderable       = std::make_unique<GUI::ReorderableSubsystem>(m_frame);
    m_dockTabDragSub    = std::make_unique<GUI::DockTabDragSubsystem>();
    m_uiCache           = std::make_unique<GUI::UICacheManager>(m_frame, elementStorage, guiStateManager);

    RegisterFrameBalanceChecks();

    // Visual-state transitions depend on the clock; disable them so tests see
    // settled visuals (re-enable with transitionManager.SetEnabled(true)).
    transitionManager.SetEnabled(false);

    // Parse the CSS theme and register every widget style (no graphics context).
    return !options.loadStyles || InitializeStyles();
}

void HeadlessGUIApp::Cleanup()
{
    layoutEngine.reset();
    inputManager.reset();
    windowManager.reset();
    textSystem    = nullptr;
    windowBackend = nullptr;
}

// ---------------------------------------------------------------------------
// Frame driver
// ---------------------------------------------------------------------------

void HeadlessGUIApp::Frame(std::function<void()> onGuiFn)
{
    // Rebuilt each frame; the stage points mirror production's OnFrame so
    // subsystem hooks fire under test.
    GUI::FrameContext ctx{
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
        .elapsedTimeSecs   = static_cast<float>(m_timeMs) / 1000.0f,
    };

    m_frame.RunStage(GUI::FrameStage::PreFrame, ctx);
    ApplyPendingStyleReload();

    // -- BeginFrame on every manager that matters for construction/dispatch.
    // Window-manager BeginFrame and popup-close book-keeping are skipped (no OS
    // windows).
    [[maybe_unused]] bool ok = idManager.BeginFrame();
    [[maybe_unused]] bool wm = windowManager->BeginFrame();
    guiStateManager.BeginFrame();
    guiStateManager.Interaction().SetFrameTime(static_cast<float>(m_timeMs) / 1000.0f);
    BeginDockingFrame();
    tabChainManager.BeginFrame(guiStateManager);
    menuManager.BeginFrame(guiStateManager, *windowManager);
    // Hover delays only fire when a test injects a MouseMove + AdvanceTime.
    bool hasMouseMove = false, hasMousePress = false, hasKeyboard = false, hasScroll = false;
    if (inputManager)
    {
        for (const auto& e : inputManager->GetRecordedEvents())
        {
            if (e.type == GUI::EventType::MouseMove)   hasMouseMove = true;
            if (e.type == GUI::EventType::MouseDown)   hasMousePress = true;
            if (e.type == GUI::EventType::KeyDown)     hasKeyboard = true;
            if (e.type == GUI::EventType::MouseScroll) hasScroll = true;
        }
    }
    tooltipManager.BeginFrame(guiStateManager.Interaction().GetHoveredID(),
                               guiStateManager.Interaction().GetFocusedID(),
                               static_cast<float>(m_timeMs) / 1000.0f,
                               hasMouseMove, hasMousePress, hasKeyboard, hasScroll,
                               menuManager.IsOpen());
    textSystem->BeginFrame(m_mousePos, m_buttonsHeld, m_timeMs);

    VERIFY(elementStorage.BeginConstruction());
    windowRegistry.Clear();

    m_frame.RunStage(GUI::FrameStage::PreConstruct, ctx);

    // -- User construction.
    if (onGuiFn) onGuiFn();

    m_frame.RunStage(GUI::FrameStage::PostConstruct, ctx);

    VERIFY(elementStorage.EndConstruction());

    textSystem->Update();

    m_frame.RunStage(GUI::FrameStage::PreLayout, ctx);

    // -- Layout. PreLayoutSync is skipped (no live OS windows).
    layoutEngine->Layout(*windowManager, elementStorage);

    // Per-element AddPostLayout callbacks are skipped (they need a
    // PostLayoutContext the stub doesn't model); subsystem OnPostLayout runs.
    m_frame.RunStage(GUI::FrameStage::PostLayout, ctx);

    windowRegistry.BuildRenderSkipLinks();

    m_frame.RunStage(GUI::FrameStage::PreInput, ctx);

    // -- Hit-test under the current mouse position so dispatch can route
    // MouseDown/MouseMove to the correct element.
    GUI::ID overID = GUI::ID::Invalid();
    if (m_rootWindowID != GUI::ID::Invalid())
    {
        GUI::ElementRef windowRef = windowRegistry.GetWindowRef(m_rootWindowID);
        if (windowRef != GUI::ElementRef::Invalid())
            overID = elementStorage.GetElementIDAt(windowRef, m_mousePos);
    }

    // -- EndFrame commits the hover/active targets for Process(), which runs after it.
    GUI::ID hoverID = (guiStateManager.Interaction().GetCaptureID() != GUI::ID::Invalid())
        ? guiStateManager.Interaction().GetCaptureID()
        : overID;
    guiStateManager.EndFrame(hoverID, m_buttonsHeld, idManager, overID);

    InvalidateCachedScopesForInput(overID);

    // -- Dispatch the queued input events to element handlers.
    inputManager->Process(guiStateManager, elementStorage, tabChainManager, textSystem, overID);

    m_frame.RunStage(GUI::FrameStage::PostInput, ctx);

    EndDockingFrame();

    // No render pass, but fire PreRender/PostRender for subsystems that hook them.
    m_frame.RunStage(GUI::FrameStage::PreRender,  ctx);
    m_frame.RunStage(GUI::FrameStage::PostRender, ctx);

    guiStateManager.GetScrollManagement().EndFrame(idManager);
    tabChainManager.EndFrame();
    tooltipManager.EndFrame();
    menuManager.EndFrame(elementStorage, guiStateManager);
    [[maybe_unused]] bool wmEnd = windowManager->EndFrame();

    m_frame.RunStage(GUI::FrameStage::EndFrame, ctx);

    idManager.EndFrame();
}

// ---------------------------------------------------------------------------
// Root window
// ---------------------------------------------------------------------------

bool HeadlessGUIApp::BeginRootWindow(GUI::int2 size)
{
    // Stand-in for BeginWindow() without an OS window: an IsWindow + NotFocusable
    // content root.
    GUI::Element root{};
    root.id    = m_rootWindowID;
    root.flags = GUI::ElementFlags::IsWindow
               | GUI::ElementFlags::WindowNoCaption
               | GUI::ElementFlags::WindowNotFocusable;
    // Production window style, for its padding + Display::Flex defaults.
    root.styleRef = theme.windowStyleRef;
    root.state    = guiStateManager.GetCurrentState(m_rootWindowID);

    GUI::LayoutOverride lo = GUI::LayoutOverride{}
        .Size(size.x, size.y)
        .Position(0, 0);

    auto add = elementStorage.Add(std::move(root), std::move(lo));
    VERIFY(add.status);
    windowRegistry.BeginWindowScope(m_rootWindowID, add.ref);
    VERIFY(elementStorage.BeginChildren());

    [[maybe_unused]] bool pushed = idManager.PushParent(m_rootWindowID);

    m_rootWindowOpen = true;
    return true;
}

void HeadlessGUIApp::EndRootWindow()
{
    if (!m_rootWindowOpen) return;
    [[maybe_unused]] bool popped = idManager.PopParent();
    VERIFY(elementStorage.EndChildren());
    [[maybe_unused]] auto wrResult = windowRegistry.EndWindowScope();
    m_rootWindowOpen = false;
}

// ---------------------------------------------------------------------------
// Input injection
// ---------------------------------------------------------------------------

void HeadlessGUIApp::InjectMouseMove(GUI::int2 pos, GUI::MouseButtons buttonsHeld,
                                      GUI::KeyModifier mods)
{
    m_mousePos    = pos;
    m_buttonsHeld = buttonsHeld;
    inputManager->OnMouseMove(m_rootWindowID, pos, buttonsHeld, mods);
}

void HeadlessGUIApp::InjectMouseDown(GUI::int2 pos, GUI::MouseButtons button,
                                      GUI::KeyModifier mods)
{
    m_mousePos = pos;
    m_buttonsHeld = m_buttonsHeld | button;
    // A MouseDown needs a preceding MouseMove to establish the element under the
    // cursor.
    inputManager->OnMouseButton(m_rootWindowID, pos, m_buttonsHeld, mods,
                                 GUI::MouseAction::MouseDown);
}

void HeadlessGUIApp::InjectMouseUp(GUI::int2 pos, GUI::MouseButtons button,
                                    GUI::KeyModifier mods)
{
    m_mousePos = pos;
    m_buttonsHeld = m_buttonsHeld & ~button;
    inputManager->OnMouseButton(m_rootWindowID, pos, m_buttonsHeld, mods,
                                 GUI::MouseAction::MouseUp);
}

void HeadlessGUIApp::InjectKey(GUI::KeyCode key, GUI::KeyAction action,
                                GUI::KeyModifier mods)
{
    inputManager->OnKeyboard(m_rootWindowID, key, /*repeat*/0, mods, action);
    // Like the SDL backend, keyboard events also go to the text system.
    if (textSystem)
        textSystem->OnKeyboard(m_rootWindowID, key, /*repeat*/0, mods, action);
}

void HeadlessGUIApp::InjectMouseScroll(GUI::int2 pos, GUI::int2 scrollOffset,
                                        GUI::KeyModifier mods)
{
    m_mousePos = pos;
    inputManager->OnMouseScroll(m_rootWindowID, pos,
                                GUI::float2{ float(scrollOffset.x), float(scrollOffset.y) }, mods);
}

void HeadlessGUIApp::ClickAt(GUI::int2 pos, GUI::MouseButtons button,
                              GUI::KeyModifier mods)
{
    InjectMouseMove(pos, m_buttonsHeld, mods);
    InjectMouseDown(pos, button, mods);
    InjectMouseUp(pos, button, mods);
}

void HeadlessGUIApp::DragFrom(GUI::int2 from, GUI::int2 to,
                               GUI::MouseButtons button, GUI::KeyModifier mods)
{
    // The move at `to` carries the held button: drag handlers only see drags
    // through such moves.
    InjectMouseMove(from, m_buttonsHeld, mods);
    InjectMouseDown(from, button, mods);
    InjectMouseMove(to, m_buttonsHeld, mods);
    InjectMouseUp(to, button, mods);
}

void HeadlessGUIApp::InjectText(std::string_view text)
{
    for (char c : text)
    {
        // Map ASCII to KeyCode payload exactly the way SDL does
        // (gui_window_backend_sdl.cpp: cast text byte → KeyCode).
        InjectKey(static_cast<GUI::KeyCode>(static_cast<unsigned char>(c)),
                  GUI::KeyAction::KeyChar);
    }
}

void HeadlessGUIApp::AdvanceTime(uint32_t deltaMs)
{
    m_timeMs += deltaMs;
}

// ---------------------------------------------------------------------------
// Inspection helpers
// ---------------------------------------------------------------------------

GUI::IntRect HeadlessGUIApp::GetElementRect(GUI::ID id) const
{
    auto ref = elementStorage.FindElementRefByID(id);
    if (ref == GUI::ElementRef::Invalid()) return GUI::IntRect{};
    auto* rp = elementStorage.GetElementRectPtr(ref);
    return rp ? rp->rect : GUI::IntRect{};
}

bool HeadlessGUIApp::HasElement(GUI::ID id) const
{
    return elementStorage.FindElementRefByID(id) != GUI::ElementRef::Invalid();
}

GUI::int2 HeadlessGUIApp::CenterOf(GUI::ID id) const
{
    auto r = GetElementRect(id);
    return GUI::int2{ r.x + r.w / 2, r.y + r.h / 2 };
}

} // namespace GUITest
