#pragma once

// Not installed: GUIApplication's internals, for the library's own code and the tests.

#include <gui_controls/gui_application.h>

namespace tachyon
{
    // Derive from this instead of GUIApplication to reach its internals.
    class GUIApplicationInternals : public GUIApplication
    {
    public:
        using GUIApplication::m_frame;
        using GUIApplication::m_sliderFieldTrack;
        using GUIApplication::m_menuRects;
        using GUIApplication::m_labelGroup;
        using GUIApplication::m_buttonGroup;
        using GUIApplication::m_colorPickerSub;
        using GUIApplication::m_gradientEditorSub;
        using GUIApplication::m_renameSub;
        using GUIApplication::m_reorderable;
        using GUIApplication::m_dockTabDragSub;
        using GUIApplication::m_toolbarWindowDrag;
        using GUIApplication::m_uiCache;

        using GUIApplication::controlStylesStack;
        using GUIApplication::tooltipWindowID;
        using GUIApplication::contextMenuWindowID;
        using GUIApplication::contextMenuTargetID;
        using GUIApplication::tabDragGhostWindowID;

        using GUIApplication::m_columnDrag;
        using GUIApplication::m_treeDropState;
        using GUIApplication::m_tableEmit;
        using GUIApplication::m_dockingState;
        using GUIApplication::m_floatingDockWindows;
        using GUIApplication::activeWindowID;
        using GUIApplication::m_windowIdScopeStack;

        using GUIApplication::RegisterFrameBalanceChecks;
        using GUIApplication::ReportMisuse;
        using GUIApplication::m_inFrame;
        using GUIApplication::RegisterFrameHooks;
        using GUIApplication::ApplyPendingStyleReload;
        using GUIApplication::InvalidateCachedScopesForInput;
        using GUIApplication::BeginDockingFrame;
        using GUIApplication::EndDockingFrame;
        using GUIApplication::OnFrame;
        using GUIApplication::OnRender;
        using GUIApplication::RegisterElementInternal;

        // For the library's free helpers, which get a plain GUIApplication.
        static ID&                   TabDragGhostWindowID(GUIApplication& app) { return app.tabDragGhostWindowID; }
        static ReorderableSubsystem& Reorderable(GUIApplication& app)          { return *app.m_reorderable; }
        static DockTabDragSubsystem& DockTabDrag(GUIApplication& app)          { return *app.m_dockTabDragSub; }
        static MenuRectsSubsystem&   MenuRects(GUIApplication& app)            { return *app.m_menuRects; }
        static auto&                 FloatingDockWindows(GUIApplication& app)  { return app.m_floatingDockWindows; }
        static auto&                 ToolbarDocksThisFrame(GUIApplication& app) { return app.m_toolbarDocksThisFrame; }
        static DockNode* FindDockLeafTabBarCachedAtScreenPos(GUIApplication& app, int2 screenPos) { return app.FindDockLeafTabBarCachedAtScreenPos(screenPos); }
        static void      NotePanelContentID(GUIApplication& app, ID panelId, ID contentID)       { app.NotePanelContentID(panelId, contentID); }
        static void      TitleBarCaption(GUIApplication& app, ID id)                              { app.TitleBarCaption(id); }
        static ColorPickerSubsystem&    ColorPicker(GUIApplication& app)    { return *app.m_colorPickerSub; }
        static GradientEditorSubsystem& GradientEditor(GUIApplication& app) { return *app.m_gradientEditorSub; }
        static void      ReloadThemeNextFrame(GUIApplication& app)                                 { app.m_themeReloadPending = true; }
    };
}
