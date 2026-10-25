// A DragLabel in a label group registers the custom draw for its sizer line.
#include <gtest/gtest.h>
#include "headless_gui_app.h"
#include <gui_core/elements/gui_element_storage.h>

namespace interstice
{
    using GUITest::HeadlessGUIApp;
    
    class SizerRenderTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }
    };

    TEST_F(SizerRenderTest, InspectorLabelGetsCustomDraw)
    {
        bool labelHasCustomDraw = false;
        ElementRef capturedLabelRef = ElementRef::Invalid();
        
        app.Frame([&] {
            app.BeginRootWindow({ 800, 600 });
            if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
            {
                app.BeginLabelGroup(120, 40, 300);
                app.DragLabel("Test Label");
                capturedLabelRef = app.elementStorage.GetLastAddedRef();
                app.EndLabelGroup();
                app.EndArea();
            }
            app.EndRootWindow();
        });
        
        ASSERT_NE(capturedLabelRef, ElementRef::Invalid());
        const auto* customDraw = app.elementStorage.GetCustomDraw(capturedLabelRef);
        labelHasCustomDraw = (customDraw != nullptr) && (bool)customDraw->callback;
        EXPECT_TRUE(labelHasCustomDraw) << "Label should have an AddCustomDraw callback for the sizer line";
    }

    // A two-line label next to a tall field (a list) runs the row's height, so its sizer
    // line, hover and drag reach the bottom of the row, not just its text.
    TEST_F(SizerRenderTest, ATwoLineLabelRunsItsRowsHeight)
    {
        const ID rowID   = app.idManager.CreateStaticID();
        const ID labelID = app.idManager.CreateStaticID();
        const ID fieldID = app.idManager.CreateStaticID();
        for (int i = 0; i < 2; i++)
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                app.BeginLabelGroup(120, 40, 300);
                app.BeginHorizontal(rowID);
                app.Label(labelID, "String list", "3 items");
                app.AddBox(fieldID, { 300, 200 }, app.theme.collectionStyleRef);
                app.EndHorizontal();
                app.EndLabelGroup();
                app.EndRootWindow();
            });
        const IntRect label = app.GetElementRect(labelID);
        const IntRect field = app.GetElementRect(fieldID);
        EXPECT_GE(label.y + label.h, field.y + field.h) << "label " << label.y << "+" << label.h
                                                         << ", field " << field.y << "+" << field.h;
    }
}
