#include <gui_controls/gui_application.h>

namespace tachyon
{

    // Settings panel: page tree | sizer | selected page's render callback.
    // SettingsPanelState owns only selection + tree width; page content state
    // belongs to the application and is captured by each render lambda.
    void GUIApplication::RenderSettingsPanel(SettingsPanelState& state)
    {
        // flexFillRow (flex-basis:0 + grow:1) bounds the split to the panel height so an
        // overflowing page scrolls inside its own viewport. Must hold down the whole chain.
        BeginHorizontal(idManager.AllocateDynamicID(),
                        theme.flexFillRowStyleRef,
                        tachyon::LayoutOverride::Auto().FlexGrow(1));

        // Left: flat page tree. Selection lives on internal.tableState as sub-range row IDs and is
        // translated to/from state.selectedID. Consume a click into selectedID before
        // seeding selectedRowID from it, or the seed overwrites the click.
        {
            // Sync page list -> flat rows. depth=0 throughout.
            state.internal.rows.clear();
            state.internal.rows.reserve(state.pages.size());
            for (size_t i = 0; i < state.pages.size(); ++i)
            {
                TreeViewRow r{};
                r.id    = state.pages[i].id;
                r.name  = state.pages[i].name;
                r.depth = 0;
                r.icon  = tachyon::IconID::Invalid;
                state.internal.rows.push_back(std::move(r));
            }
            RebuildTreeViewIndex(state.internal.rows);

            // 1. Consume any pending click; for a flat list the sub-range index is the page index.
            if (state.internal.tableState.selectedRowID.GetMode() == tachyon::IDMode::SubRange)
            {
                const int idx =
                    static_cast<int>(state.internal.tableState.selectedRowID.value.subRangeParts.index);
                if (idx >= 0 && idx < int(state.pages.size()))
                    state.selectedID = state.pages[idx].id;
            }

            // 2. Seed selectedRowID from selectedID (programmatic changes, first frame).
            int selFlat = -1;
            for (size_t i = 0; i < state.pages.size(); ++i)
                if (state.pages[i].id == state.selectedID) { selFlat = int(i); break; }
            if (selFlat >= 0
                && state.internal.tableState.internal.rowsRangeID != tachyon::ID::Invalid())
            {
                const tachyon::ID expected = idManager.GetSubID(
                    state.internal.tableState.internal.rowsRangeID,
                    static_cast<uint32_t>(selFlat));
                if (state.internal.tableState.selectedRowID != expected)
                    state.internal.tableState.selectedRowID = expected;
            }

            const tachyon::ID treeID = idManager.AllocateDynamicID();
            VirtualTreeView(treeID, state.internal.rows, state.internal.visibleIdx,
                            state.internal.tableState, theme.settingsPageSelectionRef,
                            tachyon::LayoutOverride::Auto().FixedWidth(state.treeWidth).NoGrow());
        }

        // Middle: draggable sizer, styled like the dock splitter.
        {
            tachyon::ID sizerID = idManager.AllocateDynamicID();
            tachyon::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerHorzStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = tachyon::MakeHorizontalSizerHandler(
                guiStateManager, elementStorage, state.treeWidth.value, 120, 500, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), tachyon::LayoutOverride::FixedWidth(4));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Right: page content (dock-content layout plus left padding off the sizer).
        tachyon::ID contentID = idManager.AllocateDynamicID();
        tachyon::Element content{};
        content.id       = contentID;
        content.styleRef = theme.settingsPageStyleRef;
        content.state    = guiStateManager.GetCurrentState(contentID);
        auto cRes = elementStorage.Add(std::move(content), tachyon::LayoutOverride::Auto().FlexGrow(1));
        VERIFY(cRes.status);
        VERIFY(windowRegistry.Add(cRes.ref));
        VERIFY(elementStorage.BeginChildren());

        // Render the selected page; a stale selection renders nothing.
        for (const auto& page : state.pages)
        {
            if (page.id == state.selectedID)
            {
                if (page.render) page.render();
                break;
            }
        }

        VERIFY(elementStorage.EndChildren()); // content
        EndHorizontal();                      // split
    }
}