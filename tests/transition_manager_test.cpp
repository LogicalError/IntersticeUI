// TransitionManager: each kind of change has its timing, and a change mid-transition carries
// on from what is on screen. Toggles blend their on / off; reduced motion makes it all instant.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_controls/gui_easing.h>
#include <gui_controls/gui_transition_manager.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/style/gui_style.h>

#include <optional>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        constexpr VisualBoxStyleIndex kBox = VisualBoxStyleIndex(1);
        const ID kElement = [] { ID id; id.value.integer = 1; return id; }();
        constexpr float kHoverIn  = TransitionManager::kHoverIn.duration;
        constexpr float kHoverOut = TransitionManager::kHoverOut.duration;

        // How much of `state`'s style the blend shows: 0 none of it, 1 only it.
        float Amount(const TransitionState& r, ElementState state)
        {
            const uint32_t style = ComputeGPUStyleIndex(kBox, state);
            float amount = 0.0f;
            if (r.fromStyleIndex == style) amount += 1.0f - r.blendT;
            if (r.toStyleIndex == style)   amount += r.blendT;
            return amount;
        }

        TEST(Easing, CurvesRunFromStartToEnd)
        {
            for (const CubicBezier& curve : { Easing::Linear, Easing::EaseOut, Easing::EaseInOut })
            {
                EXPECT_FLOAT_EQ(curve(0.0f), 0.0f);
                EXPECT_FLOAT_EQ(curve(1.0f), 1.0f);
                float last = 0.0f;
                for (float t = 0.05f; t < 1.0f; t += 0.05f)
                {
                    EXPECT_GE(curve(t), last) << "never goes back, at " << t;
                    last = curve(t);
                    EXPECT_NEAR(curve.TimeAt(curve(t)), t, 1e-3f) << "TimeAt undoes it, at " << t;
                }
            }
            EXPECT_NEAR(Easing::Linear(0.3f), 0.3f, 1e-4f);
            EXPECT_GT(Easing::EaseOut(0.5f), 0.8f) << "most of the way by half time";
            EXPECT_LT(Easing::EaseInOut(0.2f), 0.2f) << "slow to start";
            EXPECT_GT(Easing::EaseInOut(0.8f), 0.8f) << "slow to end";
        }

        TEST(TransitionManager, EachChangeHasItsTiming)
        {
            using S = ElementState;
            auto timing = [](S from, S to) { return TransitionManager::TimingFor(from, to); };
            EXPECT_EQ(timing(S::None, S::Hover), TransitionManager::kHoverIn);
            EXPECT_EQ(timing(S::Hover, S::None), TransitionManager::kHoverOut);
            EXPECT_EQ(timing(S::Hover, S::Hover | S::Active).duration, 0.0f) << "press";
            EXPECT_EQ(timing(S::Hover | S::Active, S::Hover), TransitionManager::kRelease);
            EXPECT_EQ(timing(S::None, S::Focused), TransitionManager::kFocus);
            EXPECT_EQ(timing(S::Focused, S::None), TransitionManager::kFocus);
            EXPECT_EQ(timing(S::None, S::Selected).duration, 0.0f) << "selection";
            EXPECT_EQ(timing(S::Hover, S::Disabled).duration, 0.0f);
            EXPECT_EQ(timing(S::None, S::Hover | S::Active).duration, 0.0f) << "the press counts over the hover";
            EXPECT_EQ(timing(S::Hover, S::Focused), TransitionManager::kHoverOut) << "the hover counts over the focus";
            EXPECT_EQ(TransitionManager::kToggle.easing, Easing::EaseInOut);
            EXPECT_EQ(TransitionManager::kHoverIn.easing, Easing::EaseOut);
        }

        TEST(TransitionManager, AStateChangeBlendsToTheNewState)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, 0.0f), ElementState::Hover), 0.0f);
            const float halfway = Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, kHoverIn / 2), ElementState::Hover);
            EXPECT_NEAR(halfway, Easing::EaseOut(0.5f), 1e-4f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, kHoverIn), ElementState::Hover), 1.0f);
        }

        // Hover out takes twice as long as hover in.
        TEST(TransitionManager, HoverOutTakesLongerThanHoverIn)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, 0.0f);
            const float at = kHoverIn;
            EXPECT_LT(Amount(transitions.Resolve(kElement, kBox, ElementState::None, at), ElementState::None), 1.0f)
                << "not done after the hover-in time";
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::None, kHoverOut), ElementState::None), 1.0f);
        }

        // A press shows at once, even mid-hover.
        TEST(TransitionManager, AnInstantChangeEndsTheRunningOne)
        {
            TransitionManager transitions;
            const ElementState pressed = ElementState::Hover | ElementState::Active;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            transitions.OnStateChange(kElement, ElementState::Hover, pressed, kHoverIn * 0.3f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, pressed, kHoverIn * 0.3f), pressed), 1.0f);
            EXPECT_FALSE(transitions.Find(kElement, kHoverIn * 0.3f).has_value());
        }

        // Hovering out before the hover blend finished runs back from where it got to,
        // in the hover-out timing, instead of starting from the full hover style.
        TEST(TransitionManager, ReversingMidBlendCarriesOnFromWhereItIs)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const float now = kHoverIn * 0.3f;
            const float shown = Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, now), ElementState::Hover);
            ASSERT_GT(shown, 0.0f);
            ASSERT_LT(shown, 1.0f);

            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, now);
            EXPECT_NEAR(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now), ElementState::Hover), shown, 1e-4f)
                << "no jump when it turns around";

            // It runs back down, and is done sooner than a whole hover out.
            const float later = Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + kHoverOut * 0.1f), ElementState::Hover);
            EXPECT_LT(later, shown);
            const float left = (1.0f - Easing::EaseOut.TimeAt(1.0f - shown)) * kHoverOut;
            ASSERT_LT(left, kHoverOut);
            EXPECT_GT(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + left * 0.9f), ElementState::Hover), 0.0f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + left + 1e-4f), ElementState::Hover), 0.0f);
        }

        // In and out several times quickly: never a jump.
        TEST(TransitionManager, RepeatedReversalsNeverJump)
        {
            TransitionManager transitions;
            ElementState state = ElementState::None;
            float now = 0.0f;
            float shown = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                const ElementState next = state == ElementState::None ? ElementState::Hover : ElementState::None;
                transitions.OnStateChange(kElement, state, next, now);
                EXPECT_NEAR(Amount(transitions.Resolve(kElement, kBox, next, now), ElementState::Hover), shown, 1e-4f) << "reversal " << i;
                state = next;
                now += kHoverIn * 0.35f;
                shown = Amount(transitions.Resolve(kElement, kBox, state, now), ElementState::Hover);
            }
        }

        // A third state mid-blend can only start from one of the two styles: the one the
        // blend is closer to.
        TEST(TransitionManager, AThirdStateStartsFromTheNearerStyle)
        {
            const ElementState focused = ElementState::Hover | ElementState::Focused;
            for (const float at : { 0.05f, 0.8f })
            {
                TransitionManager transitions;
                transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
                const float now = kHoverIn * at;
                transitions.OnStateChange(kElement, ElementState::Hover, focused, now);
                const TransitionState r = transitions.Resolve(kElement, kBox, focused, now);
                const ElementState nearer = Easing::EaseOut(at) < 0.5f ? ElementState::None : ElementState::Hover;
                EXPECT_FLOAT_EQ(Amount(r, nearer), 1.0f) << "at " << at;
            }
        }

        TEST(TransitionManager, ReducedMotionMakesEveryChangeInstant)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            ASSERT_TRUE(transitions.Find(kElement, 0.01f).has_value());
            transitions.SetReducedMotion(true);
            EXPECT_FALSE(transitions.Find(kElement, 0.01f).has_value()) << "the running one ends";
            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, 0.02f);
            EXPECT_FALSE(transitions.Find(kElement, 0.02f).has_value());
            transitions.OnSelectedSeen(kElement, ElementState::None, 0.03f);
            transitions.Purge(0.03f);
            transitions.OnSelectedSeen(kElement, ElementState::Selected, 0.04f);
            EXPECT_FALSE(transitions.Find(kElement, 0.04f).has_value()) << "a toggle too";

            transitions.SetReducedMotion(false);
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.05f);
            EXPECT_TRUE(transitions.Find(kElement, 0.06f).has_value());
        }

        // An element that animates its Selected blends when it flips between frames; the first
        // frame it's seen, or after a frame it wasn't built, it shows as it is.
        TEST(TransitionManager, ASelectedFlipBlendsLikeAToggle)
        {
            TransitionManager transitions;
            transitions.OnSelectedSeen(kElement, ElementState::Selected, 0.0f);
            EXPECT_FALSE(transitions.Find(kElement, 0.0f).has_value()) << "first seen";
            transitions.Purge(0.0f);

            transitions.OnSelectedSeen(kElement, ElementState::None, 0.1f);
            const auto blend = transitions.Find(kElement, 0.1f + TransitionManager::kToggle.duration / 2);
            ASSERT_TRUE(blend.has_value());
            EXPECT_EQ(blend->fromState, ElementState::Selected);
            EXPECT_EQ(blend->toState, ElementState::None);
            EXPECT_NEAR(blend->t, Easing::EaseInOut(0.5f), 1e-4f);
            transitions.Purge(0.1f);

            transitions.OnSelectedSeen(kElement, ElementState::None, 0.12f);
            EXPECT_TRUE(transitions.Find(kElement, 0.12f).has_value()) << "same value: carries on";
            transitions.Purge(0.12f);
            transitions.Purge(0.13f);   // a frame it wasn't built
            transitions.Purge(0.5f);
            transitions.OnSelectedSeen(kElement, ElementState::Selected, 0.5f);
            EXPECT_FALSE(transitions.Find(kElement, 0.5f).has_value()) << "not built the frame before";
        }

        // ---- In the controls ----

        // The elements of the last frame marked AnimatesSelected.
        std::vector<ID> TogglingParts(const HeadlessGUIApp& app)
        {
            std::vector<ID> ids;
            for (uint32_t depth = 0; app.elementStorage.IsValidRef(ElementRef{ .depth = depth, .index = 0 }); ++depth)
                for (uint32_t index = 0; app.elementStorage.IsValidRef(ElementRef{ .depth = depth, .index = index }); ++index)
                {
                    const Element& e = app.elementStorage.GetElementUnsafe(ElementRef{ .depth = depth, .index = index });
                    if ((e.flags & ElementFlags::AnimatesSelected) != ElementFlags::None)
                        ids.push_back(e.id);
                }
            return ids;
        }

        class ToggleTransitionTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID control{};
            bool value = false;
            enum class Kind { Switch, Checkbox, Toggle } kind = Kind::Switch;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.transitionManager.SetEnabled(true);
                control = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    switch (kind)
                    {
                        case Kind::Switch:   app.Switch(control, value, "Switch", [](bool) {}); break;
                        case Kind::Checkbox: app.Checkbox(control, value, "Check", [](bool) {}); break;
                        case Kind::Toggle:   app.Toggle(control, value, GUIContent{ "Toggle" }, [](bool) {}); break;
                    }
                    app.EndRootWindow();
                });
            }

            float Now() const { return app.guiStateManager.Interaction().GetFrameTime(); }

            // The blend of the part that turns on or off, if it's blending.
            std::optional<TransitionManager::Blend> PartBlend()
            {
                const std::vector<ID> parts = TogglingParts(app);
                EXPECT_EQ(parts.size(), 1u);
                return parts.empty() ? std::nullopt : app.transitionManager.Find(parts[0], Now());
            }
        };

        TEST_F(ToggleTransitionTest, TurningOnBlendsFor120ms)
        {
            for (const Kind k : { Kind::Switch, Kind::Checkbox, Kind::Toggle })
            {
                kind = k;
                value = false;
                Frame();
                Frame();
                EXPECT_FALSE(PartBlend().has_value()) << int(k);

                value = true;
                Frame();
                auto blend = PartBlend();
                ASSERT_TRUE(blend.has_value()) << int(k);
                EXPECT_EQ(blend->toState & ElementState::Selected, ElementState::Selected);
                EXPECT_FLOAT_EQ(blend->t, 0.0f);

                app.AdvanceTime(60);
                Frame();
                blend = PartBlend();
                ASSERT_TRUE(blend.has_value()) << int(k);
                EXPECT_GT(blend->t, 0.0f);
                EXPECT_LT(blend->t, 1.0f);

                app.AdvanceTime(61);
                Frame();
                EXPECT_FALSE(PartBlend().has_value()) << int(k) << ": done after 120 ms";
                app.AdvanceTime(1000);
            }
        }

        // Records each box with both its styles and the blend between them.
        class BlendRecorder : public GUITest::StubGUIRenderer
        {
        public:
            struct Drawn { IntRect rect; uint32_t from, to; float t; };
            std::vector<Drawn> drawn;
            void RenderBox(IntRect rect, IntRect, uint32_t from, uint32_t to, float t,
                           VisualColorStyleIndex, ImageIndex, IntRect) override
            {
                drawn.push_back({ rect, from, to, t });
            }
        };

        // The switch's knob, drawn by its track: where it is and how its two styles mix.
        TEST_F(ToggleTransitionTest, TheSwitchKnobSlidesAndChangesColorWithTheTrack)
        {
            auto knob = [&]() -> BlendRecorder::Drawn
            {
                const ID track = TogglingParts(app).at(0);
                const ElementRef ref = app.elementStorage.FindElementRefByID(track);
                const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(ref);
                EXPECT_TRUE(draw && draw->callback);
                if (!draw || !draw->callback) return {};
                const IntRect rect = app.GetElementRect(track);
                BlendRecorder r;
                CustomDrawContext ctx{ r, app.elementStorage, ref, rect, rect, IntRect{ 0, 0, 4000, 4000 },
                                       float3x2{}, int2{ 0, 0 } };
                draw->callback(ctx);
                EXPECT_EQ(r.drawn.size(), 1u);
                return r.drawn.empty() ? BlendRecorder::Drawn{} : r.drawn[0];
            };
            const VisualBoxStyleIndex knobStyle = app.theme.switchKnobStyleRef.visualBoxStyleIndex;

            Frame();
            Frame();
            const BlendRecorder::Drawn off = knob();
            EXPECT_EQ(off.from, off.to);
            EXPECT_EQ(off.to, GUIRenderer::StyleIndex(knobStyle, ElementState::None));

            value = true;
            Frame();
            app.AdvanceTime(60);
            Frame();
            const BlendRecorder::Drawn midway = knob();
            EXPECT_EQ(midway.from, GUIRenderer::StyleIndex(knobStyle, ElementState::None));
            EXPECT_EQ(midway.to, GUIRenderer::StyleIndex(knobStyle, ElementState::Selected));
            EXPECT_GT(midway.t, 0.0f);
            EXPECT_LT(midway.t, 1.0f);

            app.AdvanceTime(100);
            Frame();
            const BlendRecorder::Drawn on = knob();
            EXPECT_EQ(on.from, on.to);
            EXPECT_EQ(on.to, GUIRenderer::StyleIndex(knobStyle, ElementState::Selected));
            EXPECT_GT(on.rect.x, off.rect.x);
            EXPECT_GT(midway.rect.x, off.rect.x) << "on its way";
            EXPECT_LT(midway.rect.x, on.rect.x);
            const float travelled = float(midway.rect.x - off.rect.x) / float(on.rect.x - off.rect.x);
            EXPECT_NEAR(travelled, midway.t, 0.15f) << "the knob moves with the color";
        }

        TEST_F(ToggleTransitionTest, AControlShownOnDoesNotBlend)
        {
            value = true;
            Frame();
            EXPECT_FALSE(PartBlend().has_value());
        }

        TEST_F(ToggleTransitionTest, ReducedMotionTurnsOnAtOnce)
        {
            app.SetReducedMotion(true);
            EXPECT_TRUE(app.IsReducedMotion());
            Frame();
            value = true;
            Frame();
            EXPECT_FALSE(PartBlend().has_value());

            app.SetReducedMotion(false);
            EXPECT_FALSE(app.IsReducedMotion());
            value = false;
            Frame();
            EXPECT_TRUE(PartBlend().has_value());

            app.SetReducedMotion(std::nullopt);
            EXPECT_FALSE(app.IsReducedMotion()) << "a headless app has no OS setting";
        }
    }
}
