#include <algorithm>
#include <cmath>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/text/gui_icon_text_placement.h>

#include <gui_core/state/gui_state_manager.h>

#include <gui_core/text/gui_text_atlas_manager.h>
#include <gui_core/text/gui_clipboard.h>

#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>

#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/windows/gui_os_window_manager.h>

#include <skribidi/skb_rich_layout.h>


#undef SKB_CURRENT_SELECTION
#define SKB_CURRENT_SELECTION \
    skb_text_range_t { .start = {.offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE}, .end = {.offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE} }

#undef SKB_CURRENT_SELECTION_END
#define SKB_CURRENT_SELECTION_END \
    skb_text_position_t { .offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE }

namespace interstice
{
    struct draw_selection_context_t
    {
        float x;
        float y;
        interstice::VisualBoxStyleIndex styleIndex;
        interstice::ElementState state;
        interstice::SkribidiContext* ctx;
        interstice::IntRect clipRect;
    };


    static void draw_selection_rect(skb_rect2_t skb_rect, void* context)
    {
        draw_selection_context_t* draw_ctx = (draw_selection_context_t*)context;
        interstice::SkribidiContext* ctx = draw_ctx->ctx;

        float x1 = draw_ctx->x + skb_rect.x;
        float y1 = draw_ctx->y + skb_rect.y;
        float x2 = x1 + skb_rect.width;
        float y2 = y1 + skb_rect.height;

        x1 = floor(x1);
        y1 = floor(y1);
        x2 = ceil(x2);
        y2 = ceil(y2);

        interstice::IntRect rect {};
        rect.position = int2 { (int)x1, (int)y1 };
        rect.size = int2 { (int)(x2 - x1), (int)(y2 - y1) };

        ctx->guiRenderer->RenderBox(rect, draw_ctx->clipRect, draw_ctx->styleIndex, draw_ctx->state | interstice::ElementState::Selection);
    }

    void SkribidiTextSystem::ScreenToTextEditorLocalPoint(interstice::int2 screenSpace, const IntRect& textEditorInnerRect, interstice::float2& outLocal,
                                                          const float3x2& screenToLayout)
    {
        const float2 layout = screenToLayout.TransformPoint(float2{ float(screenSpace.x), float(screenSpace.y) });
        outLocal.x = layout.x - float(textEditorInnerRect.x);
        outLocal.y = layout.y - float(textEditorInnerRect.y);
    }

    void* SkribidiTextSystem::UpdateEditorWhenNecessary(RegisteredText* entry, float width) const
    {
        // skribidi can't re-wrap an editor at a new width, so a width change recreates it;
        // carry over the uncommitted text and the selection.
        std::string      keptText;
        skb_text_range_t keptSelection {};
        const bool keepEdit = m_skribidiContext->editor != nullptr &&
                              m_skribidiContext->editing_text_id == entry->textId &&
                              m_skribidiContext->editing_text_width != width;
        if (keepEdit)
        {
            keptText.resize(size_t(skb_editor_get_text_utf8_count(m_skribidiContext->editor)));
            keptText.resize(size_t(std::max(0, skb_editor_get_text_utf8(m_skribidiContext->editor, keptText.data(),
                                                                        int32_t(keptText.size())))));
            keptSelection = skb_editor_get_current_selection(m_skribidiContext->editor);
        }

        if (m_skribidiContext->editing_text_id != entry->textId ||
            m_skribidiContext->editing_text_width != width)
        {
            skb_editor_destroy(m_skribidiContext->editor);
            m_skribidiContext->editing_text_id = entry->textId;
            m_skribidiContext->editor = nullptr;
            m_skribidiContext->editing_text_width = 0;
        }
        if (m_skribidiContext->editor == nullptr)
        {
            auto textStyle           = m_skribidiContext->styleManager->GetVisualTextStyle(entry->styleIndex.visualTextStyleIndex, entry->state);
            auto selectedTextStyle = m_skribidiContext->styleManager->GetSelectionVisualTextStyle(entry->styleIndex.visualTextStyleIndex, entry->state);
            m_skribidiContext->editor = create_editor(m_skribidiContext, width,
                                                      &textStyle->textLayoutStyle,
                                                      &textStyle->textStyle,
                                                      &selectedTextStyle->textStyle,
                                                      keepEdit ? std::string_view(keptText) : std::string_view(entry->text));
            // Selectable rich text: re-apply link styling so links look the same while focused.
            // Ranges are codepoint offsets (see build_editor_text). Applied before the change
            // callback is set, so the relayout isn't seen as a text edit.
            for (const auto& link : entry->linkRanges)
            {
                const skb_text_range_t range =
                {
                    .start = { .offset = link.start, .affinity = SKB_AFFINITY_TRAILING },
                    .end   = { .offset = link.end,   .affinity = SKB_AFFINITY_TRAILING },
                };
                skb_editor_t* ed   = m_skribidiContext->editor;
                skb_temp_alloc_t* tmp = m_skribidiContext->temp_alloc;
                skb_editor_set_attribute(ed, tmp, range, skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, link.color));
                skb_editor_set_attribute(ed, tmp, range, skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_HOVER, link.hoverColor));
                skb_editor_set_attribute(ed, tmp, range, skb_attribute_make_paint_color(SKB_PAINT_DECORATION_LINK, SKB_PAINT_STATE_DEFAULT, link.color));
                skb_editor_set_attribute(ed, tmp, range, skb_attribute_make_paint_color(SKB_PAINT_DECORATION_LINK, SKB_PAINT_STATE_HOVER, link.hoverColor));
                skb_editor_set_attribute(ed, tmp, range, skb_attribute_make_decoration(SKB_DECORATION_LINE_UNDER, SKB_DECORATION_STYLE_SOLID, 0.0f, 0.0f, SKB_PAINT_DECORATION_LINK));
            }

            skb_editor_set_on_text_change_callback(m_skribidiContext->editor, on_editor_text_change, m_skribidiContext);
            skb_editor_set_input_filter_callback(m_skribidiContext->editor, on_editor_input_filter, m_skribidiContext);
            if (keepEdit)
                skb_editor_select(m_skribidiContext->editor, keptSelection);

            m_skribidiContext->editing_text_width = width;
            m_skribidiContext->editing_text_id = entry->textId;
        }
        assert(m_skribidiContext->editor);

        if (m_skribidiContext->pending_select_all_id == entry->textId)
        {
            skb_editor_select_all(m_skribidiContext->editor);
            m_skribidiContext->pending_select_all_id = TextID::Invalid;
        }

        if (m_skribidiContext->pending_selection_element == entry->elementId)
        {
            const int32_t count = int32_t(skb_editor_get_text_utf32_count(m_skribidiContext->editor));
            const skb_text_range_t range =
            {
                .start = { .offset = std::clamp(m_skribidiContext->pending_selection.anchor, 0, count), .affinity = SKB_AFFINITY_TRAILING },
                .end   = { .offset = std::clamp(m_skribidiContext->pending_selection.caret,  0, count), .affinity = SKB_AFFINITY_TRAILING },
            };
            skb_editor_select(m_skribidiContext->editor, range);
            m_skribidiContext->pending_selection_element = ID::Invalid();
        }

        return m_skribidiContext->editor;
    }

    void SkribidiTextSystem::RenderEditor(RegisteredText* entry, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale)
    {
        entry->innerRect = innerRectScreen;
        auto editor = (skb_editor_t*)UpdateEditorWhenNecessary(entry, float(rect.size.x));
        if (editor == nullptr) return;

        auto* ctx = m_skribidiContext;

        Update();

        float prev_x = ctx->view.cx;
        float prev_y = ctx->view.cy;

        skb_text_range_t edit_selection = skb_editor_get_current_selection(editor);

        // Resolve the selection with the same affinity as cut/delete, so glyph coloring
        // matches exactly what would be deleted. Compared as (paragraph, offset):
        // flat global offsets tie at paragraph boundaries.
        skb_paragraph_position_t sel_start = skb_editor_get_paragraph_position_from_text_position(editor, edit_selection.start);
        skb_paragraph_position_t sel_end   = skb_editor_get_paragraph_position_from_text_position(editor, edit_selection.end);
        if (!skb_paragraph_position_less_or_equal(sel_start, sel_end)) std::swap(sel_start, sel_end);

        const VisualTextStyle* selectionTextStyle = ctx->styleManager->GetSelectionVisualTextStyle(entry->styleIndex.visualTextStyleIndex, entry->state);
        VisualColorStyleIndex selectionTextColorIndex = selectionTextStyle ? selectionTextStyle->textColorIndex : entry->textColorIndex;

        // Selectable rich text: links render like the unfocused text (link colour, underline only
        // on hover). linkRanges is empty for other editors, so both lambdas are no-ops there.
        auto linkAtOffset = [&](int32_t offset, intptr_t& outContentId) -> bool
        {
            for (const auto& lr : entry->linkRanges)
                if (offset >= lr.start && offset < lr.end) { outContentId = lr.contentId; return true; }
            return false;
        };
        auto runColorIndex = [&](const skb_attribute_paint_t& p) -> VisualColorStyleIndex
        {
            const color4b c{ p.color.r, p.color.g, p.color.b, p.color.a };
            return ctx->styleManager->RegisterColorStyle(ColorStyle::Create(c), ImageIndex::Invalid);
        };

        float layout_height = 0.f;
        float layout_width = 0.f;
        for (int32_t pi = 0; pi < skb_editor_get_paragraph_count(editor); pi++)
        {
            const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(editor, pi);
            const skb_rect2_t layout_bounds = skb_layout_get_bounds(edit_layout);
            layout_height += layout_bounds.height;
            layout_width = skb_maxf(layout_width, layout_bounds.width);
        }

        // Aligned like the unfocused text, so focusing doesn't move it.
        entry->editorOffsetY = VerticalAlignOffset(entry->verticalAlignment, rect.size.y,
                                                   static_cast<int32_t>(std::ceil(layout_height)));
        ctx->view = view_t { .cx = float(rect.position.x), .cy = float(rect.position.y + entry->editorOffsetY), .scale = 1.f, .zoom_level = 0.f, };

        float ox = ctx->view.cx;
        float oy = ctx->view.cy;
        view_t base_view = ctx->view;

        if (skb_editor_get_text_range_count(editor, SKB_CURRENT_SELECTION) > 0)
        {
            const skb_rich_layout_t* rich_layout = skb_editor_get_rich_layout(editor);
            skb_text_range_t text_range = skb_editor_get_current_selection(editor);
            // With the app in the background the selection takes its unfocused color
            // (:not(:focus)::selection).
            interstice::ElementState selectionState = entry->state;
            if (ctx->guiStateManager && ctx->guiStateManager->Interaction().GetActiveWindowID() == ID::Invalid())
                selectionState = interstice::ElementState(uint8_t(selectionState) & ~uint8_t(interstice::ElementState::Focused));
            draw_selection_context_t sel_ctx =
            {
                .x = ox, .y = oy,
                .styleIndex = entry->styleIndex.visualBoxStyleIndex,
                .state = selectionState,
                .ctx = ctx, .clipRect = clipRect
            };
            skb_rich_layout_get_text_range_bounds(rich_layout, text_range, draw_selection_rect, &sel_ctx);
        }

        for (int32_t pi = 0; pi < skb_editor_get_paragraph_count(editor); pi++)
        {
            const skb_vec2_t paragraph_offset = skb_editor_get_paragraph_offset(editor, pi);
            ctx->view = base_view;
            ctx->view.cx = ox + paragraph_offset.x;
            ctx->view.cy = oy + paragraph_offset.y;

            const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(editor, pi);
            const skb_layout_line_t* lines = skb_layout_get_lines(edit_layout);
            const int32_t lines_count = skb_layout_get_lines_count(edit_layout);
            const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(edit_layout);
            const skb_glyph_t* glyphs = skb_layout_get_glyphs(edit_layout);
            const skb_cluster_t* clusters = skb_layout_get_clusters(edit_layout);
            const int32_t para_global_offset = skb_editor_get_paragraph_global_text_offset(editor, pi);

            const skb_layout_params_t* layout_params = skb_layout_get_params(edit_layout);
            const int32_t decorations_count = skb_layout_get_decorations_count(edit_layout);
            const skb_decoration_t* decorations = skb_layout_get_decorations(edit_layout);

            skb_rasterize_alpha_mode_t mode = SKB_RASTERIZE_ALPHA_SDF;

            // Draw underlines
            for (int32_t i = 0; i < decorations_count; i++)
            {
                const skb_decoration_t* decoration = &decorations[i];
                if (decoration->layer == SKB_DECORATION_UNDER)
                {
                    const skb_layout_run_t* run = &layout_runs[decoration->line.layout_run_idx];
                    const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(edit_layout, run);
                    // A link's underline shows only while it's hovered; plain underlines always show.
                    const bool isLinkDeco = (decoration->line.paint_tag == SKB_PAINT_DECORATION_LINK);
                    intptr_t linkContentId = 0;
                    if (isLinkDeco && run->glyph_range.start < run->glyph_range.end)
                        linkAtOffset(para_global_offset + clusters[glyphs[run->glyph_range.start].cluster_idx].text_offset, linkContentId);
                    // linkContentId != 0 guards the "nothing hovered" case (hoveredContentId == 0).
                    const uint32_t state = (isLinkDeco && linkContentId != 0 && linkContentId == entry->hoveredContentId)
                        ? SKB_PAINT_STATE_HOVER : SKB_PAINT_STATE_DEFAULT;
                    if (isLinkDeco && state != SKB_PAINT_STATE_HOVER)
                        continue;
                    const skb_attribute_paint_t paint = skb_attributes_get_paint(decoration->line.paint_tag, state, run_attributes, layout_params->attribute_collection);
                    if (paint.paint_tag == decoration->line.paint_tag)
                    {
                        skb_quad_t pat_quad = ctx->GetDecorationQuad(decoration->line.x, decoration->line.y, pixelScale,
                            (skb_decoration_position_t)decoration->line.position,
                            (skb_decoration_style_t)decoration->line.style,
                            decoration->line.length, decoration->line.pattern_offset, decoration->line.thickness,
                            paint.color, mode);

                        int bpp = skb_image_atlas_get_texture(ctx->atlas, pat_quad.texture_idx)->bpp;

                        draw_image_pattern_quad_sdf(ctx,
                                                    view_transform_rect(&ctx->view, pat_quad.geom),
                                                    pat_quad.pattern, pat_quad.texture,
                                                    1.0f / pat_quad.scale, bpp, mode,
                                                    isLinkDeco ? runColorIndex(paint) : entry->textColorIndex,
                                                    (uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, pat_quad.texture_idx),
                                                    clipRect);
                    }
                }
            }

            // Draw glyphs
            for (int li = 0; li < lines_count; li++)
            {
                const skb_layout_line_t* line = &lines[li];

                float pen_x = ctx->view.cx + line->bounds.x;
                skb_rect2_t run_bounds = skb_rect2_make_undefined();


                for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++)
                {
                    const skb_layout_run_t* run = &layout_runs[ri];
                    const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(edit_layout, run);
                    // Link runs use the link colour, other runs the element's text colour.
                    intptr_t linkContentId = 0;
                    const bool runIsLink = (run->glyph_range.start < run->glyph_range.end)
                        && linkAtOffset(para_global_offset + clusters[glyphs[run->glyph_range.start].cluster_idx].text_offset, linkContentId);
                    const uint32_t state = (runIsLink && linkContentId == entry->hoveredContentId)
                        ? SKB_PAINT_STATE_HOVER : SKB_PAINT_STATE_DEFAULT;
                    skb_attribute_paint_t paint = skb_attributes_get_paint(SKB_PAINT_TEXT, state, run_attributes, layout_params->attribute_collection);
                    const VisualColorStyleIndex runColorIdx = runIsLink ? runColorIndex(paint) : entry->textColorIndex;

                    for (int32_t gi = run->glyph_range.start; gi < run->glyph_range.end; gi++)
                    {
                        const skb_glyph_t* glyph = &glyphs[gi];

                        float gx = glyph->offset_x;
                        float gy = glyph->offset_y;

                        const skb_paragraph_position_t glyph_pos = { .paragraph_idx = pi, .text_offset = clusters[glyph->cluster_idx].text_offset };
                        const bool glyph_selected = skb_paragraph_position_less_or_equal(sel_start, glyph_pos)
                                                 && !skb_paragraph_position_less_or_equal(sel_end, glyph_pos);
                        VisualColorStyleIndex glyphColorIndex = glyph_selected ? selectionTextColorIndex : runColorIdx;

                        skb_quad_t quad = ctx->GetGlyphQuad(gx, gy, pixelScale,
                                                            layout_params->font_collection, run->font_handle, glyph->gid,
                                                            run->font_size, paint.color, SKB_RASTERIZE_ALPHA_SDF);
                        if (quad.flags & SKB_QUAD_IS_EMPTY)
                            continue;

                        int bpp = skb_image_atlas_get_texture(ctx->atlas, quad.texture_idx)->bpp;

                        draw_image_quad_sdf(ctx,
                                            view_transform_rect(&ctx->view, quad.geom),
                                            quad.texture,
                                            1.f / quad.scale, bpp, mode,
                                            (quad.flags & SKB_QUAD_IS_COLOR) ? VisualColorStyleIndex::Invalid : glyphColorIndex,
                                            (uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, quad.texture_idx),
                                            clipRect);

                        pen_x += glyph->advance_x;
                    }
                }
            }

            // Draw through lines.
            for (int32_t i = 0; i < decorations_count; i++)
            {
                const skb_decoration_t* decoration = &decorations[i];
                if (decoration->layer == SKB_DECORATION_OVER)
                {
                    const skb_layout_run_t* run = &layout_runs[decoration->line.layout_run_idx];
                    const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(edit_layout, run);
                    const uint32_t state = SKB_PAINT_STATE_DEFAULT;
                    const skb_attribute_paint_t paint = skb_attributes_get_paint(decoration->line.paint_tag, state, run_attributes, layout_params->attribute_collection);
                    if (paint.paint_tag == decoration->line.paint_tag)
                    {
                        skb_quad_t pat_quad = ctx->GetDecorationQuad(decoration->line.x, decoration->line.y, pixelScale,
                         (skb_decoration_position_t)decoration->line.position,
                         (skb_decoration_style_t)decoration->line.style,
                         decoration->line.length, decoration->line.pattern_offset, decoration->line.thickness,
                         paint.color, mode);


                     int bpp = skb_image_atlas_get_texture(ctx->atlas, pat_quad.texture_idx)->bpp;

                     draw_image_pattern_quad_sdf(ctx,
                                                    view_transform_rect(&ctx->view, pat_quad.geom),
                                                    pat_quad.pattern, pat_quad.texture,
                                                    1.0f / pat_quad.scale, bpp, mode,
                                                    entry->textColorIndex,
                                                    (uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, pat_quad.texture_idx),
                                                    clipRect);
                    }
                }
            }
        }


        // Caret is generally drawn only when there is no selection.
        const bool hasSelection = skb_editor_get_text_range_count(editor, SKB_CURRENT_SELECTION) > 0;
        if (!hasSelection)
        {
            const skb_caret_info_t caret_info = skb_editor_get_caret_info_at(editor, SKB_CURRENT_SELECTION_END);

            float x0 = ox + caret_info.x + caret_info.descender * caret_info.slope;
            float y0 = oy + caret_info.y + caret_info.descender;
            float x1 = ox + caret_info.x + caret_info.ascender * caret_info.slope;
            float y1 = oy + caret_info.y + caret_info.ascender;

            interstice::int2 p0 = { (int)x0, (int)y0 };
            interstice::int2 p1 = { (int)x1, (int)y1 };

            ctx->guiRenderer->RenderLine(p0, p1, clipRect,
                LineStyle{ .colorStyleIndex = ctx->caretColorStyleIndex, .lineWidth = ctx->Px(2.0f) });
        } else
        {
            // While text is dragged, the insertion caret shows here only while this editor is
            // where it would drop (the mouse over it, off the dragged selection).
            const int2 screenMousePos = ctx->frameMousePos;
            const ID activeWindowId = ctx->guiStateManager ? ctx->guiStateManager->Interaction().GetActiveWindowID() : ID::Invalid();
            ID dropTarget = ID::Invalid();
            if (ctx->drag_text &&
                (ctx->frameMouseButtons & MouseButtons::Left) != MouseButtons::None &&
                TryGetTextDragHoverTarget(activeWindowId, screenMousePos, dropTarget) &&
                dropTarget == ActiveElementID())
            {
                interstice::float2 localMouse{ static_cast<float>(screenMousePos.x), static_cast<float>(screenMousePos.y) };
                if (TryGetActiveTextEditorLocalPoint(activeWindowId, screenMousePos, localMouse))
                {
                    const skb_text_position_t hoverCaretPos = skb_editor_hit_test(editor, SKB_MOVEMENT_SELECTION, localMouse.x, localMouse.y);
                    const skb_caret_info_t hoverCaretInfo = skb_editor_get_caret_info_at(editor, hoverCaretPos);

                    const float x0 = ox + hoverCaretInfo.x + hoverCaretInfo.descender * hoverCaretInfo.slope;
                    const float y0 = oy + hoverCaretInfo.y + hoverCaretInfo.descender;
                    const float x1 = ox + hoverCaretInfo.x + hoverCaretInfo.ascender * hoverCaretInfo.slope;
                    const float y1 = oy + hoverCaretInfo.y + hoverCaretInfo.ascender;

                    interstice::int2 p0 = { (int)x0, (int)y0 };
                    interstice::int2 p1 = { (int)x1, (int)y1 };

                    ctx->guiRenderer->RenderLine(p0, p1, clipRect,
                        LineStyle{ .colorStyleIndex = ctx->caretColorStyleIndex, .lineWidth = ctx->Px(2.0f) });
                }
            }
        }

        ctx->view.cx = prev_x;
        ctx->view.cy = prev_y;
    }

    // -------------------- Editor Input Handling --------------------

    ID SkribidiTextSystem::ActiveElementID() const
    {
        return (m_skribidiContext && m_skribidiContext->guiStateManager)
            ? m_skribidiContext->guiStateManager->TextEditorFocus().GetPrevFocusedID() : ID::Invalid();
    }

    void SkribidiTextSystem::NoteSelectionBeforeEdit()
    {
        SkribidiContext* ctx = m_skribidiContext;
        if (!ctx || !ctx->editor || ctx->active_text_id != ctx->editing_text_id)
            return;
        // Only while the editor holds the committed text: then the next change is the first
        // of an edit, and this is the selection its undo gives back.
        const RegisteredText* entry = ctx->GetRegisteredText(ctx->editing_text_id);
        if (entry == nullptr || EditorText(ctx->editor) != entry->text)
            return;
        ctx->edit_selection_element = ActiveElementID();
        ctx->edit_selection_before  = EditorSelection(ctx->editor);
    }

    void SkribidiTextSystem::SwitchTextEditor(ID from, ID elementId)
    {
        if (!m_skribidiContext)
            return;

        const bool activeElementChanged = from != elementId;
        if (activeElementChanged && m_undoHistory)
            m_undoHistory->EndMerge();   // the next edit is a step of its own

        if (from != ID::Invalid() && activeElementChanged)
        {
            uint64_t key = Hash::Calculate(from);
            auto it = m_elementIdToTextIdMap.find(key);
            if (it != m_elementIdToTextIdMap.end())
            {
                ClearComposition(); // an uncommitted composition is dropped on blur
                capture_editor_text(m_skribidiContext);
                // Only an edit commits on blur: an untouched field's text may be stale by the
                // next frame (e.g. a list reordered under it) and must not overwrite its value.
                const RegisteredText* entry = m_skribidiContext->GetRegisteredText(it->second);
                const bool edited = entry == nullptr || entry->text != m_skribidiContext->pending_text;
                if (m_skribidiContext->pending_text_valid && edited)
                {
                    UpdateText(it->second, m_skribidiContext->pending_text);
                    m_skribidiContext->pending_text_prev_id = it->second;
                    // The editor goes below: keep its selection for the edit's redo.
                    if (m_skribidiContext->editor && m_skribidiContext->edit_selection_element == from)
                        m_skribidiContext->edit_selection_after = EditorSelection(m_skribidiContext->editor);
                }
            }
        }

        // A value field's edit text is kept only while it has the focus.
        if (elementId != m_valueEditElementId)
        {
            m_valueEditElementId = ID::Invalid();
            m_valueEditText.clear();
        }

        // The shared editor still holds the previous field's content until the next render
        // recreates it; destroy it now so neither RenderEditor nor ConsumeTextEditUpdate
        // reads the stale one. capture_editor_text has already saved its text.
        if (activeElementChanged && m_skribidiContext->editor != nullptr)
        {
            skb_editor_destroy(m_skribidiContext->editor);
            m_skribidiContext->editor = nullptr;
            m_skribidiContext->editing_text_id = TextID::Invalid;
            m_skribidiContext->editing_text_width = 0;
        }

        TextID textId = TextID::Invalid;
        if (elementId != ID::Invalid())
        {
            auto it = m_elementIdToTextIdMap.find(Hash::Calculate(elementId));
            if (it != m_elementIdToTextIdMap.end())
                textId = it->second;
        }

        if (m_skribidiContext->active_text_id != textId && m_skribidiContext->active_text_id != TextID::Invalid &&
            m_skribidiContext->editing_text_id != TextID::Invalid)
        {
            // Only arm a commit while an editor exists; otherwise the committed flag would
            // be orphaned and commit the next editing session on its first keystroke.
            m_skribidiContext->pending_text_id = m_skribidiContext->editing_text_id;
            m_skribidiContext->pending_text_committed = true;
            capture_editor_text(m_skribidiContext);
        }

        m_skribidiContext->active_text_id = textId;
        // The field's validator, until it registers again (RegisterEditableText).
        m_skribidiContext->active_validator = TextEditorValidator(elementId);

        if (textId == TextID::Invalid)
        {
            m_skribidiContext->editor_drag_active = false;
        }
    }

    bool SkribidiTextSystem::DoesTextEditorAcceptTabs(ID elementId) const
    {
        if (!m_skribidiContext)
            return false;
        if (elementId == ID::Invalid())
            return false;

        if (m_skribidiContext->guiStateManager && m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID() == elementId)
        {
            return m_skribidiContext->active_accepts_tabs;
        }

        return false;
    }

    bool SkribidiTextSystem::IsTextEditor(ID elementId) const
    {
        if (elementId == ID::Invalid())
            return false;

        return m_textEditorElements.find(Hash::Calculate(elementId)) != m_textEditorElements.end();
    }

    const ITextValidator* SkribidiTextSystem::TextEditorValidator(ID elementId) const
    {
        if (elementId == ID::Invalid())
            return nullptr;

        const auto it = m_textEditorValidators.find(Hash::Calculate(elementId));
        return it != m_textEditorValidators.end() ? it->second : nullptr;
    }

    bool SkribidiTextSystem::CanDropDraggedTextInto(ID targetTextEditorId) const
    {
        if (targetTextEditorId == ID::Invalid())
            return false;
        // Read-only editors never accept dropped text.
        if (m_readOnlyEditorElements.find(Hash::Calculate(targetTextEditorId)) != m_readOnlyEditorElements.end())
            return false;
        if (m_textDragSourceId == ID::Invalid())
            return true;

        // A field that takes only some text (a number) takes drops from such fields only.
        if (TextEditorValidator(targetTextEditorId) != nullptr && TextEditorValidator(m_textDragSourceId) == nullptr)
            return false;

        return true;
    }

    void SkribidiTextSystem::RequestSelectAll(ID elementId)
    {
        if (!m_skribidiContext)
            return;
        if (elementId == ID::Invalid())
            return;

        if (elementId == m_skipNextSelectAllId)
        {
            m_skipNextSelectAllId = ID::Invalid();
            return;
        }

        auto it = m_elementIdToTextIdMap.find(Hash::Calculate(elementId));
        if (it == m_elementIdToTextIdMap.end())
            return;
        m_skribidiContext->pending_select_all_id = it->second;
    }

    ITextSystem::TextEditUpdate SkribidiTextSystem::ConsumeTextEditUpdate(TextID textId, std::string& outText)
    {
        TextEditUpdate update;
        if (!m_skribidiContext)
            return update;
        if (m_skribidiContext->pending_text_id != textId)
            return update;

        update.textChanged = m_skribidiContext->pending_text_dirty;
        update.textCommitted = m_skribidiContext->pending_text_committed;
        if (update.HasUpdate())
        {
            if (m_skribidiContext->pending_text_valid)
            {
                outText = m_skribidiContext->pending_text;
            } else if (m_skribidiContext->editor && m_skribidiContext->editing_text_id == textId)
            {
                int32_t text_len = skb_editor_get_text_utf8_count(m_skribidiContext->editor);
                outText.clear();
                if (text_len > 0)
                {
                    outText.resize(text_len);
                    int32_t written = skb_editor_get_text_utf8(m_skribidiContext->editor, outText.data(), text_len);
                    if (written < 0)
                        written = 0;
                    outText.resize(written);
                }
            }
        }

        m_skribidiContext->pending_text_dirty = false;
        m_skribidiContext->pending_text_committed = false;
        m_skribidiContext->pending_text_valid = false;
        m_skribidiContext->pending_text_id = TextID::Invalid;
        m_skribidiContext->pending_text.clear();
        return update;
    }

    bool SkribidiTextSystem::UpdateText(RegisteredText* entry, std::string_view text)
    {
        if (!m_skribidiContext)
            return false;

        if (entry == nullptr ||
            entry->text == text)
            return false;

        uint64_t oldKey = entry->GetHashCode();
        auto it = m_textKeyToIdMap.find(oldKey);
        if (it != m_textKeyToIdMap.end() && it->second == entry->textId)
            m_textKeyToIdMap.erase(it);

        entry->text = std::string(text);

        m_skribidiContext->ResetLayout(entry);

        uint64_t newKey = entry->GetHashCode();
        m_textKeyToIdMap[newKey] = entry->textId;
        return true;
    }

    bool SkribidiTextSystem::UpdateText(TextID textId, std::string_view text)
    {
        if (!m_skribidiContext)
            return false;

        auto entry = m_skribidiContext->GetRegisteredText(textId);
        if (entry == nullptr || entry->text == text)
            return false;

        assert(entry->textId == textId);
        return UpdateText(entry, text);
    }

    std::string SkribidiTextSystem::Copy()
    {
        if (!m_skribidiContext || !m_skribidiContext->editor)
            return {};
        if (m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return {};

        const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
        int32_t textLen = skb_editor_get_text_utf8_count_in_range(m_skribidiContext->editor, selection);
        if (textLen <= 0)
            return {};

        std::string copiedText;
        copiedText.resize(static_cast<size_t>(textLen));
        int32_t written = skb_editor_get_text_utf8_in_range(m_skribidiContext->editor, selection, copiedText.data(), textLen);
        if (written <= 0)
            return {};
        copiedText.resize(static_cast<size_t>(written));

        m_skribidiContext->rich_text_clipboard_hash = skb_hash64_append_str(skb_hash64_empty(), copiedText.c_str());
        if (m_skribidiContext->rich_text_clipboard)
        {
            skb_editor_get_rich_text_in_range(m_skribidiContext->editor, selection, m_skribidiContext->rich_text_clipboard);
        }
        return copiedText;
    }

    std::string SkribidiTextSystem::Cut()
    {
        std::string cutText = Copy();
        if (cutText.empty())
            return cutText;
        NoteSelectionBeforeEdit();

        skb_editor_insert_text_utf8(m_skribidiContext->editor, m_skribidiContext->temp_alloc, SKB_CURRENT_SELECTION, nullptr, 0);
        m_skribidiContext->allow_char = false;
        EnsureCursorVisible(ActiveElementID());
        return cutText;
    }

    void SkribidiTextSystem::Paste(std::string_view text)
    {
        if (!m_skribidiContext || !m_skribidiContext->editor)
            return;
        if (m_skribidiContext->active_read_only)
            return;
        if (m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return;
        if (text.empty())
            return;
        NoteSelectionBeforeEdit();

        if (const ITextValidator* validator = m_skribidiContext->active_validator)
        {
            std::string filteredText(text);
            validator->Filter(filteredText);
            if (filteredText.empty())
                return;

            skb_editor_insert_text_utf8(m_skribidiContext->editor, m_skribidiContext->temp_alloc, SKB_CURRENT_SELECTION, filteredText.c_str(), static_cast<int32_t>(filteredText.size()));
        }
        else
        {
            const std::string pasteText(text);
            const uint64_t clipboardHash = skb_hash64_append_str(skb_hash64_empty(), pasteText.c_str());
            if (m_skribidiContext->rich_text_clipboard && clipboardHash == m_skribidiContext->rich_text_clipboard_hash)
            {
                skb_editor_insert_rich_text(m_skribidiContext->editor, m_skribidiContext->temp_alloc, SKB_CURRENT_SELECTION, m_skribidiContext->rich_text_clipboard);
            }
            else
            {
                skb_editor_insert_text_utf8(m_skribidiContext->editor, m_skribidiContext->temp_alloc, SKB_CURRENT_SELECTION, pasteText.c_str(), static_cast<int32_t>(pasteText.size()));
            }
        }

        m_skribidiContext->allow_char = false;
        EnsureCursorVisible(ActiveElementID());
    }

    void SkribidiTextSystem::ClearCache()
    {
        // Free every entry's layout (as Shutdown does) before dropping the entries.
        for (auto& entry : m_skribidiContext->m_registry)
            m_skribidiContext->ResetLayout(&entry);
        m_skribidiContext->m_registry.clear();
        m_skribidiContext->m_freeTextSlots.clear();

        // Text IDs restart after the clear: drop the live editor and every
        // reference to the old IDs. The focused element keeps its focus and is
        // re-linked when it registers its text again (RegisterEditableText).
        if (m_skribidiContext->editor)
        {
            skb_editor_destroy(m_skribidiContext->editor);
            m_skribidiContext->editor = nullptr;
        }
        m_skribidiContext->editing_text_id       = TextID::Invalid;
        m_skribidiContext->editing_text_width    = 0;
        m_skribidiContext->active_text_id        = TextID::Invalid;
        m_skribidiContext->pending_select_all_id = TextID::Invalid;
        m_skribidiContext->pending_text_prev_id  = TextID::Invalid;
        m_textEditorElements.clear();
        m_readOnlyEditorElements.clear();

        // A theme reload cleared the style manager's colors, the caret's among them.
        m_skribidiContext->RegisterCaretColor();
        m_elementIdToTextIdMap.clear();
        m_textKeyToIdMap.clear();
        m_textEditorValidators.clear();
        m_skribidiContext->active_validator = nullptr;
        m_valueEditElementId = ID::Invalid();
        m_valueEditText.clear();
        m_skribidiContext->pending_text_id = TextID::Invalid;
        m_skribidiContext->pending_text_dirty = false;
        m_skribidiContext->pending_text_committed = false;
        m_skribidiContext->pending_text_valid = false;
        m_skribidiContext->pending_text.clear();
    }

    void SkribidiTextSystem::EnsureCursorVisible(ID textEditorId)
    {
        if (!m_skribidiContext || !m_skribidiContext->editor)
            return;
        if (textEditorId == ID::Invalid())
            return;
        m_pendingCursorVisibleEditorId = textEditorId;
    }

    std::optional<IntRect> SkribidiTextSystem::GetCursorRect(ID textEditorId)
    {
        if (m_pendingCursorVisibleEditorId == ID::Invalid() ||
            m_pendingCursorVisibleEditorId != textEditorId ||
            !m_skribidiContext || !m_skribidiContext->editor)
        {
            return std::nullopt;
        }

        m_pendingCursorVisibleEditorId = ID::Invalid();

        const skb_caret_info_t caretInfo = skb_editor_get_caret_info_at(m_skribidiContext->editor, SKB_CURRENT_SELECTION_END);

        const float caretX0 = caretInfo.x + caretInfo.descender * caretInfo.slope;
        const float caretY0 = caretInfo.y + caretInfo.descender;
        const float caretX1 = caretInfo.x + caretInfo.ascender * caretInfo.slope;
        const float caretY1 = caretInfo.y + caretInfo.ascender;

        const int32_t minX = static_cast<int32_t>(std::floor(std::min(caretX0, caretX1)));
        const int32_t minY = static_cast<int32_t>(std::floor(std::min(caretY0, caretY1)));
        const int32_t height = std::max(1, static_cast<int32_t>(std::ceil(std::abs(caretY1 - caretY0))));

        // Inner-rect-local: add the text's vertical alignment offset.
        const RegisteredText* entry = m_skribidiContext->GetRegisteredText(m_skribidiContext->editing_text_id);
        const int32_t offsetY = entry ? entry->editorOffsetY : 0;
        return IntRect { .x = minX, .y = minY + offsetY, .w = 1, .h = height };
    }

    std::optional<IntRect> SkribidiTextSystem::GetCaretScreenRect() const
    {
        SkribidiContext* ctx = m_skribidiContext;
        if (!ctx || !ctx->editor || ctx->active_text_id != ctx->editing_text_id)
            return std::nullopt;
        const RegisteredText* entry = ctx->GetRegisteredText(ctx->active_text_id);
        if (!entry)
            return std::nullopt;

        // Editor-local caret (as in GetCursorRect) -> absolute layout -> screen.
        const skb_caret_info_t caret = skb_editor_get_caret_info_at(ctx->editor, SKB_CURRENT_SELECTION_END);
        const float2 origin = entry->EditorOrigin();
        const float3x2 layoutToScreen = entry->screenToLayout.Inverse();
        const float2 top    = layoutToScreen.TransformPoint(origin + float2{ caret.x + caret.ascender * caret.slope, caret.y + caret.ascender });
        const float2 bottom = layoutToScreen.TransformPoint(origin + float2{ caret.x + caret.descender * caret.slope, caret.y + caret.descender });

        const float minX = std::min(top.x, bottom.x), maxX = std::max(top.x, bottom.x);
        const float minY = std::min(top.y, bottom.y), maxY = std::max(top.y, bottom.y);
        return IntRect{ int32_t(std::floor(minX)), int32_t(std::floor(minY)),
                        std::max(1, int32_t(std::ceil(maxX - minX))), std::max(1, int32_t(std::ceil(maxY - minY))) };
    }

} // namespace interstice