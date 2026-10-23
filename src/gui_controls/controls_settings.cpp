#include <gui_controls/gui_application.h>

namespace interstice
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
                        interstice::LayoutOverride::Auto().FlexGrow(1));

        // Left: flat page tree. Selection lives on _tableState as sub-range row IDs and is
        // translated to/from state.selectedID. Consume a click into selectedID before
        // seeding selectedRowID from it, or the seed overwrites the click.
        {
            // Sync page list → flat rows. depth=0 throughout.
            state._rows.clear();
            state._rows.reserve(state.pages.size());
            for (size_t i = 0; i < state.pages.size(); ++i)
            {
                TreeViewRow r{};
                r.id    = state.pages[i].id;
                r.name  = state.pages[i].name;
                r.depth = 0;
                r.icon  = interstice::IconID::Invalid;
                state._rows.push_back(std::move(r));
            }
            RebuildTreeViewIndex(state._rows);

            // 1. Consume any pending click; for a flat list the sub-range index is the page index.
            if (state._tableState.selectedRowID.GetMode() == interstice::IDMode::SubRange)
            {
                const int idx =
                    static_cast<int>(state._tableState.selectedRowID.value.subRangeParts.index);
                if (idx >= 0 && idx < int(state.pages.size()))
                    state.selectedID = state.pages[idx].id;
            }

            // 2. Seed selectedRowID from selectedID (programmatic changes, first frame).
            int selFlat = -1;
            for (size_t i = 0; i < state.pages.size(); ++i)
                if (state.pages[i].id == state.selectedID) { selFlat = int(i); break; }
            if (selFlat >= 0
                && state._tableState._rowsRangeID != interstice::ID::Invalid())
            {
                const interstice::ID expected = idManager.GetSubID(
                    state._tableState._rowsRangeID,
                    static_cast<uint32_t>(selFlat));
                if (state._tableState.selectedRowID != expected)
                    state._tableState.selectedRowID = expected;
            }

            const interstice::ID treeID = idManager.AllocateDynamicID();
            VirtualTreeView(treeID, state._rows, state._visibleIdx,
                            state._tableState, theme.settingsPageSelectionRef,
                            interstice::LayoutOverride::Auto().FixedWidth(styleManager.Px(state.treeWidth)).NoGrow());
        }

        // Middle: draggable sizer, styled like the dock splitter.
        {
            interstice::ID sizerID = idManager.AllocateDynamicID();
            interstice::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerHorzStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = interstice::MakeHorizontalSizerHandler(
                guiStateManager, state.treeWidth, 120, 500, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), interstice::LayoutOverride::FixedWidth(styleManager.Px(4)));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Right: page content (dock-content layout plus left padding off the sizer).
        interstice::ID contentID = idManager.AllocateDynamicID();
        interstice::Element content{};
        content.id       = contentID;
        content.styleRef = theme.settingsPageStyleRef;
        content.state    = guiStateManager.GetCurrentState(contentID);
        auto cRes = elementStorage.Add(std::move(content), interstice::LayoutOverride::Auto().FlexGrow(1));
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