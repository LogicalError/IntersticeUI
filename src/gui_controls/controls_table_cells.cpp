// Body cells:
//   - Cell(text), Cell(lambda), TreeCell - called from a column's generateCell;
//     allocate IDs, resolve selection, and emit into the open body section.
//   - EmitDirectCell{Text,Custom,Tree} - emit one cell element (plus children).
//
// Header cells, row backdrops, sizers and the surrounding section/scrollbar
// geometry live in controls_table_virtual.cpp.

#include "controls_table_internal.h"

#include <algorithm>
#include <cassert>
#include <string>

namespace interstice
{
    using namespace interstice::TableInternal;

    namespace
    {
        // 300 ms double-click toggle for `*isOpen`. Cell mode only - in
        // row mode the equivalent lives in BuildRowBackdropHandler (the
        // tree cell shares rowID with the backdrop, which catches dispatch first).
        interstice::EventHandler MakeTreeCellDoubleClickToggle(DoubleClickTracker* tracker, bool* isOpen, interstice::EventHandler inner)
        {
            return [tracker, isOpen, inner = std::move(inner)](interstice::ElementEvent&& event) -> bool
            {
                if (event.type == interstice::EventType::MouseDown && event.state.LeftPressed())
                {
                    const auto r = tracker->Update(event.id);
                    if (r.sameAsPrevious && r.elapsedMs < 300.0f && isOpen)
                    {
                        *isOpen = !*isOpen;
                        tracker->Reset();
                        return true;
                    }
                }
                if (inner) return inner(std::move(event));
                return false;
            };
        }

        // Recording mode: store one replay closure at (currentCol, current
        // row), padding skipped cells, then advance to the next column.
        // EndTable replays the closures through VirtualTable's row loop.
        void RecordReplayCell(interstice::TableEmit& emit, std::function<void()> replay)
        {
            assert(emit.rowCount > 0 && "Cell() outside BeginRow/EndRow");
            assert(emit.currentCol < int(emit.columnCount) && "more Cell() calls than columns");
            auto& col = emit.columnCells[size_t(emit.currentCol)];
            col.resize(size_t(emit.rowCount));
            col.back() = std::move(replay);
            emit.currentCol++;
        }
    }

    // ---- Tree-cell child emitters: each emits one flex-row child of the tree cell ----

    // Indent spacer (depth>0). Pushes arrow/icon/text rightward.
    void EmitTreeCellIndent(GUIApplication& app, int depth)
    {
        if (depth <= 0) return;
        interstice::Element spacer{};
        spacer.id       = app.idManager.AllocateDynamicID();
        spacer.styleRef = app.theme.treeTableIndentStyleRef;
        spacer.flags    = interstice::ElementFlags::NoInput;
        auto res = app.elementStorage.Add(std::move(spacer),
            interstice::LayoutOverride::FixedWidth(depth * app.theme.treeTableIndent));
        VERIFY(res.status);
        VERIFY(app.windowRegistry.Add(res.ref));
    }

    // Arrow (branch, isOpen != nullptr) or invisible spacer (leaf).
    // Both use the arrow style's width so text-column X is the same on branch + leaf rows.
    // The arrow uses a flex wrapper + inner glyph for a larger hit target.
    void EmitTreeCellArrowOrLeafSpacer(GUIApplication& app, bool* isOpen)
    {
        if (isOpen)
        {
            interstice::ID arrowID = app.idManager.AllocateDynamicID();
            interstice::Element arrow{};
            arrow.id       = arrowID;
            arrow.styleRef = app.theme.treeTableArrowStyleRef;
            arrow.state    = app.guiStateManager.GetCurrentState(arrowID);
            arrow.eventHandler = [isOpen](ElementEvent&& event) -> bool {
                if (event.type == interstice::EventType::MouseDown && event.state.LeftPressed())
                    { *isOpen = !*isOpen; return true; }
                return false;
            };
            auto res = app.elementStorage.Add(std::move(arrow), interstice::LayoutOverride{});
            VERIFY(res.status);
            VERIFY(app.windowRegistry.Add(res.ref));

            VERIFY(app.elementStorage.BeginChildren());
            {
                interstice::ID glyphID = app.idManager.AllocateDynamicID();
                interstice::Element glyph{};
                glyph.id       = glyphID;
                glyph.styleRef = app.theme.treeTableArrowGlyphStyleRef;
                glyph.flags    = interstice::ElementFlags::NoInput;
                glyph.state    = app.guiStateManager.GetCurrentState(glyphID);
                // Explicit size - the arrow SVG is 8x8 intrinsic.
                const float glyphSize = static_cast<float>(app.theme.treeTableArrowGlyphSize);
                interstice::ContentRun arrowRun[] = {
                    { .type = interstice::ContentRun::Icon,
                      .iconID = (*isOpen ? app.theme.treeArrowDown : app.theme.treeArrowRight),
                      .iconWidth = glyphSize, .iconHeight = glyphSize }
                };
                glyph.textID = app.textSystem->RegisterRichText(glyphID, arrowRun,
                                                                app.theme.treeTableArrowGlyphStyleRef, glyph.state);
                auto gRes = app.elementStorage.Add(std::move(glyph), interstice::LayoutOverride::Auto());
                VERIFY(gRes.status);
                VERIFY(app.windowRegistry.Add(gRes.ref));
            }
            VERIFY(app.elementStorage.EndChildren());
        }
        else
        {
            interstice::Element spacer{};
            spacer.id       = app.idManager.AllocateDynamicID();
            spacer.styleRef = app.theme.treeTableArrowStyleRef;
            spacer.flags    = interstice::ElementFlags::NoInput;
            auto res = app.elementStorage.Add(std::move(spacer), interstice::LayoutOverride{});
            VERIFY(res.status);
            VERIFY(app.windowRegistry.Add(res.ref));
        }
    }

    // Optional icon between arrow and text. On branches (isOpen != null) the
    // icon is hittable: a < 300 ms double-click toggles the branch. Single
    // clicks return false so the row backdrop's selection / drag-prime still
    // sees them.
    void EmitTreeCellIcon(GUIApplication& app, IconID icon, bool* isOpen,
                          interstice::DoubleClickTracker* tracker)
    {
        if (icon == IconID::Invalid) return;
        interstice::ID iconElemID = app.idManager.AllocateDynamicID();
        interstice::Element iconElem{};
        iconElem.id       = iconElemID;
        iconElem.styleRef = app.theme.treeTableIconStyleRef;
        const bool hittable = (isOpen != nullptr);
        iconElem.state    = app.guiStateManager.GetCurrentState(iconElemID);
        if (!hittable) iconElem.flags |= interstice::ElementFlags::NoInput;
        const float iconSize = static_cast<float>(app.theme.treeTableIconSize);
        interstice::ContentRun iconRun[] = {
            { .type = interstice::ContentRun::Icon, .iconID = icon,
              .iconWidth = iconSize, .iconHeight = iconSize }
        };
        iconElem.textID = app.textSystem->RegisterRichText(iconElemID, iconRun,
                                                           app.theme.treeTableIconStyleRef, iconElem.state);
        if (hittable)
        {
            iconElem.eventHandler = [isOpen, tracker](interstice::ElementEvent&& event) -> bool
            {
                if (event.type != interstice::EventType::MouseDown) return false;
                if (!event.state.LeftPressed()) return false;
                const auto r = tracker->Update(event.id);
                if (r.sameAsPrevious && r.elapsedMs < 300.0f)
                {
                    *isOpen = !*isOpen;
                    tracker->Reset();
                    return true;
                }
                return false;
            };
        }
        auto res = app.elementStorage.Add(std::move(iconElem), interstice::LayoutOverride::Auto());
        VERIFY(res.status);
        VERIFY(app.windowRegistry.Add(res.ref));
    }

    void GUIApplication::Cell(std::string_view text)
    {
        if (!m_tableEmit.has_value())
        {
            ReportMisuse("GUIApplication: a table cell outside a table (ignored)");
            return;
        }
        if (m_tableEmit->isRecording)
        {
            RecordReplayCell(*m_tableEmit, [this, text = std::string(text)]
                             { Cell(std::string_view(text)); });
            return;
        }
        const int     c        = CurrentCellCol();
        TableEmit&    emit     = *m_tableEmit;
        TableState*   state    = emit.userState;
        const bool    cellMode = emit.cellMode;
        const interstice::ID rowID    = emit.currentRowID;
        const StyleRef columnStyle = emit.columns[c].column.cellStyle;
        const StyleRef styleRef = (columnStyle.layoutStyleIndex != LayoutStyleIndex::Invalid) ? columnStyle
            : (emit.cellStyleRef.layoutStyleIndex != LayoutStyleIndex::Invalid) ? emit.cellStyleRef
            : theme.tableCellStyleRef;

        const interstice::ID cellID = idManager.AllocateDynamicID();
        auto [cellState, cellSelected] = ComputeCellSelectionBits(
            guiStateManager, state, cellID, rowID, c, cellMode);
        const interstice::ElementFlags cellFlags =
            cellMode ? interstice::ElementFlags::None : interstice::ElementFlags::NoInput;

        const interstice::TextID textID = textSystem->RegisterText(
            cellID, text, styleRef, cellState);

        const int32_t cellW = textSystem->GetTextMeasurement(textID).maxWidth;
        emit.columns[c].internal.maxContentWidth =
            std::max(emit.columns[c].internal.maxContentWidth, cellW);

        interstice::EventHandler cellHandler;
        if (cellMode)
        {
            state->EnsureCellLookup()[cellID] = { rowID, c };
            cellHandler = emit.sharedCellHandler;
        }

        EmitDirectCellText(emit.columns[c], emit,
                           cellID, cellState, cellFlags, styleRef, std::move(cellHandler),
                           textID,
                           c, emit.directColStart,
                           static_cast<int16_t>(emit.directGridRow));
    }


    void GUIApplication::Cell(std::function<void()> emit_fn)
    {
        if (!m_tableEmit.has_value())
        {
            ReportMisuse("GUIApplication: a table cell outside a table (ignored)");
            return;
        }
        if (m_tableEmit->isRecording)
        {
            RecordReplayCell(*m_tableEmit, [this, fn = std::move(emit_fn)]
                             { Cell(fn); });
            return;
        }
        const int     c        = CurrentCellCol();
        TableEmit&    emit     = *m_tableEmit;
        TableState*   state    = emit.userState;
        const bool    cellMode = emit.cellMode;
        const interstice::ID rowID    = emit.currentRowID;
        const StyleRef columnStyle = emit.columns[c].column.cellStyle;
        const StyleRef styleRef = (columnStyle.layoutStyleIndex != LayoutStyleIndex::Invalid) ? columnStyle
            : (emit.cellStyleRef.layoutStyleIndex != LayoutStyleIndex::Invalid) ? emit.cellStyleRef
            : theme.tableCellStyleRef;

        const interstice::ID cellID = idManager.AllocateDynamicID();
        auto [cellState, cellSelected] = ComputeCellSelectionBits(
            guiStateManager, state, cellID, rowID, c, cellMode);
        // Row-mode cells are NoInput (click -> select row), except columns that
        // host their own widget - those stay hittable so the widget gets input.
        const bool cellInteractive = cellMode || emit.columns[c].column.interactiveContent;
        const interstice::ElementFlags cellFlags =
            cellInteractive ? interstice::ElementFlags::None : interstice::ElementFlags::NoInput;

        interstice::EventHandler cellHandler;
        if (cellMode)
        {
            state->EnsureCellLookup()[cellID] = { rowID, c };
            cellHandler = emit.sharedCellHandler;
        }

        EmitDirectCellCustom(emit.columns[c], emit,
                             cellID, cellState, cellFlags, styleRef, std::move(cellHandler),
                             cellSelected, std::move(emit_fn),
                             c, emit.directColStart,
                             static_cast<int16_t>(emit.directGridRow));
    }


    void GUIApplication::TreeCell(std::string_view text, int depth, bool* isOpen,
                                  IconID icon, uint32_t ancestorLineMask,
                                  bool isLastSibling, std::string* renameBuffer,
                                  bool reserveExpander)
    {
        if (!m_tableEmit.has_value())
        {
            ReportMisuse("GUIApplication: a table cell outside a table (ignored)");
            return;
        }
        if (m_tableEmit->isRecording)
        {
            RecordReplayCell(*m_tableEmit,
                [this, text = std::string(text), depth, isOpen, icon,
                 ancestorLineMask, isLastSibling, renameBuffer, reserveExpander]
                { TreeCell(text, depth, isOpen, icon, ancestorLineMask,
                           isLastSibling, renameBuffer, reserveExpander); });
            return;
        }
        const int     c        = CurrentCellCol();
        TableEmit&    emit     = *m_tableEmit;
        TableState*   state    = emit.userState;
        const bool    cellMode = emit.cellMode;
        const interstice::ID rowID    = emit.currentRowID;

        // Tree-row metadata; slots are reused across frames (no steady-state allocation).
        auto& info = state->EnsureTreeRowInfo()[rowID];
        info.isOpen      = isOpen;     // null for leaves
        info.depth       = depth;
        info.renameBuf   = renameBuffer;
        info.hasChildren = (isOpen != nullptr);
        if (renameBuffer != nullptr) info.renameSeed.assign(text);
        else                         info.renameSeed.clear();

        const interstice::ID cellID = cellMode ? idManager.AllocateDynamicID() : rowID;
        auto [cellState, cellSelected] = ComputeCellSelectionBits(
            guiStateManager, state, cellID, rowID, c, cellMode);
        // In row mode the cell is the row (its state follows the row's): it shares the row's ID.
        const interstice::ElementFlags cellFlags = cellID == rowID ? interstice::ElementFlags::SharesID : interstice::ElementFlags::None;

        const interstice::ID bodyID = idManager.AllocateDynamicID();

        if (renameBuffer && (*m_renameSub).isActive &&
            (*m_renameSub).targetID == rowID &&
            !(*m_renameSub).firstFrame &&
            guiStateManager.Interaction().GetFocusedID() != bodyID)
        {
            (*m_renameSub).Cancel();
        }
        const bool renameActive = (renameBuffer != nullptr) &&
                                  (*m_renameSub).isActive &&
                                  (*m_renameSub).targetID == rowID;

        interstice::EventHandler cellHandler;
        if (cellMode)
        {
            state->EnsureCellLookup()[cellID] = { rowID, c };
            cellHandler = emit.sharedCellHandler;
        }
        else
        {
            cellHandler = emit.currentRowHandler;
        }
        if (isOpen)
            cellHandler = MakeTreeCellDoubleClickToggle(&state->internal.treeCellClickTracker, isOpen, std::move(cellHandler));

        const StyleRef textStyle = theme.treeTableTextStyleRef;
        const interstice::ElementState textState = guiStateManager.GetCurrentState(bodyID);
        const interstice::TextID bodyTextID = textSystem->RegisterText(
            bodyID, text, textStyle, textState);
        // Branches always reserve the arrow / leaf-spacer column (the arrow lives
        // there); leaves only when reserveExpander is set.
        const bool hasExpanderColumn = reserveExpander || (isOpen != nullptr);
        int32_t prefix = (depth > 0 ? depth * theme.treeTableIndent : 0)
                       + (hasExpanderColumn ? theme.treeTableExpanderWidth : 0)
                       + (icon != IconID::Invalid ? theme.treeTableIconSize : 0);
        if (const interstice::LayoutStyle* ts = styleManager.GetLayoutStyle(textStyle.layoutStyleIndex))
            prefix += ts->padding.Horizontal() + ts->borderWidths.Horizontal();
        const int32_t treeRowW = textSystem->GetTextMeasurement(bodyTextID).maxWidth + prefix;
        emit.columns[c].internal.maxContentWidth =
            std::max(emit.columns[c].internal.maxContentWidth, treeRowW);

        const StyleRef cellStyleRef = cellMode ? theme.treeTableCellStyleRef
                                               : theme.treeTableCellRowModeStyleRef;

        EmitDirectCellTree(emit.columns[c], emit,
                           cellID, cellState, cellFlags, cellStyleRef, std::move(cellHandler),
                           cellSelected, bodyTextID,
                           depth, isOpen, icon, ancestorLineMask, isLastSibling,
                           renameBuffer, renameActive, bodyID, reserveExpander,
                           c, emit.directColStart,
                           static_cast<int16_t>(emit.directGridRow));
    }


    void GUIApplication::EmitDirectCellText(const VirtualTableColumn& column, TableEmit& emit,
                                            interstice::ID cellID, interstice::ElementState cellState, interstice::ElementFlags cellFlags,
                                            interstice::StyleRef cellStyleRef, interstice::EventHandler cellHandler,
                                            interstice::TextID textID,
                                            int colIdx, int colStart, int16_t gridRow)
    {
        // Pre-resolved per-column track from VirtualTable's setup pass.
        const GridTrack& track = column.internal.columnTrack;

        interstice::Element cell{};
        cell.id           = cellID;
        cell.flags        = cellFlags;
        cell.state        = cellState;
        cell.styleRef     = cellStyleRef;
        cell.textID       = textID;
        cell.eventHandler = std::move(cellHandler);
        interstice::LayoutOverride cellLo;
        cellLo = ApplyColumnDragOffsetX(
                    std::move(cellLo)
                        .GridColumn(static_cast<int16_t>(colIdx - colStart))
                        .GridRow(gridRow)
                        .ColumnTrack(track), emit, colIdx);
        auto cRes = elementStorage.Add(std::move(cell), std::move(cellLo));
        VERIFY(cRes.status);
        VERIFY(windowRegistry.Add(cRes.ref));
    }


    void GUIApplication::EmitDirectCellCustom(const VirtualTableColumn& column, TableEmit& emit,
                                              interstice::ID cellID, interstice::ElementState cellState, interstice::ElementFlags cellFlags,
                                              interstice::StyleRef cellStyleRef, interstice::EventHandler cellHandler,
                                              bool cellSelected,
                                              std::function<void()> emit_fn,
                                              int colIdx, int colStart, int16_t gridRow)
    {
        // Pre-resolved per-column track from VirtualTable's setup pass.
        const GridTrack& track = column.internal.columnTrack;

        interstice::Element cell{};
        cell.id           = cellID;
        cell.flags        = cellFlags;
        cell.state        = cellState;
        cell.styleRef     = cellStyleRef;
        cell.eventHandler = std::move(cellHandler);
        interstice::LayoutOverride cellLo;
        cellLo = ApplyColumnDragOffsetX(
                    std::move(cellLo)
                        .GridColumn(static_cast<int16_t>(colIdx - colStart))
                        .GridRow(gridRow)
                        .ColumnTrack(track), emit, colIdx);
        auto cRes = elementStorage.Add(std::move(cell), std::move(cellLo));
        VERIFY(cRes.status);
        VERIFY(windowRegistry.Add(cRes.ref));

        VERIFY(elementStorage.BeginChildren());
        if (cellSelected) guiStateManager.Construction().PushSelected();
        if (emit_fn) emit_fn();
        if (cellSelected) guiStateManager.Construction().PopSelected();
        VERIFY(elementStorage.EndChildren());
    }


    void GUIApplication::EmitDirectCellTree(const VirtualTableColumn& column, TableEmit& emit,
                                            interstice::ID cellID, interstice::ElementState cellState, interstice::ElementFlags cellFlags,
                                            interstice::StyleRef cellStyleRef, interstice::EventHandler cellHandler,
                                            bool cellSelected,
                                            interstice::TextID textID,
                                            int treeDepth, bool* treeIsOpen, IconID treeIcon,
                                            uint32_t treeAncestorMask, bool treeIsLastSibling,
                                            std::string* treeRenameBuffer, bool treeRenameActive,
                                            interstice::ID treeBodyID, bool reserveExpander,
                                            int colIdx, int colStart, int16_t gridRow)
    {
        // Pre-resolved per-column track from VirtualTable's setup pass.
        const GridTrack& track = column.internal.columnTrack;

        interstice::Element cell{};
        cell.id           = cellID;
        cell.flags        = cellFlags;
        cell.state        = cellState;
        cell.styleRef     = cellStyleRef;
        cell.eventHandler = std::move(cellHandler);
        interstice::LayoutOverride cellLo;
        cellLo = ApplyColumnDragOffsetX(
                    std::move(cellLo)
                        .GridColumn(static_cast<int16_t>(colIdx - colStart))
                        .GridRow(gridRow)
                        .ColumnTrack(track), emit, colIdx);
        auto cRes = elementStorage.Add(std::move(cell), std::move(cellLo));
        VERIFY(cRes.status);
        VERIFY(windowRegistry.Add(cRes.ref));

        // Tree-cell guide lines via custom-draw. The cell's overflow:visible
        // lets the lines extend into padding so they connect to the next
        // row's lines.
        if (treeDepth > 0)
        {
            [[maybe_unused]] auto cdResult = elementStorage.AddCustomDraw(cRes.ref,
                [depth          = treeDepth,
                 ancestorMask   = treeAncestorMask,
                 isLastSibling  = treeIsLastSibling,
                 hasArrow       = (treeIsOpen != nullptr),
                 lineColor      = theme.treeGuideLineStyle,
                 treeIndent     = theme.treeTableIndent,
                 sm             = &styleManager,
                 cellStyle      = theme.treeTableCellStyleRef]
                (interstice::CustomDrawContext& ctx)
                {
                    // Vertical guides span the full OUTER row so consecutive rows'
                    // guides stay connected (no padding-sized gaps between rows).
                    const auto outer = ctx.ElementRect();
                    const auto inner = ctx.ElementInnerRect();
                    const int32_t h    = outer.h;
                    const int32_t topY = outer.y - inner.y;
                    const int32_t leftX = outer.x - inner.x;
                    int32_t padLeft = 0;
                    if (const interstice::LayoutStyle* ls = sm->GetLayoutStyle(cellStyle.layoutStyleIndex))
                        padLeft = ls->borderWidths.left + ls->padding.left;
                    const int32_t indent  = treeIndent;
                    const int32_t centerY = topY + h / 2;

                    interstice::LineStyle style { .colorStyleIndex = lineColor, .lineWidth = sm->Px(1.f) };

                    for (int d = 0; d < depth - 1; d++)
                    {
                        if ((ancestorMask & (1u << d)) == 0) continue;
                        int32_t x = leftX + padLeft + d * indent + indent / 2;
                        ctx.RenderLine({ x, topY }, { x, topY + h }, style);
                    }

                    const int32_t kLeafTickExtra = sm->Px(12);
                    int32_t x      = leftX + padLeft + (depth - 1) * indent + indent / 2;
                    int32_t bottom = isLastSibling ? centerY : topY + h;
                    int32_t tickX  = leftX + padLeft + depth * indent + (hasArrow ? 0 : kLeafTickExtra);
                    ctx.RenderLine({ x, topY },    { x,     bottom  }, style);
                    ctx.RenderLine({ x, centerY }, { tickX, centerY }, style);
                });
        }

        VERIFY(elementStorage.BeginChildren());
        if (cellSelected) guiStateManager.Construction().PushSelected();

        EmitTreeCellIndent(*this, treeDepth);
        // Branches always keep their arrow; leaves keep the spacer only when
        // the tree reserves an expander column (false for flat trees).
        if (reserveExpander || treeIsOpen != nullptr)
            EmitTreeCellArrowOrLeafSpacer(*this, treeIsOpen);
        EmitTreeCellIcon(*this, treeIcon, treeIsOpen, &emit.userState->internal.treeCellClickTracker);

        if (treeRenameActive)
        {
            TreeItemRenameCheck(treeBodyID, treeRenameBuffer,
                                theme.treeTableTextEditStyleRef);
        }
        else
        {
            interstice::Element textElem{};
            textElem.id       = treeBodyID;
            textElem.styleRef = theme.treeTableTextStyleRef;
            textElem.flags    = interstice::ElementFlags::NoInput;
            textElem.state    = guiStateManager.GetCurrentState(treeBodyID);
            textElem.textID   = textID;
            auto res = elementStorage.Add(std::move(textElem), interstice::LayoutOverride::Auto());
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));
        }

        if (cellSelected) guiStateManager.Construction().PopSelected();
        VERIFY(elementStorage.EndChildren());
    }


}
