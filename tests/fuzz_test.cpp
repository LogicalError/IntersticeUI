// Robustness (#87), round three: the parsers and internals fed garbage. Each input either
// parses or is rejected; nothing crashes, hangs or leaves a value that breaks later code.
// Deterministic (fixed seeds), so a failure reproduces.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"
#include "skribidi_test_fixture.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_core/input/gui_undo_history.h>
#include <gui_core/style/gui_style_sheet.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;
    using GUITest::MisuseReports;

    std::string Pick(std::mt19937& rng, const std::vector<std::string>& parts, int maxCount)
    {
        std::string out;
        const int count = int(rng() % uint32_t(maxCount + 1));
        for (int i = 0; i < count; i++)
            out += parts[rng() % parts.size()];
        return out;
    }

    void ExpectAFrameWorks(HeadlessGUIApp& app)
    {
        const ID buttonID = app.idManager.CreateStaticID();
        for (int i = 0; i < 2; i++)
            app.Frame([&] { app.BeginRootWindow(); app.Button(buttonID, "OK", [] {}); app.EndRootWindow(); });
        EXPECT_GT(app.GetElementRect(buttonID).w, 0);
    }

    // ---- CSS ----

    const std::vector<std::string> kCssParts = {
        ".a", ".b", " ", "\n", "{", "}", ":", ";", ",", "(", ")", "[", "]", "*", ">", "+", "~", "::", ":hover",
        "color", "padding", "margin", "width", "border-width", "background-color", "font-size", "display", "flex",
        "var(--x)", "var(--y, 3px)", "var(", "--x", "--y", "--x:", "--y:", "calc(", "100%", "px", "em", "-",
        "1", "0", "1e40", "-1e40", "99999999999999999999", "0.0000001", "#", "#fff", "#ff00ff80", "#zz",
        "rgb(", "rgba(1,2,3,4)", "hsl(400, -10%, 200%)", "linear-gradient(", "to right", "deg", "/*", "*/",
        "\"", "'", "\\", "@media", "@import", "!important", "url(", std::string(1, '\0'), "\xff", "\xc3",
    };

    TEST(FuzzTest, StyleSheetParserTakesAnything)
    {
        std::mt19937 rng(1234);
        for (int i = 0; i < 4000; i++)
        {
            const std::string source = Pick(rng, kCssParts, 120);
            const StyleSheet sheet = ParseStyleSheet(source);
            (void)sheet;
        }
        SUCCEED();
    }

    TEST(FuzzTest, StyleSheetEdgeCases)
    {
        const std::string cases[] = {
            ":root { --a: var(--b); --b: var(--a); } .x { color: var(--a); padding: var(--b); }",   // a var() cycle
            ":root { --a: var(--a); } .x { width: var(--a); }",                                       // self-reference
            ":root { --a: var(--a, var(--a, var(--a))); } .x { width: var(--a); }",
            std::string(100000, '{'),
            std::string(100000, '}'),
            ".x { color: red; /* never closed",
            ".x { content: \"never closed; }",
            ".x" + std::string(1 << 20, 'y') + " { width: 1px; }",
            ".x { width: " + std::string(5000, '9') + "px; }",
            ".x { width: calc(" + std::string(5000, '(') + "1px" + std::string(5000, ')') + "); }",
            std::string("\0\0\0{}", 5),
            "",
        };
        for (const std::string& source : cases)
        {
            const StyleSheet sheet = ParseStyleSheet(source);
            (void)sheet;
        }
        SUCCEED();
    }

    // Fuzzed palettes through a real theme reload (var() resolution, style registration):
    // each one loads or falls back, and the app keeps working.
    TEST(FuzzTest, ThemeReloadsWithFuzzedPalettes)
    {
        namespace fs = std::filesystem;
        const fs::path dir = "fuzz-theme";
        fs::create_directories(dir);
        fs::copy_file("themes/styles.css", dir / "styles.css", fs::copy_options::overwrite_existing);
        std::string realPalette;
        {
            std::ifstream in("themes/dark.css");
            realPalette.assign(std::istreambuf_iterator<char>(in), {});
        }

        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        std::mt19937 rng(99);
        for (int i = 0; i < 30; i++)
        {
            // Half are the real palette with garbage spliced in; half pure garbage.
            std::string palette = (i % 2 == 0) ? realPalette : std::string();
            const size_t at = palette.empty() ? 0 : rng() % palette.size();
            palette.insert(at, Pick(rng, kCssParts, 60));
            const fs::path path = dir / ((i % 2 == 0) ? "a.css" : "b.css");
            {
                std::ofstream out(path, std::ios::binary);
                out << palette;
            }
            app.RequestReloadTheme(path.string(), 1.0f);
            ExpectAFrameWorks(app);
        }
        app.RequestReloadTheme("themes/dark.css", 1.0f);
        ExpectAFrameWorks(app);
        app.Cleanup();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // ---- Saved dock layouts ----

    struct Panels
    {
        std::vector<std::string> names = { "A", "B", "C", "D" };
        std::function<std::optional<ID>(std::string_view)> idOf() const
        {
            return [this](std::string_view name) -> std::optional<ID> {
                for (size_t i = 0; i < names.size(); i++)
                    if (names[i] == name) return ID{ .value{ .integer = 7000 + i } };
                return std::nullopt;
            };
        }
    };

    // A tree that parsed must be one the dock code can use.
    void ExpectASaneTree(const DockNode* node, int depth = 0)
    {
        ASSERT_NE(node, nullptr);
        ASSERT_LE(depth, 64);
        if (node->isLeaf)
        {
            EXPECT_FALSE(node->panelIds.empty()) << "an empty leaf survived";
            EXPECT_GE(node->selectedTab, 0);
            EXPECT_LT(node->selectedTab, int(node->panelIds.size())) << "the selected tab is out of range";
            return;
        }
        EXPECT_GE(node->splitSize, 0);
        EXPECT_GE(node->minFirstSize, 0);
        EXPECT_GE(node->minSecondSize, 0);
        ExpectASaneTree(node->first.get(), depth + 1);
        ExpectASaneTree(node->second.get(), depth + 1);
    }

    const std::string kLayout =
        "{\"split\":\"LeftPinned\",\"size\":300,\"minFirst\":80,\"minSecond\":80,"
        "\"first\":{\"tabs\":[\"A\",\"B\"],\"selected\":1},"
        "\"second\":{\"split\":\"BottomPinned\",\"size\":200,\"minFirst\":80,\"minSecond\":80,"
        "\"first\":{\"tabs\":[\"C\"],\"selected\":0},\"second\":{\"tabs\":[\"D\"],\"selected\":0}}}";

    TEST(FuzzTest, DockLayoutParserTakesAnything)
    {
        Panels panels;
        const auto whole = ParseDockTree(kLayout, panels.idOf());
        ASSERT_NE(whole, nullptr) << "test setup: the sample parses";
        ExpectASaneTree(whole.get());

        // Every truncation.
        for (size_t n = 0; n < kLayout.size(); n++)
            if (const auto tree = ParseDockTree(std::string_view(kLayout).substr(0, n), panels.idOf()))
                ExpectASaneTree(tree.get());

        // Every byte replaced by something that matters to JSON.
        const char swaps[] = { '"', '{', '}', '[', ']', ',', ':', '9', '-', '\0', 'x' };
        for (size_t at = 0; at < kLayout.size(); at++)
            for (char c : swaps)
            {
                std::string text = kLayout;
                text[at] = c;
                if (const auto tree = ParseDockTree(text, panels.idOf()))
                    ExpectASaneTree(tree.get());
            }
    }

    TEST(FuzzTest, DockLayoutEdgeCases)
    {
        Panels panels;
        std::string deep;
        for (int i = 0; i < 100000; i++) deep += "{\"split\":\"LeftPinned\",\"first\":";
        const std::string cases[] = {
            deep,
            "{\"tabs\":[\"A\"],\"selected\":999}",
            "{\"tabs\":[\"A\",\"B\"],\"selected\":-5}",
            "{\"tabs\":[\"A\",\"A\",\"A\"],\"selected\":2}",
            "{\"tabs\":[],\"selected\":0}",
            "{\"tabs\":[\"Gone\"],\"selected\":0}",
            "{\"split\":\"LeftPinned\",\"size\":-300,\"minFirst\":-1,\"minSecond\":99999999999999,\"first\":{\"tabs\":[\"A\"]},\"second\":{\"tabs\":[\"B\"]}}",
            "{\"split\":\"Sideways\",\"size\":10,\"first\":{\"tabs\":[\"A\"]},\"second\":{\"tabs\":[\"B\"]}}",
            "{\"split\":\"LeftPinned\",\"first\":{\"tabs\":[\"A\"]}}",
            "{\"tabs\":[\"A\"],\"selected\":1e400}",
            "[[[[[[[[[[",
            "",
        };
        for (const std::string& text : cases)
            if (const auto tree = ParseDockTree(text, panels.idOf()))
                ExpectASaneTree(tree.get());
    }

    // ---- SVG icons through the real text system ----

    class SvgFuzzTest : public GUITest::SkribidiHeadlessTest {};

    TEST_F(SvgFuzzTest, MalformedIconsAreRejectedOrDrawn)
    {
        const std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\">"
            "<path d=\"M5 12h14 L3 4 Q 1 2 9 9 Z\"/><circle cx=\"12\" cy=\"12\" r=\"4\"/>"
            "<rect x=\"1\" y=\"1\" width=\"5\" height=\"5\" rx=\"1\"/><polyline points=\"1,2 3,4 5,6\"/></svg>";
        int index = 0;
        auto tryIcon = [&](const std::string& source) {
            const std::string name = "fuzz" + std::to_string(index++);
            const IconID icon = textSystem->RegisterIconFromSVG(name.c_str(), source.data(), source.size());
            if (icon == IconID::Invalid)
                return;
            (void)textSystem->MeasureIcon(icon);
            textSystem->RenderIcon(icon, IntRect{ 0, 0, 24, 24 }, IntRect{ 0, 0, 100, 100 }, VisualColorStyleIndex::Invalid);
        };
        for (size_t n = 0; n < svg.size(); n += 3)
            tryIcon(svg.substr(0, n));
        std::mt19937 rng(7);
        for (int i = 0; i < 400; i++)
        {
            std::string s = svg;
            for (int k = 0; k < 4; k++)
                s[rng() % s.size()] = char(rng() % 256);
            tryIcon(s);
        }
        std::string deepGroups = "<svg viewBox=\"0 0 24 24\">";
        for (int i = 0; i < 20000; i++) deepGroups += "<g>";
        tryIcon(deepGroups + "<path d=\"M1 1L9 9\"/>");
        std::string longPath = "<svg viewBox=\"0 0 24 24\"><path d=\"M0 0";
        for (int i = 0; i < 100000; i++) longPath += " L" + std::to_string(i % 24) + " " + std::to_string((i * 7) % 24);
        tryIcon(longPath + "\"/></svg>");
        tryIcon("<svg viewBox=\"0 0 0 0\"><path d=\"M1 1L9 9\"/></svg>");
        tryIcon("<svg viewBox=\"-1e38 -1e38 1e38 1e38\"><circle cx=\"nan\" cy=\"inf\" r=\"-5\"/></svg>");
        tryIcon("<svg width=\"999999999\" height=\"999999999\"><rect width=\"1e30\" height=\"1e30\"/></svg>");
        SUCCEED();
    }

    // ---- Undo history re-entrancy ----

    TEST(FuzzTest, UndoStepsThatTouchTheHistory)
    {
        UndoHistory history;
        int value = 0;
        history.Push({ .description = "pushes", .undo = [&] { history.Push({ .description = "inner", .undo = [&] { value = -1; } }); },
                       .redo = [&] { value = 1; } });
        history.EndMerge();
        history.Push({ .description = "undoes", .undo = [&] { history.Undo(); }, .redo = [&] { history.Redo(); } });
        history.EndMerge();
        history.Push({ .description = "clears", .undo = [&] { history.Clear(); }, .redo = [] {} });
        history.EndMerge();
        for (int i = 0; i < 10; i++) history.Undo();
        for (int i = 0; i < 10; i++) history.Redo();
        for (int i = 0; i < 10; i++) history.Undo();
        EXPECT_LE(history.UndoCount() + history.RedoCount(), 4u);
    }

    // ---- Input garbage ----

    TEST(FuzzTest, InputGarbage)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID fieldID = app.idManager.CreateStaticID();
        const ID buttonID = app.idManager.CreateStaticID();
        std::string text = "abc";
        auto draw = [&] {
            app.BeginRootWindow();
            app.Field(fieldID, text);
            app.Button(buttonID, "B", [] {});
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(fieldID));
        app.Frame(draw);

        const int big = std::numeric_limits<int>::max(), small = std::numeric_limits<int>::min();
        app.InjectMouseMove({ big, small });
        app.InjectMouseDown({ big, big });
        app.InjectMouseUp({ small, small });
        app.Frame(draw);
        app.inputManager->OnMouseScroll(ID::Invalid(), { 10, 10 }, { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }, KeyModifier::None);
        app.inputManager->OnMouseScroll(ID{ .value{ .integer = 123456 } }, { -5, -5 }, { 1e30f, -1e30f }, KeyModifier::None);
        app.inputManager->OnMouseButton(ID{ .value{ .integer = 999 } }, { 3, 3 }, MouseButtons(0xFFu), KeyModifier(0xFFu), MouseAction(77));
        app.Frame(draw);

        std::mt19937 rng(5);
        for (int i = 0; i < 3000; i++)
        {
            const uint32_t r = rng();
            const KeyCode key = (i % 3 == 0) ? KeyCode(r) : KeyCode(r % 512);
            const KeyAction action = KeyAction(rng() % 3);
            app.InjectKey(key, action, KeyModifier(rng() & 0xFFu));
            if (i % 100 == 0)
                app.Frame(draw);
        }
        for (uint32_t codepoint : { 0xD800u, 0xDFFFu, 0x110000u, 0xFFFFFFFFu, 0u, 0x7Fu, 0xFFFEu })
            app.InjectKey(KeyCode(codepoint), KeyAction::KeyChar);
        app.Frame(draw);
        app.Frame(draw);
        ExpectAFrameWorks(app);
        app.Cleanup();
    }

    // ---- Colours, gradients, commands ----

    TEST(FuzzTest, BadGradientsAndColors)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        Gradient tooMany = Gradient::Solid(color4b{ 255, 0, 0, 255 });
        tooMany.stopCount = 200;              // past the stop array
        Gradient none{};                      // no stops, no type
        Gradient oddType = Gradient::Solid(color4b{ 0, 255, 0, 255 });
        oddType.type = GradientType(77);
        oddType.colorSpace = ColorSpace(99);
        color4f nanColor{ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1e30f, 1e30f };
        color4b byteColor{ 1, 2, 3, 4 };
        Gradient* gradients[] = { &tooMany, &none, &oddType };
        std::vector<ID> ids;
        auto draw = [&] {
            app.BeginRootWindow();
            for (Gradient* g : gradients)
                app.GradientField(*g);
            app.ColorField(nanColor, true);
            app.ColorField(byteColor);
            app.EndRootWindow();
        };
        MisuseReports reports;
        for (int i = 0; i < 3; i++)
            app.Frame(draw);
        EXPECT_EQ(tooMany.stopCount, Gradient::kMaxStops) << "clamped where it entered the field";
        EXPECT_TRUE(reports.Contains("more stops than")) << reports.All();
        ExpectAFrameWorks(app);
        app.Cleanup();
    }

    // A gradient built in code reaches the sampler (styles, the gradient atlas) unchecked:
    // it reads only the stops there are.
    TEST(FuzzTest, AnOversizedGradientIsSampledWithinItsStops)
    {
        Gradient g = Gradient::Solid(color4b{ 10, 20, 30, 255 });
        g.stopCount = 255;
        std::vector<color4b> row(64);
        g.WriteTo(row);
        EXPECT_EQ(g.StopCount(), Gradient::kMaxStops);
        EXPECT_TRUE(g == g);
    }

    TEST(FuzzTest, CommandMisuse)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        app.inputManager->PushGlobalHandler(app.commandManager.MakeHandler());
        int fired = 0;
        const CommandID nothing = app.commandManager.RegisterCommand(KeyCode::KeyQ, KeyModifier::Control, nullptr, "Nothing");
        app.commandManager.RegisterCommand(KeyCode::KeyW, KeyModifier::Control, [&] { fired++; }, "First");
        app.commandManager.RegisterCommand(KeyCode::KeyW, KeyModifier::Control, [&] { fired++; }, "Second, same key");
        app.commandManager.BeginRebind(CommandID(99999));   // no such command: nothing to rebind
        auto draw = [&] {
            app.BeginRootWindow();
            app.CommandButton(CommandID(99999));
            app.CommandButton(nothing);
            app.EndRootWindow();
        };
        app.Frame(draw);
        // Rebind the do-nothing command onto the other two's shortcut.
        app.commandManager.BeginRebind(nothing);
        app.InjectKey(KeyCode::KeyW, KeyAction::KeyDown, KeyModifier::Control);
        app.InjectKey(KeyCode::KeyW, KeyAction::KeyUp, KeyModifier::Control);
        app.Frame(draw);
        for (KeyCode key : { KeyCode::KeyQ, KeyCode::KeyW, KeyCode::KeyE })
        {
            app.InjectKey(key, KeyAction::KeyDown, KeyModifier::Control);
            app.InjectKey(key, KeyAction::KeyUp, KeyModifier::Control);
            app.Frame(draw);
        }
        EXPECT_LE(fired, 1);
        ExpectAFrameWorks(app);
        app.Cleanup();
    }
}
