// Regression tests: the dictionary Field's value column must not collapse to
// zero width while the key keeps its border + padding min size.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <unordered_map>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class DictionaryFieldTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            // Walk the element tree depth-first starting at `root`, calling
            // `visit(ref, element, rect, depth)` on each node.
            void Walk(ElementRef root,
                      const std::function<void(ElementRef, const Element&, const ElementRect&, int)>& visit,
                      int depth = 0)
            {
                if (!app.elementStorage.IsValidRef(root)) return;
                auto* e = app.elementStorage.GetElementPtr(root);
                auto* r = app.elementStorage.GetElementRectPtr(root);
                if (e && r) visit(root, *e, *r, depth);
                auto range = app.elementStorage.GetChildRange(root);
                for (uint32_t idx = range.begin; idx < range.end; ++idx)
                    Walk(ElementRef{ root.depth + 1, idx }, visit, depth + 1);
            }

            // Find the LAST element registered with `style` (depth-first order).
            ElementRef FindLastAreaByStyle(StyleRef style)
            {
                ElementRef found = ElementRef::Invalid();
                Walk(app.elementStorage.GetFirstElementRef(),
                     [&](ElementRef ref, const Element& e, const ElementRect&, int) {
                         if (e.styleRef == style) found = ref;
                     });
                return found;
            }

            // Dump a subtree to stdout.
            void DumpSubtree(ElementRef root, const char* label)
            {
                printf("--- %s ---\n", label);
                Walk(root, [](ElementRef ref, const Element& e, const ElementRect& r, int depth) {
                    for (int i = 0; i < depth; ++i) printf("  ");
                    bool ctrl = ((uint32_t)e.flags & (uint32_t)ElementFlags::IsControl) != 0;
                    printf("depth=%u idx=%u  x=%d y=%d w=%d h=%d  clip=(x=%d y=%d w=%d h=%d)  ctrl=%d\n",
                           ref.depth, ref.index,
                           r.rect.x, r.rect.y, r.rect.w, r.rect.h,
                           r.clipRect.x, r.clipRect.y, r.clipRect.w, r.clipRect.h,
                           (int)ctrl);
                });
            }

            // Every control inside `dictArea` must lie within its own clipRect (the
            // effective scissor of all ancestor clips).
            void AssertControlsVisibleInArea(ElementRef dictArea, const char* label)
            {
                int bad = 0;
                int controls = 0;
                Walk(dictArea, [&](ElementRef ref, const Element& e, const ElementRect& r, int) {
                    if (ref == dictArea) return;
                    bool ctrl = ((uint32_t)e.flags & (uint32_t)ElementFlags::IsControl) != 0;
                    if (!ctrl) return;
                    // Only textbox controls, identified by style.
                    if (e.styleRef != app.theme.controls->textbox) return;
                    controls++;
                    int ctrlLeft   = r.rect.x;
                    int ctrlRight  = r.rect.x + r.rect.w;
                    int clipLeft   = r.clipRect.x;
                    int clipRight  = r.clipRect.x + r.clipRect.w;
                    int minBorderBoxW = r.layoutStyle
                        ? (r.layoutStyle->padding.Horizontal() + r.layoutStyle->borderWidths.Horizontal())
                        : 0;
                    bool tooSmall  = r.rect.w < minBorderBoxW;
                    bool clipped   = ctrlRight <= clipLeft || ctrlLeft >= clipRight;
                    bool zeroWidth = tooSmall;
                    if (zeroWidth || clipped)
                    {
                        bad++;
                        printf("  BAD control: rect=[%d..%d] clip=[%d..%d]  zero=%d clipped=%d\n",
                               ctrlLeft, ctrlRight, clipLeft, clipRight,
                               (int)zeroWidth, (int)clipped);
                    }
                });
                EXPECT_EQ(bad, 0) << label << ": found " << bad << " of "
                                  << controls << " controls that are zero-width or clipped";
                if (bad > 0) DumpSubtree(dictArea, label);
            }
        };

        // ---- Tests --------------------------------------------------------

        // The real `Field(vector<pair<string,V>>&, ...)` overload inside a label group
        // + dock content wrapper, as the inspector panel uses it.
        // The unordered_map overload: renaming a key (committed with Enter) moves
        // its value to the new key.
        TEST_F(DictionaryFieldTest, MapOverload_RenamingAKeyKeepsItsValue)
        {
            std::unordered_map<std::string, int> map = { { "ab", 7 } };
            auto draw = [&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    app.Field(map);
                    app.EndArea();
                }
                app.EndRootWindow();
            };
            app.Frame(draw);

            ElementRef area = FindLastAreaByStyle(app.theme.collectionAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid());
            ID keyField = ID::Invalid();
            Walk(area, [&](ElementRef, const Element& e, const ElementRect&, int) {
                if (keyField == ID::Invalid() && e.styleRef == app.theme.controls->textbox) keyField = e.id;
            });
            ASSERT_NE(keyField, ID::Invalid());

            app.ClickAt(app.CenterOf(keyField));
            app.Frame(draw);
            app.InjectKey(KeyCode::Backspace, KeyAction::KeyDown);
            app.Frame(draw);
            app.InjectText("z");
            app.Frame(draw);
            EXPECT_TRUE(map.contains("ab")) << "not renamed before the edit is committed";
            app.InjectKey(KeyCode::Return, KeyAction::KeyDown);
            app.Frame(draw);
            app.Frame(draw);

            ASSERT_TRUE(map.contains("az"));
            EXPECT_EQ(map.at("az"), 7);
            EXPECT_EQ(map.size(), 1u);
        }

        TEST_F(DictionaryFieldTest, ActualOverload_NoZeroWidthControls)
        {
            std::vector<std::pair<std::string, float>> dict = {
                {"speed", 1.0f}, {"health", 100.0f}, {"damage", 25.0f},
            };

            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    app.BeginLabelGroup(120, 40, 300);
                    app.Label("Dictionary");
                    app.Field(dict, tachyon::LayoutOverride{}.FlexGrow(1));
                    app.EndLabelGroup();
                    app.EndArea();
                }
                app.EndRootWindow();
            });

            ElementRef area = FindLastAreaByStyle(app.theme.collectionAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid()) << "could not locate the dictionary BeginArea by style";
            DumpSubtree(area, "ActualOverload subtree");
            AssertControlsVisibleInArea(area, "ActualOverload");
        }

        // Same, across multiple frames (catches dynamic ID drift or state-dependent
        // sizing).
        TEST_F(DictionaryFieldTest, ActualOverload_NoZeroWidthControls_MultiFrame)
        {
            std::vector<std::pair<std::string, float>> dict = {
                {"speed", 1.0f}, {"health", 100.0f}, {"damage", 25.0f},
            };

            auto frame = [&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    app.BeginLabelGroup(120, 40, 300);
                    app.Label("Dictionary");
                    app.Field(dict, tachyon::LayoutOverride{}.FlexGrow(1));
                    app.EndLabelGroup();
                    app.EndArea();
                }
                app.EndRootWindow();
            };

            for (int i = 0; i < 5; ++i)
                app.Frame(frame);

            ElementRef area = FindLastAreaByStyle(app.theme.collectionAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid());
            AssertControlsVisibleInArea(area, "MultiFrame");
        }

        // The full Inspector panel: many widgets before the dictionary, in the same
        // label group + dock content as the showcase.
        TEST_F(DictionaryFieldTest, FullInspectorPanel_NoZeroWidthControls)
        {
            std::string textBox1 = "hello";
            std::string textBox2 = "multi\nline";
            float       fVal     = 3.14f;
            double      dVal     = 2.718;
            int         iVal     = 42;
            bool        toggle   = true;
            std::vector<std::string> stringList = { "a", "b", "c" };
            std::vector<float>       floatList  = { 1.0f, 2.0f, 3.0f };
            std::vector<std::pair<std::string, float>> dict = {
                {"speed", 1.0f}, {"health", 100.0f}, {"damage", 25.0f},
            };

            ID stringListID = app.idManager.CreateStaticID();
            ID floatListID  = app.idManager.CreateStaticID();

            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    app.BeginLabelGroup(120, 40, 300);

                    app.Label("Toggle");
                    app.Field(toggle, toggle ? "ON" : "OFF");

                    app.Label("Text Box");
                    app.Field(textBox1, TextEditBehavior::None);

                    app.Label("Text Box2");
                    app.Field(textBox2, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);

                    app.DragLabel("Float Value");
                    app.Field(fVal);

                    app.DragLabel("Double Value");
                    app.Field(dVal);

                    app.DragLabel("Int Value");
                    app.Field(iVal);

                    app.BeginIndeterminate();
                    app.DragLabel("Indeterminate");
                    app.Field(fVal);
                    app.EndIndeterminate();

                    app.Label("String List");
                    app.Field(stringListID, stringList, tachyon::LayoutOverride{}.FlexGrow(1));

                    app.Label("Float List");
                    app.Field(floatListID, floatList, tachyon::LayoutOverride{}.FlexGrow(1));

                    app.Label("Dictionary");
                    app.Field(dict, tachyon::LayoutOverride{}.FlexGrow(1));

                    app.EndLabelGroup();
                    app.EndArea();
                }
                app.EndRootWindow();
            });

            ElementRef area = FindLastAreaByStyle(app.theme.collectionAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid());
            AssertControlsVisibleInArea(area, "FullInspectorPanel");
        }

        // Reproduce the realistic showcase scenario: the inspector panel is in a
        // narrow dock leaf (~260 px wide), and the user has dragged the label-
        // group splitter so the right column has only ~50 px. With a wide
        // label, the dictionary row's intrinsic widths can equal or exceed
        // the area's content width, leaving no `remaining > 0` space for
        // flex-grow to distribute, and the column-flex KeepWithNext path has
        // no shrink handling - so trailing children (value, X) end up past
        // the area's right edge and get clipped invisible.

        // Headless test passes with 308px each for key and value. But in the
        // real showcase, ":" and "X" are measured with real glyph widths (not the
        // stub's 0/0), and the row already fits at exactly 1px of slack. Any
        // slight change to intrinsic widths overflows the area; with
        // flex-shrink defaulting to 0, the column-flex KeepWithNext row does
        // not shrink, so trailing children (value, then X) get positioned past
        // the area's right edge and clipped.
        //
        // Reproduce that by making the parent area just NARROW enough that
        // the intrinsic widths exceed it. The value field should still be
        // visible (we have FlexGrow(1) on it), but with no shrink it overflows.
        // A row-flex container narrower than its children, with FlexShrink(1) on the
        // textboxes: shrinking must stop at each textbox's padding + border.
        TEST_F(DictionaryFieldTest, ShrinkRespectsTextboxChromeMinWidth)
        {
            std::string key = "k";
            float value = 1.0f;
            ID rowID = app.idManager.CreateStaticID();

            // Room for both textboxes' padding + border and a little more, so they shrink to their chrome.
            const LayoutStyle* textbox = app.styleManager.GetLayoutStyle(app.theme.controls->textbox.layoutStyleIndex);
            ASSERT_NE(textbox, nullptr);
            const int32_t rowWidth = 2 * (textbox->padding.Horizontal() + textbox->borderWidths.Horizontal()) + 8;

            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginHorizontal(rowID, LayoutOverride::FixedWidth(CssPx(rowWidth)).NoGrow()))
                {
                    app.Field(key,   LayoutOverride{}.FlexGrow(1).FlexShrink(1));
                    app.Text (":",   LayoutOverride{}.NoGrow());
                    app.Field(value, LayoutOverride{}.FlexGrow(1).FlexShrink(1));
                    app.Button("X", []() {}, LayoutOverride{}.NoGrow());
                    app.EndHorizontal();
                }
                app.EndRootWindow();
            });

            ElementRef row = app.elementStorage.FindElementRefByID(rowID);
            ASSERT_NE(row, ElementRef::Invalid());
            DumpSubtree(row, "ShrinkRespectsChromeMin");
            AssertControlsVisibleInArea(row, "ShrinkRespectsChromeMin");
        }

        TEST_F(DictionaryFieldTest, NarrowArea_ValueOverflowsToTheRight)
        {
            std::vector<std::pair<std::string, float>> dict = {
                {"speed", 1.0f},
            };

            ID outerID = app.idManager.CreateStaticID();

            // Outer container is fixed-width and JUST big enough that the
            // row's intrinsic widths (key=12 + ":"=8 + value=12 + X=18 + gaps)
            // are guaranteed to fit, but flex-grow should still distribute
            // remaining space. If layout is broken, value/X overflow.
            const int outerWidth = 60;

            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(outerID, app.theme.dockContentStyleRef,
                                  LayoutOverride::FixedWidth(CssPx(outerWidth)).NoGrow()))
                {
                    app.Field(dict, tachyon::LayoutOverride{}.FlexGrow(1));
                    app.EndArea();
                }
                app.EndRootWindow();
            });

            ElementRef area = FindLastAreaByStyle(app.theme.collectionAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid());
            DumpSubtree(area, "NarrowArea subtree");
            AssertControlsVisibleInArea(area, "NarrowArea");
        }

        // ---- BeginHorizontal variants ------------------------------------
        // Same scenarios as above, but each dictionary row is wrapped in a
        // BeginHorizontal (row-flex) container instead of using KeepWithNext
        // in a column-flex container; both layouts must keep the value visible.

        TEST_F(DictionaryFieldTest, NarrowRightColumn_ValueClipped_BeginHorizontal)
        {
            std::vector<std::pair<std::string, float>> dict = {
                {"speed", 1.0f},
            };

            app.Frame([&] {
                app.BeginRootWindow({ 260, 600 });
                if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    app.BeginLabelGroup(200, 40, 300);
                    app.Label("Dictionary");
                    if (app.BeginArea(app.theme.listAreaStyleRef, LayoutOverride{}.FlexGrow(1)))
                    {
                        for (auto& item : dict)
                        {
                            if (app.BeginHorizontal(LayoutOverride{}.FlexGrow(1)))
                            {
                                app.Field(item.first,  LayoutOverride{}.FlexGrow(1));
                                app.Text(":",          LayoutOverride{}.NoGrow());
                                app.Field(item.second, LayoutOverride{}.FlexGrow(1));
                                app.Button("X", []() {}, LayoutOverride{}.NoGrow());
                                app.EndHorizontal();
                            }
                        }
                        app.EndArea();
                    }
                    app.EndLabelGroup();
                    app.EndArea();
                }
                app.EndRootWindow();
            });

            ElementRef area = FindLastAreaByStyle(app.theme.listAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid());
            DumpSubtree(area, "NarrowRightColumn_BeginHorizontal subtree");
            AssertControlsVisibleInArea(area, "NarrowRightColumn_BeginHorizontal");
        }

        TEST_F(DictionaryFieldTest, NarrowArea_ValueOverflowsToTheRight_BeginHorizontal)
        {
            std::vector<std::pair<std::string, float>> dict = {
                {"speed", 1.0f},
            };

            ID outerID = app.idManager.CreateStaticID();
            const int outerWidth = 60;

            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(outerID, app.theme.dockContentStyleRef,
                                  LayoutOverride::FixedWidth(CssPx(outerWidth)).NoGrow()))
                {
                    if (app.BeginArea(app.theme.listAreaStyleRef, LayoutOverride{}.FlexGrow(1)))
                    {
                        for (auto& item : dict)
                        {
                            if (app.BeginHorizontal(LayoutOverride{}.FlexGrow(1)))
                            {
                                app.Field(item.first,  LayoutOverride{}.FlexGrow(1));
                                app.Text(":",          LayoutOverride{}.NoGrow());
                                app.Field(item.second, LayoutOverride{}.FlexGrow(1));
                                app.Button("X", []() {}, LayoutOverride{}.NoGrow());
                                app.EndHorizontal();
                            }
                        }
                        app.EndArea();
                    }
                    app.EndArea();
                }
                app.EndRootWindow();
            });

            ElementRef area = FindLastAreaByStyle(app.theme.listAreaStyleRef);
            ASSERT_NE(area, ElementRef::Invalid());
            DumpSubtree(area, "NarrowArea_BeginHorizontal subtree");
            AssertControlsVisibleInArea(area, "NarrowArea_BeginHorizontal");
        }
    }
}
