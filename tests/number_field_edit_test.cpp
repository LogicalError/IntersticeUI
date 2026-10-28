// A number field takes typed input after a click, whatever box it sits in.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <limits>
#include <string_view>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class NumberFieldEdit : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const std::string_view files[] = { "styles.css", "showcase.css" };
            app.SetStyleFiles(files);
            ASSERT_TRUE(app.Initialize());
            fieldID = app.idManager.CreateStaticID();
        }

        // `boxed`: inside the Inspector's typed-field box, with its borderless field style.
        void Emit(float& value, bool boxed)
        {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (boxed)
                {
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("box"), app.theme.GetStyle("typed-field"),
                                        LayoutOverride{}.Width(200));
                    app.Field(fieldID, value, TextEditBehavior::ContinuousUpdate, app.theme.GetStyle("typed-field-input"),
                              {}, LayoutOverride{}.FlexGrow(1));
                    app.EndHorizontal();
                }
                else
                    app.Field(fieldID, value, TextEditBehavior::ContinuousUpdate, LayoutOverride{}.Width(200));
                app.EndRootWindow();
            });
        }

        void TypeInto(float& value, bool boxed)
        {
            Emit(value, boxed);
            app.ClickAt(app.CenterOf(fieldID));
            Emit(value, boxed);
            app.InjectText("42");
            Emit(value, boxed);
            Emit(value, boxed);
        }

        HeadlessGUIApp app;
        ID fieldID{};
    };

    TEST_F(NumberFieldEdit, PlainFieldTakesTypedInput)
    {
        float value = 0.5f;
        TypeInto(value, false);
        EXPECT_NE(value, 0.5f) << "the typed digits change the value";
    }

    TEST_F(NumberFieldEdit, BoxedFieldTakesTypedInput)
    {
        float value = 0.5f;
        TypeInto(value, true);
        EXPECT_NE(value, 0.5f) << "the typed digits change the value";
    }

    // NaN reads as a warning: its text takes the palette's warning text color.
    TEST_F(NumberFieldEdit, NaNShowsInTheWarningColor)
    {
        auto textColor = [&](float value) {
            app.ownedTextSystem.textColors.clear();
            Emit(value, false);
            const auto ref = app.elementStorage.FindElementRefByID(fieldID);
            const auto it = app.ownedTextSystem.textColors.find(app.elementStorage.GetElementUnsafe(ref).textID);
            return it != app.ownedTextSystem.textColors.end() ? it->second : VisualColorStyleIndex::Invalid;
        };
        EXPECT_EQ(textColor(std::numeric_limits<float>::quiet_NaN()), app.theme.nanTextColor);
        EXPECT_EQ(textColor(0.5f), VisualColorStyleIndex::Invalid) << "other values keep their style's color";
    }

    // Dragging a slider field's label changes its value, as for a number field.
    TEST_F(NumberFieldEdit, DraggingASliderFieldsLabelChangesIt)
    {
        const ID labelID = app.idManager.CreateStaticID();
        float volume = 50.0f;
        auto frame = [&] {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                app.BeginLabelGroup(120, 40, 300);
                app.DragLabel(labelID, "Volume");
                app.SliderField(fieldID, volume, 0.0f, 100.0f, "Volume");
                app.EndLabelGroup();
                app.EndRootWindow();
            });
        };
        frame();
        const int2 from = app.CenterOf(labelID);
        app.InjectMouseMove(from);
        frame();
        app.InjectMouseDown(from);
        frame();
        for (int dx : { 20, 40, 60 })
        {
            app.InjectMouseMove({ from.x + dx, from.y }, MouseButtons::Left);
            frame();
        }
        app.InjectMouseUp({ from.x + 60, from.y });
        frame();
        EXPECT_GT(volume, 50.0f);
    }

    TEST_F(NumberFieldEdit, DraggingANumberFieldsLabelChangesIt)
    {
        const ID labelID = app.idManager.CreateStaticID();
        float number = 50.0f;
        bool returned = false, modified = false;
        auto frame = [&] {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                app.BeginLabelGroup(120, 40, 300);
                app.DragLabel(labelID, "Number");
                returned = app.Field(fieldID, number) || returned;
                modified = app.guiStateManager.IsModified() || modified;
                app.EndLabelGroup();
                app.EndRootWindow();
            });
        };
        frame();
        const int2 from = app.CenterOf(labelID);
        app.InjectMouseMove(from);
        frame();
        app.InjectMouseDown(from);
        frame();
        for (int dx : { 20, 40, 60 })
        {
            app.InjectMouseMove({ from.x + dx, from.y }, MouseButtons::Left);
            frame();
        }
        app.InjectMouseUp({ from.x + 60, from.y });
        frame();
        EXPECT_GT(number, 50.0f);
        EXPECT_TRUE(returned) << "the field reports the change";
        EXPECT_TRUE(modified);
    }
}
