#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/input/gui_command_manager.h> // CommandMode enum (why is this defined here?)
#include <gui_core/diagnostics/gui_profiler.h>  // Diagnostics::IsProfilingEnabled (profiler panel cache opt-out)
#include <gui_controls/gui_node_view_helpers.h> // ApplyNodeViewEdit (node-view undo)

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

static constexpr tachyon::Diagnostics::LogCategory kLogShowcase = tachyon::Diagnostics::MakeLogCategory("showcase");

namespace tachyon
{

    Result<void> Application::Initialize(const InitializeOptions& options)
    {
        const std::string_view styleFiles[] = { "styles.css", "showcase.css" };
        SetStyleFiles(styleFiles);
        // The showcase shows text in many scripts, so it loads the optional Noto fallbacks.
        InitializeOptions withFallbacks = options;
        withFallbacks.fallbackFonts = "fonts/Noto-otf";
        if (Result<void> result = GUIApplication::Initialize(withFallbacks); !result)
            return result;

        // Application-specific resources and styles
        if (!InitializeShowcaseResources() ||
            !InitializeShowcaseStyles() ||
            !InitializeShowcaseGUI())
            return Error{ ErrorCode::Failed, "initialization failed" };

        SetDrawCacheEnabled(true);

        // The saved UI state (the showcase stores it next to its themes): the main window, the
        // toolbars and the dock, which is applied again once the dock exists.
        PersistWindow("main", showcase.windowLayoutID2);
        PersistToolbars("toolbars", toolbars.sides);
        PersistDocking("dock", showcase.dockingLayout);
        if (std::ifstream file(ResourceFile(kUIStateFile), std::ios::binary); file)
        {
            showcase.savedUIState.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            if (Result<void> restored = RestoreUIState(showcase.savedUIState); !restored)
                TACHYON_LOG_WARNING(kLogShowcase, "{} not restored: {}", kUIStateFile, restored.GetError().message);
        }
        return {};
    }

    void Application::OnStylesReloaded()
    {
        // A theme reload wipes styleManager, so every showcase StyleRef is stale and
        // gets re-registered. Icons and textures survive the reload.
        boxStyleRefs.clear();
        animatedHueStyle = tachyon::VisualColorStyleIndex::Invalid;
        InitializeShowcaseStyles();
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
        for (size_t i = 0; i < showcase.nodeView.nodes.size() && i < std::size(categories); ++i)
        {
            showcase.nodeView.nodes[i].category      = categories[i].name;
            showcase.nodeView.nodes[i].categoryColor = theme.GetColor(categories[i].color);
        }
    }

    void Application::Cleanup()
    {
        idManager.ReleaseStaticID(showcase.windowLayoutID1);
        idManager.ReleaseStaticID(showcase.windowLayoutID2);
        idManager.ReleaseStaticID(showcase.dragGhostWindowID);

        idManager.ReleaseStaticID(showcase.smallMenuID);
        idManager.ReleaseStaticID(showcase.cameraMenuID);
        idManager.ReleaseStaticID(showcase.bigMenuID);

        idManager.ReleaseStaticID(showcase.reorderTabsID);
        idManager.ReleaseStaticID(showcase.reorderListID);
        idManager.ReleaseStaticID(showcase.reorderWrappedHorzID);
        idManager.ReleaseStaticID(showcase.reorderWrappedVertID);
        idManager.ReleaseStaticID(showcase.reorderStringListID);
        idManager.ReleaseStaticID(showcase.reorderFloatListID);

        for (int i = 0; i < kToolbarSideCount; ++i)
        {
            for (auto& tb : toolbars.sides[i])
                if (tb.id != ID::Invalid())
                    idManager.ReleaseStaticID(tb.id);
        }
        ReleaseToolbarDockState(toolbars.dockState, *this);

        GUIApplication::Cleanup();
    }

    bool Application::InitializeShowcaseGUI()
    {
        showcase.windowLayoutID1 = idManager.CreateStaticID();
        showcase.windowLayoutID2 = idManager.CreateStaticID();
        showcase.exitDialogID    = idManager.CreateStaticID();
        showcase.dragGhostWindowID = idManager.CreateStaticID();

        showcase.smallMenuID = idManager.CreateStaticID();
        showcase.bigMenuID = idManager.CreateStaticID();

        showcase.reorderTabsID = idManager.CreateStaticID();
        showcase.reorderListID = idManager.CreateStaticID();
        showcase.reorderWrappedHorzID = idManager.CreateStaticID();
        showcase.reorderWrappedVertID = idManager.CreateStaticID();
        showcase.reorderStringListID = idManager.CreateStaticID();
        showcase.cameraMenuID = idManager.CreateStaticID();
        showcase.reorderFloatListID = idManager.CreateStaticID();

        InitializeToolbarDockState(toolbars.dockState, *this);

        // Toolbars on several sides exercise wrap, in-side reorder and cross-side
        // drag. The dock wraps each renderBody in BeginToolbar / EndToolbar.
        toolbars.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "File",
                .renderBody = [this]()
                {
                    // Icon, callback and tooltip (with shortcut) come from the command.
                    CommandButton(showcase.cmdNew);
                    CommandButton(showcase.cmdOpen);
                    CommandButton(showcase.cmdSave);
                    CommandButton(showcase.cmdSaveAs);
                    CommandButton(showcase.cmdUndo);
                    CommandButton(showcase.cmdRedo);
                }
            });
        toolbars.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "View",
                .renderBody = [this]()
                {
					auto viewModeNoLit = (int)viewMode & ~(int)ViewMode::Lit;
					auto viewModeLit = (int)viewMode & (int)ViewMode::Lit;
                    Toggle(viewModeNoLit == (int)ViewMode::Wireframe, showcase.viewWireframe, [this](bool value) { if (value) viewMode = ViewMode::Wireframe; });
					SetTooltip("Toggle wireframe view mode");
                    Toggle(viewModeNoLit == (int)ViewMode::Solid,     showcase.viewSolid,     [this, viewModeLit](bool value) { if (value) viewMode = (ViewMode)((int)ViewMode::Solid | viewModeLit); });
					SetTooltip("Toggle solid view mode");
                    Toggle(viewModeNoLit == (int)ViewMode::Textured,  showcase.viewTextured,  [this, viewModeLit](bool value) { if (value) viewMode = (ViewMode)((int)ViewMode::Textured | viewModeLit); });
					SetTooltip("Toggle textured view mode");

                    bool isLit = viewModeLit == (int)ViewMode::Lit;
                    Button(isLit ? showcase.lightOnIcon : showcase.lightOffIcon,  [this, isLit, viewModeNoLit]()
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

                    Button(enableAudio ? showcase.audioOnIcon : showcase.audioOffIcon, [this]()
                        {
							enableAudio = !enableAudio;
                        });
                    SetTooltip(enableAudio ? "Audio on" : "Audio off");
                }
            });
        toolbars.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Camera",
                .renderBody = [this]()
                {
                    static const char* const kCameraViews[] = { "Perspective", "Orthographic", "Top", "Front", "Side" };
                    if (BeginMenu(showcase.cameraMenuID, kCameraViews[showcase.cameraView]))
                    {
                        MenuFilter();
                        for (int i = 0; i < int(std::size(kCameraViews)); ++i)
                            MenuItem(kCameraViews[i], [this, i]() { showcase.cameraView = i; });
                    }
                    EndMenu();
                    SetTooltip("Camera view");
                }
            });
        toolbars.sides[(int)ToolbarSide::Left].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Tools",
                .renderBody = [this]()
                {
                    Toggle(tool == Tool::Select, showcase.toolSelectIcon, [this](bool value) { if (value) tool = Tool::Select; });
					SetTooltip("Select mode");
                    Toggle(tool == Tool::Move,   showcase.toolMoveIcon,   [this](bool value) { if (value) tool = Tool::Move;   });
					SetTooltip("Move mode");
                    Toggle(tool == Tool::Rotate, showcase.toolRotateIcon, [this](bool value) { if (value) tool = Tool::Rotate; });
					SetTooltip("Rotate mode");
                    Toggle(tool == Tool::Scale,  showcase.toolScaleIcon,  [this](bool value) { if (value) tool = Tool::Scale;  });
					SetTooltip("Scale mode");
                }
            });
        toolbars.sides[(int)ToolbarSide::Bottom].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Status",
                .renderBody = [this]()
                {
                    Button(showGrid ? showcase.gridOnIcon : showcase.gridOffIcon, [this]() { showGrid = !showGrid; });
					SetTooltip(showGrid ? "Grid on" : "Grid off");
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
        toolbarCallbacks.onTearOff = [this](ToolbarSide srcSide, int from, tachyon::int2 mousePos)
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

        showcase.textBox1 = "Editable text...";
        showcase.textBox2 = "Multiline\nEditable\ntext2...";

        nodeViewMode = commandManager.RegisterMode("NodeView");
        commandManager.RegisterCommand(
            nodeViewMode,
            tachyon::KeyCode::Home,
            tachyon::KeyModifier::None,
            [this]()
            {
                TACHYON_LOG_INFO(kLogShowcase, "Reset view");
                showcase.nodeView.transform = tachyon::float3x2::Identity();
            },
            "Reset view"
        );

        // Example edit mode registration
        [[maybe_unused]] tachyon::CommandMode sculptMode = commandManager.RegisterMode("Sculpt");

        // Example: Ctrl+D fires in every edit mode (Default fallback)
        commandManager.RegisterCommand(
            showcase.duplicateIcon,
            tachyon::KeyCode::KeyD,
            tachyon::KeyModifier::Control,
            []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Duplicate"); },
            "Duplicate"
        );

        // --- Editor commands. Registered once with an optional icon + callback;
        // the main menu, toolbar and Shortcuts panel all reference these by handle.
        // Callbacks just print for now. A command may have no shortcut key (still
        // listed in the panel, where the user can bind one).
        auto& cmds = commandManager;
        showcase.cmdNew    = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.newIcon,      tachyon::KeyCode::KeyN, tachyon::KeyModifier::Control,                           []() { TACHYON_LOG_INFO(kLogShowcase, "Command: New"); },     "New");
        showcase.cmdOpen   = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.openFileIcon, tachyon::KeyCode::KeyO, tachyon::KeyModifier::Control,                           []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Open"); },    "Open...");
        showcase.cmdSave   = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.saveIcon,     tachyon::KeyCode::KeyS, tachyon::KeyModifier::Control,                           []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Save"); },    "Save");
        showcase.cmdSaveAs = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.saveAsIcon,   tachyon::KeyCode::KeyS, tachyon::KeyModifier::Control | tachyon::KeyModifier::Shift, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Save As"); }, "Save As...");
        showcase.cmdExit   = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.closeIcon,    tachyon::KeyCode::F4,   tachyon::KeyModifier::Alt,                               [this]() { RequestExit(); },   "Exit");

        showcase.cmdUndo   = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.undoIcon,  tachyon::KeyCode::KeyZ, tachyon::KeyModifier::Control, [this]() { commandManager.History().Undo(); }, "Undo");
        showcase.cmdRedo   = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.redoIcon,  tachyon::KeyCode::KeyY, tachyon::KeyModifier::Control, [this]() { commandManager.History().Redo(); },  "Redo");
        showcase.cmdCut    = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.cutIcon,   tachyon::KeyCode::KeyX, tachyon::KeyModifier::Control, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Cut"); },   "Cut");
        showcase.cmdCopy   = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.copyIcon,  tachyon::KeyCode::KeyC, tachyon::KeyModifier::Control, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Copy"); },  "Copy");
        showcase.cmdPaste  = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.pasteIcon, tachyon::KeyCode::KeyV, tachyon::KeyModifier::Control, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Paste"); }, "Paste");

        // Commands shown on a menu but with NO shortcut key (the user can assign
        // one in the Shortcuts panel).
        showcase.cmdFullscreen = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.fullscreenIcon, tachyon::KeyCode::F11, tachyon::KeyModifier::None,
                                                  [this]() { ToggleFullscreen(WindowMode::Fullscreen); }, "Fullscreen");
        showcase.cmdDocumentation = cmds.RegisterCommand(tachyon::CommandMode::Default, tachyon::IconID::Invalid, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: Documentation"); },     "Documentation");
        showcase.cmdAbout         = cmds.RegisterCommand(tachyon::CommandMode::Default, tachyon::IconID::Invalid, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: About TachyonUI"); }, "About TachyonUI");
        showcase.cmdSearchCommands = cmds.RegisterCommand(tachyon::CommandMode::Default, showcase.findIcon, tachyon::KeyCode::KeyK, tachyon::KeyModifier::Control,
                                                       [this]() { FocusCommandSearch(showcase.commandSearch); }, "Search Commands");
        showcase.commandSearch.shortcut = cmds.GetCommand(showcase.cmdSearchCommands)->displayString;

        // --- Further commands so the Shortcuts panel has realistic content.
        // These are panel/keyboard only (not referenced by the menu or toolbar).

        // Default (global).
        commandManager.RegisterCommand(tachyon::CommandMode::Default, showcase.selectAllIcon, tachyon::KeyCode::KeyA,   tachyon::KeyModifier::Control, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Select All"); }, "Select All");
        commandManager.RegisterCommand(tachyon::CommandMode::Default, showcase.deleteIcon, tachyon::KeyCode::Delete, tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Delete Selection"); }, "Delete Selection");
        commandManager.RegisterCommand(tachyon::CommandMode::Default, showcase.findIcon, tachyon::KeyCode::KeyF,   tachyon::KeyModifier::Control, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Find"); }, "Find");
        commandManager.RegisterCommand(tachyon::CommandMode::Default, showcase.playIcon, tachyon::KeyCode::F5,     tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Play"); }, "Play");
        commandManager.RegisterCommand(tachyon::CommandMode::Default, showcase.stopIcon, tachyon::KeyCode::F5,     tachyon::KeyModifier::Shift, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Stop"); }, "Stop");
        // Held shortcut: onBegin fires on press, onEnd on release.
        commandManager.RegisterHeldCommand(tachyon::CommandMode::Default, tachyon::KeyCode::Space, tachyon::KeyModifier::None,
            []() { TACHYON_LOG_INFO(kLogShowcase, "Hold begin: Pan View"); },
            []() { TACHYON_LOG_INFO(kLogShowcase, "Hold end: Pan View"); },
            "Pan View (hold)");

        // NodeView - graph navigation
        commandManager.RegisterCommand(nodeViewMode, showcase.frameSelectedIcon, tachyon::KeyCode::KeyF,  tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Frame Selected"); }, "Frame Selected");
        commandManager.RegisterCommand(nodeViewMode,                         tachyon::KeyCode::KeyA,  tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Frame All"); }, "Frame All");
        commandManager.RegisterCommand(nodeViewMode,                         tachyon::KeyCode::Space, tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Search Nodes"); }, "Search Nodes");
        commandManager.RegisterCommand(nodeViewMode,                         tachyon::KeyCode::KeyG,  tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Group Selection"); }, "Group Selection");

        // Sculpt - brush operations.
        commandManager.RegisterCommand(sculptMode,                  tachyon::KeyCode::LeftBracket,  tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Decrease Brush Size"); }, "Decrease Brush Size");
        commandManager.RegisterCommand(sculptMode,                  tachyon::KeyCode::RightBracket, tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Increase Brush Size"); }, "Increase Brush Size");
        commandManager.RegisterCommand(sculptMode, showcase.brushIcon,  tachyon::KeyCode::KeyB,         tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Brush Tool"); }, "Brush Tool");
        commandManager.RegisterCommand(sculptMode, showcase.eraserIcon, tachyon::KeyCode::KeyE,         tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Erase Tool"); }, "Erase Tool");
        commandManager.RegisterCommand(sculptMode,                  tachyon::KeyCode::KeyM,         tachyon::KeyModifier::None, []() { TACHYON_LOG_INFO(kLogShowcase, "Command: {}", "Smooth Tool"); }, "Smooth Tool");

        // Seed the showcase's node view. Each renderBody fills the row's middle slot;
        // sockets come from input/output presence.
        {
            showcase.nodeView.commandMode = nodeViewMode;

            // Socket colours double as the connection type: canConnect below only
            // allows wires between same-coloured sockets.
            const tachyon::color4f kTeal  { 0.20f, 0.75f, 0.72f, 1.0f };
            const tachyon::color4f kAmber { 0.95f, 0.62f, 0.20f, 1.0f };
            const tachyon::color4f kPurple{ 0.62f, 0.45f, 0.92f, 1.0f };

            // NodeText aligns to its socket (output => right, input => left,
            // pass-through => centred); editable rows use Label + Field.
            NodeModel src;
            src.title = "Source";
            src.pos   = { styleManager.Px(-340), styleManager.Px(-140) };
            src.properties.push_back(NodeProperty
            {
                .output      = true,
                .renderBody  = [this]() { NodeText(showcase.nodeName); },   // output => right-aligned
                .socketColor = kTeal
            });
            src.properties.push_back(NodeProperty
            {
                .output      = true,
                .renderBody  = [this]() { Label("Seed"); Field(showcase.nodeSeed); },
                .socketColor = kAmber
            });
            src.properties.push_back(NodeProperty
            {
                .output      = true,
                .renderBody  = [this]() { Label("Enabled"); Switch(showcase.nodeEnabled); },
                .socketColor = kPurple
            });

            NodeModel mix;
            mix.title = "Mix";
            mix.pos   = { styleManager.Px(40), styleManager.Px(-60) };
            mix.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Strength"); },     // input => left-aligned
                .socketColor = kTeal
            });
            mix.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { Label("Gain"); Field(showcase.nodeGain); },
                .socketColor = kAmber
            });
            mix.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .output      = true,
                .renderBody  = [this]() { NodeText("Bypass"); },       // both dots => centred
                .socketColor = kPurple
            });
            mix.properties.push_back(NodeProperty
            {
                .output      = true,
                .renderBody  = [this]() { NodeText("Result"); },       // output => right-aligned
                .socketColor = kTeal
            });

            NodeModel out;
            out.title = "Output";
            out.pos   = { styleManager.Px(180), styleManager.Px(100) };
            out.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { NodeText("Value"); },        // input => left-aligned
                .socketColor = kTeal     // accepts Mix.Result / Source.Name
            });
            out.properties.push_back(NodeProperty
            {
                .input       = PropertyEndpoint{},
                .renderBody  = [this]() { Label("Tint"); Field(showcase.nodeGain); },
                .socketColor = kPurple   // accepts Source.Enabled
            });
            out.properties.push_back(NodeProperty
            {
                .output      = true,
                .renderBody  = [this]() { NodeText("Level"); },        // output => right-aligned
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
                    .output      = true,   // Color output - the dot sits at the image's bottom grid line
                    .renderBody  = [this, imgSide]() { Image(demoImageIndex, tachyon::uint2{ 64, 64 }, tachyon::LayoutOverride::FixedSize(Pixels{ imgSide }, Pixels{ imgSide })); },
                    .socketColor = kTeal,                // can feed Mix.Strength / Output.Value
                    .rowSpan     = rowSpan,
                });
            }

            showcase.nodeView.nodes.push_back(std::move(src));
            showcase.nodeView.nodes.push_back(std::move(mix));
            showcase.nodeView.nodes.push_back(std::move(out));
            showcase.nodeView.nodes.push_back(std::move(tex));

            // Stable IDs keep per-node state keyed when view.ordering reshuffles.
            for (auto& node : showcase.nodeView.nodes)
                node.stableID = idManager.CreateStaticID();
            ApplyNodeCategories();
            showcase.nodeView.breadcrumb = { "Graphs", "Terrain material" };

            // Connection rule (delegate): only sockets of the same colour may
            // connect. Endpoints are bounds-checked before this is called.
            showcase.nodeView.canConnect =
                [](const NodeView& v, PropertyEndpoint o, PropertyEndpoint i)
                {
                    return v.nodes[o.nodeIdx].properties[o.propIdx].socketColor
                         == v.nodes[i.nodeIdx].properties[i.propIdx].socketColor;
                };

            // The view reports each edit once it is made; it goes on the app's undo history,
            // with the text edits.
            showcase.nodeView.onEdit = [this](const NodeViewEdit& edit)
            {
                commandManager.History().Push({ .description = "Edit node graph",
                    .undo = [this, edit] { ApplyNodeViewEdit(showcase.nodeView, edit, /*undo*/ true); },
                    .redo = [this, edit] { ApplyNodeViewEdit(showcase.nodeView, edit, /*undo*/ false); } });
                commandManager.History().EndMerge();
            };

            // One initial connection: Source.Name (0,0) output -> Mix.Strength (1,0) input
            showcase.nodeView.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
        }

        return true;
    }

    bool Application::InitializeShowcaseStyles()
    {
        hierarchyVizCellStyleRef    = theme.GetStyle("hierarchy-viz-cell");
        hierarchyVizBgStyleRef      = theme.GetStyle("hierarchy-viz-bg");
        hierarchyVisibilityStyleRef = theme.GetStyle("hierarchy-visibility");
        logLevelIconStyleRef        = theme.GetStyle("log-level-icon");
        logLevelWellStyleRef        = theme.GetStyle("log-level-well");
        logLevelPillStyleRefs[0]    = theme.GetStyle("log-level-info");
        logLevelPillStyleRefs[1]    = theme.GetStyle("log-level-warning");
        logLevelPillStyleRefs[2]    = theme.GetStyle("log-level-error");
        iconToggleStyleRef          = theme.GetStyle("icon-toggle");
        logMessageStyleRef          = theme.GetStyle("log-message");
        logCellStyleRef             = theme.GetStyle("log-cell");
        logCellTimeStyleRef         = theme.GetStyle("log-cell-time");
        logTableStyleRef            = theme.GetStyle("log-table");
        filesToolbarStyleRef        = theme.GetStyle("files-toolbar");
        appIconStyleRef             = theme.GetStyle("app-icon");
        inspectorPairHalfStyleRef   = theme.GetStyle("inspector-pair-half");
        inspectorCaptionStyleRef    = theme.GetStyle("inspector-caption");
        inspectorHeaderStyleRef     = theme.GetStyle("inspector-header");
        inspectorObjectIconStyleRef = theme.GetStyle("inspector-object-icon");
        inspectorObjectNamesStyleRef= theme.GetStyle("inspector-object-names");
        inspectorObjectTitleStyleRef= theme.GetStyle("inspector-object-title");
        inspectorObjectPathStyleRef = theme.GetStyle("inspector-object-path");
        inspectorSearchRowStyleRef  = theme.GetStyle("inspector-search-row");
        inspectorSectionStyleRef    = theme.GetStyle("inspector-section");
        inspectorSectionFirstStyleRef= theme.GetStyle("inspector-section-first");
        inspectorSectionBodyStyleRef= theme.GetStyle("inspector-section-body");
        profilerHeaderStyleRef      = theme.GetStyle("profiler-header");
        profilerRecordStyleRef      = theme.GetStyle("profiler-record");
        profilerPauseStyleRef       = theme.GetStyle("profiler-pause");
        profilerMsStyleRef          = theme.GetStyle("profiler-ms");
        profilerFpsStyleRef         = theme.GetStyle("profiler-fps");
        profilerPctCellStyleRef     = theme.GetStyle("profiler-pct-cell");
        profilerPctTrackStyleRef    = theme.GetStyle("profiler-pct-track");
        profilerPctFillStyleRef     = theme.GetStyle("profiler-pct-fill");
        profilerPctTextStyleRef     = theme.GetStyle("profiler-pct-text");
        logCountBadgeStyleRef       = theme.GetStyle("log-count-badge");
        logDetailPaneStyleRef       = theme.GetStyle("log-detail-pane");
        logDetailTextStyleRef       = theme.GetStyle("log-detail-text");
        shortcutCategoryStyleRef    = theme.GetStyle("shortcut-category");
        shortcutTableStyleRef       = theme.GetStyle("shortcut-table");
        shortcutCellStyleRef        = theme.GetStyle("shortcut-cell");
        dataTableCellStyleRef       = theme.GetStyle("data-table-cell");
        dataTableNumberStyleRef     = theme.GetStyle("data-table-number");
        settingsTitleStyleRef       = theme.GetStyle("settings-title");
        {
            const char* kSideTag[4]   = { "left", "right", "top", "bottom" };
            const char* kVAlignTag[3] = { "top",  "mid",   "bot" };
            for (int s = 0; s < 4; ++s)
                for (int v = 0; v < 3; ++v)
                    iconVAlignDemoStyleRefs[s][v] = theme.GetStyle(std::string("icon-demo-") + kSideTag[s] + "-" + kVAlignTag[v]);
        }

        // -------------------------------------------------------------------------
        // Showcase-specific styles below
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
            boxStyle.textLayoutStyle = tachyon::TextLayoutStyle
            {
                .textDirection = tachyon::TextDirection::Auto,
                .textWrapping = tachyon::TextWrap::WordChar,
                .horizontalAlignment = tachyon::TextAlign::Start,
                .baseline = tachyon::TextBaseline::Middle
            };
            boxStyle.borderWidths = noBorder;
            boxStyle.borderRadius = noRadius;
            boxStyle.margin = BoxEdges(2);
            boxStyle.SetOverflow(tachyon::Overflow::Hidden);
            boxStyle.padding = tachyon::BoxEdges(10);
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

        showcase.testBoxStyleCount = int(boxStyleRefs.size()) - 1;

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

    bool Application::InitializeShowcaseResources()
    {
        {
            //checkerboard image
            {
                tachyon::color4b black = tachyon::color4b(0, 0, 0, 255);
                tachyon::color4b magenta = tachyon::color4b(255, 0, 255, 255);
                std::array<tachyon::color4b, 16 * 16 > pixels{};
                for (int y = 0, n = 0; y < 16; y++)
                {
                    for (int x = 0; x < 16; x++, n++)
                    {
                        pixels[n] = ((x % 2) ^ (y % 2)) ? magenta : black;
                    }
                }
                tachyon::Image* checkerboardImage = graphicsContext->CreateImage(pixels.data(), uint2{ 16, 16 }, tachyon::ImageFormat::R8G8B8A8, tachyon::Sampler::LinearRepeat, true);
                if (checkerboardImage == nullptr)
                {
                    TACHYON_LOG_ERROR(kLogShowcase, "Failed to create checkerboard image");
                    return false;
                }
                checkerboardIndex = graphicsContext->RegisterImage(checkerboardImage);

                graphicsContext->DestroyImageAtEnd(checkerboardImage, tachyon::DeviceScope::Application);
            }

            // Sample gradient image to showcase the Image() element (image fields,
            // node thumbnails, ...).
            {
                constexpr int W = 64, H = 64;
                std::array<tachyon::color4b, W * H> pixels{};
                for (int y = 0, n = 0; y < H; y++)
                    for (int x = 0; x < W; x++, n++)
                        pixels[n] = tachyon::color4b(static_cast<tachyon::byte>(x * 4),
                                                 static_cast<tachyon::byte>(y * 4),
                                                 static_cast<tachyon::byte>(160 - (x + y)), 255);
                tachyon::Image* gradient = graphicsContext->CreateImage(
                    pixels.data(), uint2{ W, H }, tachyon::ImageFormat::R8G8B8A8,
                    tachyon::Sampler::LinearClampToEdge, true);
                if (gradient == nullptr)
                {
                    TACHYON_LOG_ERROR(kLogShowcase, "Failed to create demo image");
                    return false;
                }
                demoImageIndex = graphicsContext->RegisterImage(gradient);
                graphicsContext->DestroyImageAtEnd(gradient, tachyon::DeviceScope::Application);
            }

            // Register icons from SVG
            {
                // Simple circle icon (using path with arc)
                const char* circleSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M 12 2 A 10 10 0 1 1 12 22 A 10 10 0 1 1 12 2 Z" fill="#4A90E2"/></svg>)";
                showcase.circleIcon = textSystem->RegisterIconFromSVG("circle", circleSvg, strlen(circleSvg));

                // Heart icon - simplified triangular approximation
                const char* heartSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 21 L6 12 L6 8 L9 5 L12 7 L15 5 L18 8 L18 12 Z" fill="#E74C3C"/></svg>)";
                showcase.heartIcon = textSystem->RegisterIconFromSVG("heart", heartSvg, strlen(heartSvg));

                // Star icon - simple 5-point star using only lines
                const char* starSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 2 L14.5 9 L22 9.5 L16.5 14 L18 22 L12 17.5 L6 22 L7.5 14 L2 9.5 L9.5 9 Z" fill="#F39C12"/></svg>)";
                showcase.starIcon = textSystem->RegisterIconFromSVG("star", starSvg, strlen(starSvg));

                // Console log-level icons - coloured badge with a white glyph.
                const char* infoSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#4A90E2"/><circle cx="12" cy="7.5" r="1.6" fill="#ffffff"/><rect x="10.4" y="10.5" width="3.2" height="7.5" rx="1.6" fill="#ffffff"/></svg>)";
                showcase.infoIcon = textSystem->RegisterIconFromSVG("info", infoSvg, strlen(infoSvg));

                const char* warningSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 2.5 L22.5 21 L1.5 21 Z" fill="#F5A623"/><rect x="10.7" y="8.5" width="2.6" height="6.5" rx="1.3" fill="#ffffff"/><circle cx="12" cy="18" r="1.4" fill="#ffffff"/></svg>)";
                showcase.warningIcon = textSystem->RegisterIconFromSVG("warning", warningSvg, strlen(warningSvg));

                const char* errorSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#E5484D"/><path d="M9.2 7.8 L12 10.6 L14.8 7.8 L16.2 9.2 L13.4 12 L16.2 14.8 L14.8 16.2 L12 13.4 L9.2 16.2 L7.8 14.8 L10.6 12 L7.8 9.2 Z" fill="#ffffff"/></svg>)";
                showcase.errorIcon = textSystem->RegisterIconFromSVG("error", errorSvg, strlen(errorSvg));

                // Monochrome glyphs that take the text (or icon) color: the log's level
                // dots, the profiler's pause and the files view's list mode.
                const char* dotSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="12" fill="#ffffff"/></svg>)";
                showcase.dotIcon = textSystem->RegisterIconFromSVG("dot", dotSvg, strlen(dotSvg));

                const char* pauseSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><rect x="6" y="4" width="4" height="16" rx="1" fill="#ffffff"/><rect x="14" y="4" width="4" height="16" rx="1" fill="#ffffff"/></svg>)";
                showcase.pauseIcon = textSystem->RegisterIconFromSVG("pause", pauseSvg, strlen(pauseSvg));

                const char* listSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/></svg>)";
                showcase.listIcon = textSystem->RegisterIconFromSVG("list", listSvg, strlen(listSvg));

                const char* moreSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="5" cy="12" r="2" fill="#ffffff"/><circle cx="12" cy="12" r="2" fill="#ffffff"/><circle cx="19" cy="12" r="2" fill="#ffffff"/></svg>)";
                showcase.moreIcon = textSystem->RegisterIconFromSVG("more", moreSvg, strlen(moreSvg));

                const char* chevronRightSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m9 18 6-6-6-6"/></svg>)";
                showcase.chevronRightIcon = textSystem->RegisterIconFromSVG("chevron-right", chevronRightSvg, strlen(chevronRightSvg));

                // Folder icons for GridView demo - closed and open variants
                showcase.folderClosedIcon    = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/folder_closed.svg").c_str());
                showcase.folderOpenIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/folder_open.svg").c_str()); 
                                         
                showcase.audioOnIcon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/volume-2.svg").c_str());
                showcase.audioOffIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/volume-x.svg").c_str());
                                         
                showcase.gridOnIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/grid-3x3.svg").c_str());
                showcase.layoutGridIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/layout-grid.svg").c_str());
                showcase.boxIcon             = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/box.svg").c_str());
                showcase.chevronDownIcon     = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/chevron-down.svg").c_str());
                showcase.gridOffIcon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/grid-2x2-x.svg").c_str());
                                         
                showcase.lightOnIcon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/lightbulb.svg").c_str());
                showcase.lightOffIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/lightbulb-off.svg").c_str());
                                         
                showcase.toolSelectIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/square-dashed.svg").c_str());
                showcase.toolMoveIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/move.svg").c_str());
                showcase.toolScaleIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/maximize-2.svg").c_str());
                showcase.toolRotateIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/rotate-cw.svg").c_str());
                                         
                showcase.viewWireframe       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/globe.svg").c_str());
                showcase.viewSolid           = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/circle.svg").c_str());
                showcase.viewTextured        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/view-textured.svg").c_str());

                showcase.panelFilesIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/folder.svg").c_str());
				showcase.panelGameIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/gamepad-2.svg").c_str());
                showcase.panelHierarchyIcon  = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/list-tree.svg").c_str());
				showcase.panelInspectorIcon  = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/info.svg").c_str());
				showcase.panelLogIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/terminal.svg").c_str());
				showcase.panelNodeViewIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/workflow.svg").c_str());
				showcase.panelProfilerIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/activity.svg").c_str());
				showcase.panelSettingsIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/sliders-horizontal.svg").c_str());
				showcase.panelViewportIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/monitor.svg").c_str());
                showcase.panelMiscIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/layout-grid.svg").c_str());
                
				showcase.objectIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/box.svg").c_str());
				showcase.object2Icon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/shapes.svg").c_str());
                                         
				showcase.fileImageIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-image.svg").c_str());
				showcase.fileAudioIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-audio.svg").c_str());
                showcase.fileVideoIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-video.svg").c_str());
				showcase.fileModelIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-box.svg").c_str());
                showcase.fileTextIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-text.svg").c_str());
				showcase.fileArchiveIcon     = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-archive.svg").c_str());
                                         
                                         
                showcase.openFileIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/folder-open.svg").c_str());
                showcase.saveIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/save.svg").c_str());
                showcase.saveAsIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/save-all.svg").c_str());
                showcase.closeIcon           = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/x.svg").c_str());
                showcase.addIcon             = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/plus.svg").c_str());
                showcase.removeIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/minus.svg").c_str());
                showcase.undoIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/undo-2.svg").c_str());
                showcase.redoIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/redo-2.svg").c_str());
                showcase.cutIcon             = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/scissors.svg").c_str());
                showcase.copyIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/copy.svg").c_str());
                showcase.pasteIcon           = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/clipboard-paste.svg").c_str());
                showcase.blockIcon           = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/ban.svg").c_str());
                showcase.syncIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/refresh-cw.svg").c_str());
                showcase.downloadIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/download.svg").c_str());
                showcase.uploadIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/upload.svg").c_str());
                showcase.publishIcon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/send.svg").c_str());
                showcase.openInWindowIcon    = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/external-link.svg").c_str());
                                         
                showcase.duplicateIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/copy-plus.svg").c_str());
                showcase.newIcon             = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/file-plus.svg").c_str());
                showcase.selectAllIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/text-select.svg").c_str());
                showcase.deleteIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/trash-2.svg").c_str());
                showcase.zoomInIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/zoom-in.svg").c_str());
                showcase.zoomOutIcon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/zoom-out.svg").c_str());
                showcase.findIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/search.svg").c_str());
                showcase.playIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/play.svg").c_str());
                showcase.stopIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/square.svg").c_str());
                showcase.recenterIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/locate-fixed.svg").c_str());
                showcase.frameSelectedIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/scan.svg").c_str());
                showcase.brushIcon           = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/brush.svg").c_str());
                showcase.eraserIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/eraser.svg").c_str());
                showcase.draggableIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/grip-vertical.svg").c_str());
                
				showcase.darkModeIcon        = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/moon.svg").c_str());
                showcase.lightModeIcon       = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/sun.svg").c_str());
                showcase.lightModeAutoIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/sun-moon.svg").c_str());
                showcase.fullscreenIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/maximize.svg").c_str());
                showcase.fullscreenExitIcon  = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/minimize.svg").c_str());
                showcase.viewSelectedIcon    = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/scan-eye.svg").c_str());
                showcase.dropperIcon         = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/pipette.svg").c_str());
                showcase.editIcon            = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/pencil.svg").c_str());
                showcase.tagIcon             = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/tag.svg").c_str());
                showcase.targetIcon          = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/target.svg").c_str());
                showcase.visibilityLockIcon  = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/lock.svg").c_str());
                showcase.visibilityMixedIcon = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/eye-dashed.svg").c_str());
                showcase.visibilityOffIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/eye-off.svg").c_str());
                showcase.visibilityOnIcon    = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/eye.svg").c_str());

                showcase.dropdownArrowIcon   = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/chevron-down.svg").c_str());
                showcase.iconCollapseAllIcon = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/chevrons-down-up.svg").c_str());

				showcase.keyCommandIcon      = textSystem->RegisterIconFromSVGFile(ResourceFile("icons/lucide/command.svg").c_str());

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

        // The application's mark, before the menus.
        Text(idManager.AllocateDynamicID("appIcon"), "T", appIconStyleRef);

        // ---- File ----
        if (BeginMenu(idManager.AllocateDynamicID("menuFile"), "File",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            // Icon, label, shortcut and callback come from the command.
            MenuItem(showcase.cmdNew);
            MenuItem(showcase.cmdOpen);
            MenuItem(showcase.cmdSave);
            MenuItem(showcase.cmdSaveAs);
            MenuSeparator();
            MenuItem(showcase.cmdExit);
        }
        EndMenu();

        // ---- Edit ----
        if (BeginMenu(idManager.AllocateDynamicID("menuEdit"), "Edit",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            // The app's undo history; disabled with nothing to undo or redo.
            const auto& history = commandManager.History();
            if (!history.CanUndo()) BeginDisabled();
            MenuItem(showcase.cmdUndo);
            if (!history.CanUndo()) EndDisabled();
            if (!history.CanRedo()) BeginDisabled();
            MenuItem(showcase.cmdRedo);
            if (!history.CanRedo()) EndDisabled();
            MenuSeparator();
            MenuItem(showcase.cmdCut);
            MenuItem(showcase.cmdCopy);
            MenuItem(showcase.cmdPaste);
            MenuSeparator();
            DestructiveMenuItem({ theme.deleteIcon, "Delete" }, [] {});
        }
        EndMenu();

        // ---- View ----
        if (BeginMenu(idManager.AllocateDynamicID("menuView"), "View",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            // Theme submenu - mirrors the Appearance settings page; both mutate
            // showcase.settingsThemeIndex so the two stay in sync.
            static const char* const themeNames[] = { "Dark", "Light" };
            static const char* const themePaths[] = { "themes/dark.css", "themes/light.css" };
            if (BeginMenu(idManager.AllocateDynamicID("menuViewTheme"), "Theme"))
            {
                for (int i = 0; i < 2; i++)
                {
                    int idx = i;
                    bool isCurrent = (idx == showcase.settingsThemeIndex);
                    if (isCurrent) BeginSelected();
                    MenuItem(themeNames[i], [this, idx]() {
                        showcase.settingsThemeIndex = idx;
                        RequestReloadTheme(themePaths[idx], showcase.settingsFontScalePct / 100.0f);
                    });
                    if (isCurrent) EndSelected();
                }
            }
            EndMenu();

            MenuSeparator();

            // Zoom = font scale, clamped to [100, 300]% in steps of 10 to match the
            // Appearance page. RequestReloadTheme no-ops when the scale is unchanged.
            MenuItem({ showcase.zoomInIcon, "Zoom In" }, [this]() {
                showcase.settingsFontScalePct = std::clamp(showcase.settingsFontScalePct + 10.0f, 100.0f, 300.0f);
                RequestReloadTheme(GetThemePath(), showcase.settingsFontScalePct / 100.0f);
            });
            MenuItem({ showcase.zoomOutIcon, "Zoom Out" }, [this]() {
                showcase.settingsFontScalePct = std::clamp(showcase.settingsFontScalePct - 10.0f, 100.0f, 300.0f);
                RequestReloadTheme(GetThemePath(), showcase.settingsFontScalePct / 100.0f);
            });
            MenuItem({ showcase.recenterIcon, "Reset Zoom" }, [this]() {
                showcase.settingsFontScalePct = 100.0f;
                RequestReloadTheme(GetThemePath(), 1.0f);
            });

            MenuSeparator();
            const WindowMode mode = windowManager->GetWindowMode(showcase.windowLayoutID2);
            if (mode == WindowMode::Fullscreen || mode == WindowMode::ExclusiveFullscreen)
                MenuItem({ showcase.fullscreenExitIcon, "Exit Fullscreen" }, "F11", [this]() { ToggleFullscreen(WindowMode::Fullscreen); });
            else
            {
                MenuItem(showcase.cmdFullscreen);
                MenuItem("Exclusive Fullscreen", [this]() { ToggleFullscreen(WindowMode::ExclusiveFullscreen); });
            }
        }
        EndMenu();

        // ---- Help ---- (commands with no shortcut key)
        if (BeginMenu(idManager.AllocateDynamicID("menuHelp"), "Help",
                      theme.menuBarTriggerStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
        {
            MenuItem(showcase.cmdDocumentation);
            MenuItem(showcase.cmdAbout);
        }
        EndMenu();

        // The command search, centred in the space after the menus.
        BeginHorizontal(LayoutOverride::Auto().FlexGrow(1));
        EndHorizontal();
        CommandSearch(showcase.commandSearch);
        BeginHorizontal(LayoutOverride::Auto().FlexGrow(1));
        EndHorizontal();
        StatusText("Test");

        EndTitleBar();
    }

    // Undo (or redo) the node view's last edit, moving it to the other stack.
    // Quitting loses the edits on the undo history: ask first when there are any.
    void Application::ToggleFullscreen(WindowMode fullscreen)
    {
        const ID window = showcase.windowLayoutID2;
        const WindowMode mode = windowManager->GetWindowMode(window);
        if (mode == WindowMode::Fullscreen || mode == WindowMode::ExclusiveFullscreen)
        {
            windowManager->SetWindowMode(window, showcase.modeBeforeFullscreen);
            return;
        }
        showcase.modeBeforeFullscreen = mode == WindowMode::Maximized ? WindowMode::Maximized : WindowMode::Normal;
        windowManager->SetWindowMode(window, fullscreen);
    }

    void Application::RequestExit()
    {
        if (!commandManager.History().CanUndo()) applicationQuit = true;
        else                       showcase.exitRequested = true;
    }

    void Application::AskBeforeExit()
    {
        if (!showcase.exitRequested) return;
        static constexpr MessageButton kButtons[] = {
            { .label = "Cancel" },
            { .label = "Quit", .kind = MessageButtonKind::Danger },
        };
        const size_t edits = commandManager.History().UndoCount();
        const std::string message = std::format("There {} {} unsaved edit{}. Quitting discards {}.",
                                                edits == 1 ? "is" : "are", edits, edits == 1 ? "" : "s", edits == 1 ? "it" : "them");
        const int choice = MessageDialog(showcase.exitDialogID, MessageDialogDesc{
            .icon = MessageIcon::Quit, .title = "Quit Test?", .message = message, .buttons = kButtons,
            .defaultButton = 0, .cancelButton = 0 });   // Cancel: quitting destroys work
        if (choice == 1) applicationQuit = true;
        if (choice >= 0) showcase.exitRequested = false;
    }

    // Status bar: state on the left, frame time on the right.
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
        StatusText("Ready", LayoutOverride::Auto().FlexGrow(1));
        StatusText(std::format("{:.2f} ms \u00B7 {:.0f} fps", frameMs, frameMs > 0.0f ? 1000.0f / frameMs : 0.0f));
        EndStatusBar();
    }

    void Application::OnGUI()
    {
        auto&& sizeConstraints = LayoutOverride::FixedSize(1700, 900);
        
        {
            static bool mainWindowClosed = false;
            // Closed by the user; reset mainWindowClosed to reopen.
            if (!mainWindowClosed)
            {
                if (BeginWindow(showcase.windowLayoutID2, "Test", theme.mainWindowStyleRef, std::move(sizeConstraints.Centered()),
                                ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered | ElementFlags::WindowCustomTitleBar
                                | ElementFlags::WindowConfirmClose))
                {
                    if (CloseRequested(showcase.windowLayoutID2))
                        RequestExit();
                    AskBeforeExit();

                    RenderMainMenu();

                    // ---- DockingContainer demo ----
                    {
                        if (!showcase.dockInitialized)
                        {
                            auto hierarchyPanelID   = CreatePanel(showcase.dockingLayout.panels, "Hierarchy",   showcase.panelHierarchyIcon, [this]() { RenderHierarchyPanel(); }, {}, [this]() { EnableHierarchyPanel(); }, [this]() { DisableHierarchyPanel(); });
                            auto inspectorPanelID   = CreatePanel(showcase.dockingLayout.panels, "Inspector",   showcase.panelInspectorIcon, [this]() { RenderInspectorPanel(); }, {}, [this]() { EnableInspectorPanel(); }, [this]() { DisableInspectorPanel(); });
                            auto consolePanelID     = CreatePanel(showcase.dockingLayout.panels, "Log",         showcase.panelLogIcon,       [this]() { RenderLogPanel(); },       {}, [this]() { EnableLogPanel(); },       [this]() { DisableLogPanel(); });
                            auto filesPanelID       = CreatePanel(showcase.dockingLayout.panels, "Files",       showcase.panelFilesIcon,     [this]() { RenderFilesPanel(); }, {}, [this]() { EnableFilesPanel(); }, [this]() { DisableFilesPanel(); });
                            auto nodeViewPanelID    = CreatePanel(showcase.dockingLayout.panels, "NodeView",    showcase.panelNodeViewIcon,  [this]() { RenderNodeViewPanel(); });
                            auto viewportPanelID    = CreatePanel(showcase.dockingLayout.panels, "Viewport",    showcase.panelViewportIcon,  [this]() { RenderViewportPanel(); });
                            auto profilerPanelID    = CreatePanel(showcase.dockingLayout.panels, "Profiler",    showcase.panelProfilerIcon,  [this]() { RenderProfilerTreePanel(); }, {}, [this]() { EnableProfilerTreePanel(); });
                            // Panels are cached by default; the profiler opts out while profiling is on.
                            showcase.dockingLayout.panels[profilerPanelID].cacheSignature =
                                []() -> std::optional<uint64_t> { return Diagnostics::IsProfilingEnabled() ? std::nullopt : std::optional<uint64_t>(0); };

                            auto treeTablePanelID   = CreatePanel(showcase.dockingLayout.panels, "TreeTable",   showcase.panelMiscIcon, [this]() { RenderTreeTablePanel(); });
                            auto tablePanelID       = CreatePanel(showcase.dockingLayout.panels, "Table",       showcase.panelMiscIcon, [this]() { RenderTablePanel(); });
                            auto splitScrollPanelID = CreatePanel(showcase.dockingLayout.panels, "FrozenTable", showcase.panelMiscIcon, [this]() { RenderSplitScrollPanel(); });
                            auto reorderablePanelID = CreatePanel(showcase.dockingLayout.panels, "Reorderable", showcase.panelMiscIcon, [this]() { RenderReorderablePanel(); });
                            auto boxTestPanelID     = CreatePanel(showcase.dockingLayout.panels, "BoxTest",     showcase.panelMiscIcon, [this]() { RenderBoxTestPanel(); });
                            auto miscPanelID        = CreatePanel(showcase.dockingLayout.panels, "Misc",        showcase.panelMiscIcon, [this]() { RenderMiscPanel(); });
                            // Misc animates during construction (rotating triangle, cycling hue), so it
                            // must rebuild every frame - opt out of caching.
                            showcase.dockingLayout.panels[miscPanelID].cacheSignature =
                                []() -> std::optional<uint64_t> { return std::nullopt; };

                            // Pages are registered once in InitSettingsPages.
                            auto settingsPanelID    = CreatePanel(showcase.dockingLayout.panels, "Settings", showcase.panelSettingsIcon, [this]() { RenderSettingsPanel(showcase.settings); });
                        
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
                            showcase.dockingLayout.root =
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
                            showcase.dockInitialized = true;
                            if (!showcase.savedUIState.empty())
                                (void)RestoreUIState(showcase.savedUIState);   // now with the dock's panels

                            // --panel: bring that panel to the front of its tab group.
                            auto selectStartPanel = [this](auto& self, DockNode* node) -> void {
                                if (!node) return;
                                for (size_t t = 0; t < node->panelIds.size(); ++t)
                                {
                                    const auto it = showcase.dockingLayout.panels.find(node->panelIds[t]);
                                    if (it != showcase.dockingLayout.panels.end()
                                        && std::find(m_startPanels.begin(), m_startPanels.end(), it->second.title) != m_startPanels.end())
                                        node->selectedTab = int(t);
                                }
                                self(self, node->first.get());
                                self(self, node->second.get());
                            };
                            if (!m_startPanels.empty())
                                selectStartPanel(selectStartPanel, showcase.dockingLayout.root.get());

                            // --float: those panels start in their own windows.
                            int2 floatAt{ styleManager.Px(200), styleManager.Px(200) };
                            for (const std::string& title : m_floatPanels)
                                for (const auto& [id, panel] : showcase.dockingLayout.panels)
                                    if (panel.title == title)
                                    {
                                        (void)FloatPanel(showcase.dockingLayout, id, floatAt);
                                        floatAt += int2{ styleManager.Px(40), styleManager.Px(40) };
                                        break;
                                    }
                        }

                        showcase.dockContainerID = idManager.AllocateDynamicID("dockDemo");
                        BeginDockingContainer(showcase.dockContainerID, &showcase.dockingLayout,
                            theme.dockContainerMainStyleRef, LayoutOverride::Auto());
                        EndDockingContainer();
                    }

                    RenderStatusBar();

                    // Must be emitted inside the main window's scope so main becomes
                    // their OS owner; otherwise SDL's hidden helper owns them and they
                    // die when it is torn down.
                    EmitFloatingDockWindows();
                    EmitFloatingToolbarWindows(toolbars.floatingItems, &toolbarOpts);
                } else
                {
                    // BeginWindow returned false - window is closing
                    mainWindowClosed = true;
                }
                EndWindow();
            }
        }
    }
}
