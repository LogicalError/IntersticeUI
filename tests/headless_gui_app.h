#pragma once

// Headless test harness for GUIApplication.
//
// Stands up enough of the framework to drive a full frame (construction ->
// layout -> input dispatch) without SDL, Vulkan, or any OS window. Use this
// to integration-test widgets whose behaviour lives inside event-handler
// lambdas (Table sort/select/reorder, Tabs, TreeView, etc).
//
// Usage:
//     HeadlessGUIApp app;
//     app.Initialize();
//     // Set up one or more frames. Each Frame() runs:
//     //   BeginFrame on managers  ->  your onGuiFn()  ->  layout  ->  dispatch.
//     app.Frame([&](){
//         app.BeginRootWindow();
//         /* widgets here */
//         app.EndRootWindow();
//     });
//     // Inject input and run another frame to observe handler effects.
//     app.InjectMouseDown({ 120, 40 });
//     app.Frame([&](){ ... });
//
// Compared to the real OnFrame, this harness:
//   - Uses StubTextSystem (no font/glyph loading, hit-tests are no-ops).
//   - Uses a NiceMock IWindowBackend stubbed to return one 1920x1080 monitor.
//   - Skips Vulkan / GraphicsContext entirely.
//   - Emits a single root "window" element per frame sized to the monitor;
//     widgets become its children.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <gui_controls/gui_application_internal.h>

#include "gui_text_system_stub.h"
#include "mock_window_backend.h"

#include <functional>
#include <memory>
#include <string>

namespace GUITest
{

class HeadlessGUIApp : public interstice::GUIApplicationInternals
{
public:
    // CSS path; defaults to the engine's theme. Relative to workdirectory/, the
    // tests' cwd.
    explicit HeadlessGUIApp(std::string themePath = "themes/dark.css");
    ~HeadlessGUIApp() override;

    // Headless stand-in for GUIApplication::Initialize: no window system, so only
    // loadStyles applies.
    bool Initialize(const InitializeOptions& options = {}) override;
    void Cleanup() override;

    // The harness never calls OnGUI - tests pass their widget callback
    // directly into Frame().
    void OnGUI() override {}

    // One full frame: BeginFrame, widget callback, layout, hover, dispatch,
    // EndFrame. Handler effects from frame N are observable from frame N+1.
    void Frame(std::function<void()> onGuiFn);

    // Emit a root "window" element filling the monitor area. Widgets
    // emitted between BeginRootWindow/EndRootWindow become its children
    // and participate in hit-testing/layout.
    // The default style is the production `.window` (a wrapping row). Pass
    // theme.mainWindowStyleRef (a stretching column, like the app's main
    // window or a docked panel) for content that fills the height, such as
    // the node view's canvas.
    bool BeginRootWindow(interstice::int2 size = { 1920, 1080 },
                         interstice::ElementFlags extraFlags = interstice::ElementFlags::None,
                         interstice::StyleRef style = {});
    void EndRootWindow();

    // ---- Input injection ------------------------------------------------
    //
    // Queued events dispatch during the next Frame(), after construction +
    // layout, as with real input.
    void InjectMouseMove(interstice::int2 pos,
                         interstice::MouseButtons buttonsHeld = interstice::MouseButtons::None,
                         interstice::KeyModifier mods = interstice::KeyModifier::None);
    void InjectMouseDown(interstice::int2 pos,
                         interstice::MouseButtons button = interstice::MouseButtons::Left,
                         interstice::KeyModifier mods = interstice::KeyModifier::None);
    void InjectMouseUp(interstice::int2 pos,
                       interstice::MouseButtons button = interstice::MouseButtons::Left,
                       interstice::KeyModifier mods = interstice::KeyModifier::None);
    void InjectKey(interstice::KeyCode key,
                   interstice::KeyAction action = interstice::KeyAction::KeyDown,
                   interstice::KeyModifier mods = interstice::KeyModifier::None);

    // Wheel/scroll. scrollOffset is in "ticks" - the engine multiplies by
    // kWheelScrollStep at the UI scale (see TryDispatchHierarchicalScroll). Use {0, +1} to
    // scroll down one notch, {0, -1} to scroll up.
    void InjectMouseScroll(interstice::int2 pos,
                           interstice::int2 scrollOffset,
                           interstice::KeyModifier mods = interstice::KeyModifier::None);

    // Press + release at `pos`, each with a matching MouseMove so hit-testing
    // sees the cursor there.
    void ClickAt(interstice::int2 pos,
                 interstice::MouseButtons button = interstice::MouseButtons::Left,
                 interstice::KeyModifier mods = interstice::KeyModifier::None);

    // Convenience: press at `from`, drag through `to` with `button` held,
    // release at `to`. Emits one intermediate MouseMove so handlers that
    // require >0 delta to enter drag mode see motion.
    void DragFrom(interstice::int2 from, interstice::int2 to,
                  interstice::MouseButtons button = interstice::MouseButtons::Left,
                  interstice::KeyModifier mods = interstice::KeyModifier::None);

    // Per-character text input. Each character is sent to the text system
    // as a KeyAction::KeyChar event (matching the SDL backend's path). For
    // the StubTextSystem this routes characters into the active editable
    // text registered by Field(string&) / NumericalField. The text is UTF-8:
    // one KeyChar per codepoint.
    void InjectText(std::string_view text);

    // Advance the harness's notion of "now". Forwarded to
    // textSystem->BeginFrame on the next Frame() call so tooltip-delay /
    // hover-debounce code sees elapsed time. Defaults to 0 (frozen clock).
    void AdvanceTime(uint32_t deltaMs);

    // ---- Inspection helpers ---------------------------------------------
    //
    // Call after Frame() to read the results of the last frame's layout.
    interstice::IntRect GetElementRect(interstice::ID id) const;
    bool         HasElement(interstice::ID id) const;
    // Screen-space center of an element with the given ID. Useful for
    // "click the middle of this cell" style tests.
    interstice::int2    CenterOf(interstice::ID id) const;
    interstice::ID      RootWindowID() const { return m_rootWindowID; }

    // Mock backend & owned systems - exposed so tests can tune monitor
    // configuration or read recorded events.
    ::testing::NiceMock<MockWindowBackend> mockBackend{};
    interstice::StubTextSystem                    ownedTextSystem{};

private:
    std::string m_themePath;
    interstice::ID     m_rootWindowID       = interstice::ID::Invalid();
    bool        m_rootWindowOpen     = false;
    interstice::int2   m_mousePos           { 0, 0 }; // last injected mouse position
    interstice::MouseButtons m_buttonsHeld  = interstice::MouseButtons::None;
    uint32_t    m_timeMs             = 0;      // monotonic clock fed to BeginFrame
};

} // namespace GUITest
