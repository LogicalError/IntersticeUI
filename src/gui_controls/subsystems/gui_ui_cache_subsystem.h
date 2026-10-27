#pragma once

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element.h>

namespace tachyon
{
    class FramePipeline;
    struct FrameContext;
    class ElementStorage;
    class GUIStateManager;
    class ITextSystem;

    /// Retained-render cache for BeginCached/EndCached scopes. On a frame where nothing
    /// relevant changed, the scope's body is skipped and OnRender blits the primitives
    /// captured when it was last rendered.
    ///
    /// This subsystem owns the per-scope state and the invalidation policy; the draw
    /// capture/blit lives in GUIApplication::OnRender. A scope rebuilds when:
    ///   - its container's allotted (inner) size changed       (auto, OnPostLayout)
    ///   - hover/active/focus/capture crossed its element set   (auto, EvaluateHit)
    ///   - Invalidate(id) was called, or an ancestor scope was  (explicit)
    ///   - the optional data signature changed
    ///   - the theme was reloaded                               (global epoch)
    class UICacheManager
    {
    public:
        /// Only what's needed to decide whether a scope is still clean and to blit it;
        /// not the element tree.
        struct Scope
        {
            std::unordered_set<ID> scopeIDs;  ///< every element id in the subtree (membership tests)
            /// Scroll offsets of scroll containers in the scope at capture time. A
            /// change (wheel/drag scroll) invalidates the cache so virtual content
            /// re-emits its visible rows - scroll isn't an interaction-state change.
            std::vector<std::pair<ID, int2>> scrollSnapshot;
            /// Text-editor element ids in the scope (recorded at capture). A scope with a
            /// focused editor rebuilds every frame so the editor's editable-text registration
            /// (typing, caret) re-runs.
            std::unordered_set<ID> textEditorIds;
            /// Swatch color-style ids claimed while building the scope; kept alive on hit frames.
            std::vector<ID> claimedSwatches;

            /// Hovered element at capture time when it is in the scope, else Invalid; the
            /// other snap* fields work the same for active, focus and capture.
            ID snapHover   = ID::Invalid();
            ID snapActive  = ID::Invalid();
            ID snapFocus   = ID::Invalid();
            ID snapCapture = ID::Invalid();

            uint64_t   signature        = 0;   ///< data signature of the last build
            ElementRef lastContainerRef = ElementRef::Invalid(); ///< refreshed every frame in BeginCached
            int2       cachedSize {};          ///< container inner size at capture
            IntRect    cachedContentBounds {}; ///< the built content's bounds, restored on clean frames
            bool       hasCachedSize    = false;
            /// The subtree uses AddPostLayout, so it needs the deferred-measurement settle.
            bool       usesPostLayout   = false;

            bool     captured        = false; ///< a snapshot exists
            bool     nonCacheable    = false; ///< the subtree has a window; always rebuilds
            bool     dirty           = false; ///< forced rebuild (Invalidate / size change / propagation)
            bool     missedThisFrame = false; ///< rebuilt (not replayed) this frame
            uint64_t firstCaptureFrame = 0;   ///< frame of first capture (for the deferred-measurement settle)

            ID       parentScopeID = ID::Invalid(); ///< enclosing cached scope (for invalidation propagation)
            uint64_t themeEpoch    = 0;   ///< global epoch at capture; a newer one rebuilds
            uint64_t lastUsedFrame = 0;   ///< last frame BeginCached reached it, for eviction

            /// Draw cache: the container's window-local position when its primitives
            /// were captured, used to translate the blit to its current position.
            int2  drawCapturePos = {};
        };

        UICacheManager(FramePipeline& frame, ElementStorage& storage, GUIStateManager& stateManager);

        /// The scope with `id`, created on first use.
        Scope& GetOrCreate(ID id);
        /// The scope with `id`, or nullptr.
        Scope* Find(ID id);

        /// Per-frame input facts that decide whether a scope may be served from cache.
        struct HitInput
        {
            bool dragging   = false; ///< a drag-and-drop is in progress
            bool keyboard   = false; ///< a key/text event will be dispatched to the focused element this frame
            bool rebuildAll = false; ///< Tab: the tab chain is built from construction, so every scope must run
        };

        /// True when scope `s`, built with data `signature`, can be served from cache this frame.
        bool EvaluateHit(const Scope& s, uint64_t signature, const HitInput& input) const;

        /// Rebuilds scope `id` the next time it is reached, and its enclosing scopes
        /// (an outer hit skips inner content too).
        void Invalidate(ID id);

        /// Bumps the global epoch so every scope rebuilds (theme reload, display scale change).
        void InvalidateAll() { m_themeEpoch++; }

        /// Frame counter, advanced once per frame.
        uint64_t CurrentFrame() const { return m_frame; }
        /// Global epoch that InvalidateAll() bumps.
        uint64_t ThemeEpoch()   const { return m_themeEpoch; }

        /// When off, EvaluateHit always misses and every scope rebuilds (for A/B comparison).
        void  SetDrawCacheEnabled(bool e) { m_drawCacheEnabled = e; }
        bool  IsDrawCacheEnabled() const  { return m_drawCacheEnabled; }
        /// Seconds for transitions to settle after a rebuild before capturing.
        static constexpr float kDrawSettleSecs = 0.15f;

        // Per-frame diagnostics, reset at the start of each frame

        /// Counts a scope served from cache (`hit`) or rebuilt.
        void     RecordResult(bool hit) { if (hit) m_frameHits++; else m_frameMisses++; }
        void     RecordDrawReplay()  { m_frameDrawReplays++; }   ///< a scope's draw blitted from cache
        void     RecordDrawCapture() { m_frameDrawCaptures++; }  ///< a scope's draw (re)captured this frame
        uint32_t FrameHits()         const { return m_frameHits; }
        uint32_t FrameMisses()       const { return m_frameMisses; }
        uint32_t FrameDrawReplays()  const { return m_frameDrawReplays; }
        uint32_t FrameDrawCaptures() const { return m_frameDrawCaptures; }
        size_t   ScopeCount()       const { return m_scopes.size(); }

        /// `id` when scope `s` contains it, else Invalid.
        static ID MemberOrNull(const Scope& s, ID id)
        {
            return s.scopeIDs.find(id) != s.scopeIDs.end() ? id : ID::Invalid();
        }

    private:
        void OnPreFrame();
        void OnPostLayout(ITextSystem* textSystem);
        void OnEndFrame();

        /// Records a rebuilt scope's subtree after layout: element ids, interaction /
        /// scroll / text-editor state and size, for later invalidation checks.
        void CaptureScope(Scope& s, ITextSystem* textSystem);

        std::unordered_map<ID, Scope> m_scopes;

        ElementStorage&  m_storage;
        GUIStateManager& m_stateManager;

        uint64_t m_frame      = 0;
        uint64_t m_themeEpoch = 0;
        uint32_t m_frameHits   = 0;
        uint32_t m_frameMisses = 0;
        uint32_t m_frameDrawReplays  = 0;
        uint32_t m_frameDrawCaptures = 0;
        bool     m_drawCacheEnabled = true;

        /// Scopes not reached for this many frames are evicted.
        static constexpr uint64_t kEvictAfterFrames = 240;

        /// Rebuilds after first capture for scopes using AddPostLayout, whose first
        /// frame is built before last frame's measured size exists.
        static constexpr uint64_t kSettleFrames = 2;
    };
}
