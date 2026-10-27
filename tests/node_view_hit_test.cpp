// Node-view hit-test integration tests: the real node-view geometry (overflow:auto
// canvas with a centring contentTransform, absolutely positioned nodes) driven by
// real mouse events through the app's pipeline. Fields inside a node must be
// hittable at their rendered screen position on either side of (0, 0).

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/gui_base_types.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class NodeViewHitTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            // Created once so hit-test finds the same elements after each rebuild.
            ID canvasID;
            ID negNodeID,  posNodeID;
            ID negFieldID, posFieldID;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                canvasID    = app.idManager.CreateStaticID();
                negNodeID   = app.idManager.CreateStaticID();
                posNodeID   = app.idManager.CreateStaticID();
                negFieldID  = app.idManager.CreateStaticID();
                posFieldID  = app.idManager.CreateStaticID();
            }

            void TearDown() override { app.Cleanup(); }

            static constexpr int  kCanvasW = 800;
            static constexpr int  kCanvasH = 600;
            static constexpr int  kNodeW   = 180;
            static constexpr int2 kNegPos  { -340, -60 };
            static constexpr int2 kPosPos  {  200,  80 };

            // A node at `pos` with a body row holding a clickable field.
            void BuildNode(ID nodeID, ID fieldID, int2 pos, int& clickCounter)
            {
                Element node{};
                node.id       = nodeID;
                node.styleRef = app.theme.nodeStyleRef;
                node.state    = app.guiStateManager.GetCurrentState(nodeID);
                auto nodeRes = app.elementStorage.Add(std::move(node),
                    LayoutOverride{}.FixedWidth(kNodeW).Absolute(pos.x, pos.y));
                ASSERT_EQ(nodeRes.status, ElementStorageResult::Success);
                ASSERT_EQ(app.windowRegistry.Add(nodeRes.ref), tachyon::RegistryResult::Success);
                ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);

                // Body row inside the node - same nesting depth as the
                // showcase (BeginHorizontal -> BeginHorizontal -> Field).
                app.BeginHorizontal(LayoutOverride{}.Auto());
                app.BeginHorizontal(LayoutOverride{}.FlexGrow(1));
                app.Button(fieldID, "Field", [&clickCounter]() { clickCounter++; });
                app.EndHorizontal();
                app.EndHorizontal();

                ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
            }

            // Root window -> canvas (overflow:auto, centring contentTransform) -> nodes at
            // (-340, -60) and (200, 80), each with a body row holding a clickable field.
            // `negClicks` / `posClicks` count handler invocations.
            void BuildScene(int& negClicks, int& posClicks)
            {
                app.BeginRootWindow({ 1920, 1080 });

                // Canvas: full node-view style (overflow:auto, padding 4) plus
                // a centring contentTransform applied AFTER Add().
                Element canvas{};
                canvas.id       = canvasID;
                canvas.styleRef = app.theme.nodeViewStyleRef;
                canvas.state    = app.guiStateManager.GetCurrentState(canvasID);
                auto canvasRes = app.elementStorage.Add(std::move(canvas),
                    LayoutOverride{}.Size(kCanvasW, kCanvasH).Position(0, 0));
                ASSERT_EQ(canvasRes.status, ElementStorageResult::Success);

                // Hand-computed half-size (no lastInnerSize round-trip) so every
                // frame, including the first, uses the same transform.
                const float halfW = (float)((kCanvasW - 8) / 2);  // -8 = 4px padding x 2
                const float halfH = (float)((kCanvasH - 8) / 2);
                app.elementStorage.SetContentTransform(canvasRes.ref,
                    float3x2::Translate(float2{ halfW, halfH }));
                ASSERT_EQ(app.windowRegistry.Add(canvasRes.ref), tachyon::RegistryResult::Success);
                ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);

                BuildNode(negNodeID, negFieldID, kNegPos, negClicks);
                BuildNode(posNodeID, posFieldID, kPosPos, posClicks);

                ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
                app.EndRootWindow();
            }

            // Layout-space point to rendered screen position (contentTransform around
            // the canvas innerRect.position pivot, as the renderer does).
            int2 LayoutToScreen(int2 layoutPt) const
            {
                ElementRef cref = app.elementStorage.FindElementRefByID(canvasID);
                EXPECT_TRUE(cref.IsValid());
                const ElementRect& cr = app.elementStorage.GetElementRectUnsafe(cref);
                const float2 pivot{ (float)cr.innerRect.position.x, (float)cr.innerRect.position.y };
                const float2 lp{ (float)layoutPt.x, (float)layoutPt.y };
                const float2 transformed = pivot + cr.contentTransform.TransformPoint(lp - pivot);
                return int2{ (int)transformed.x, (int)transformed.y };
            }

            int2 FieldScreenCenter(ID fieldID) const
            {
                IntRect r = app.GetElementRect(fieldID);
                EXPECT_GT(r.w, 0) << "field rect has zero width — layout did not run";
                EXPECT_GT(r.h, 0) << "field rect has zero height — layout did not run";
                return LayoutToScreen(int2{ r.x + r.w / 2, r.y + r.h / 2 });
            }
        };

        // Sanity check: both fields exist, have non-zero size, and end up at
        // visibly different LAYOUT positions (one negative, one positive).
        TEST_F(NodeViewHitTest, BothFieldsAreLaidOutWithExpectedSigns)
        {
            int negClicks = 0, posClicks = 0;
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            ASSERT_TRUE(app.HasElement(negFieldID));
            ASSERT_TRUE(app.HasElement(posFieldID));

            const auto negRect = app.GetElementRect(negFieldID);
            const auto posRect = app.GetElementRect(posFieldID);

            EXPECT_GT(negRect.w, 0); EXPECT_GT(negRect.h, 0);
            EXPECT_GT(posRect.w, 0); EXPECT_GT(posRect.h, 0);

            // Per the node positions: negNode is laid out at LAYOUT x = -340 +
            // canvas.rect.x (= 0), so its field's center sits at a NEGATIVE
            // LAYOUT x. posNode sits at POSITIVE LAYOUT x.
            EXPECT_LT(negRect.x, 0)
                << "negative-positioned node's field should have negative LAYOUT x; got " << negRect.x;
            EXPECT_GT(posRect.x, 0)
                << "positive-positioned node's field should have positive LAYOUT x; got " << posRect.x;
        }

        // Both fields render on-screen (positive coordinates) after the centring transform.
        TEST_F(NodeViewHitTest, BothFieldsRenderAtPositiveScreenCoords)
        {
            int negClicks = 0, posClicks = 0;
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            const int2 negCenter = FieldScreenCenter(negFieldID);
            const int2 posCenter = FieldScreenCenter(posFieldID);

            EXPECT_GT(negCenter.x, 0) << "neg-node field renders off-screen (x=" << negCenter.x << ")";
            EXPECT_GT(negCenter.y, 0) << "neg-node field renders off-screen (y=" << negCenter.y << ")";
            EXPECT_GT(posCenter.x, 0) << "pos-node field renders off-screen (x=" << posCenter.x << ")";
            EXPECT_GT(posCenter.y, 0) << "pos-node field renders off-screen (y=" << posCenter.y << ")";
        }

        // Hovering at each field's rendered screen position hovers that field.
        TEST_F(NodeViewHitTest, HoverHitsFieldOnPositiveNode)
        {
            int negClicks = 0, posClicks = 0;
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            const int2 posCenter = FieldScreenCenter(posFieldID);
            app.InjectMouseMove(posCenter);
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredID(), posFieldID)
                << "Hovered ID should be the field on the positive-positioned node. "
                << "Cursor at screen (" << posCenter.x << ", " << posCenter.y << ").";
        }

        TEST_F(NodeViewHitTest, HoverHitsFieldOnNegativeNode)
        {
            int negClicks = 0, posClicks = 0;
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            const int2 negCenter = FieldScreenCenter(negFieldID);
            app.InjectMouseMove(negCenter);
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredID(), negFieldID)
                << "Hovered ID should be the field on the negative-positioned node. "
                << "Cursor at screen (" << negCenter.x << ", " << negCenter.y << ").";
        }

        // Pressing over each field's rendered area invokes that field's handler.
        TEST_F(NodeViewHitTest, ClickReachesFieldOnPositiveNode)
        {
            int negClicks = 0, posClicks = 0;
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            const int2 posCenter = FieldScreenCenter(posFieldID);
            app.ClickAt(posCenter);
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            EXPECT_EQ(posClicks, 1) << "Click on positive-node field did not invoke handler";
            EXPECT_EQ(negClicks, 0) << "Click on positive-node field wrongly invoked negative-node field";
        }

        TEST_F(NodeViewHitTest, ClickReachesFieldOnNegativeNode)
        {
            int negClicks = 0, posClicks = 0;
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            const int2 negCenter = FieldScreenCenter(negFieldID);
            app.ClickAt(negCenter);
            app.Frame([&] { BuildScene(negClicks, posClicks); });

            EXPECT_EQ(negClicks, 1) << "Click on negative-node field did not invoke handler";
            EXPECT_EQ(posClicks, 0) << "Click on negative-node field wrongly invoked positive-node field";
        }

        // Label sizer in a negatively positioned node: grabbing near the label's
        // rendered right edge and dragging changes the shared label width by the
        // screen-space delta.
        TEST_F(NodeViewHitTest, LabelSizerDraggableAtRendererRightEdgeForNegativeNode)
        {
            // A real Label + Field in a label group, so the sizer path is exercised.
            ID labelGroupID = app.idManager.CreateStaticID();
            ID labelID      = app.idManager.CreateStaticID();
            std::string fieldValue = "x";

            auto build = [&] {
                app.BeginRootWindow({ 1920, 1080 });
                Element canvas{};
                canvas.id       = canvasID;
                canvas.styleRef = app.theme.nodeViewStyleRef;
                canvas.state    = app.guiStateManager.GetCurrentState(canvasID);
                auto cv = app.elementStorage.Add(std::move(canvas),
                    LayoutOverride{}.Size(800, 600).Position(0, 0));
                ASSERT_EQ(cv.status, ElementStorageResult::Success);
                app.elementStorage.SetContentTransform(cv.ref,
                    float3x2::Translate(float2{ 396.f, 296.f }));
                ASSERT_EQ(app.windowRegistry.Add(cv.ref), tachyon::RegistryResult::Success);
                ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);

                // One negative-positioned node containing a label-group with
                // a single Label("Name") + Field row.
                Element node{};
                node.id       = negNodeID;
                node.styleRef = app.theme.nodeStyleRef;
                node.state    = app.guiStateManager.GetCurrentState(negNodeID);
                auto n = app.elementStorage.Add(std::move(node),
                    LayoutOverride{}.FixedWidth(180).Absolute(-340, -60));
                ASSERT_EQ(n.status, ElementStorageResult::Success);
                ASSERT_EQ(app.windowRegistry.Add(n.ref), tachyon::RegistryResult::Success);
                ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);
                {
                    app.BeginHorizontal(LayoutOverride{}.Auto());  // row
                    app.BeginLabelGroup(labelGroupID, /*defaultWidth*/60, /*minWidth*/30, /*maxWidth*/120);
                    app.Label(labelID, "Name");
                    app.Field(fieldValue, TextEditBehavior::None);
                    app.EndLabelGroup();
                    app.EndHorizontal();
                }
                ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
                ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
                app.EndRootWindow();
            };

            app.Frame(build);

            // Sanity: label is at a negative LAYOUT x (under canvas's centring
            // transform, its rendered screen x is positive).
            const auto labelRect = app.GetElementRect(labelID);
            ASSERT_GT(labelRect.w, 0) << "label rect has zero width — layout did not run";
            EXPECT_LT(labelRect.x, 0) << "label LAYOUT x should be negative for a node at content (-340, -60)";

            // The label's rendered right edge: where the user grabs the sizer.
            const int2 layoutRightEdge{ labelRect.x + labelRect.w, labelRect.y + labelRect.h / 2 };
            ElementRef cref = app.elementStorage.FindElementRefByID(canvasID);
            const ElementRect& cr = app.elementStorage.GetElementRectUnsafe(cref);
            const float2 pivot{ (float)cr.innerRect.position.x, (float)cr.innerRect.position.y };
            const float2 lp{ (float)layoutRightEdge.x, (float)layoutRightEdge.y };
            const float2 screen = pivot + cr.contentTransform.TransformPoint(lp - pivot);
            const int2 rightEdgeScreen{ (int)screen.x, (int)screen.y };

            // Drag the sizer from 2px-inside the rendered right edge by +25 px.
            const int2 dragStart{ rightEdgeScreen.x - 2, rightEdgeScreen.y };
            const int2 dragEnd  { rightEdgeScreen.x - 2 + 25, rightEdgeScreen.y };

            app.InjectMouseMove(dragStart);
            app.Frame(build);            // frame 1 - handler sets edgeHovered
            app.InjectMouseDown(dragStart);
            app.Frame(build);            // frame 2 - handler starts drag
            app.InjectMouseMove(dragEnd, MouseButtons::Left);
            app.Frame(build);            // frame 3 - handler updates labelWidth
            app.InjectMouseUp(dragEnd);
            app.Frame(build);

            // Wider by roughly the 25px screen delta; still 60 means the sizer never triggered.
            const auto labelRectAfter = app.GetElementRect(labelID);
            EXPECT_GT(labelRectAfter.w, labelRect.w)
                << "label sizer drag did not widen the label group; "
                << "before=" << labelRect.w << " after=" << labelRectAfter.w
                << ". Sizer hit area may be misplaced under the canvas's contentTransform.";
            EXPECT_NEAR(labelRectAfter.w, labelRect.w + 25, 3)
                << "label sizer drag changed width but not by the screen-space delta; "
                << "expected approximately " << (labelRect.w + 25)
                << ", got " << labelRectAfter.w;
        }

        // Negative and positive nodes must behave identically; a difference means
        // asymmetric coordinate handling.
        TEST_F(NodeViewHitTest, HitTestBehaviourIsSymmetricAcrossOrigin)
        {
            int negClicks = 0, posClicks = 0;

            app.Frame([&] { BuildScene(negClicks, posClicks); });

            const int2 negCenter = FieldScreenCenter(negFieldID);
            app.InjectMouseMove(negCenter);
            app.Frame([&] { BuildScene(negClicks, posClicks); });
            const ID negHover = app.guiStateManager.Interaction().GetHoveredID();

            const int2 posCenter = FieldScreenCenter(posFieldID);
            app.InjectMouseMove(posCenter);
            app.Frame([&] { BuildScene(negClicks, posClicks); });
            const ID posHover = app.guiStateManager.Interaction().GetHoveredID();

            const bool negReachable = (negHover == negFieldID);
            const bool posReachable = (posHover == posFieldID);

            EXPECT_EQ(negReachable, posReachable)
                << "Hit-test behaviour differs between negative and positive sides of the origin. "
                << "negReachable=" << negReachable << " posReachable=" << posReachable
                << ". Both must be reachable (or both unreachable for the same reason). "
                << "Asymmetry indicates a coordinate-handling bug in the layout/hit-test pipeline.";
            EXPECT_TRUE(negReachable && posReachable)
                << "Neither side should be unreachable.";
        }
    }
}
