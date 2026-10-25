// ColorField: swatch button + anchored color-picker popup (mock window backend).
// The open picker is detected by walking storage for the picker style.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>

#include <vector>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class ColorFieldTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            interstice::color4f   color{ 0.2f, 0.4f, 0.8f, 1.0f };

            void SetUp() override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            void EmitField()
            {
                app.BeginRootWindow();
                app.ColorField(color, /*allowHDR*/false,
                               interstice::LayoutOverride{}.Size(120, 24));
                app.EndRootWindow();
            }

            // First element rect carrying `style` (unique to the swatch / picker).
            interstice::IntRect FirstRectWithStyle(interstice::StyleRef style) const
            {
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                {
                    const uint32_t count = app.elementStorage.DepthSizeOf(d);
                    for (uint32_t i = 0; i < count; i++)
                    {
                        interstice::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref))
                            return rp->rect;
                    }
                }
                return {};
            }

            bool HasStyle(interstice::StyleRef style) const
            {
                return FirstRectWithStyle(style).w > 0 || FirstRectWithStyle(style).h > 0;
            }

            bool PickerOpen() const  { return HasStyle(app.theme.colorPickerStyleRef); }
            interstice::IntRect Swatch() const { return FirstRectWithStyle(app.theme.colorSwatchStyleRef); }

            void ClickSwatch()
            {
                const auto s = Swatch();
                const interstice::int2 c{ s.x + s.w / 2, s.y + s.h / 2 };
                app.InjectMouseMove(c);
                app.InjectMouseDown(c);
                app.InjectMouseUp(c);
            }
        };

        TEST_F(ColorFieldTest, RendersSwatch)
        {
            app.Frame([&] { EmitField(); });
            const auto s = Swatch();
            EXPECT_GT(s.w, 0);
            EXPECT_GT(s.h, 0);
            EXPECT_FALSE(PickerOpen()) << "picker is closed until the swatch is clicked";
        }

        // The swatch's color style must be one slot updated in place; a new style per
        // frame leaks slots and wraps the gradient atlas refcount.
        TEST_F(ColorFieldTest, RepeatedFramesDoNotLeakColorStyles)
        {
            // Warm up: the first frames may legitimately create slots.
            for (int i = 0; i < 3; ++i)
                app.Frame([&] { EmitField(); });
            const size_t baseline = app.styleManager.VisualColorStyleCount();

            for (int i = 0; i < 16; ++i)
                app.Frame([&] { EmitField(); });

            EXPECT_EQ(app.styleManager.VisualColorStyleCount(), baseline)
                << "each frame must reuse the swatch's color-style slot, not allocate a new one";
        }

        TEST_F(ColorFieldTest, ClickSwatchOpensPicker)
        {
            app.Frame([&] { EmitField(); });
            ASSERT_FALSE(PickerOpen());

            ClickSwatch();
            app.Frame([&] { EmitField(); }); // dispatch the click
            app.Frame([&] { EmitField(); }); // picker popup lays out

            EXPECT_TRUE(PickerOpen()) << "clicking the swatch opens the picker popup";
        }

        TEST_F(ColorFieldTest, ClickSwatchAgainClosesPicker)
        {
            app.Frame([&] { EmitField(); });
            ClickSwatch();
            app.Frame([&] { EmitField(); });
            app.Frame([&] { EmitField(); });
            ASSERT_TRUE(PickerOpen());

            ClickSwatch();
            app.Frame([&] { EmitField(); });
            app.Frame([&] { EmitField(); });

            EXPECT_FALSE(PickerOpen()) << "clicking the swatch again closes the picker";
        }

        TEST_F(ColorFieldTest, BoundValueSurvivesOpenClose)
        {
            const interstice::color4f original = color;
            app.Frame([&] { EmitField(); });
            ClickSwatch();
            app.Frame([&] { EmitField(); });
            app.Frame([&] { EmitField(); });
            ClickSwatch();
            app.Frame([&] { EmitField(); });
            app.Frame([&] { EmitField(); });

            // Opening and closing the picker without interacting must not
            // mutate the bound value.
            EXPECT_FLOAT_EQ(color.r, original.r);
            EXPECT_FLOAT_EQ(color.g, original.g);
            EXPECT_FLOAT_EQ(color.b, original.b);
            EXPECT_FLOAT_EQ(color.a, original.a);
        }

        // Opening the picker on an HDR colour must not change it: the picker
        // edits hue/saturation/value with the over-range part as brightness.
        TEST_F(ColorFieldTest, OpeningPickerKeepsHdrColor)
        {
            color = interstice::color4f{ 2.0f, 1.0f, 0.5f, 1.0f };
            auto emitHdr = [&] {
                app.BeginRootWindow();
                app.ColorField(color, /*allowHDR*/true, interstice::LayoutOverride{}.Size(120, 24));
                app.EndRootWindow();
            };
            app.Frame(emitHdr);
            ClickSwatch();
            app.Frame(emitHdr);
            app.Frame(emitHdr);
            ASSERT_TRUE(PickerOpen());

            EXPECT_NEAR(color.r, 2.0f, 1e-3f);
            EXPECT_NEAR(color.g, 1.0f, 1e-3f);
            EXPECT_NEAR(color.b, 0.5f, 1e-3f);
        }

        // ---- GradientField ----

        class GradientFieldTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            interstice::Gradient  a, b;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                a.AddStop(interstice::color4f{ 1, 0, 0, 1 }, 0.0f);
                a.AddStop(interstice::color4f{ 0, 0, 1, 1 }, 1.0f);
                b.AddStop(interstice::color4f{ 0, 1, 0, 1 }, 0.0f);
                b.AddStop(interstice::color4f{ 1, 1, 0, 1 }, 1.0f);
            }
            void TearDown() override { app.Cleanup(); }

            void EmitFields()
            {
                app.BeginRootWindow();
                app.GradientField(a, interstice::LayoutOverride{}.Size(120, 24));
                app.GradientField(b, interstice::LayoutOverride{}.Size(120, 24));
                app.EndRootWindow();
            }

            // Swatch rects in emission order.
            std::vector<interstice::IntRect> Swatches() const
            {
                std::vector<interstice::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        interstice::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != app.theme.gradientSwatchStyleRef) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            }
        };

        // Only the field whose swatch was clicked owns the editor: with two fields on screen the
        // other one's gradient must not be overwritten by the open editor's working copy.
        TEST_F(GradientFieldTest, OpeningOneFieldsEditorLeavesTheOtherFieldAlone)
        {
            const interstice::Gradient aBefore = a, bBefore = b;
            app.Frame([&] { EmitFields(); });
            const auto swatches = Swatches();
            ASSERT_EQ(swatches.size(), 2u);

            const interstice::int2 c{ swatches[0].x + swatches[0].w / 2, swatches[0].y + swatches[0].h / 2 };
            app.InjectMouseMove(c);
            app.InjectMouseDown(c);
            app.InjectMouseUp(c);
            for (int i = 0; i < 4; i++) app.Frame([&] { EmitFields(); });

            EXPECT_TRUE(b == bBefore) << "the second field's gradient was overwritten by the first field's editor";
            EXPECT_TRUE(a == aBefore) << "opening the editor must not change the edited gradient";
        }

        // ---- Two ColorFields ----

        class TwoColorFieldsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            interstice::color4f   a{ 1.0f, 0.0f, 0.0f, 1.0f };
            interstice::color4f   b{ 0.0f, 0.0f, 1.0f, 1.0f };
            interstice::color4b   bytes{ 200, 40, 10, 255 };

            void SetUp() override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            void EmitFields()
            {
                app.BeginRootWindow();
                app.ColorField(a, /*allowHDR*/false, interstice::LayoutOverride{}.Size(120, 24));
                app.ColorField(b, /*allowHDR*/false, interstice::LayoutOverride{}.Size(120, 24));
                app.ColorField(bytes, interstice::LayoutOverride{}.Size(120, 24));
                app.EndRootWindow();
            }

            std::vector<interstice::IntRect> RectsWithStyle(interstice::StyleRef style) const
            {
                std::vector<interstice::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        interstice::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            }

            void ClickSwatch(size_t index)
            {
                const auto swatches = RectsWithStyle(app.theme.colorSwatchStyleRef);
                ASSERT_EQ(swatches.size(), 3u);
                const auto& s = swatches[index];
                const interstice::int2 c{ s.x + s.w / 2, s.y + s.h / 2 };
                app.InjectMouseMove(c);
                app.InjectMouseDown(c);
                app.InjectMouseUp(c);
                for (int i = 0; i < 4; i++) app.Frame([&] { EmitFields(); });
            }
        };

        // Only the clicked field owns the picker; the others keep their colors.
        TEST_F(TwoColorFieldsTest, OpeningOnePickerLeavesTheOtherFieldsAlone)
        {
            const interstice::color4f aBefore = a, bBefore = b;
            app.Frame([&] { EmitFields(); });
            ClickSwatch(0);

            EXPECT_EQ(RectsWithStyle(app.theme.colorPickerStyleRef).size(), 1u) << "exactly one picker popup";
            EXPECT_TRUE(b == bBefore) << "the second field was overwritten by the first field's picker";
            EXPECT_TRUE(a == aBefore) << "opening the picker must not change the edited color";
        }

        // The byte-color overload converts through a temporary; opening its
        // picker must start from the bound color, not from a dead stack slot.
        TEST_F(TwoColorFieldsTest, ByteColorPickerStartsFromTheBoundColor)
        {
            const interstice::color4b before = bytes;
            app.Frame([&] { EmitFields(); });
            ClickSwatch(2);

            EXPECT_EQ(RectsWithStyle(app.theme.colorPickerStyleRef).size(), 1u);
            EXPECT_EQ(bytes.r, before.r);
            EXPECT_EQ(bytes.g, before.g);
            EXPECT_EQ(bytes.b, before.b);
            EXPECT_EQ(bytes.a, before.a);
        }

    } // namespace
}
