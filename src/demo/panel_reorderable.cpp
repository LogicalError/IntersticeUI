#include "application.h"

//
// This file is for testing and prototyping a reorderable panel, which can be used for things like tab bars, lists, etc.
//

namespace GUI
{

    void Application::RenderReorderablePanel()
    {
        BeginHorizontalWrap();

        // Reorderable tabs demo (horizontal) — items stretch to fill the bar's vertical space
        {
            Label("Reorderable Tabs");

            static std::vector<std::string> reorderTabs = {"Home", "Settings", "Profile", "Help"};
            if (BeginReorderableGroup(demo.reorderTabsID, ReorderDirection::Horizontal,
                [](int from, int to) {
                    auto item = std::move(reorderTabs[from]);
                    reorderTabs.erase(reorderTabs.begin() + from);
                    reorderTabs.insert(reorderTabs.begin() + to, std::move(item));
                }, theme.emptyHorzStyleRef, GUI::LayoutOverride{}.Height(styleManager.Px(40))))
            {
                for (int i = 0; i < static_cast<int>(reorderTabs.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, reorderTabs[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        // Reorderable list demo (vertical)
        {
            Label("Reorderable List");

            static std::vector<std::string> reorderItems = {"First", "Second", "Third", "Fourth", "Fifth"};
            if (BeginReorderableGroup(demo.reorderListID, ReorderDirection::Vertical,
                [](int from, int to) {
                    auto item = std::move(reorderItems[from]);
                    reorderItems.erase(reorderItems.begin() + from);
                    reorderItems.insert(reorderItems.begin() + to, std::move(item));
                }, GUI::LayoutOverride{}.FixedWidth(styleManager.Px(300))))
            {
                for (int i = 0; i < static_cast<int>(reorderItems.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, reorderItems[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        // Wrapped reorderable demo (horizontal, wraps into rows)
        {
            Label("Wrapped Reorderable (horizontal)");

            static std::vector<std::string> wrappedHorzItems = {
                "Alpha", "Beta", "Gamma", "Delta", "Epsilon",
                "Zeta", "Eta", "Theta", "Iota", "Kappa"
            };
            if (BeginReorderableGroup(demo.reorderWrappedHorzID, ReorderDirection::Horizontal, true,
                [](int from, int to) {
                    auto item = std::move(wrappedHorzItems[from]);
                    wrappedHorzItems.erase(wrappedHorzItems.begin() + from);
                    wrappedHorzItems.insert(wrappedHorzItems.begin() + to, std::move(item));
                }, GUI::LayoutOverride::FixedSize(styleManager.Px(400), styleManager.Px(200))))
            {
                for (int i = 0; i < static_cast<int>(wrappedHorzItems.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, wrappedHorzItems[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        // Wrapped reorderable demo (vertical, wraps into columns)
        {
            Label("Wrapped Reorderable (vertical)");

            static std::vector<std::string> wrappedVertItems = {
                "One", "Two", "Three", "Four", "Five",
                "Six", "Seven", "Eight", "Nine", "Ten"
            };
            if (BeginReorderableGroup(demo.reorderWrappedVertID, ReorderDirection::Vertical, true,
                [](int from, int to) {
                    auto item = std::move(wrappedVertItems[from]);
                    wrappedVertItems.erase(wrappedVertItems.begin() + from);
                    wrappedVertItems.insert(wrappedVertItems.begin() + to, std::move(item));
                }, GUI::LayoutOverride::FixedSize(styleManager.Px(400), styleManager.Px(200))))
            {
                for (int i = 0; i < static_cast<int>(wrappedVertItems.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, wrappedVertItems[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        EndHorizontalWrap();
    }
}