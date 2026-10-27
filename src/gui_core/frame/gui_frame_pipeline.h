#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace tachyon
{
    class GUIApplication;
    class IDManager;
    class GUIStateManager;
    class StyleManager;
    class ElementStorage;
    class WindowRegistry;
    class OSWindowManager;
    class TransitionManager;
    class TabChainManager;
    class MenuManager;
    class TooltipManager;
    class GUITheme;
    class ITextSystem;
    class InputManager;
    class IWindowBackend;

    /// Per-frame values plus references to long-lived managers, passed to every stage
    /// callback. Subsystems should keep their own manager references; this is the source
    /// of truth for per-frame values (elapsedTimeSecs).
    struct FrameContext
    {
        GUIApplication&    app;
        IDManager&         idManager;
        GUIStateManager&   guiStateManager;
        StyleManager&      styleManager;
        ElementStorage&    elementStorage;
        WindowRegistry&    windowRegistry;
        OSWindowManager&   windowManager;
        TransitionManager& transitionManager;
        TabChainManager&   tabChainManager;
        MenuManager&       menuManager;
        TooltipManager&    tooltipManager;
        GUITheme&          theme;
        ITextSystem*       textSystem;
        InputManager*      inputManager;
        IWindowBackend*    windowBackend;
        float              elapsedTimeSecs;
    };

    /// The stages of a frame, in run order.
    enum class FrameStage : uint8_t
    {
        PreFrame,        ///< theme reload, synthetic mouse events, per-frame cache clears
        PreConstruct,    ///< after elementStorage.BeginConstruction(), before OnGUI()
        PostConstruct,   ///< after OnGUI() returns, before elementStorage.EndConstruction()
        PreLayout,       ///< after textSystem->Update() and SyncWindowsBeforeLayout, before Layout()
        PostLayout,      ///< after Layout() and per-element AddPostLayout callbacks
        PreInput,        ///< hit-test, hover/active/cursor resolution, transitions
        PostInput,       ///< after inputManager->Process; scroll-offset/rect snapshots
        PreRender,       ///< before OnRender()
        PostRender,      ///< after OnRender(); transition purge
        EndFrame,        ///< before idManager.EndFrame / windowManager.EndFrame
    };

    using StageCallback = std::function<void(FrameContext&)>;

    /// Stage callbacks run in registration order (no priorities): if A must run before B,
    /// construct A first (see GUIApplication::Initialize()).
    ///
    /// Lifetime: subsystems captured by callbacks (typically [this]) must stay alive as
    /// long as the pipeline can run them; declare subsystem members after the pipeline.
    class FramePipeline
    {
    public:
        /// Adds `cb` to the end of that stage's callbacks.
        ///@{
        void OnPreFrame      (StageCallback cb) { m_preFrame.push_back(std::move(cb)); }
        void OnPreConstruct  (StageCallback cb) { m_preConstruct.push_back(std::move(cb)); }
        void OnPostConstruct (StageCallback cb) { m_postConstruct.push_back(std::move(cb)); }
        void OnPreLayout     (StageCallback cb) { m_preLayout.push_back(std::move(cb)); }
        void OnPostLayout    (StageCallback cb) { m_postLayout.push_back(std::move(cb)); }
        void OnPreInput      (StageCallback cb) { m_preInput.push_back(std::move(cb)); }
        void OnPostInput     (StageCallback cb) { m_postInput.push_back(std::move(cb)); }
        void OnPreRender     (StageCallback cb) { m_preRender.push_back(std::move(cb)); }
        void OnPostRender    (StageCallback cb) { m_postRender.push_back(std::move(cb)); }
        void OnEndFrame      (StageCallback cb) { m_endFrame.push_back(std::move(cb)); }
        ///@}

        /// Runs the callbacks of `stage` in registration order.
        void RunStage(FrameStage stage, FrameContext& ctx);

    private:
        std::vector<StageCallback> m_preFrame;
        std::vector<StageCallback> m_preConstruct;
        std::vector<StageCallback> m_postConstruct;
        std::vector<StageCallback> m_preLayout;
        std::vector<StageCallback> m_postLayout;
        std::vector<StageCallback> m_preInput;
        std::vector<StageCallback> m_postInput;
        std::vector<StageCallback> m_preRender;
        std::vector<StageCallback> m_postRender;
        std::vector<StageCallback> m_endFrame;
    };
}
