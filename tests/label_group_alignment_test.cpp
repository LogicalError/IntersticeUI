// Rows of a label group line up whatever their label is: a plain Label and a
// DragLabel end at the same x (the sizer is one straight line), and their fields
// start at the same x.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <string>

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;

    TEST(LabelGroupAlignment, LabelAndDragLabelRowsLineUp)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID labelID = app.idManager.CreateStaticID(), textID  = app.idManager.CreateStaticID();
        const ID dragID  = app.idManager.CreateStaticID(), floatID = app.idManager.CreateStaticID();
        std::string text = "Editable";
        float value = 0.5f;

        app.Frame([&] {
            app.BeginRootWindow({ 800, 600 });
            if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
            {
                app.BeginLabelGroup(120, 40, 300);
                app.Label(labelID, "Text Box", app.theme.labelStyleRef);
                app.Field(textID, text, TextEditBehavior::ContinuousUpdate, app.theme.controls->textbox);
                app.DragLabel(dragID, "Float Value", app.theme.dragLabelStyleRef);
                app.Field(floatID, value);
                app.EndLabelGroup();
                app.EndArea();
            }
            app.EndRootWindow();
        });

        const IntRect label = app.GetElementRect(labelID), drag = app.GetElementRect(dragID);
        const IntRect textField = app.GetElementRect(textID), floatField = app.GetElementRect(floatID);
        ASSERT_GT(label.w, 0);
        ASSERT_GT(drag.w, 0);
        EXPECT_EQ(label.x + label.w, drag.x + drag.w) << "the sizer runs along the labels' right edge";
        EXPECT_EQ(textField.x, floatField.x) << "the fields start in one column";
    }
}
