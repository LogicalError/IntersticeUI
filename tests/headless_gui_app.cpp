#include <gui_core/text/gui_utf8.h>
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
{
    m_themePath = std::move(themePath);
    m_rootWindowID.value.dynamicParts.mode      = tachyon::IDMode::Static;
    m_rootWindowID.value.dynamicParts.hashValue = 0xBEEF;
}

HeadlessGUIApp::~HeadlessGUIApp()
{
    Cleanup();
}

tachyon::Result<void> HeadlessGUIApp::Initialize(const InitializeOptions& options)
{
    using ::testing::_;

    // One fake 1920x1080 monitor; layout queries GetMonitorArea for absolute
    // elements and auto-centered windows.
    tachyon::MonitorInfo monitor{};
    monitor.rect       = tachyon::IntRect{ 0, 0, 1920, 1080 };
    monitor.workArea   = tachyon::IntRect{ 0, 0, 1920, 1080 };
    monitor.isPrimary  = true;
    std::vector<tachyon::MonitorInfo> monitors{ monitor };
    ON_CALL(mockBackend, GetMonitors()).WillByDefault(Return(monitors));
    ON_CALL(mockBackend, GetMonitorAt(_)).WillByDefault(Return(0));
    ON_CALL(mockBackend, GetMonitorFromWindow(_)).WillByDefault(Return(0));
    ON_CALL(mockBackend, GetScreenMousePos()).WillByDefault(Return(tachyon::int2{0,0}));
    ON_CALL(mockBackend, GetMouseButtons()).WillByDefault(Return(tachyon::MouseButtons::None));
    // Popup/tooltip window creation returns a stable non-null sentinel so
    // OSWindowManager registers the window; GetWindowRect returns the monitor
    // area so layout doesn't collapse popups.
    static int dummyHandleStorage = 0;
    tachyon::OSWindowHandle fakeHandle = &dummyHandleStorage;
    ON_CALL(mockBackend, CreateWindow(_, ::testing::An<tachyon::int2>(), _, _))
        .WillByDefault(Return(fakeHandle));
    ON_CALL(mockBackend, CreateWindow(_, ::testing::An<tachyon::IntRect>(), _, _))
        .WillByDefault(Return(fakeHandle));
    ON_CALL(mockBackend, IsWindow(_)).WillByDefault(Return(true));
    ON_CALL(mockBackend, GetWindowRect(_))
        .WillByDefault(Return(tachyon::IntRect{ 0, 0, 200, 300 }));
    ON_CALL(mockBackend, GetWindowPos(_)).WillByDefault(Return(tachyon::int2{ 0, 0 }));
    ON_CALL(mockBackend, GetWindowSize(_)).WillByDefault(Return(tachyon::int2{ 200, 300 }));
    ON_CALL(mockBackend, GetTicks()).WillByDefault(Return(0u));

    // Slots GUIApplication's Initialize() normally fills. StubTextSystem measures
    // text by character count; no font backend.
    textSystem    = &ownedTextSystem;
    windowBackend = &mockBackend;
    inputManager  = std::make_unique<tachyon::InputManager>();
    layoutEngine  = std::make_unique<tachyon::LayoutEngine>(*textSystem, guiStateManager.GetScrollManagement());
    // Menus, tooltips and popups reach the mock-backed OSWindowManager through this.
    windowManager = std::make_unique<tachyon::OSWindowManager>(nullptr, &mockBackend);

    // Static window IDs Initialize() reserves for tooltip / context-menu /
    // tab-drag-ghost popups; widgets using them assert otherwise.
    tooltipWindowID      = idManager.CreateStaticID();
    contextMenuWindowID  = idManager.CreateStaticID();
    tabDragGhostWindowID = idManager.CreateStaticID();

    // Subsystems, as Initialize() creates them after the manager wiring; widgets
    // dereference these.
    m_sliderFieldTrack  = std::make_unique<tachyon::SliderFieldTrackSubsystem>();
    m_menuRects         = std::make_unique<tachyon::MenuRectsSubsystem>();
    m_labelGroup        = std::make_unique<tachyon::LabelGroupSubsystem>(m_frame, elementStorage, guiStateManager, theme);
    m_buttonGroup       = std::make_unique<tachyon::ButtonGroupSubsystem>(m_frame, elementStorage, styleManager);
    m_colorPickerSub    = std::make_unique<tachyon::ColorPickerSubsystem>(m_frame);
    m_gradientEditorSub = std::make_unique<tachyon::GradientEditorSubsystem>(m_frame);
    m_renameSub         = std::make_unique<tachyon::RenameSubsystem>();
    m_reorderable       = std::make_unique<tachyon::ReorderableSubsystem>(m_frame);
    m_dockTabDragSub    = std::make_unique<tachyon::DockTabDragSubsystem>();
    m_uiCache           = std::make_unique<tachyon::UICacheManager>(m_frame, elementStorage, guiStateManager);

    RegisterFrameBalanceChecks();
    RegisterFrameHooks();

    // Visual-state transitions depend on the clock; disable them so tests see
    // settled visuals (re-enable with transitionManager.SetEnabled(true)).
    transitionManager.SetEnabled(false);

    // Parse the CSS theme and register every widget style (no graphics context).
    if (!options.loadStyles)
        return {};
    return InitializeStyles();
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
    // As OnFrame: a frame run from inside a frame is reported and skipped.
    if (m_inFrame)
    {
        ReportMisuse("GUIApplication: a frame run from inside a frame (ignored)");
        return;
    }
    struct FrameScope { bool& inFrame; FrameScope(bool& f) : inFrame(f) { inFrame = true; } ~FrameScope() { inFrame = false; } } frameScope{ m_inFrame };

    // Rebuilt each frame; the stage points mirror production's OnFrame so
    // subsystem hooks fire under test.
    tachyon::FrameContext ctx{
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

    m_frame.RunStage(tachyon::FrameStage::PreFrame, ctx);
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
            if (e.type == tachyon::EventType::MouseMove)   hasMouseMove = true;
            if (e.type == tachyon::EventType::MouseDown)   hasMousePress = true;
            if (e.type == tachyon::EventType::KeyDown)     hasKeyboard = true;
            if (e.type == tachyon::EventType::MouseScroll) hasScroll = true;
        }
    }
    tooltipManager.BeginFrame(guiStateManager.Interaction().GetHoveredID(),
                               guiStateManager.Interaction().GetFocusedID(),
                               static_cast<float>(m_timeMs) / 1000.0f,
                               hasMouseMove, hasMousePress, hasKeyboard, hasScroll,
                               menuManager.IsOpen());
    NoteTargetAncestors();
    textSystem->BeginFrame(m_mousePos, m_buttonsHeld, m_timeMs);

    VERIFY(elementStorage.BeginConstruction());
    windowRegistry.Clear();
    guiStateManager.Cursor().Clear();

    m_frame.RunStage(tachyon::FrameStage::PreConstruct, ctx);

    // -- User construction.
    if (onGuiFn) onGuiFn();

    m_frame.RunStage(tachyon::FrameStage::PostConstruct, ctx);

    // As OnFrame: a drag's visual (a ghost window following the cursor) is declared now.
    if (guiStateManager.Drag().IsDragging())
    {
        tachyon::DragVisualContext visual;
        visual.screenPos     = m_mousePos;
        visual.sourceID      = guiStateManager.Drag().GetDraggedID();
        visual.targetID      = guiStateManager.Drag().GetDragOverID();
        visual.isDropValid   = guiStateManager.Drag().GetValidatedTargetID() != tachyon::ID::Invalid();
        visual.leftMouseDown = (m_buttonsHeld & tachyon::MouseButtons::Left) != tachyon::MouseButtons::None;
        visual.payloadType   = guiStateManager.Drag().GetEventPayloadType();
        visual.payloadValue  = guiStateManager.Drag().GetEventPayloadValue();
        visual.payload       = &guiStateManager.Drag().GetPayload();
        guiStateManager.Drag().InvokeActiveVisualizer(visual);
    }

    VERIFY(elementStorage.EndConstruction());

    textSystem->Update();

    m_frame.RunStage(tachyon::FrameStage::PreLayout, ctx);

    // -- Layout. SyncWindowsBeforeLayout is skipped (no live OS windows).
    layoutEngine->Layout(*windowManager, elementStorage);

    // AddPostLayout callbacks only when asked (runPostLayoutCallbacks); the subsystems'
    // OnPostLayout always runs, after them as in GUIApplication::Frame.
    for (const auto& entry : elementStorage.GetPostLayoutEntries())
    {
        if (!runPostLayoutCallbacks) break;
        const auto* er = elementStorage.GetElementRectPtr(entry.ownerRef);
        if (!er) continue;
        tachyon::PostLayoutContext plCtx{ elementStorage, entry.ownerRef,
                                          er->rect, er->innerRect, er->contentTransform };
        entry.callback(plCtx);
    }
    m_frame.RunStage(tachyon::FrameStage::PostLayout, ctx);

    windowRegistry.BuildRenderSkipLinks();

    m_frame.RunStage(tachyon::FrameStage::PreInput, ctx);

    // -- EndFrame hit-tests the one root window and commits the hover/active
    // targets for Process(), which runs after it.
    inputManager->UpdatePreHover(guiStateManager);
    tachyon::FrameInput frameInput
    {
        .activeWindowID   = activeWindow != tachyon::ID::Invalid() ? activeWindow : m_rootWindowID,
        .windowUnderMouse = m_rootWindowID,
        .mousePos         = m_mousePos,
        .mouseButtons     = m_buttonsHeld,
        .hitTest = [this](tachyon::ID window, tachyon::int2 pos)
        {
            const tachyon::ElementRef windowRef = windowRegistry.GetWindowRef(window);
            return windowRef != tachyon::ElementRef::Invalid() ? elementStorage.GetElementIDAt(windowRef, pos)
                                                                  : tachyon::ID::Invalid();
        },
    };
    const tachyon::ID overID = guiStateManager.EndFrame(frameInput, idManager).overID;

    // The PropagatesDescendantState ancestors of the hovered and focused elements, as in
    // GUIApplication::Frame (without its transitions).
    guiStateManager.Interaction().SetPropagatedHoverIDs(
        CollectPropagatedAncestors(elementStorage, guiStateManager.Interaction().GetHoveredID()));
    guiStateManager.Interaction().SetPropagatedFocusedIDs(
        CollectPropagatedAncestors(elementStorage, guiStateManager.Interaction().GetFocusedID()));
    UpdateMouseCursor(guiStateManager.Interaction().GetHoveredID(), m_rootWindowID, m_mousePos);

    menuManager.CloseOnInputOutside(*inputManager, elementStorage, guiStateManager);
    RunPressOutsideWatches();
    InvalidateCachedScopesForInput(overID);

    // -- Dispatch the queued input events to element handlers.
    inputManager->Process(guiStateManager, elementStorage, tabChainManager, textSystem, overID);

    m_frame.RunStage(tachyon::FrameStage::PostInput, ctx);

    EndDockingFrame();

    // No render pass, but fire PreRender/PostRender for subsystems that hook them.
    m_frame.RunStage(tachyon::FrameStage::PreRender,  ctx);
    m_frame.RunStage(tachyon::FrameStage::PostRender, ctx);
    transitionManager.Purge(static_cast<float>(m_timeMs) / 1000.0f);

    guiStateManager.GetScrollManagement().EndFrame(idManager);
    tabChainManager.EndFrame();
    tooltipManager.EndFrame();
    menuManager.EndFrame(elementStorage, guiStateManager);
    [[maybe_unused]] bool wmEnd = windowManager->EndFrame();

    m_frame.RunStage(tachyon::FrameStage::EndFrame, ctx);

    idManager.EndFrame();
}

// ---------------------------------------------------------------------------
// Root window
// ---------------------------------------------------------------------------

bool HeadlessGUIApp::BeginRootWindow(tachyon::int2 size, tachyon::ElementFlags extraFlags, tachyon::StyleRef style)
{
    // Stand-in for BeginWindow() without an OS window: an IsWindow + NotFocusable
    // content root.
    tachyon::Element root{};
    root.id    = m_rootWindowID;
    root.flags = tachyon::ElementFlags::IsWindow
               | tachyon::ElementFlags::WindowNoCaption
               | tachyon::ElementFlags::WindowNotFocusable
               | extraFlags;
    // Production window style, for its padding + Display::Flex defaults.
    root.styleRef = style != tachyon::StyleRef::Invalid() ? style : theme.windowStyleRef;
    root.state    = guiStateManager.GetCurrentState(m_rootWindowID);

    tachyon::LayoutOverride lo = tachyon::LayoutOverride{}
        .Size(size)   // the root window's pixels
        .Position(0, 0);

    auto add = elementStorage.Add(std::move(root), std::move(lo));
    VERIFY(add.status);
    windowRegistry.BeginWindowScope(m_rootWindowID, add.ref);
    VERIFY(elementStorage.BeginChildren());

    tabChainManager.BeginWindow(m_rootWindowID);
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
    tabChainManager.EndWindow();
    m_rootWindowOpen = false;
}

// ---------------------------------------------------------------------------
// Input injection
// ---------------------------------------------------------------------------

void HeadlessGUIApp::InjectMouseMove(tachyon::int2 pos, tachyon::MouseButtons buttonsHeld,
                                      tachyon::KeyModifier mods)
{
    m_mousePos    = pos;
    m_buttonsHeld = buttonsHeld;
    inputManager->OnMouseMove(m_rootWindowID, pos, buttonsHeld, mods);
}

void HeadlessGUIApp::InjectMouseDown(tachyon::int2 pos, tachyon::MouseButtons button,
                                      tachyon::KeyModifier mods)
{
    m_mousePos = pos;
    m_buttonsHeld = m_buttonsHeld | button;
    // A MouseDown needs a preceding MouseMove to establish the element under the
    // cursor.
    inputManager->OnMouseButton(m_rootWindowID, pos, m_buttonsHeld, mods,
                                 tachyon::MouseAction::MouseDown);
}

void HeadlessGUIApp::InjectMouseUp(tachyon::int2 pos, tachyon::MouseButtons button,
                                    tachyon::KeyModifier mods)
{
    m_mousePos = pos;
    m_buttonsHeld = m_buttonsHeld & ~button;
    inputManager->OnMouseButton(m_rootWindowID, pos, m_buttonsHeld, mods,
                                 tachyon::MouseAction::MouseUp);
}

void HeadlessGUIApp::InjectKey(tachyon::KeyCode key, tachyon::KeyAction action,
                                tachyon::KeyModifier mods)
{
    inputManager->OnKeyboard(m_rootWindowID, key, /*repeat*/0, mods, action);
    // Like the SDL backend, keyboard events also go to the text system.
    if (textSystem)
        textSystem->OnKeyboard(m_rootWindowID, key, /*repeat*/0, mods, action);
}

void HeadlessGUIApp::InjectMouseScroll(tachyon::int2 pos, tachyon::int2 scrollOffset,
                                        tachyon::KeyModifier mods)
{
    m_mousePos = pos;
    inputManager->OnMouseScroll(m_rootWindowID, pos,
                                tachyon::float2{ float(scrollOffset.x), float(scrollOffset.y) }, mods);
}

void HeadlessGUIApp::ClickAt(tachyon::int2 pos, tachyon::MouseButtons button,
                              tachyon::KeyModifier mods)
{
    InjectMouseMove(pos, m_buttonsHeld, mods);
    InjectMouseDown(pos, button, mods);
    InjectMouseUp(pos, button, mods);
}

void HeadlessGUIApp::DragFrom(tachyon::int2 from, tachyon::int2 to,
                               tachyon::MouseButtons button, tachyon::KeyModifier mods)
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
    // One KeyChar per codepoint, as the OS backend sends typed text; malformed text stops.
    for (size_t i = 0; i < text.size(); )
    {
        const uint32_t codepoint = tachyon::NextUtf8Codepoint(text, i);
        if (codepoint == tachyon::kInvalidCodepoint)
            return;
        InjectKey(static_cast<tachyon::KeyCode>(codepoint), tachyon::KeyAction::KeyChar);
    }
}

void HeadlessGUIApp::AdvanceTime(uint32_t deltaMs)
{
    m_timeMs += deltaMs;
}

// ---------------------------------------------------------------------------
// Inspection helpers
// ---------------------------------------------------------------------------

tachyon::IntRect HeadlessGUIApp::GetElementRect(tachyon::ID id) const
{
    auto ref = elementStorage.FindElementRefByID(id);
    if (ref == tachyon::ElementRef::Invalid()) return tachyon::IntRect{};
    auto* rp = elementStorage.GetElementRectPtr(ref);
    return rp ? rp->rect : tachyon::IntRect{};
}

bool HeadlessGUIApp::HasElement(tachyon::ID id) const
{
    return elementStorage.FindElementRefByID(id) != tachyon::ElementRef::Invalid();
}

tachyon::int2 HeadlessGUIApp::CenterOf(tachyon::ID id) const
{
    auto r = GetElementRect(id);
    return tachyon::int2{ r.x + r.w / 2, r.y + r.h / 2 };
}

tachyon::ID HeadlessGUIApp::FindByText(std::string_view text) const
{
    const auto& storage = elementStorage;
    for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
        for (const tachyon::Element& e : it->GetChildren(storage))
            if (e.textID != tachyon::TextID::Invalid && ownedTextSystem.GetText(e.textID) == text)
                return e.id;
    return tachyon::ID::Invalid();
}

int HeadlessGUIApp::CountByText(std::string_view text) const
{
    const auto& storage = elementStorage;
    int count = 0;
    for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
        for (const tachyon::Element& e : it->GetChildren(storage))
            if (e.textID != tachyon::TextID::Invalid && ownedTextSystem.GetText(e.textID) == text)
                ++count;
    return count;
}

tachyon::ID HeadlessGUIApp::FindControlAfterLabel(std::string_view label) const
{
    const auto& storage = elementStorage;
    for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
    {
        const auto children = it->GetChildren(storage);
        for (size_t i = 0; i + 1 < children.size(); ++i)
            if (children[i].textID != tachyon::TextID::Invalid && ownedTextSystem.GetText(children[i].textID) == label)
                return children[i + 1].id;
    }
    return tachyon::ID::Invalid();
}

std::string_view HeadlessGUIApp::TextOf(tachyon::ID id) const
{
    const auto& storage = elementStorage;
    for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
        for (const tachyon::Element& e : it->GetChildren(storage))
            if (e.id == id)
                return e.textID != tachyon::TextID::Invalid ? ownedTextSystem.GetText(e.textID) : std::string_view{};
    return {};
}

} // namespace GUITest
