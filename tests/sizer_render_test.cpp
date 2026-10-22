// A DragLabel in a label group registers the custom draw for its sizer line.
#include <gtest/gtest.h>
#include "headless_gui_app.h"
#include <gui_core/elements/gui_element_storage.h>

namespace GUI
{
    using GUITest::HeadlessGUIApp;
    
    class SizerRenderTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp()    override { app.Initialize(); }
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
}
