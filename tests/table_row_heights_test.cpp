// Table row and header heights the default theme sets: 22 for the profiler (the
// default cell), 24 for the data table and the log, 27 for the shortcuts list,
// headers 26-28.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <string_view>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    // A row is its text line plus the cell's vertical padding and border, as
    // VirtualTable measures it.
    int RowHeight(HeadlessGUIApp& app, StyleRef style)
    {
        const TextID probe = app.textSystem->RegisterText(ID::Invalid(), "Mg", style, ElementState::None);
        int h = app.textSystem->GetTextMeasurement(probe).minHeight;
        if (const LayoutStyle* ls = app.styleManager.GetLayoutStyle(style.layoutStyleIndex))
            h += ls->padding.Vertical() + ls->borderWidths.Vertical();
        return h;
    }

    class TableRowHeights : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const std::string_view files[] = { "styles.css", "showcase.css" };
            app.SetStyleFiles(files);
            ASSERT_TRUE(app.Initialize());
        }
        int Px(int n) { return app.styleManager.Px(n); }
        HeadlessGUIApp app;
    };

    TEST_F(TableRowHeights, ProfilerRowsAre22)
    {
        EXPECT_EQ(RowHeight(app, app.theme.tableCellStyleRef), Px(22));
        EXPECT_EQ(app.theme.tableRowHeight, Px(22));
    }

    TEST_F(TableRowHeights, DataTableRowsAre24)
    {
        EXPECT_EQ(RowHeight(app, app.theme.GetStyle("data-table-cell")), Px(24));
    }

    TEST_F(TableRowHeights, LogRowsAre24)
    {
        EXPECT_EQ(RowHeight(app, app.theme.GetStyle("log-cell")), Px(24));
    }

    TEST_F(TableRowHeights, ShortcutRowsAre27)
    {
        EXPECT_EQ(RowHeight(app, app.theme.GetStyle("shortcut-cell")), Px(27));
    }

    // Number cells sit in the same rows as text cells, so they are as high.
    TEST_F(TableRowHeights, NumberCellsMatchTheirRows)
    {
        EXPECT_EQ(RowHeight(app, app.theme.tableCellNumberStyleRef), RowHeight(app, app.theme.tableCellStyleRef));
        EXPECT_EQ(RowHeight(app, app.theme.GetStyle("data-table-number")), RowHeight(app, app.theme.GetStyle("data-table-cell")));
    }

    TEST_F(TableRowHeights, HeadersAre26To28)
    {
        const int h = RowHeight(app, app.theme.tableHeaderStyleRef);
        EXPECT_GE(h, Px(26));
        EXPECT_LE(h, Px(28));
    }
}
