#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/input/gui_command_manager.h> // EditMode enum (why is this defined here?)
#include <gui_core/diagnostics/gui_profiler.h>  // Diagnostics::IsProfilingEnabled (profiler panel cache opt-out)

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

static constexpr interstice::Diagnostics::LogCategory kLogDemo = interstice::Diagnostics::MakeLogCategory("demo");

namespace interstice
{

    bool Application::Initialize(const InitializeOptions& options)
    {
        const std::string_view styleFiles[] = { "styles.css", "demo.css" };
        SetStyleFiles(styleFiles);
        if (!GUIApplication::Initialize(options))
            return false;

        // Application-specific resources and styles
        if (!InitializeDemoResources() ||
            !InitializeDemoStyles() ||
            !InitializeDemoGUI())
            return false;

        SetDrawCacheEnabled(true);
        return true;
    }

    void Application::OnStylesReloaded()
    {
        // A theme reload wipes styleManager, so every demo StyleRef is stale and
        // gets re-registered. Icons and textures survive the reload.
        boxStyleRefs.clear();
        animatedHueStyle = interstice::VisualColorStyleIndex::Invalid;
        InitializeDemoStyles();
        ApplyNodeCategories();
    }

    void Application::ApplyNodeCategories()
    {
        // Category names and colors; the colors are palette colors, so they follow the
        // theme. Order matches the nodes seeded in Initialize.
        struct Category { const char* name; const char* color; };
        static const Category categories[] = {
            { "Generator", "--node-generator" }, { "Operator", "--accent" },
            { "Output",    "--node-output" },    { "Input",    "--node-input" } };
        for (size_t i = 0; i < demo.nodeView.nodes.size() && i < std::size(categories); ++i)
        {
            demo.nodeView.nodes[i].category      = categories[i].name;
            demo.nodeView.nodes[i].categoryColor = theme.GetColor(categories[i].color);
        }
    }

    void Application::Cleanup()
    {
        idManager.ReleaseStaticID(demo.windowLayoutID1);
        idManager.ReleaseStaticID(demo.windowLayoutID2);
        idManager.ReleaseStaticID(demo.dragGhostWindowID);

        idManager.ReleaseStaticID(demo.smallMenuID);
        idManager.ReleaseStaticID(demo.bigMenuID);

        idManager.ReleaseStaticID(demo.reorderTabsID);
        idManager.ReleaseStaticID(demo.reorderListID);
        idManager.ReleaseStaticID(demo.reorderWrappedHorzID);
        idManager.ReleaseStaticID(demo.reorderWrappedVertID);
        idManager.ReleaseStaticID(demo.reorderStringListID);
        idManager.ReleaseStaticID(demo.reorderFloatListID);

        for (int i = 0; i < kToolbarSideCount; ++i)
        {
            for (auto& tb : toolbars.sides[i])
                if (tb.id != ID::Invalid())
                    idManager.ReleaseStaticID(tb.id);
        }
        ReleaseToolbarDockState(toolbars.dockState, *this);

        GUIApplication::Cleanup();
    }

    bool Application::InitializeDemoGUI()
    {
        demo.windowLayoutID1 = idManager.CreateStaticID();
        demo.windowLayoutID2 = idManager.CreateStaticID();
        demo.dragGhostWindowID = idManager.CreateStaticID();

        demo.smallMenuID = idManager.CreateStaticID();
        demo.bigMenuID = idManager.CreateStaticID();

        demo.reorderTabsID = idManager.CreateStaticID();
        demo.reorderListID = idManager.CreateStaticID();
        demo.reorderWrappedHorzID = idManager.CreateStaticID();
        demo.reorderWrappedVertID = idManager.CreateStaticID();
        demo.reorderStringListID = idManager.CreateStaticID();
        demo.reorderFloatListID = idManager.CreateStaticID();

        InitializeToolbarDockState(toolbars.dockState, *this);

        // Toolbars on several sides exercise wrap, in-side reorder and cross-side
        // drag. The dock wraps each renderBody in BeginToolbar / EndToolbar.
        toolbars.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "File",
                .renderBody = [this]()
                {
                    // Icon, callback and tooltip (with shortcut) come from the command.
                    CommandButton(demo.cmdNew);
                    CommandButton(demo.cmdOpen);
                    CommandButton(demo.cmdSave);
                    CommandButton(demo.cmdSaveAs);
                    CommandButton(demo.cmdUndo);
                    CommandButton(demo.cmdRedo);
                }
            });
        toolbars.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "View",
                .renderBody = [this]()
                {
					auto viewModeNoLit = (int)viewMode & ~(int)ViewMode::Lit;
					auto viewModeLit = (int)viewMode & (int)ViewMode::Lit;
                    Toggle(viewModeNoLit == (int)ViewMode::Wireframe, demo.viewWireframe, [this](bool value) { if (value) viewMode = ViewMode::Wireframe; });
					SetTooltip("Toggle wireframe view mode");
                    Toggle(viewModeNoLit == (int)ViewMode::Solid,     demo.viewSolid,     [this, viewModeLit](bool value) { if (value) viewMode = (ViewMode)((int)ViewMode::Solid | viewModeLit); });
					SetTooltip("Toggle solid view mode");
                    Toggle(viewModeNoLit == (int)ViewMode::Textured,  demo.viewTextured,  [this, viewModeLit](bool value) { if (value) viewMode = (ViewMode)((int)ViewMode::Textured | viewModeLit); });
					SetTooltip("Toggle textured view mode");

                    bool isLit = viewModeLit == (int)ViewMode::Lit;
                    Button(isLit ? demo.lightOnIcon : demo.lightOffIcon,  [this, isLit, viewModeLit, viewModeNoLit]()
                    { 
                        if (!isLit) 
                        {
                            if (viewModeNoLit == (int)ViewMode::Wireframe)
                                viewMode = ViewMode::TexturedLit;
                            else
								viewMode = (ViewMode)((int)viewModeNoLit | (int)ViewMode::Lit);
                        } else
                        {
                            viewMode = (ViewMode)viewModeNoLit;
                        }
                    });
					SetTooltip(isLit ? "Lighting on" : "Lighting off");

                    Button(enableAudio ? demo.audioOnIcon : demo.audioOffIcon, [this]()
                        {
							enableAudio = !enableAudio;
                        });
                    SetTooltip(enableAudio ? "Audio on" : "Audio off");
                }
            });
        toolbars.sides[(int)ToolbarSide::Left].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Tools",
                .renderBody = [this]()
                {
                    Toggle(nodeViewEditMode == EditMode::Select, demo.toolSelectIcon, [this](bool value) { if (value) nodeViewEditMode = EditMode::Select; });
					SetTooltip("Select mode");
                    Toggle(nodeViewEditMode == EditMode::Move,   demo.toolMoveIcon,   [this](bool value) { if (value) nodeViewEditMode = EditMode::Move;   });
					SetTooltip("Move mode");
                    Toggle(nodeViewEditMode == EditMode::Rotate, demo.toolRotateIcon, [this](bool value) { if (value) nodeViewEditMode = EditMode::Rotate; });
					SetTooltip("Rotate mode");
                    Toggle(nodeViewEditMode == EditMode::Scale,  demo.toolScaleIcon,  [this](bool value) { if (value) nodeViewEditMode = EditMode::Scale;  });
					SetTooltip("Scale mode");
                }
            });
        toolbars.sides[(int)ToolbarSide::Bottom].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Status",
                .renderBody = [this]()
                {
                    Button(showGrid ? demo.gridOnIcon : demo.gridOffIcon, [this]() { showGrid = !showGrid; });
					SetTooltip(showGrid ? "Grid on" : "Grid off");

                    Button("FPS 60",    []() { INTERSTICE_LOG_INFO(kLogDemo, "Toolbar: FPS"); });
                }
            });

        toolbarCallbacks.onReorder = [this](ToolbarSide side, int from, int to)
        {
            auto& src = toolbars.sides[(int)side];
            if (from < 0 || from >= (int)src.size()) return;
            to = std::clamp(to, 0, (int)src.size() - 1);
            if (from == to) return;
            
            auto item = src[from];
            src.erase(src.begin() + from);
            src.insert(src.begin() + to, std::move(item));
        };
        toolbarCallbacks.onCrossSideDrop = [this](ToolbarSide srcSide, int from, ToolbarSide targetSide)
        {
            auto& src = toolbars.sides[(int)srcSide];
            if (from < 0 || from >= (int)src.size()) return;

            auto item = src[from];
            src.erase(src.begin() + from);
            toolbars.sides[(int)targetSide].push_back(std::move(item));
        };
        toolbarCallbacks.onTearOff = [this](ToolbarSide srcSide, int from, interstice::int2 mousePos)
        {
            auto& src = toolbars.sides[(int)srcSide];
            if (from < 0 || from >= (int)src.size()) return;
            
            auto item = src[from];
            src.erase(src.begin() + from);
            toolbars.floatingItems.push_back(FloatingToolbarItem
            {
                .item          = item,
                .windowID      = idManager.CreateStaticID(),
                .spawnPosition = mousePos,
                .positioned    = false,
                .title         = item.debugName,
                .homeSide      = srcSide
            });
        };
                
        toolbarOpts.dockState = &toolbars.dockState;
        toolbarOpts.onDockBack = [this](ID windowID, ToolbarSide targetSide)
        {
            for (auto it = toolbars.floatingItems.begin();
                    it != toolbars.floatingItems.end(); ++it)
            {
                if (it->windowID != windowID)
                    continue;
                
                windowManager->RequestCloseAllowReopen(windowID);
                idManager.ReleaseStaticID(windowID);
                toolbars.sides[(int)targetSide].push_back(std::move(it->item));
                toolbars.floatingItems.erase(it);
                break;
            }
        };

        demo.textBox1 = "Editable text...";
        demo.textBox2 = "Multiline\nEditable\ntext2...";

        nodeViewEditMode = commandManager.RegisterEditMode("NodeView");
        commandManager.RegisterCommand(
            nodeViewEditMode,
            interstice::KeyCode::Home,
            interstice::KeyModifier::None,
            [this]()
            {
                INTERSTICE_LOG_INFO(kLogDemo, "Reset view");
                demo.nodeView.transform = interstice::float3x2::Identity();
            },
            "Reset view"
        );

        // Example edit mode registration
        [[maybe_unused]] interstice::EditMode sculptMode = commandManager.RegisterEditMode("Sculpt");

        // Example: Ctrl+D fires in every edit mode (Default fallback)
        commandManager.RegisterCommand(
            demo.duplicateIcon,
            interstice::KeyCode::KeyD,
            interstice::KeyModifier::Control,
            []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Duplicate"); },
            "Duplicate"
        );

        // --- Editor commands. Registered once with an optional icon + callback;
        // the main menu, toolbar and Shortcuts panel all reference these by handle.
        // Callbacks just print for now. A command may have no shortcut key (still
        // listed in the panel, where the user can bind one).
        auto& cmds = commandManager;
        demo.cmdNew    = cmds.RegisterCommand(interstice::EditMode::Default, demo.newIcon,      interstice::KeyCode::KeyN, interstice::KeyModifier::Control,                           []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: New"); },     "New");
        demo.cmdOpen   = cmds.RegisterCommand(interstice::EditMode::Default, demo.openFileIcon, interstice::KeyCode::KeyO, interstice::KeyModifier::Control,                           []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Open"); },    "Open...");
        demo.cmdSave   = cmds.RegisterCommand(interstice::EditMode::Default, demo.saveIcon,     interstice::KeyCode::KeyS, interstice::KeyModifier::Control,                           []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Save"); },    "Save");
        demo.cmdSaveAs = cmds.RegisterCommand(interstice::EditMode::Default, demo.saveAsIcon,   interstice::KeyCode::KeyS, interstice::KeyModifier::Control | interstice::KeyModifier::Shift, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Save As"); }, "Save As...");
        demo.cmdExit   = cmds.RegisterCommand(interstice::EditMode::Default, demo.closeIcon,    interstice::KeyCode::F4,   interstice::KeyModifier::Alt,                               [this]() { applicationQuit = true; },   "Exit");

        demo.cmdUndo   = cmds.RegisterCommand(interstice::EditMode::Default, demo.undoIcon,  interstice::KeyCode::KeyZ, interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Undo"); },  "Undo");
        demo.cmdRedo   = cmds.RegisterCommand(interstice::EditMode::Default, demo.redoIcon,  interstice::KeyCode::KeyY, interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Redo"); },  "Redo");
        demo.cmdCut    = cmds.RegisterCommand(interstice::EditMode::Default, demo.cutIcon,   interstice::KeyCode::KeyX, interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Cut"); },   "Cut");
        demo.cmdCopy   = cmds.RegisterCommand(interstice::EditMode::Default, demo.copyIcon,  interstice::KeyCode::KeyC, interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Copy"); },  "Copy");
        demo.cmdPaste  = cmds.RegisterCommand(interstice::EditMode::Default, demo.pasteIcon, interstice::KeyCode::KeyV, interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Paste"); }, "Paste");

        // Commands shown on a menu but with NO shortcut key (the user can assign
        // one in the Shortcuts panel).
        demo.cmdDocumentation = cmds.RegisterCommand(interstice::EditMode::Default, interstice::IconID::Invalid, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: Documentation"); },     "Documentation");
        demo.cmdAbout         = cmds.RegisterCommand(interstice::EditMode::Default, interstice::IconID::Invalid, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: About IntersticeUI"); }, "About IntersticeUI");
        demo.cmdSearchCommands = cmds.RegisterCommand(interstice::EditMode::Default, demo.findIcon, interstice::KeyCode::KeyK, interstice::KeyModifier::Control,
                                                       [this]() { FocusCommandSearch(demo.commandSearch); }, "Search Commands");
        demo.commandSearch.shortcut = cmds.GetCommands()[demo.cmdSearchCommands].displayString;

        // --- Further commands so the Shortcuts panel has realistic content.
        // These are panel/keyboard only (not referenced by the menu or toolbar).
        auto dummy = [this](interstice::EditMode mode, interstice::KeyCode key, interstice::KeyModifier mods,
            const char* desc)
            {
                commandManager.RegisterCommand(mode, key, mods,
                    [desc]() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", desc); }, desc);
            };

        // Default (global).
        commandManager.RegisterCommand(interstice::EditMode::Default, demo.selectAllIcon, interstice::KeyCode::KeyA,   interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Select All"); }, "Select All");
        commandManager.RegisterCommand(interstice::EditMode::Default, demo.deleteIcon, interstice::KeyCode::Delete, interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Delete Selection"); }, "Delete Selection");
        commandManager.RegisterCommand(interstice::EditMode::Default, demo.findIcon, interstice::KeyCode::KeyF,   interstice::KeyModifier::Control, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Find"); }, "Find");
        commandManager.RegisterCommand(interstice::EditMode::Default, demo.playIcon, interstice::KeyCode::F5,     interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Play"); }, "Play");
        commandManager.RegisterCommand(interstice::EditMode::Default, demo.stopIcon, interstice::KeyCode::F5,     interstice::KeyModifier::Shift, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Stop"); }, "Stop");
        // Held shortcut: onBegin fires on press, onEnd on release.
        commandManager.RegisterHeldCommand(interstice::EditMode::Default, interstice::KeyCode::Space, interstice::KeyModifier::None,
            []() { INTERSTICE_LOG_INFO(kLogDemo, "Hold begin: Pan View"); },
            []() { INTERSTICE_LOG_INFO(kLogDemo, "Hold end: Pan View"); },
            "Pan View (hold)");

        // NodeView — graph navigation
        commandManager.RegisterCommand(nodeViewEditMode, demo.frameSelectedIcon, interstice::KeyCode::KeyF,  interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Frame Selected"); }, "Frame Selected");
        commandManager.RegisterCommand(nodeViewEditMode,                         interstice::KeyCode::KeyA,  interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Frame All"); }, "Frame All");
        commandManager.RegisterCommand(nodeViewEditMode,                         interstice::KeyCode::Space, interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Search Nodes"); }, "Search Nodes");
        commandManager.RegisterCommand(nodeViewEditMode,                         interstice::KeyCode::KeyG,  interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Group Selection"); }, "Group Selection");

        // Sculpt — brush operations.
        commandManager.RegisterCommand(sculptMode,                  interstice::KeyCode::LeftBracket,  interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Decrease Brush Size"); }, "Decrease Brush Size");
        commandManager.RegisterCommand(sculptMode,                  interstice::KeyCode::RightBracket, interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Increase Brush Size"); }, "Increase Brush Size");
        commandManager.RegisterCommand(sculptMode, demo.brushIcon,  interstice::KeyCode::KeyB,         interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Brush Tool"); }, "Brush Tool");
        commandManager.RegisterCommand(sculptMode, demo.eraserIcon, interstice::KeyCode::KeyE,         interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Erase Tool"); }, "Erase Tool");
        commandManager.RegisterCommand(sculptMode,                  interstice::KeyCode::KeyM,         interstice::KeyModifier::None, []() { INTERSTICE_LOG_INFO(kLogDemo, "Command: {}", "Smooth Tool"); }, "Smooth Tool");

        // Seed the demo node-view. Each renderBody fills the row's middle slot;
        // sockets come from input/output presence.
        {
            demo.nodeView.editMode = nodeViewEditMode;

            // Socket colours double as the connection type: canConnect below only
            // allows wires between same-coloured sockets.
            const interstice::color4f kTeal  { 0.20f, 0.75f, 0.72f, 1.0f };
            const interstice::color4f kAmber { 0.95f, 0.62f, 0.20f, 1.0f };
            const interstice::color4f kPurple{ 0.62f, 0.45f, 0.92f, 1.0f };

            // NodeText aligns to its socket (output ⇒ right, input ⇒ left,
            // pass-through ⇒ centred); editable rows use Label + Field.
            NodeModel src;
            src.title = "Source";
            src.pos   = { styleManager.Px(-340), styleManager.Px(-140) };
            src.properties.push_back(NodeProperty
            {
                .output      = PropertyEndpoint{},   // present, unconnected
                .renderBody  = [this]() { NodeText(demo.nodeName); },   // output ⇒ right-aligned
                .socketColor = kTeal
            });
            src.properties.push_back(NodeProperty
            {
                .output      = PropertyEndpoint{},
                .renderBody  = [this]() { Label("Seed"); Field(demo.nodeSeed); },
                .socketColor = kAmber
            });
            src.properties.push_back(NodeProperty
            {
                .output      = PropertyEndpoint{},
                .renderBody  = [this]() { Label("Enabled"); Field(demo.nodeEnabled, demo.nodeEnabled ? "ON" : "OFF"); },
                .socketColor = kPurple
            });

            NodeModel mix;
            mix.title = "Mix";
            mix.pos   = { styleManager.Px(40), styleManager.Px(-60) };
            mix.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Strength"); },     // input ⇒ left-aligned
                .socketColor = kTeal
            });
            mix.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { Label("Gain"); Field(demo.nodeGain); },
                .socketColor = kAmber
            });
            mix.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .output      = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Bypass"); },       // both dots ⇒ centred
                .socketColor = kPurple
            });
            mix.properties.push_back(NodeProperty
            {
                .output      = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Result"); },       // output ⇒ right-aligned
                .socketColor = kTeal
            });

            NodeModel out;
            out.title = "Output";
            out.pos   = { styleManager.Px(180), styleManager.Px(100) };
            out.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Value"); },        // input ⇒ left-aligned
                .socketColor = kTeal     // accepts Mix.Result / Source.Name
            });
            out.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { Label("Tint"); Field(demo.nodeGain); },
                .socketColor = kPurple   // accepts Source.Enabled
            });
            out.properties.push_back(NodeProperty
            {
                .output      = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Level"); },        // output ⇒ right-aligned
                .socketColor = kAmber    // can feed Mix.Gain (a backward wire)
            });

            // Texture node: a square Image() preview filling its rows exactly. The
            // node sizes to it in whole grid cells; the output dot sits on the
            // image row's bottom grid line.
            NodeModel tex;
            tex.title = "Texture";
            tex.pos   = { styleManager.Px(-340), styleManager.Px(100) };
            {
                const int rowPitch = 2 * theme.nodeGridSize;
                const int margin   = styleManager.Px(8);     // matches the image style's margin
                const int border   = styleManager.Px(1);     // .node border each side; subtract so image+margin
                const int rowSpan  = 4;     // +border lands the node on a clean grid multiple
                const int imgSide  = std::max(theme.nodeGridSize, rowSpan * rowPitch - 2 * margin - 2 * border);
                tex.properties.push_back(NodeProperty
                {
                    .output      = PropertyEndpoint{},   // Color output — the dot sits at the image's bottom grid line
                    .renderBody  = [this, imgSide]() { Image(demoImageIndex, interstice::uint2{ 64, 64 }, interstice::LayoutOverride::FixedSize(imgSide, imgSide)); },
                    .socketColor = kTeal,                // can feed Mix.Strength / Output.Value
                    .rowSpan     = rowSpan,
                });
            }

            demo.nodeView.nodes.push_back(std::move(src));
            demo.nodeView.nodes.push_back(std::move(mix));
            demo.nodeView.nodes.push_back(std::move(out));
            demo.nodeView.nodes.push_back(std::move(tex));

            // Stable IDs keep per-node state keyed when view.ordering reshuffles.
            for (auto& node : demo.nodeView.nodes)
                node.stableID = idManager.CreateStaticID();
            ApplyNodeCategories();
            demo.nodeView.breadcrumb = { "Graphs", "Terrain material" };

            // Connection rule (delegate): only sockets of the same colour may
            // connect. Endpoints are bounds-checked before this is called.
            demo.nodeView.canConnect =
                [](const NodeView& v, PropertyEndpoint o, PropertyEndpoint i)
                {
                    return v.nodes[o.nodeIdx].properties[o.propIdx].socketColor
                         == v.nodes[i.nodeIdx].properties[i.propIdx].socketColor;
                };

            // One initial connection: Source.Name (0,0).output -> Mix.Strength (1,0).input
            demo.nodeView.nodes[0].properties[0].output = PropertyEndpoint{ 1, 0 };
            demo.nodeView.nodes[1].properties[0].input  = PropertyEndpoint{ 0, 0 };
        }

        return true;
    }

    bool Application::InitializeDemoStyles()
    {
        hierarchyVizCellStyleRef    = theme.GetStyle("hierarchy-viz-cell");
        hierarchyVizBgStyleRef      = theme.GetStyle("hierarchy-viz-bg");
        hierarchyVisibilityStyleRef = theme.GetStyle("hierarchy-visibility");
        logLevelIconStyleRef        = theme.GetStyle("log-level-icon");
        logLevelToggleLeftStyleRef  = theme.GetStyle("log-level-toggle-left");
        logLevelToggleMidStyleRef   = theme.GetStyle("log-level-toggle-mid");
        logLevelToggleRightStyleRef = theme.GetStyle("log-level-toggle-right");
        logCollapseToggleStyleRef   = theme.GetStyle("log-collapse-toggle");
        logMessageStyleRef          = theme.GetStyle("log-message");
        logCellStyleRef             = theme.GetStyle("log-cell");
        logCountBadgeStyleRef       = theme.GetStyle("log-count-badge");
        logDetailPaneStyleRef       = theme.GetStyle("log-detail-pane");
        logDetailTextStyleRef       = theme.GetStyle("log-detail-text");
        shortcutCategoryStyleRef    = theme.GetStyle("shortcut-category");
        shortcutTableStyleRef       = theme.GetStyle("shortcut-table");
        settingsTitleStyleRef       = theme.GetStyle("settings-title");
        {
            const char* kSideTag[4]   = { "left", "right", "top", "bottom" };
            const char* kVAlignTag[3] = { "top",  "mid",   "bot" };
            for (int s = 0; s < 4; ++s)
                for (int v = 0; v < 3; ++v)
                    iconVAlignDemoStyleRefs[s][v] = theme.GetStyle(std::string("icon-demo-") + kSideTag[s] + "-" + kVAlignTag[v]);
        }

        // -------------------------------------------------------------------------
        // Demo-specific styles below
        // -------------------------------------------------------------------------

        BoxEdges   noBorder = { 0, 0, 0, 0 };
        BoxCorners noRadius = { 0, 0, 0, 0 };

        constexpr int borderRadius = 10;
        constexpr int borderWidth = 2;

        BoxEdges   fullBorder = { borderWidth, borderWidth, borderWidth, borderWidth };
        BoxCorners fullRadius = { borderRadius, borderRadius, borderRadius, borderRadius };

        auto normal_border_color_style = ColorStyle::Create(color4b(121, 153, 169, 255));
        auto hover_border_color_style = ColorStyle::Create(color4b(255, 255, 255, 255));
        auto normal_background_color_style = ColorStyle::Create(color4b(101, 133, 149, 255));
        auto active_border_color_style = ColorStyle::Create(color4b(255, 255, 255, 255));

        // -------------------------------------------------------------------------
        // Sunset gradient (used for boxStyleRefs test loop)
        // -------------------------------------------------------------------------
        Gradient sunset = Gradient::Linear(90.0f) // vertical
            .WithColorSpace(ColorSpace::LinearRGB);
        {
            sunset.AddStop(color4f{ 0.98f, 0.78f, 0.35f, 1.0f }, 0.00f); // soft gold
            sunset.AddStop(color4f{ 0.93f, 0.42f, 0.33f, 1.0f }, 0.45f); // coral
            sunset.AddStop(color4f{ 0.15f, 0.25f, 0.45f, 1.0f }, 1.00f); // deep dusk blue
        }

        // Animated sunset gradient for a triangle fill
        {
            GradientHandle h;
            VisualColorStyle triangleVisualColorStyle = VisualColorStyle::Create(
                gradientManager, sunset, ColorType::LinearGradient, h);
            triangleVisualColorStyle.SetAnimSpeed(0.5, 0.0f);
            triangleFillStyle = styleManager.RegisterVisualColorStyle(triangleVisualColorStyle, h);
        }

        triangleFillColorStyle = ColorStyle::Create(color4b{ 255,0,0,255 }, color4b{ 0,255,0,255 }, color4b{ 0,0,255,255 }, ColorType::InterpolatedTriangle);
        quadFillColorStyle = ColorStyle::Create(color4b{ 255,0,0,255 }, color4b{ 0,255,0,255 }, color4b{ 0,0,255,255 }, color4b{ 255,0,255,255 }, ColorType::InterpolatedBox);

        triangleFillStyle2 = styleManager.RegisterColorStyle(triangleFillColorStyle);
        quadFillStyle = styleManager.RegisterColorStyle(quadFillColorStyle);

        Style boxStyle;
        {
            boxStyle.textLayoutStyle = interstice::TextLayoutStyle
            {
                .textDirection = interstice::TextDirection::Auto,
                .textWrapping = interstice::TextWrap::WordChar,
                .horizontalAlignment = interstice::TextAlign::Start,
                .baseline = interstice::TextBaseline::Middle
            };
            boxStyle.borderWidths = noBorder;
            boxStyle.borderRadius = noRadius;
            boxStyle.margin = BoxEdges(2);
            boxStyle.SetOverflow(interstice::Overflow::Hidden);
            boxStyle.padding = interstice::BoxEdges(10);
            for (int i = 0; i < Style::kStateCount; i++)
            {
                boxStyle.states[i].SetBorderColor({});
                boxStyle.states[i].backgroundColor = normal_border_color_style;
            }

            StyleState& normalStyle = boxStyle.states[Style::kNormal];
            normalStyle.SetBorderColor(normal_border_color_style);
            normalStyle.backgroundColor = normal_background_color_style;

            StyleState& hoverStyle = boxStyle.states[Style::kHover];
            hoverStyle.SetBorderColor(hover_border_color_style);
            hoverStyle.backgroundColor = normal_background_color_style;

            StyleState& activeStyle = boxStyle.states[Style::kActive];
            activeStyle.SetBorderColor(active_border_color_style);
            activeStyle.backgroundColor = normal_background_color_style;

            StyleState& focusStyle = boxStyle.states[Style::kFocus];
            focusStyle.SetBorderColor(active_border_color_style);
            focusStyle.backgroundColor = normal_background_color_style;

            StyleState& selectedStyle = boxStyle.states[Style::kSelected];
            selectedStyle.SetBorderColor(normal_border_color_style);
            selectedStyle.backgroundColor = normal_background_color_style;
        }

        constexpr int colorTypeCount = 10;
        static const ColorType colorTypes[colorTypeCount] =
        {
            ColorType::SolidColor,
            ColorType::StretchedTexture,
            ColorType::RepeatedTexture,
            ColorType::LinearGradient,
            ColorType::CircularAngularGradient,
            ColorType::EllipsoidAngularGradient,
            ColorType::RadialGradient,
            ColorType::InterpolatedTriangle,
            ColorType::InterpolatedBox,
            ColorType::SDFGradient
        };

        constexpr int cornerRadiusCount = 15;
        static const BoxCorners cornerRadiuses[cornerRadiusCount] =
        {
            BoxCorners{           0, borderRadius, borderRadius, borderRadius},
            BoxCorners{borderRadius,            0, borderRadius, borderRadius},
            BoxCorners{borderRadius, borderRadius,            0, borderRadius},
            BoxCorners{borderRadius, borderRadius, borderRadius,            0},
            BoxCorners{           0,            0, borderRadius, borderRadius},
            BoxCorners{borderRadius, borderRadius,            0,            0},
            BoxCorners{borderRadius,            0,            0, borderRadius},
            BoxCorners{           0, borderRadius, borderRadius,            0},
            BoxCorners{           0, borderRadius,            0, borderRadius},
            BoxCorners{borderRadius,            0, borderRadius,            0},
            BoxCorners{borderRadius,            0,            0,            0},
            BoxCorners{           0, borderRadius,            0,            0},
            BoxCorners{           0,            0, borderRadius,            0},
            BoxCorners{           0,            0,            0, borderRadius},
            BoxCorners{           0,            0,            0,            0}
        };

        constexpr int borderWidthCount = 15;
        static const BoxEdges borderWidths[borderWidthCount] =
        {
            BoxEdges{          0, borderWidth, borderWidth, borderWidth},
            BoxEdges{borderWidth,           0, borderWidth, borderWidth},
            BoxEdges{borderWidth, borderWidth,           0, borderWidth},
            BoxEdges{borderWidth, borderWidth, borderWidth,           0},
            BoxEdges{          0,           0, borderWidth, borderWidth},
            BoxEdges{borderWidth, borderWidth,           0,           0},
            BoxEdges{borderWidth,           0,           0, borderWidth},
            BoxEdges{          0, borderWidth, borderWidth,           0},
            BoxEdges{          0, borderWidth,           0, borderWidth},
            BoxEdges{borderWidth,           0, borderWidth,           0},
            BoxEdges{borderWidth,           0,           0,           0},
            BoxEdges{          0, borderWidth,           0,           0},
            BoxEdges{          0,           0, borderWidth,           0},
            BoxEdges{          0,           0,           0, borderWidth},
            BoxEdges{          0,           0,           0,           0}
        };

        constexpr int styleCount = ((colorTypeCount * 2) + cornerRadiusCount + borderWidthCount + 2);

        // background color boxStyles
        for (int i = 0; i < colorTypeCount; i++)
        {
            auto newStyle = boxStyle;
            newStyle.borderRadius = fullRadius;
            if (colorTypes[i] == ColorType::SolidColor)
                newStyle.states[Style::kNormal].backgroundColor = normal_background_color_style;
            else
                if (colorTypes[i] == ColorType::InterpolatedTriangle)
                    newStyle.states[Style::kNormal].backgroundColor = triangleFillColorStyle;
                else
                    if (colorTypes[i] == ColorType::InterpolatedBox)
                        newStyle.states[Style::kNormal].backgroundColor = quadFillColorStyle;
                    else
                        newStyle.states[Style::kNormal].backgroundColor = ColorStyle::Create(sunset, colorTypes[i]);

            newStyle.textureIndex = checkerboardIndex;

            boxStyleRefs.push_back(styleManager.Register(newStyle));
        }

        // border color boxStyles
        for (int i = 0; i < colorTypeCount; i++)
        {
            auto newStyle = boxStyle;
            newStyle.borderWidths = fullBorder;
            newStyle.borderRadius = fullRadius;
            if (colorTypes[i] == ColorType::SolidColor)
                newStyle.states[Style::kNormal].SetBorderColor(normal_border_color_style);
            else
                if (colorTypes[i] == ColorType::InterpolatedTriangle)
                    newStyle.states[Style::kNormal].backgroundColor = triangleFillColorStyle;
                else
                    if (colorTypes[i] == ColorType::InterpolatedBox)
                        newStyle.states[Style::kNormal].backgroundColor = quadFillColorStyle;
                    else
                        newStyle.states[Style::kNormal].SetBorderColor(ColorStyle::Create(sunset, colorTypes[i]));

            newStyle.textureIndex = checkerboardIndex;

            boxStyleRefs.push_back(styleManager.Register(newStyle));
        }

        // corner radiuses
        for (int i = 0; i < cornerRadiusCount; i++)
        {
            auto newStyle = boxStyle;
            newStyle.borderWidths = fullBorder;
            newStyle.borderRadius = cornerRadiuses[i];

            for (int j = 0; j < Style::kStateCount; j++)
            {
                newStyle.states[j].SetBorderColor(hover_border_color_style);
            }

            boxStyleRefs.push_back(styleManager.Register(newStyle));
        }

        // border widths
        for (int i = 0; i < borderWidthCount; i++)
        {
            auto newStyle = boxStyle;
            newStyle.borderWidths = borderWidths[i];
            newStyle.borderRadius = fullRadius;

            for (int j = 0; j < Style::kStateCount; j++)
            {
                newStyle.states[j].SetBorderColor(hover_border_color_style);
            }

            boxStyleRefs.push_back(styleManager.Register(newStyle));
        }

        demo.testBoxStyleCount = int(boxStyleRefs.size()) - 1;

        auto lastStyle = boxStyle;
        lastStyle.borderWidths = noBorder;
        lastStyle.borderRadius = noRadius;
        boxStyleRefs.push_back(styleManager.Register(lastStyle));

        {
            auto newStyle = boxStyle;
            newStyle.borderRadius = fullRadius;
            newStyle.borderWidths = fullBorder;

            auto border_color_style_top = ColorStyle::Create(color4b(255, 0, 0, 255));
            auto border_color_style_right = ColorStyle::Create(color4b(0, 255, 0, 255));
            auto border_color_style_bottom = ColorStyle::Create(color4b(0, 0, 255, 255));
            auto border_color_style_left = ColorStyle::Create(color4b(255, 0, 255, 255));
            auto background_color_style = ColorStyle::Create(color4b(90, 90, 90, 255));

            for (int i = 0; i < Style::kStateCount; i++)
            {
                newStyle.states[i].backgroundColor = background_color_style;
                newStyle.states[i].SetBorderColor(border_color_style_top, border_color_style_right, border_color_style_bottom, border_color_style_left);
            }
            borderTestStyleRef = styleManager.Register(newStyle);
        }

        (void)styleCount; // suppress unused-variable warning

        return true;
    }

    bool Application::InitializeDemoResources()
    {
        {
            //checkerboard image
            {
                interstice::color4b black = interstice::color4b(0, 0, 0, 255);
                interstice::color4b magenta = interstice::color4b(255, 0, 255, 255);
                std::array<interstice::color4b, 16 * 16 > pixels{};
                for (int y = 0, n = 0; y < 16; y++)
                {
                    for (int x = 0; x < 16; x++, n++)
                    {
                        pixels[n] = ((x % 2) ^ (y % 2)) ? magenta : black;
                    }
                }
                interstice::Image* checkerboardImage = graphicsContext->CreateImage(pixels.data(), uint2{ 16, 16 }, interstice::ImageFormat::R8G8B8A8, interstice::Sampler::LinearRepeat, true);
                if (checkerboardImage == nullptr)
                {
                    INTERSTICE_LOG_ERROR(kLogDemo, "Failed to create checkerboard image");
                    return false;
                }
                checkerboardIndex = graphicsContext->RegisterImage(checkerboardImage);

                graphicsContext->DestroyImageAtEnd(checkerboardImage, interstice::DeviceScope::Application);
            }

            // Sample gradient image to showcase the Image() element (image fields,
            // node thumbnails, …).
            {
                constexpr int W = 64, H = 64;
                std::array<interstice::color4b, W * H> pixels{};
                for (int y = 0, n = 0; y < H; y++)
                    for (int x = 0; x < W; x++, n++)
                        pixels[n] = interstice::color4b(static_cast<interstice::byte>(x * 4),
                                                 static_cast<interstice::byte>(y * 4),
                                                 static_cast<interstice::byte>(160 - (x + y)), 255);
                interstice::Image* gradient = graphicsContext->CreateImage(
                    pixels.data(), uint2{ W, H }, interstice::ImageFormat::R8G8B8A8,
                    interstice::Sampler::LinearClampToEdge, true);
                if (gradient == nullptr)
                {
                    INTERSTICE_LOG_ERROR(kLogDemo, "Failed to create demo image");
                    return false;
                }
                demoImageIndex = graphicsContext->RegisterImage(gradient);
                graphicsContext->DestroyImageAtEnd(gradient, interstice::DeviceScope::Application);
            }

            // Register icons from SVG
            {
                // Simple circle icon (using path with arc)
                const char* circleSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M 12 2 A 10 10 0 1 1 12 22 A 10 10 0 1 1 12 2 Z" fill="#4A90E2"/></svg>)";
                demo.circleIcon = textSystem->RegisterIconFromSVG("circle", circleSvg, strlen(circleSvg));

                // Heart icon - simplified triangular approximation
                const char* heartSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 21 L6 12 L6 8 L9 5 L12 7 L15 5 L18 8 L18 12 Z" fill="#E74C3C"/></svg>)";
                demo.heartIcon = textSystem->RegisterIconFromSVG("heart", heartSvg, strlen(heartSvg));

                // Star icon - simple 5-point star using only lines
                const char* starSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 2 L14.5 9 L22 9.5 L16.5 14 L18 22 L12 17.5 L6 22 L7.5 14 L2 9.5 L9.5 9 Z" fill="#F39C12"/></svg>)";
                demo.starIcon = textSystem->RegisterIconFromSVG("star", starSvg, strlen(starSvg));

                // Console log-level icons — coloured badge with a white glyph.
                const char* infoSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#4A90E2"/><circle cx="12" cy="7.5" r="1.6" fill="#ffffff"/><rect x="10.4" y="10.5" width="3.2" height="7.5" rx="1.6" fill="#ffffff"/></svg>)";
                demo.infoIcon = textSystem->RegisterIconFromSVG("info", infoSvg, strlen(infoSvg));

                const char* warningSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 2.5 L22.5 21 L1.5 21 Z" fill="#F5A623"/><rect x="10.7" y="8.5" width="2.6" height="6.5" rx="1.3" fill="#ffffff"/><circle cx="12" cy="18" r="1.4" fill="#ffffff"/></svg>)";
                demo.warningIcon = textSystem->RegisterIconFromSVG("warning", warningSvg, strlen(warningSvg));

                const char* errorSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#E5484D"/><path d="M9.2 7.8 L12 10.6 L14.8 7.8 L16.2 9.2 L13.4 12 L16.2 14.8 L14.8 16.2 L12 13.4 L9.2 16.2 L7.8 14.8 L10.6 12 L7.8 9.2 Z" fill="#ffffff"/></svg>)";
                demo.errorIcon = textSystem->RegisterIconFromSVG("error", errorSvg, strlen(errorSvg));

                // Monochrome variants (grey badge, same glyph) for the log
                // panel's level-filter toggles.
                const char* infoBwSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#8a8a8a"/><circle cx="12" cy="7.5" r="1.6" fill="#ffffff"/><rect x="10.4" y="10.5" width="3.2" height="7.5" rx="1.6" fill="#ffffff"/></svg>)";
                demo.infoBwIcon = textSystem->RegisterIconFromSVG("info-bw", infoBwSvg, strlen(infoBwSvg));

                const char* warningBwSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 2.5 L22.5 21 L1.5 21 Z" fill="#8a8a8a"/><rect x="10.7" y="8.5" width="2.6" height="6.5" rx="1.3" fill="#ffffff"/><circle cx="12" cy="18" r="1.4" fill="#ffffff"/></svg>)";
                demo.warningBwIcon = textSystem->RegisterIconFromSVG("warning-bw", warningBwSvg, strlen(warningBwSvg));

                const char* errorBwSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#8a8a8a"/><path d="M9.2 7.8 L12 10.6 L14.8 7.8 L16.2 9.2 L13.4 12 L16.2 14.8 L14.8 16.2 L12 13.4 L9.2 16.2 L7.8 14.8 L10.6 12 L7.8 9.2 Z" fill="#ffffff"/></svg>)";
                demo.errorBwIcon = textSystem->RegisterIconFromSVG("error-bw", errorBwSvg, strlen(errorBwSvg));

                // Folder icons for GridView demo — closed and open variants
                demo.folderClosedIcon    = textSystem->RegisterIconFromSVGFile("icons/folder_closed.svg");
                demo.folderOpenIcon      = textSystem->RegisterIconFromSVGFile("icons/folder_open.svg"); 
                                         
                demo.audioOnIcon         = textSystem->RegisterIconFromSVGFile("icons/lucide/volume-2.svg");
                demo.audioOffIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/volume-x.svg");
                                         
                demo.gridOnIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/grid-3x3.svg");
                demo.gridOffIcon         = textSystem->RegisterIconFromSVGFile("icons/lucide/grid-2x2-x.svg");
                                         
                demo.lightOnIcon         = textSystem->RegisterIconFromSVGFile("icons/lucide/lightbulb.svg");
                demo.lightOffIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/lightbulb-off.svg");
                                         
                demo.toolSelectIcon      = textSystem->RegisterIconFromSVGFile("icons/lucide/square-dashed.svg");
                demo.toolMoveIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/move.svg");
                demo.toolScaleIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/maximize-2.svg");
                demo.toolRotateIcon      = textSystem->RegisterIconFromSVGFile("icons/lucide/rotate-cw.svg");
                                         
                demo.viewWireframe       = textSystem->RegisterIconFromSVGFile("icons/lucide/globe.svg");
                demo.viewSolid           = textSystem->RegisterIconFromSVGFile("icons/lucide/circle.svg");
                demo.viewTextured        = textSystem->RegisterIconFromSVGFile("icons/lucide/view-textured.svg");

                demo.panelFilesIcon      = textSystem->RegisterIconFromSVGFile("icons/lucide/folder.svg");
				demo.panelGameIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/gamepad-2.svg");
                demo.panelHierarchyIcon  = textSystem->RegisterIconFromSVGFile("icons/lucide/list-tree.svg");
				demo.panelInspectorIcon  = textSystem->RegisterIconFromSVGFile("icons/lucide/info.svg");
				demo.panelLogIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/terminal.svg");
				demo.panelNodeViewIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/workflow.svg");
				demo.panelProfilerIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/activity.svg");
				demo.panelSettingsIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/sliders-horizontal.svg");
				demo.panelViewportIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/monitor.svg");
                demo.panelMiscIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/layout-grid.svg");
                
				demo.objectIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/box.svg");
				demo.object2Icon         = textSystem->RegisterIconFromSVGFile("icons/lucide/shapes.svg");
                                         
				demo.fileImageIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/file-image.svg");
				demo.fileAudioIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/file-audio.svg");
                demo.fileVideoIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/file-video.svg");
				demo.fileModelIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/file-box.svg");
                demo.fileTextIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/file-text.svg");
				demo.fileArchiveIcon     = textSystem->RegisterIconFromSVGFile("icons/lucide/file-archive.svg");
                                         
                                         
                demo.openFileIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/folder-open.svg");
                demo.saveIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/save.svg");
                demo.saveAsIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/save-all.svg");
                demo.closeIcon           = textSystem->RegisterIconFromSVGFile("icons/lucide/x.svg");
                demo.addIcon             = textSystem->RegisterIconFromSVGFile("icons/lucide/plus.svg");
                demo.removeIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/minus.svg");
                demo.undoIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/undo-2.svg");
                demo.redoIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/redo-2.svg");
                demo.cutIcon             = textSystem->RegisterIconFromSVGFile("icons/lucide/scissors.svg");
                demo.copyIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/copy.svg");
                demo.pasteIcon           = textSystem->RegisterIconFromSVGFile("icons/lucide/clipboard-paste.svg");
                demo.blockIcon           = textSystem->RegisterIconFromSVGFile("icons/lucide/ban.svg");
                demo.syncIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/refresh-cw.svg");
                demo.downloadIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/download.svg");
                demo.uploadIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/upload.svg");
                demo.publishIcon         = textSystem->RegisterIconFromSVGFile("icons/lucide/send.svg");
                demo.openInWindowIcon    = textSystem->RegisterIconFromSVGFile("icons/lucide/external-link.svg");
                                         
                demo.duplicateIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/copy-plus.svg");
                demo.newIcon             = textSystem->RegisterIconFromSVGFile("icons/lucide/file-plus.svg");
                demo.selectAllIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/text-select.svg");
                demo.deleteIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/trash-2.svg");
                demo.zoomInIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/zoom-in.svg");
                demo.zoomOutIcon         = textSystem->RegisterIconFromSVGFile("icons/lucide/zoom-out.svg");
                demo.findIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/search.svg");
                demo.playIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/play.svg");
                demo.stopIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/square.svg");
                demo.recenterIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/locate-fixed.svg");
                demo.frameSelectedIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/scan.svg");
                demo.brushIcon           = textSystem->RegisterIconFromSVGFile("icons/lucide/brush.svg");
                demo.eraserIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/eraser.svg");
                demo.draggableIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/grip-vertical.svg");
                
				demo.darkModeIcon        = textSystem->RegisterIconFromSVGFile("icons/lucide/moon.svg");
                demo.lightModeIcon       = textSystem->RegisterIconFromSVGFile("icons/lucide/sun.svg");
                demo.lightModeAutoIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/sun-moon.svg");
                demo.fullscreenIcon      = textSystem->RegisterIconFromSVGFile("icons/lucide/maximize.svg");
                demo.fullscreenExitIcon  = textSystem->RegisterIconFromSVGFile("icons/lucide/minimize.svg");
                demo.viewSelectedIcon    = textSystem->RegisterIconFromSVGFile("icons/lucide/scan-eye.svg");
                demo.dropperIcon         = textSystem->RegisterIconFromSVGFile("icons/lucide/pipette.svg");
                demo.editIcon            = textSystem->RegisterIconFromSVGFile("icons/lucide/pencil.svg");
                demo.tagIcon             = textSystem->RegisterIconFromSVGFile("icons/lucide/tag.svg");
                demo.targetIcon          = textSystem->RegisterIconFromSVGFile("icons/lucide/target.svg");
                demo.visibilityLockIcon  = textSystem->RegisterIconFromSVGFile("icons/lucide/lock.svg");
                demo.visibilityMixedIcon = textSystem->RegisterIconFromSVGFile("icons/lucide/eye-dashed.svg");
                demo.visibilityOffIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/eye-off.svg");
                demo.visibilityOnIcon    = textSystem->RegisterIconFromSVGFile("icons/lucide/eye.svg");

                demo.dropdownArrowIcon   = textSystem->RegisterIconFromSVGFile("icons/lucide/chevron-down.svg");
                demo.iconCollapseAllIcon = textSystem->RegisterIconFromSVGFile("icons/lucide/chevrons-down-up.svg");

				demo.keyCommandIcon      = textSystem->RegisterIconFromSVGFile("icons/lucide/command.svg");

                // Must run after icon registration; the pages capture IconIDs.
                InitSettingsPages();
            }
        }

        return true;
    }

    // Title bar with the main menu and the command search. Most items only log;
    // Theme and Zoom reload the theme and Exit ends the run loop.
    void Application::RenderMainMenu()
    {
        BeginTitleBar(idManager.AllocateDynamicID("mainMenuBar"));

        // ---- File ----
        if (BeginMenu(idManager.AllocateDynamicID("menuFile"), "File",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            // Icon, label, shortcut and callback come from the command.
            MenuItem(demo.cmdNew);
            MenuItem(demo.cmdOpen);
            MenuItem(demo.cmdSave);
            MenuItem(demo.cmdSaveAs);
            MenuSeparator();
            MenuItem(demo.cmdExit);
            EndMenu();
        }

        // ---- Edit ----
        if (BeginMenu(idManager.AllocateDynamicID("menuEdit"), "Edit",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            MenuItem(demo.cmdUndo);
            MenuItem(demo.cmdRedo);
            MenuSeparator();
            MenuItem(demo.cmdCut);
            MenuItem(demo.cmdCopy);
            MenuItem(demo.cmdPaste);
            EndMenu();
        }

        // ---- View ----
        if (BeginMenu(idManager.AllocateDynamicID("menuView"), "View",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            // Theme submenu — mirrors the Appearance settings page; both mutate
            // demo.settingsThemeIndex so the two stay in sync.
            static const char* const themeNames[] = { "Dark", "Light" };
            static const char* const themePaths[] = { "themes/dark.css", "themes/light.css" };
            if (BeginMenu(idManager.AllocateDynamicID("menuViewTheme"), "Theme"))
            {
                for (int i = 0; i < 2; i++)
                {
                    int idx = i;
                    bool isCurrent = (idx == demo.settingsThemeIndex);
                    if (isCurrent) BeginSelected();
                    MenuItem(themeNames[i], [this, idx]() {
                        demo.settingsThemeIndex = idx;
                        RequestReloadTheme(themePaths[idx], demo.settingsFontScalePct / 100.0f);
                    });
                    if (isCurrent) EndSelected();
                }
                EndMenu();
            }

            MenuSeparator();

            // Zoom = font scale, clamped to [100, 300]% in steps of 10 to match the
            // Appearance page. RequestReloadTheme no-ops when the scale is unchanged.
            MenuItem(demo.zoomInIcon, "Zoom In", [this]() {
                demo.settingsFontScalePct = std::clamp(demo.settingsFontScalePct + 10.0f, 100.0f, 300.0f);
                RequestReloadTheme(GetThemePath(), demo.settingsFontScalePct / 100.0f);
            });
            MenuItem(demo.zoomOutIcon, "Zoom Out", [this]() {
                demo.settingsFontScalePct = std::clamp(demo.settingsFontScalePct - 10.0f, 100.0f, 300.0f);
                RequestReloadTheme(GetThemePath(), demo.settingsFontScalePct / 100.0f);
            });
            MenuItem(demo.recenterIcon, "Reset Zoom", [this]() {
                demo.settingsFontScalePct = 100.0f;
                RequestReloadTheme(GetThemePath(), 1.0f);
            });
            EndMenu();
        }

        // ---- Help ---- (commands with no shortcut key)
        if (BeginMenu(idManager.AllocateDynamicID("menuHelp"), "Help",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            MenuItem(demo.cmdDocumentation);
            MenuItem(demo.cmdAbout);
            EndMenu();
        }

        // The command search, centred in the space after the menus.
        BeginHorizontal(LayoutOverride::Auto().FlexGrow(1));
        EndHorizontal();
        CommandSearch(demo.commandSearch);
        BeginHorizontal(LayoutOverride::Auto().FlexGrow(1));
        EndHorizontal();
        StatusText("Test");

        EndTitleBar();
    }

    // Status bar: state and graphics API on the left, frame time and theme on the right.
    void Application::RenderStatusBar()
    {
        std::array<FrameTimingSample, kFrameHistoryCount> history{};
        GetFrameTimingHistory(history);
        constexpr int kAveraged = 60;
        float totalMs = 0.0f;
        for (int i = kFrameHistoryCount - kAveraged; i < kFrameHistoryCount; ++i)
            totalMs += history[size_t(i)].totalMs;
        const float frameMs = totalMs / float(kAveraged);

        BeginStatusBar(idManager.AllocateDynamicID("statusBar"));
        StatusText("Ready");
        StatusText("Vulkan", LayoutOverride::Auto().FlexGrow(1));
        StatusText(std::format("{:.2f} ms \u00B7 {:.0f} fps", frameMs, frameMs > 0.0f ? 1000.0f / frameMs : 0.0f));
        StatusText(GetThemePath());
        EndStatusBar();
    }

    void Application::OnGUI()
    {
        auto&& sizeConstraints = LayoutOverride::FixedSize(styleManager.Px(1700), styleManager.Px(900));
        
        {
            interstice::int2 size{ styleManager.Px(100), styleManager.Px(50) };
            static bool mainWindowClosed = false;
            if (mainWindowClosed)
            {
                // Closed by the user; reset mainWindowClosed to reopen.
            } else if (BeginWindow(demo.windowLayoutID2, "Test", std::move(sizeConstraints.Centered()), theme.mainWindowStyleRef,
                                   ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered | ElementFlags::WindowCustomTitleBar))
            {
                RenderMainMenu();

                // ---- DockingContainer demo ----
                {
                    if (!demo.dockInitialized)
                    {
                        auto hierarchyPanelID   = CreatePanel(demo.dockingLayout.panels, "Hierarchy",   demo.panelHierarchyIcon, [this]() { RenderHierarchyPanel(); }, {}, [this]() { EnableHierarchyPanel(); }, [this]() { DisableHierarchyPanel(); });
                        auto inspectorPanelID   = CreatePanel(demo.dockingLayout.panels, "Inspector",   demo.panelInspectorIcon, [this]() { RenderInspectorPanel(); }, {}, [this]() { EnableInspectorPanel(); }, [this]() { DisableInspectorPanel(); });
                        auto consolePanelID     = CreatePanel(demo.dockingLayout.panels, "Log",         demo.panelLogIcon,       [this]() { RenderLogPanel(); },       {}, [this]() { EnableLogPanel(); },       [this]() { DisableLogPanel(); });
                        auto filesPanelID       = CreatePanel(demo.dockingLayout.panels, "Files",       demo.panelFilesIcon,     [this]() { RenderFilesPanel(); }, {}, [this]() { EnableFilesPanel(); }, [this]() { DisableFilesPanel(); });
                        auto nodeViewPanelID    = CreatePanel(demo.dockingLayout.panels, "NodeView",    demo.panelNodeViewIcon,  [this]() { RenderNodeViewPanel(); });
                        auto viewportPanelID    = CreatePanel(demo.dockingLayout.panels, "Viewport",    demo.panelViewportIcon,  [this]() { RenderViewportPanel(); });
                        auto profilerPanelID    = CreatePanel(demo.dockingLayout.panels, "Profiler",    demo.panelProfilerIcon,  [this]() { RenderProfilerTreePanel(); }, {}, [this]() { EnableProfilerTreePanel(); });
                        // Panels are cached by default; the profiler opts out while profiling is on.
                        demo.dockingLayout.panels[profilerPanelID].cacheSignature =
                            []() -> std::optional<uint64_t> { return Diagnostics::IsProfilingEnabled() ? std::nullopt : std::optional<uint64_t>(0); };

                        auto treeTablePanelID   = CreatePanel(demo.dockingLayout.panels, "TreeTable",   demo.panelMiscIcon, [this]() { RenderTreeTablePanel(); });
                        auto tablePanelID       = CreatePanel(demo.dockingLayout.panels, "Table",       demo.panelMiscIcon, [this]() { RenderTablePanel(); });
                        auto splitScrollPanelID = CreatePanel(demo.dockingLayout.panels, "FrozenTable", demo.panelMiscIcon, [this]() { RenderSplitScrollPanel(); });
                        auto reorderablePanelID = CreatePanel(demo.dockingLayout.panels, "Reorderable", demo.panelMiscIcon, [this]() { RenderReorderablePanel(); });
                        auto boxTestPanelID     = CreatePanel(demo.dockingLayout.panels, "BoxTest",     demo.panelMiscIcon, [this]() { RenderBoxTestPanel(); });
                        auto miscPanelID        = CreatePanel(demo.dockingLayout.panels, "Misc",        demo.panelMiscIcon, [this]() { RenderMiscPanel(); });
                        // Misc animates during construction (rotating triangle, cycling hue), so it
                        // must rebuild every frame — opt out of caching.
                        demo.dockingLayout.panels[miscPanelID].cacheSignature =
                            []() -> std::optional<uint64_t> { return std::nullopt; };

                        // Pages are registered once in InitSettingsPages.
                        auto settingsPanelID    = CreatePanel(demo.dockingLayout.panels, "Settings", demo.panelSettingsIcon, [this]() { RenderSettingsPanel(demo.settings); });
                        
                        // bottom
                        auto&& filesLeaf         = MakeLeaf({ filesPanelID });
                        auto&& consolePanelLeaf  = MakeLeaf({ consolePanelID });
						// left / center / right
                        auto&& leftPanelGroup    = MakeLeaf({ hierarchyPanelID });
                        auto&& centerPanelGroup  = MakeLeaf({ nodeViewPanelID, viewportPanelID, settingsPanelID });
                        auto&& rightPanelGroup   = MakeLeaf({ profilerPanelID, inspectorPanelID,
                                    reorderablePanelID, miscPanelID, boxTestPanelID, treeTablePanelID,
                                    tablePanelID, splitScrollPanelID });

                        // Build tree:  Vsplit( Hsplit( left, Hsplit(center, right) ), Hsplit(files, console) )
                        // Resize semantics:
                        //  - outer Vsplit:  bottom row pinned -> top group absorbs vertical resize
                        //  - top Hsplit:    left pinned       -> (center+right) absorbs horizontal resize
                        //  - inner Hsplit:  right pinned      -> center absorbs horizontal resize
                        //  - bottom Hsplit: left (files) pinned
                        demo.dockingLayout.root =
                            MakeSplit(DockSplitMode::BottomPinned, 280,
                                MakeSplit(DockSplitMode::LeftPinned, 320,
                                    // left panel group
                                    std::move(leftPanelGroup),
                                    MakeSplit(DockSplitMode::RightPinned, 540,
                                        // center panel group
                                        std::move(centerPanelGroup),
                                        // right panel group
                                        std::move(rightPanelGroup)
                                    )
                                ),
                                // bottom panel group
                                MakeSplit(DockSplitMode::LeftPinned, 500,
                                    std::move(filesLeaf),
                                    std::move(consolePanelLeaf)
                                ),
                                120, // minFirst
                                160  // minSecond
                            );
                        demo.dockInitialized = true;
                    }

                    demo.dockContainerID = idManager.AllocateDynamicID("dockDemo");
                    BeginDockingContainer(demo.dockContainerID, &demo.dockingLayout,
                        LayoutOverride::Auto());
                    EndDockingContainer();
                }

                RenderStatusBar();

                // Must be emitted inside the main window's scope so main becomes
                // their OS owner; otherwise SDL's hidden helper owns them and they
                // die when it is torn down.
                EmitFloatingDockWindows();
                EmitFloatingToolbarWindows(toolbars.floatingItems, &toolbarOpts);

                EndWindow();
            } else
            {
                // BeginWindow returned false — window is closing
                mainWindowClosed = true;
            }
        }
    }
}
