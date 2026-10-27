// Unit tests for FramePipeline, the subsystem stage dispatcher:
//   - RunStage(X) invokes only the callbacks registered for stage X
//   - Within a stage, callbacks fire in registration order
//   - Multiple callbacks per stage are all invoked
//   - Empty stages are silent (no crash, no side effect)
//   - The FrameContext reference is threaded through unchanged

#include <gtest/gtest.h>

#include <gui_core/frame/gui_frame_pipeline.h>

#include "headless_gui_app.h"

#include <string>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // FrameContext bound to the harness; the callbacks don't use the manager
        // refs, but its reference members need real lvalues.
        FrameContext MakeCtx(HeadlessGUIApp& app, float elapsed = 0.0f)
        {
            return FrameContext{
                .app               = app,
                .idManager         = app.idManager,
                .guiStateManager   = app.guiStateManager,
                .styleManager      = app.styleManager,
                .elementStorage    = app.elementStorage,
                .windowRegistry    = app.windowRegistry,
                .windowManager     = *app.windowManager,
                .transitionManager = app.transitionManager,
                .tabChainManager   = app.tabChainManager,
                .menuManager       = app.menuManager,
                .tooltipManager    = app.tooltipManager,
                .theme             = app.theme,
                .textSystem        = app.textSystem,
                .inputManager      = app.inputManager.get(),
                .windowBackend     = app.windowBackend,
                .elapsedTimeSecs   = elapsed,
            };
        }
    }

    TEST(FramePipelineTest, EmptyStageIsNoOp)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        // No callbacks registered - every stage must run cleanly.
        pipeline.RunStage(FrameStage::PreFrame,      ctx);
        pipeline.RunStage(FrameStage::PreConstruct,  ctx);
        pipeline.RunStage(FrameStage::PostConstruct, ctx);
        pipeline.RunStage(FrameStage::PreLayout,     ctx);
        pipeline.RunStage(FrameStage::PostLayout,    ctx);
        pipeline.RunStage(FrameStage::PreInput,      ctx);
        pipeline.RunStage(FrameStage::PostInput,     ctx);
        pipeline.RunStage(FrameStage::PreRender,     ctx);
        pipeline.RunStage(FrameStage::PostRender,    ctx);
        pipeline.RunStage(FrameStage::EndFrame,      ctx);
    }

    TEST(FramePipelineTest, RunStageInvokesOnlyMatchingStage)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        std::vector<std::string> log;
        pipeline.OnPreFrame   ([&](FrameContext&) { log.push_back("pre"); });
        pipeline.OnPostLayout ([&](FrameContext&) { log.push_back("post"); });
        pipeline.OnEndFrame   ([&](FrameContext&) { log.push_back("end"); });

        pipeline.RunStage(FrameStage::PreFrame, ctx);
        EXPECT_EQ(log, (std::vector<std::string>{ "pre" }));

        pipeline.RunStage(FrameStage::PreLayout, ctx); // not registered, must not fire
        EXPECT_EQ(log, (std::vector<std::string>{ "pre" }));

        pipeline.RunStage(FrameStage::PostLayout, ctx);
        EXPECT_EQ(log, (std::vector<std::string>{ "pre", "post" }));

        pipeline.RunStage(FrameStage::EndFrame, ctx);
        EXPECT_EQ(log, (std::vector<std::string>{ "pre", "post", "end" }));
    }

    TEST(FramePipelineTest, CallbacksFireInRegistrationOrder)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        std::vector<int> log;
        // Three callbacks on the same stage, registered in order 0, 1, 2.
        pipeline.OnPreConstruct([&](FrameContext&) { log.push_back(0); });
        pipeline.OnPreConstruct([&](FrameContext&) { log.push_back(1); });
        pipeline.OnPreConstruct([&](FrameContext&) { log.push_back(2); });

        pipeline.RunStage(FrameStage::PreConstruct, ctx);
        EXPECT_EQ(log, (std::vector<int>{ 0, 1, 2 }));

        // Re-running the same stage fires the callbacks again in the same order.
        pipeline.RunStage(FrameStage::PreConstruct, ctx);
        EXPECT_EQ(log, (std::vector<int>{ 0, 1, 2, 0, 1, 2 }));
    }

    TEST(FramePipelineTest, FrameContextIsThreadedThrough)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app, /*elapsed*/ 1.5f);

        FramePipeline pipeline;
        const FrameContext* seen = nullptr;
        float                seenElapsed = -1.0f;
        pipeline.OnPreFrame([&](FrameContext& c) {
            seen        = &c;
            seenElapsed = c.elapsedTimeSecs;
        });

        pipeline.RunStage(FrameStage::PreFrame, ctx);
        EXPECT_EQ(seen, &ctx);
        EXPECT_FLOAT_EQ(seenElapsed, 1.5f);
    }

    TEST(FramePipelineTest, StagesAreIndependent)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto ctx = MakeCtx(app);

        FramePipeline pipeline;
        int preCalls  = 0;
        int postCalls = 0;
        pipeline.OnPreLayout ([&](FrameContext&) { ++preCalls;  });
        pipeline.OnPostLayout([&](FrameContext&) { ++postCalls; });

        pipeline.RunStage(FrameStage::PreLayout, ctx);
        EXPECT_EQ(preCalls,  1);
        EXPECT_EQ(postCalls, 0);

        pipeline.RunStage(FrameStage::PostLayout, ctx);
        EXPECT_EQ(preCalls,  1);
        EXPECT_EQ(postCalls, 1);

        pipeline.RunStage(FrameStage::PreLayout, ctx);
        pipeline.RunStage(FrameStage::PreLayout, ctx);
        EXPECT_EQ(preCalls,  3);
        EXPECT_EQ(postCalls, 1);
    }
}
