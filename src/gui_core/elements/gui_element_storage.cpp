#include <gui_core/style/gui_style_manager.h>

#include <gui_core/input/gui_input_handler_func.h>
#include <gui_core/input/gui_input_state.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_data_store.h>
#include <gui_core/diagnostics/gui_logging.h>

namespace tachyon
{
    [[nodiscard]] AddResult ElementStorage::Add(Element &&element, LayoutOverride&& constraints)
    {
        if (!m_constructionModeActive) return AddResult{ .status = ElementStorageResult::NotConstructing };
        if (element.styleRef == StyleRef::Invalid()) return AddResult{ .status = ElementStorageResult::InvalidStyle };

        const LayoutStyle* layoutStyle = m_styleManager.GetLayoutStyle(element.styleRef.layoutStyleIndex);
        if (layoutStyle == nullptr) return AddResult{.status = ElementStorageResult::InvalidStyle};
        constraints.ResolveCss([this](int32_t css) { return m_styleManager.Px(css); });

        uint32_t depth = 0;

        // Roots all get parentIndex 0, which makes them siblings.
        uint32_t parentIndex = 0; 
        if (!m_parentStack.empty())
        {
            auto& back = m_parentStack.back();
            if (element.monitorIndex == -1)
            {
				element.monitorIndex = GetElementUnsafe(back.ref).monitorIndex;
            }
            depth = back.ref.depth + 1;
            parentIndex = back.ref.index;

            while (depth >= m_depths.size()) { m_depths.emplace_back(); }
        }

        if (m_usedDepthCount < depth + 1)
            m_usedDepthCount = depth + 1;

        auto& depthLevel = m_depths[depth];
        uint32_t newIndex = depthLevel.PeekIndex();
        ElementRef newChildRef = ElementRef{ depth, newIndex };

        // CSS sizes are defaults; per-element constraints win.
        {
            const auto& ld = layoutStyle->layoutData;
            if (!constraints.HasFixedWidth()  && ld.width  != LayoutData::kAutoSize)                          constraints.width.value  = ld.width;
            if (!constraints.HasFixedHeight() && ld.height != LayoutData::kAutoSize)                          constraints.height.value = ld.height;
            if (constraints.width.min  == 0                              && ld.minWidth  != 0)                constraints.width.min  = ld.minWidth;
            if (constraints.width.max  == SizeConstraintAxis::kUnbounded && ld.maxWidth  != LayoutData::kUnboundedMax) constraints.width.max  = ld.maxWidth;
            if (constraints.height.min == 0                              && ld.minHeight != 0)                constraints.height.min = ld.minHeight;
            if (constraints.height.max == SizeConstraintAxis::kUnbounded && ld.maxHeight != LayoutData::kUnboundedMax) constraints.height.max = ld.maxHeight;
        }

        // display:none / visibility:collapsed take no space in the parent's flex/grid.
        if (!layoutStyle->IsInFlow())
            constraints.outOfFlow = true;

        ElementRect elementRect
        {
            .rect         = {},
            .clipRect     = {},
            .constraints  = constraints,
            .layoutStyle  = layoutStyle,
			.monitorIndex = element.monitorIndex,
			.flags        = element.flags,
            .display      = layoutStyle->display
        };

        // Keeps this ID's ElementDataStore entry from being evicted.
        if (m_elementDataStore)
            m_elementDataStore->Touch(element.id);

        [[maybe_unused]] uint32_t index = depthLevel.Add(parentIndex, std::move(element), std::move(elementRect));
        assert(newIndex == index);
            
        // Siblings are stored contiguously, so the previous element may be our previous sibling.
        if (newIndex > 0)
        {
            auto& prev = depthLevel.hierarchy[newIndex - 1];
            if (prev.parentIndex == parentIndex)
            {
                prev.nextSiblingIndex = newIndex;
            }
        }

        m_lastAddedRef = newChildRef;
        m_lastAddedID = GetElementUnsafe(newChildRef).id;
        if ((GetElementUnsafe(newChildRef).flags & ElementFlags::IsWindow) == ElementFlags::None)
            m_lastItemID = m_lastAddedID;
        m_childrenBegun = false;

        if (!m_idIndexStale)
            IndexElement(m_lastAddedID, newChildRef);

        m_elementCount++;
        return AddResult{ .ref = newChildRef };
    }

    [[nodiscard]] ElementStorageResult ElementStorage::AddCustomDraw(const ElementRef& ref, CustomDrawCallback callback)
    {
        if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
        if (!IsValidRef(ref)) return ElementStorageResult::UnknownProblem;

        m_depths[ref.depth].elementCustomDraws[ref.index].callback = std::move(callback);
        return ElementStorageResult::Success;
    }

    [[nodiscard]] ElementStorageResult ElementStorage::AddCustomDrawAfter(const ElementRef& ref, CustomDrawCallback callback)
    {
        if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
        if (!IsValidRef(ref)) return ElementStorageResult::UnknownProblem;

        uint32_t newIndex = static_cast<uint32_t>(m_postDrawEntries.size());
        m_postDrawEntries.push_back({ std::move(callback), ref, UINT32_MAX });

        auto& cd = m_depths[ref.depth].elementCustomDraws[ref.index];
        if (cd.afterHead == UINT32_MAX)
            cd.afterHead = newIndex;
        else
            m_postDrawEntries[cd.afterTail].nextIndex = newIndex;
        cd.afterTail = newIndex;

        return ElementStorageResult::Success;
    }

    [[nodiscard]] ElementStorageResult ElementStorage::AddPostLayout(const ElementRef& ref, PostLayoutCallback callback)
    {
        if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
        if (!IsValidRef(ref)) return ElementStorageResult::UnknownProblem;

        m_postLayoutEntries.push_back({ std::move(callback), ref });
        return ElementStorageResult::Success;
    }
        
    const ID ElementStorage::GetElementIDAt(ElementRef root, int2 position) const
    {
        // `pos` is in the parent's content space (where ref.rect/clipRect live).
        // Children are visited topmost-first; the first input-accepting hit wins.
        // overflow:visible elements are descended into even when the cursor is outside
        // them, since children may paint outside (e.g. sockets straddling a node border).
        // NoInput subtrees are transparent; a Disabled element under the cursor absorbs the hit.
        bool aborted = false;

        auto hitTest = [this, &aborted](auto&& self, ElementRef ref, float2 pos) -> ID
        {
            const ElementRect* r = GetElementRectPtr(ref);
            if (r == nullptr) return ID::Invalid();

            // clipRect (in ref's space) gates ref AND its whole subtree.
            if (r->clipRect.w > 0 && r->clipRect.h > 0)
            {
                const IntRect& c = r->clipRect;
                if (pos.x < (float)c.x || pos.x >= (float)(c.x + c.w) ||
                    pos.y < (float)c.y || pos.y >= (float)(c.y + c.h))
                    return ID::Invalid();
            }

            const IntRect& rr = r->rect;
            const bool insideRect = (pos.x >= (float)rr.x && pos.x < (float)(rr.x + rr.w) &&
                                     pos.y >= (float)rr.y && pos.y < (float)(rr.y + rr.h));

            const Element& elem = GetElementUnsafe(ref);
            if ((elem.state & ElementState::Disabled) == ElementState::Disabled)
            {
                if (insideRect) aborted = true;   // disabled absorbs the hit
                return ID::Invalid();
            }
            if ((elem.flags & ElementFlags::NoInput) == ElementFlags::NoInput)
                return ID::Invalid();             // transparent to hit-testing

            const bool overflowVisible =
                r->layoutStyle != nullptr &&
                r->layoutStyle->overflowX == Overflow::Visible &&
                r->layoutStyle->overflowY == Overflow::Visible;

            if ((insideRect || overflowVisible) && HasChildren(ref))
            {
                // Into children's space, pivoting the content transform about the innerRect origin.
                float2 childPos = pos;
                if (!r->contentTransform.IsIdentity())
                {
                    const float2 pivot{ (float)r->innerRect.position.x, (float)r->innerRect.position.y };
                    childPos = pivot + r->contentTransform.Inverse().TransformPoint(pos - pivot);
                }

                // Reverse order: IndexRange is render order, topmost is last.
                const IndexRange range = GetChildRange(ref);
                for (auto i = range.end; i-- > range.begin; )
                {
                    const ID childHit = self(self, ElementRef{ ref.depth + 1, i }, childPos);
                    if (aborted) return ID::Invalid();
                    if (childHit != ID::Invalid()) return childHit;
                }
            }

            return insideRect ? elem.id : ID::Invalid();
        };

        const ID result = hitTest(hitTest, root, float2{ (float)position.x, (float)position.y });
        return aborted ? ID::Invalid() : result;
    }

    int2 ElementEvent::LocalCursor(const ElementStorage& storage) const
    {
        return storage.ScreenToElementSpace(state.screenPos, elementRef);
    }

    int2 ElementStorage::ScreenToElementSpace(int2 screenPos, ElementRef ref) const
    {
        // Ancestors only: ref's own contentTransform applies to its children, not its rect.
        // Inverses are applied root-downward, matching GetElementIDAt.
        thread_local std::vector<ElementRef> ancestors;
        ancestors.clear();
        ElementRef cur = GetParentRef(ref);
        while (cur.IsValid())
        {
            ancestors.push_back(cur);
            cur = GetParentRef(cur);
        }
        float2 p = { (float)screenPos.x, (float)screenPos.y };
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
        {
            const ElementRect& er = GetElementRectUnsafe(*it);
            if (!er.contentTransform.IsIdentity())
            {
                const float2 pivot{ (float)er.innerRect.position.x,
                                    (float)er.innerRect.position.y };
                p = pivot + er.contentTransform.Inverse().TransformPoint(p - pivot);
            }
        }
        return int2{ (int32_t)p.x, (int32_t)p.y };
    }

    [[nodiscard]] ElementStorageResult ElementStorage::BeginChildren()
    {
        if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
        if (m_childrenBegun) return ElementStorageResult::DuplicateCall;
        if (!m_lastAddedRef.IsValid()) return ElementStorageResult::MissingAdd;

        m_childrenBegun = true;
        const ElementRef parentRef = m_lastAddedRef;

        uint32_t childDepth = parentRef.depth + 1;
        while (childDepth >= m_depths.size()) { m_depths.emplace_back(); }

        uint32_t startChildIndex = m_depths[childDepth].Size();
        m_parentStack.push_back(ParentStackEntry
            {
                .ref = parentRef,
                .startChildIndex = startChildIndex,
                .itemBefore = m_lastItemID
            });
        return ElementStorageResult::Success;
    }

    
    [[nodiscard]] ElementStorageResult ElementStorage::EndChildren()
    {
        if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
        if (m_parentStack.empty()) return ElementStorageResult::MissingBeginChildren;

        ParentStackEntry entry = m_parentStack.back();
        m_parentStack.pop_back();

        uint32_t childDepth = entry.ref.depth + 1;
        uint32_t endChildIndex = m_depths[childDepth].Size();
        uint32_t childCount = (endChildIndex >= entry.startChildIndex) ? (endChildIndex - entry.startChildIndex) : 0u;

        HierarchyInfo* parentHierarchy = GetHierarchyInfoPtrUnsafe(entry.ref);
        parentHierarchy->childOffset = entry.startChildIndex;
        parentHierarchy->childCount = childCount;

        // :first-child / :last-child / :only-child: the children are known now. The box and
        // its layout take the variant; the text keeps the style it was registered with.
        if (childCount > 0 && m_styleManager.HasChildStyles())
        {
            auto applyChildStyle = [&](uint32_t index, bool first, bool last)
            {
                const ElementRef ref{ childDepth, index };
                Element& element = GetElementUnsafe(ref);
                const StyleManager::ChildStyleRefs* styles = m_styleManager.GetChildStyles(element.styleRef.layoutStyleIndex);
                if (!styles)
                    return;
                const StyleRef& to = (first && last) ? styles->only : first ? styles->first : styles->last;
                element.styleRef.layoutStyleIndex    = to.layoutStyleIndex;
                element.styleRef.visualBoxStyleIndex = to.visualBoxStyleIndex;
                GetElementRectUnsafe(ref).layoutStyle = m_styleManager.GetLayoutStyle(to.layoutStyleIndex);
            };
            applyChildStyle(entry.startChildIndex, true, childCount == 1);
            if (childCount > 1)
                applyChildStyle(endChildIndex - 1, false, true);
        }

        // The parent, finished, is the item at its level; a window leaves the item as it was.
        const Element& parent = GetElementUnsafe(entry.ref);
        m_lastItemID = (parent.flags & ElementFlags::IsWindow) != ElementFlags::None ? entry.itemBefore : parent.id;

        // Prevents a second BeginChildren on the now-complete element.
        m_lastAddedRef = ElementRef::Invalid();
        m_childrenBegun = false;
        return ElementStorageResult::Success;
    }


    void ElementStorage::RotateDepthLevel(uint32_t depth, uint32_t begin, uint32_t mid, uint32_t end)
    {
        auto& dl = m_depths[depth];
        std::rotate(dl.elements.begin() + begin, dl.elements.begin() + mid, dl.elements.begin() + end);
        std::rotate(dl.elementRects.begin() + begin, dl.elementRects.begin() + mid, dl.elementRects.begin() + end);
        std::rotate(dl.elementCustomDraws.begin() + begin, dl.elementCustomDraws.begin() + mid, dl.elementCustomDraws.begin() + end);
        std::rotate(dl.hierarchy.begin() + begin, dl.hierarchy.begin() + mid, dl.hierarchy.begin() + end);
    }

    void ElementStorage::RebuildSiblingLinks(uint32_t depth, uint32_t rangeBegin, uint32_t rangeEnd, uint32_t parentIndex)
    {
        auto& dl = m_depths[depth];
        for (uint32_t i = rangeBegin; i < rangeEnd; i++)
        {
            dl.hierarchy[i].parentIndex = parentIndex;
            dl.hierarchy[i].nextSiblingIndex = (i + 1 < rangeEnd) ? (i + 1) : HierarchyInfo::kInvalidNextIndex;
        }
    }

    [[nodiscard]] ElementStorageResult ElementStorage::MoveChild(
        uint32_t childDepth, uint32_t rangeBegin,
        uint32_t fromLocal, uint32_t toLocal,
        ElementMove* outMove)
    {
        if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
        if (fromLocal == toLocal) return ElementStorageResult::Success;

        uint32_t absFrom = rangeBegin + fromLocal;
        uint32_t absTo = rangeBegin + toLocal;
        uint32_t lo = std::min(absFrom, absTo);
        uint32_t hi = std::max(absFrom, absTo);
        bool movingForward = absFrom < absTo;
        if (childDepth >= m_usedDepthCount || hi >= m_depths[childDepth].Size())
            return ElementStorageResult::UnknownProblem;

        m_idIndexStale = true; // elements are about to change slots

        ElementMove move{ .childDepth = childDepth, .siblingsBegin = lo, .siblingsEnd = hi + 1 };

        // Block being moved and the affected sibling range, per depth level.
        uint32_t blockBegin = absFrom;
        uint32_t blockEnd = absFrom + 1;
        uint32_t affectedBegin = lo;
        uint32_t affectedEnd = hi + 1;

        for (uint32_t depth = childDepth; depth < m_usedDepthCount; depth++)
        {
            // Collect child ranges before rotating, while hierarchy still points at pre-rotation slots.
            uint32_t nextBlockBegin = UINT32_MAX;
            uint32_t nextBlockEnd = 0;
            uint32_t nextAffectedBegin = UINT32_MAX;
            uint32_t nextAffectedEnd = 0;
            bool hasNextLevel = (depth + 1 < m_usedDepthCount);

            if (hasNextLevel)
            {
                auto& dl = m_depths[depth];
                for (uint32_t i = affectedBegin; i < affectedEnd; i++)
                {
                    auto& h = dl.hierarchy[i];
                    if (h.childCount == 0) continue;

                    uint32_t childStart = h.childOffset;
                    uint32_t childEnd = childStart + h.childCount;

                    nextAffectedBegin = std::min(nextAffectedBegin, childStart);
                    nextAffectedEnd = std::max(nextAffectedEnd, childEnd);

                    if (i >= blockBegin && i < blockEnd)
                    {
                        nextBlockBegin = std::min(nextBlockBegin, childStart);
                        nextBlockEnd = std::max(nextBlockEnd, childEnd);
                    }
                }
            }

            const ElementMove::Rotation rotation = movingForward
                ? ElementMove::Rotation{ depth, absFrom, absFrom + (blockEnd - blockBegin), affectedEnd }
                : ElementMove::Rotation{ depth, affectedBegin, blockBegin, blockEnd };
            RotateDepthLevel(depth, rotation.begin, rotation.mid, rotation.end);
            move.rotations.push_back(rotation);

            // Re-pack childOffset for the affected parents.
            if (hasNextLevel && nextAffectedEnd > nextAffectedBegin)
            {
                uint32_t runningOffset = nextAffectedBegin;
                for (uint32_t i = affectedBegin; i < affectedEnd; i++)
                {
                    auto& h = m_depths[depth].hierarchy[i];
                    h.childOffset = runningOffset;
                    runningOffset += h.childCount;
                }
            }

            if (!hasNextLevel || nextAffectedBegin >= nextAffectedEnd || nextBlockBegin >= nextBlockEnd)
                break;

            blockBegin = nextBlockBegin;
            blockEnd = nextBlockEnd;
            affectedBegin = nextAffectedBegin;
            affectedEnd = nextAffectedEnd;
            absFrom = nextBlockBegin;
        }

        // Rebuild parentIndex and nextSiblingIndex below childDepth.
        for (uint32_t depth = childDepth; depth < m_usedDepthCount; depth++)
        {
            auto& parentDl = m_depths[depth];
            uint32_t nextDepth = depth + 1;
            if (nextDepth >= m_usedDepthCount) break;

            auto& childDl = m_depths[nextDepth];
            uint32_t parentCount = parentDl.Size();
            for (uint32_t pi = 0; pi < parentCount; pi++)
            {
                auto& ph = parentDl.hierarchy[pi];
                for (uint32_t ci = ph.childOffset; ci < ph.childOffset + ph.childCount; ci++)
                {
                    if (ci >= childDl.Size()) break;
                    childDl.hierarchy[ci].parentIndex = pi;
                    childDl.hierarchy[ci].nextSiblingIndex =
                        (ci + 1 < ph.childOffset + ph.childCount) ? (ci + 1) : HierarchyInfo::kInvalidNextIndex;
                }
            }
        }

        // Sibling links at childDepth (all share one parent).
        {
            uint32_t itemLo = std::min(rangeBegin + fromLocal, rangeBegin + toLocal);
            uint32_t itemHi = std::max(rangeBegin + fromLocal, rangeBegin + toLocal) + 1;
            uint32_t parentIdx = m_depths[childDepth].hierarchy[itemLo].parentIndex;
            RebuildSiblingLinks(childDepth, itemLo, itemHi, parentIdx);

            if (itemLo > 0)
            {
                auto& prev = m_depths[childDepth].hierarchy[itemLo - 1];
                if (prev.parentIndex == parentIdx)
                    prev.nextSiblingIndex = itemLo;
            }
        }

        // Stored refs follow their elements to the new slots.
        for (PostLayoutEntry& entry : m_postLayoutEntries)
            entry.ownerRef = move.Remap(entry.ownerRef);
        for (PostDrawEntry& entry : m_postDrawEntries)
            entry.ownerRef = move.Remap(entry.ownerRef);
        if (m_lastAddedRef.IsValid())
            m_lastAddedRef = move.Remap(m_lastAddedRef);

        if (outMove != nullptr)
            *outMove = std::move(move);
        return ElementStorageResult::Success;
    }


    void ElementStorage::AdvanceTopDownDepthScan(DepthScanCursor& cursor) const
    {
        if (cursor.IsDone())
            return;

        cursor.parentIndex++;

        if (cursor.parentIndex >= cursor.parentCountAtDepth)
        {
            cursor.parentDepth++;
            cursor.parentIndex = 0;
            cursor.parentCountAtDepth = DepthSizeOf(cursor.parentDepth);

            if (cursor.parentCountAtDepth == 0)
            {
                cursor.SetDone();
                return;
            }
        }

        cursor.childRange = GetChildRangeUnsafe(cursor.CurrentRef());
    }

    void ElementStorage::AdvanceBottomUpDepthScan(DepthScanCursor &cursor) const
    {
        if (cursor.IsDone())
            return;

        cursor.parentIndex++;

        if (cursor.parentIndex >= cursor.parentCountAtDepth)
        {
            if (cursor.parentDepth == 0)
            {
                cursor.SetDone();
                return;
            }

            cursor.parentDepth--;
            cursor.parentIndex = 0;
            cursor.parentCountAtDepth = DepthSizeOf(cursor.parentDepth);

            if (cursor.parentCountAtDepth == 0)
            {
                cursor.SetDone();
                return;
            }
        }

        cursor.childRange = GetChildRangeUnsafe(cursor.CurrentRef());
    }


    void ElementStorage::ClearStorage()
    {
        for (size_t i = 0, end = static_cast<size_t>(m_usedDepthCount); i < end; i++) { m_depths[i].Clear(); }
        m_postDrawEntries.clear();
        m_postLayoutEntries.clear();
        m_elementCount = 0;
        m_parentStack.clear();
        m_usedDepthCount = 0;
        m_lastAddedRef = ElementRef::Invalid();
        m_lastAddedID = ID::Invalid();
        m_lastItemID = ID::Invalid();
        m_childrenBegun = false;
        m_idIndex.clear();
        m_idIndexStale = false;
    }


    void ElementStorage::DepthLevel::Clear()
    {
        elements.clear();
        elementRects.clear();
        elementCustomDraws.clear();
        hierarchy.clear();
    }

    void ElementStorage::DepthLevel::Reserve(uint32_t capacity)
    {
        elements.reserve(capacity);
        elementRects.reserve(capacity);
        elementCustomDraws.reserve(capacity);
        hierarchy.reserve(capacity);
    }


    void ElementStorage::IndexElement(ID id, ElementRef ref) const
    {
        if (id == ID::Invalid()) return;
        auto [it, inserted] = m_idIndex.try_emplace(id, ref);
        if (inserted) return;
        if ((GetElementUnsafe(ref).flags & ElementFlags::SharesID) == ElementFlags::None)
            Diagnostics::ReportMisuse(Diagnostics::MakeLogCategory("elements"),
                "ElementStorage: two elements share an ID this frame (the first is kept for input and state)");
        // Duplicate ID: keep the shallowest, then earliest, element.
        const ElementRef& held = it->second;
        if (ref.depth < held.depth || (ref.depth == held.depth && ref.index < held.index))
            it->second = ref;
    }

    ElementRef ElementStorage::FindElementRefByID(ID id) const
    {
        if (id == ID::Invalid() || Empty()) return ElementRef::Invalid();

        if (m_idIndexStale)
        {
            m_idIndex.clear();
            for (uint32_t d = 0; d < m_usedDepthCount; ++d)
                for (uint32_t i = 0; i < m_depths[d].Size(); ++i)
                    IndexElement(m_depths[d].elements[i].id, ElementRef{ d, i });
            m_idIndexStale = false;
        }

        const auto it = m_idIndex.find(id);
        return it == m_idIndex.end() ? ElementRef::Invalid() : it->second;
    }

}//namespace tachyon