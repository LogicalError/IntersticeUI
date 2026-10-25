#include <gui_controls/subsystems/gui_ui_cache_subsystem.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_scroll_management.h>
#include <gui_core/text/gui_text_system.h>

namespace interstice
{
    UICacheManager::UICacheManager(FramePipeline& frame, ElementStorage& storage, GUIStateManager& stateManager)
        : m_storage(storage)
        , m_stateManager(stateManager)
    {
        frame.OnPreFrame  ([this](FrameContext&)     { OnPreFrame();             });
        frame.OnPostLayout([this](FrameContext& ctx) { OnPostLayout(ctx.textSystem); });
        frame.OnEndFrame  ([this](FrameContext&)     { OnEndFrame();             });
    }

    UICacheManager::Scope& UICacheManager::GetOrCreate(ID id) { return m_scopes[id]; }

    UICacheManager::Scope* UICacheManager::Find(ID id)
    {
        auto it = m_scopes.find(id);
        return it == m_scopes.end() ? nullptr : &it->second;
    }

    bool UICacheManager::EvaluateHit(const Scope& s, uint64_t signature, const HitInput& input) const
    {
        if (!m_drawCacheEnabled)          return false; // caching off -> always rebuild
        if (!s.captured)                  return false;
        if (s.nonCacheable)               return false;
        if (s.dirty)                      return false;
        if (s.themeEpoch != m_themeEpoch) return false;
        if (s.signature != signature)     return false;
        if (input.dragging)               return false;
        if (input.rebuildAll)             return false;

        // Deferred-measurement settle: a scope that uses AddPostLayout reads last
        // frame's measured size, so its first frame is built before that size
        // exists. Force a couple rebuilds after first capture so it settles.
        if (s.usesPostLayout && (m_frame - s.firstCaptureFrame) < kSettleFrames)
            return false;

        const InteractionState& I = m_stateManager.Interaction();
        // Active/capture = a press or drag in progress on an in-scope element; its
        // content typically changes every frame (slider/sizer/reorder/pan/drag), so
        // rebuild while it's held AND on the release frame (snapshot still records it).
        if (MemberOrNull(s, I.GetActiveID())  != ID::Invalid() || s.snapActive  != ID::Invalid()) return false;
        if (MemberOrNull(s, I.GetCaptureID()) != ID::Invalid() || s.snapCapture != ID::Invalid()) return false;
        // Hover/focus only restyle, so rebuild on the transition.
        if (MemberOrNull(s, I.GetHoveredID()) != s.snapHover) return false;
        if (MemberOrNull(s, I.GetFocusedID()) != s.snapFocus) return false;

        // A focused text editor in the scope rebuilds every frame (re-registers
        // editable text for live typing/caret) - focus alone doesn't change which
        // element is focused, so the focus transition check above wouldn't catch it.
        const ID focused = I.GetFocusedID();
        if (focused != ID::Invalid() && s.textEditorIds.find(focused) != s.textEditorIds.end())
            return false;

        // Keys are routed to the focused element, which only exists in a rebuilt scope.
        if (input.keyboard && focused != ID::Invalid() && s.scopeIDs.find(focused) != s.scopeIDs.end())
            return false;

        // A scroll within the scope must rebuild (virtual content re-emits rows).
        ScrollManagement& sm = m_stateManager.GetScrollManagement();
        for (const auto& [id, cached] : s.scrollSnapshot)
        {
            const ScrollOffsetHandle h = sm.GetScrollOffsetHandle(id);
            const int2 cur = sm.IsScrollOffsetHandleValid(h) ? sm.GetScrollOffset(h) : int2{};
            if (cur.x != cached.x || cur.y != cached.y) return false;
        }
        return true;
    }

    void UICacheManager::Invalidate(ID id)
    {
        // Ancestors rebuild too, since an outer hit skips inner content. Takes effect next
        // frame (handlers run after capture); capture clears dirty.
        ID cur = id;
        while (cur != ID::Invalid())
        {
            auto it = m_scopes.find(cur);
            if (it == m_scopes.end()) break;
            it->second.dirty = true;
            ID parent = it->second.parentScopeID;
            if (parent == cur) break;
            cur = parent;
        }
    }

    void UICacheManager::OnPreFrame()
    {
        m_frame++;
        m_frameHits          = 0;
        m_frameMisses        = 0;
        m_frameDrawReplays   = 0;
        m_frameDrawCaptures  = 0;
    }

    void UICacheManager::OnPostLayout(ITextSystem* textSystem)
    {
        for (auto& [id, s] : m_scopes)
        {
            if (s.lastUsedFrame != m_frame) continue;

            if (s.missedThisFrame)
            {
                // Rebuilt this frame: record it for the invalidation checks.
                CaptureScope(s, textSystem);
            }
            else
            {
                // Clean (empty container, blitted in OnRender): nothing touches its scroll
                // handles, so keep them alive or the offset resets on the next rebuild.
                ScrollManagement& sm = m_stateManager.GetScrollManagement();
                for (const auto& [sid, off] : s.scrollSnapshot)
                {
                    (void)off;
                    sm.GetOrCreateScrollOffsetHandle(sid);
                }

                if (s.lastContainerRef.IsValid())
                {
                    ElementRect* er = m_storage.GetElementRectPtr(s.lastContainerRef);
                    if (er && s.hasCachedSize)
                    {
                        // The container is empty, but its blitted content isn't: scrolling
                        // (a wheel this frame) clamps against the built content.
                        er->contentBounds = s.cachedContentBounds;
                        const int2 cur = er->innerRect.size;
                        if (cur.x != s.cachedSize.x || cur.y != s.cachedSize.y)
                            Invalidate(id);
                    }
                }
            }
        }
    }

    void UICacheManager::OnEndFrame()
    {
        // Evict scopes not reached for a while (hidden panels, dead dynamic ids).
        for (auto it = m_scopes.begin(); it != m_scopes.end(); )
        {
            if (m_frame - it->second.lastUsedFrame > kEvictAfterFrames)
                it = m_scopes.erase(it);
            else
                ++it;
        }
    }

    void UICacheManager::CaptureScope(Scope& s, ITextSystem* textSystem)
    {
        s.scopeIDs.clear();
        s.textEditorIds.clear();
        s.nonCacheable   = false;
        s.usesPostLayout = false;

        const ElementRect* containerRect = m_storage.GetElementRectPtr(s.lastContainerRef);
        if (containerRect == nullptr) { s.captured = false; return; }
        if (s.firstCaptureFrame == 0) s.firstCaptureFrame = m_frame; // for the deferred-measurement settle

        s.cachedSize          = containerRect->innerRect.size;
        s.cachedContentBounds = containerRect->contentBounds;
        s.hasCachedSize       = true;

        // Collect the subtree's element ids (membership for the interaction/scroll
        // invalidation checks). A window opened in the subtree can't be replayed from
        // a blit, so mark the scope non-cacheable (always rebuild).
        auto recurse = [&](auto&& self, ElementRef parentRef) -> void
        {
            const IndexRange range = m_storage.GetChildRange(parentRef);
            for (uint32_t i = range.begin; i < range.end; i++)
            {
                const ElementRef childRef{ parentRef.depth + 1, i };
                const Element* e = m_storage.GetElementPtr(childRef);
                if (!e) continue;
                if ((e->flags & ElementFlags::IsWindow) != ElementFlags::None)
                    s.nonCacheable = true;
                s.scopeIDs.insert(e->id);
                if (m_storage.HasChildren(childRef))
                    self(self, childRef);
            }
        };
        recurse(recurse, s.lastContainerRef);

        // A scope using AddPostLayout (deferred size measurement - virtual tables,
        // node view) reads last frame's size, so it needs the EvaluateHit settle.
        for (const auto& entry : m_storage.GetPostLayoutEntries())
        {
            const Element* owner = m_storage.GetElementPtr(entry.ownerRef);
            if (owner && s.scopeIDs.find(owner->id) != s.scopeIDs.end()) { s.usesPostLayout = true; break; }
        }

        const InteractionState& I = m_stateManager.Interaction();
        s.snapHover   = MemberOrNull(s, I.GetHoveredID());
        s.snapActive  = MemberOrNull(s, I.GetActiveID());
        s.snapFocus   = MemberOrNull(s, I.GetFocusedID());
        s.snapCapture = MemberOrNull(s, I.GetCaptureID());

        // Scroll snapshot: any scroll container in the scope (the dock content
        // itself or an inner table body). A wheel/drag scroll changes the offset
        // without changing interaction state, so record it here to invalidate.
        s.scrollSnapshot.clear();
        ScrollManagement& sm = m_stateManager.GetScrollManagement();
        auto recordScroll = [&](ID id)
        {
            const ScrollOffsetHandle h = sm.GetScrollOffsetHandle(id);
            if (sm.IsScrollOffsetHandleValid(h))
                s.scrollSnapshot.push_back({ id, sm.GetScrollOffset(h) });
        };
        if (const Element* containerElem = m_storage.GetElementPtr(s.lastContainerRef))
            recordScroll(containerElem->id);
        for (ID id : s.scopeIDs)
            recordScroll(id);

        // Text editors in the scope: a focused one must rebuild each frame (its
        // editable-text registration is per-frame). Recorded here because the
        // editor is registered during this (miss) frame's construction.
        if (textSystem)
            for (ID id : s.scopeIDs)
                if (textSystem->IsTextEditor(id))
                    s.textEditorIds.insert(id);

        s.captured      = true;
        s.dirty         = false;
        s.themeEpoch    = m_themeEpoch;
        s.lastUsedFrame = m_frame;
    }

}
