#include <gui_core/frame/gui_frame_pipeline.h>

#include <gui_core/diagnostics/gui_profiler.h>

namespace tachyon
{
#if TACHYON_PROFILE_ENABLED
    namespace
    {
        // Indexed by FrameStage; each caches its node index after the first hit.
        Diagnostics::ScopeLocation kStageScopes[] =
        {
            { Hash::CalculateConst("PreFrame"),      "PreFrame",      __FILE__, __LINE__, false },
            { Hash::CalculateConst("PreConstruct"),  "PreConstruct",  __FILE__, __LINE__, false },
            { Hash::CalculateConst("PostConstruct"), "PostConstruct", __FILE__, __LINE__, false },
            { Hash::CalculateConst("PreLayout"),     "PreLayout",     __FILE__, __LINE__, false },
            { Hash::CalculateConst("PostLayout"),    "PostLayout",    __FILE__, __LINE__, false },
            { Hash::CalculateConst("PreInput"),      "PreInput",      __FILE__, __LINE__, false },
            { Hash::CalculateConst("PostInput"),     "PostInput",     __FILE__, __LINE__, false },
            { Hash::CalculateConst("PreRender"),     "PreRender",     __FILE__, __LINE__, false },
            { Hash::CalculateConst("PostRender"),    "PostRender",    __FILE__, __LINE__, false },
            { Hash::CalculateConst("EndFrame"),      "EndFrame",      __FILE__, __LINE__, false },
        };
    }
#endif

    void FramePipeline::RunStage(FrameStage stage, FrameContext& ctx)
    {
        TACHYON_PROFILE_SCOPE_REF(kStageScopes[static_cast<size_t>(stage)]);

        const std::vector<StageCallback>* callbacks = nullptr;
        switch (stage)
        {
            case FrameStage::PreFrame:      callbacks = &m_preFrame;      break;
            case FrameStage::PreConstruct:  callbacks = &m_preConstruct;  break;
            case FrameStage::PostConstruct: callbacks = &m_postConstruct; break;
            case FrameStage::PreLayout:     callbacks = &m_preLayout;     break;
            case FrameStage::PostLayout:    callbacks = &m_postLayout;    break;
            case FrameStage::PreInput:      callbacks = &m_preInput;      break;
            case FrameStage::PostInput:     callbacks = &m_postInput;     break;
            case FrameStage::PreRender:     callbacks = &m_preRender;     break;
            case FrameStage::PostRender:    callbacks = &m_postRender;    break;
            case FrameStage::EndFrame:      callbacks = &m_endFrame;      break;
        }
        if (!callbacks) return;
        for (const auto& cb : *callbacks)
            cb(ctx);
    }
}
