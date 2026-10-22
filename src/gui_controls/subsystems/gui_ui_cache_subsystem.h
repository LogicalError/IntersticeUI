#pragma once

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element.h>

namespace GUI
{
    class FramePipeline;
    struct FrameContext;
    class ElementStorage;
    class GUIStateManager;
    class ITextSystem;

    // Retained-render cache for BeginCached/EndCached scopes. On a frame where nothing
    // relevant changed, the scope's body is skipped and OnRender blits the primitives
    // captured when it was last rendered.
    //
    // This subsystem owns the per-scope state and the invalidation policy; the draw
    // capture/blit lives in GUIApplication::OnRender. A scope rebuilds when:
    //   - its container's allotted (inner) size changed       (auto, OnPostLayout)
    //   - hover/active/focus/capture crossed its element set   (auto, EvaluateHit)
    //   - Invalidate(id) was called, or an ancestor scope was  (explicit)
    //   - the optional data signature changed
    //   - the theme was reloaded                               (global epoch)
    class UICacheManager
    {
    public:
        // Only what's needed to decide whether a scope is still clean and to blit it;
        // not the element tree.
        struct Scope
        {
            std::unordered_set<ID> scopeIDs;  // every element id in the subtree (membership tests)
            // Scroll offsets of scroll containers in the scope at capture time. A
            // change (wheel/drag scroll) invalidates the cache so virtual content
            // re-emits its visible rows — scroll isn't an interaction-state change.
            std::vector<std::pair<ID, int2>> scrollSnapshot;
            // Text-editor element ids in the scope (recorded at capture). A focused
            // editor must rebuild every frame so its per-frame editable-text
            // registration re-runs (typing/caret); focus alone doesn't change id.
            std::unordered_set<ID> textEditorIds;
            // Swatch color-style ids claimed while building the scope; kept alive on hit frames.
            std::vector<ID> claimedSwatches;

            ID snapHover   = ID::Invalid(); // in-scope interaction ids at capture time (Invalid = none in scope)
            ID snapActive  = ID::Invalid();
            ID snapFocus   = ID::Invalid();
            ID snapCapture = ID::Invalid();

            uint64_t   signature        = 0;
            ElementRef lastContainerRef = ElementRef::Invalid(); // refreshed every frame in BeginCached
            int2       cachedSize {};
            IntRect    cachedContentBounds {}; // the built content's bounds, restored on clean frames
            bool       hasCachedSize    = false;
            bool       usesPostLayout   = false; // subtree uses AddPostLayout → needs the deferred-measurement settle

            bool     captured        = false; // a snapshot exists
            bool     nonCacheable    = false; // subtree opened a window etc. → always rebuild
            bool     dirty           = false; // forced rebuild (Invalidate / size change / propagation)
            bool     missedThisFrame = false; // rebuilt (not replayed) this frame
            uint64_t firstCaptureFrame = 0;   // frame of first capture (for the deferred-measurement settle)

            ID       parentScopeID = ID::Invalid(); // enclosing cached scope (for invalidation propagation)
            uint64_t themeEpoch    = 0;
            uint64_t lastUsedFrame = 0;

            // Draw cache: the container's window-local position when its primitives
            // were captured, used to translate the blit to its current position.
            int2  drawCapturePos = {};
        };

        UICacheManager(FramePipeline& frame, ElementStorage& storage, GUIStateManager& stateManager);

        // Scope access for GUIApplication.
        Scope& GetOrCreate(ID id);
        Scope* Find(ID id);

        // Per-frame input facts that decide whether a scope may be served from cache.
        struct HitInput
        {
            bool dragging   = false;
            bool keyboard   = false; // a key/text event will be dispatched to the focused element this frame
            bool rebuildAll = false; // Tab: the tab chain is built from construction, so every scope must run
        };

        // Decide whether scope s can be served from cache this frame.
        bool EvaluateHit(const Scope& s, uint64_t signature, const HitInput& input) const;

        // Rebuild id's scope next time it is reached, and its enclosing scopes
        // (an outer hit skips inner content too).
        void Invalidate(ID id);

        // Bump the global epoch so every scope rebuilds (theme reload, display scale change).
        void InvalidateAll() { m_themeEpoch++; }

        uint64_t CurrentFrame() const { return m_frame; }
        uint64_t ThemeEpoch()   const { return m_themeEpoch; }

        void  SetDrawCacheEnabled(bool e) { m_drawCacheEnabled = e; }
        bool  IsDrawCacheEnabled() const  { return m_drawCacheEnabled; }
        // Seconds for transitions to settle after a rebuild before capturing.
        static constexpr float kDrawSettleSecs = 0.15f;

        // Per-frame diagnostics, reset in OnPreFrame.
        void     RecordResult(bool hit) { if (hit) m_frameHits++; else m_frameMisses++; }
        void     RecordDrawReplay()  { m_frameDrawReplays++; }   // a scope's draw blitted from cache
        void     RecordDrawCapture() { m_frameDrawCaptures++; }  // a scope's draw (re)captured this frame
        uint32_t FrameHits()         const { return m_frameHits; }
        uint32_t FrameMisses()       const { return m_frameMisses; }
        uint32_t FrameDrawReplays()  const { return m_frameDrawReplays; }
        uint32_t FrameDrawCaptures() const { return m_frameDrawCaptures; }
        size_t   ScopeCount()       const { return m_scopes.size(); }

        // True iff scopeIDs contains id (helper shared with EvaluateHit semantics).
        static ID MemberOrNull(const Scope& s, ID id)
        {
            return s.scopeIDs.find(id) != s.scopeIDs.end() ? id : ID::Invalid();
        }

    private:
        void OnPreFrame();
        void OnPostLayout(ITextSystem* textSystem);
        void OnEndFrame();

        // Records a rebuilt scope's subtree after layout: element ids, interaction /
        // scroll / text-editor state and size, for later invalidation checks.
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
        // When off, EvaluateHit always misses and every scope rebuilds (for A/B comparison).
        bool     m_drawCacheEnabled = true;

        // Scopes not reached for this many frames are evicted.
        static constexpr uint64_t kEvictAfterFrames = 240;

        // Rebuilds after first capture for scopes using AddPostLayout, whose first
        // frame is built before last frame's measured size exists.
        static constexpr uint64_t kSettleFrames = 2;
    };
}
