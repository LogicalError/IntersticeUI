#include "application.h"
#include <gui_controls/gui_tree_utils.h>
#include <gui_controls/gui_default_handlers.h>   // MakeVerticalSizerHandler

#include <gui_core/diagnostics/gui_profiler.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/text/gui_text_system.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <inttypes.h>
#include <stdio.h>

//
// Hierarchical profiler panel: the per-frame GUI::Diagnostics timing tree as a
// tree table plus an aggregated flame graph. Both share one selection (the
// scope's call-path hash). Double-clicking a scope jumps to its source line.
//

namespace GUI
{
    namespace
    {
        constexpr int kPaletteN = 8;

        // Flat tree row (TreeUtils::TreeRowLike). Numbers are pre-formatted so the
        // cell lambdas hand VirtualTable stable storage.
        struct ProfRow
        {
            uint64_t    key      = 0;
            std::string name;
            std::string totalStr;
            std::string selfStr;
            std::string pctStr;
            std::string callsStr;
            const char* file     = "";
            int         line     = 0;
            double      totalMs  = 0.0;
            double      pctFrame = 0.0;
            int         depth     = 0;
            int         skipCount = 0;
            bool        isOpen        = true;
            bool        isLastSibling = true;
            bool        isWait        = false;
            uint32_t    ancestorLineMask = 0;
        };

        // One flame-graph bar in normalized [0,1] horizontal coordinates.
        struct FlameBar
        {
            float       x = 0, w = 0;
            int         depth = 0;
            int         colorIdx = 0;
            bool        isWait = false;
            uint64_t    key = 0;
            const char* name = "";
            const char* file = "";
            int         line = 0;
        };

        // Flame canvas inner width from the previous frame's custom draw (only
        // known after layout).
        int s_flameWidth = 0;   // 0: not measured yet

        // Selection shared by the tree and the flame graph: the selected scope's
        // stable call-path hash (0 = nothing selected).
        uint64_t s_selectedKey = 0;
        // Set by a flame-bar click so the tree scrolls to the picked row; tree
        // clicks leave it false so the tree doesn't move under the cursor.
        bool s_flameSelectPending = false;

        // "Jump to source": emit an MSVC-clickable location to the debugger
        // Output window (and stdout). Clicking it in the IDE navigates to the
        // scope's declaration.
        void OpenSourceFile(const char* file, int line)
        {
            if (!file || !*file) return;
            char msg[1024];
            std::snprintf(msg, sizeof(msg), "%s(%d): profiler scope\n", file, line);
            std::fputs(msg, stdout);
            std::fflush(stdout);
        }
    }

    static std::vector<ProfRow>               rows;
    static std::unordered_map<uint64_t, bool> openState;
    static std::vector<int>                   visibleIdx;
    static GUI::TableState                    tableState;
    static GUI::ID                            prevSelectedRow = GUI::ID::Invalid();
    static uint64_t                           prevSelectedKey = 0;
	static GUI::ID                            profilerTableID = GUI::ID::Invalid();
    static GUI::ID                            flameRootID = GUI::ID::Invalid();
	static GUI::ID                            flameSizerID = GUI::ID::Invalid();

    // Flame-bar styles (background-colored boxes). Registered on first use after
    // the panel is enabled; a theme reload clears every style and re-enables it.
    static bool                          flameStylesInit = false;
    static std::array<StyleRef, kPaletteN> barStyles{};
    static StyleRef                      selBarStyle{};
    static StyleRef                      waitBarStyle{};
    static StyleRef                      flameClipStyle{};
    static int                           flameHeight = 150;   // resizable (CSS px); content clipped, not scaled

    // Frame-graph (per-frame CPU build-time history) state.
    static GUI::ID                       frameGraphID = GUI::ID::Invalid();
    static bool                          frameGraphStylesInit = false;
    static StyleRef                      frameGraphBgStyle{};         // container background
    static VisualColorStyleIndex         frameGraphBarUnderStyle{};   // build <= budget (green)
    static VisualColorStyleIndex         frameGraphBarOverStyle{};    // build  > budget (red)
    static constexpr float               kFrameBudgetMs   = 16.67f;
    static constexpr int                 kFrameGraphHeight = 60;

    // Frame-rate budget reference lines (ms, ascending). Each has a static ID so
    // its label doesn't churn the text registry.
    struct FrameRefLine { float ms; const char* label; };
    static constexpr FrameRefLine kFrameRefs[] = {
        {   2.08f, "2 ms"    }, {   4.17f, "4 ms"   }, {   8.33f, "8 ms"   },
        {  16.67f, "16.7 ms" }, {  33.33f, "33 ms"  }, {  66.67f, "67 ms"  },
        { 100.00f, "100 ms"  }, { 200.00f, "200 ms" },
    };
    static constexpr int kFrameRefCount = int(sizeof(kFrameRefs) / sizeof(kFrameRefs[0]));
    static GUI::ID frameGraphLabelIDs[kFrameRefCount] = {};

    void Application::EnableProfilerTreePanel()
    {
        profilerTableID = idManager.CreateStaticID();
		flameRootID = idManager.CreateStaticID();
        flameSizerID = idManager.CreateStaticID();
        frameGraphID = idManager.CreateStaticID();
        for (int i = 0; i < kFrameRefCount; ++i)
            frameGraphLabelIDs[i] = idManager.CreateStaticID();
        flameStylesInit      = false;
        frameGraphStylesInit = false;
    }

    void Application::RenderProfilerTreePanel()
    {
        auto&        collector = Diagnostics::MainThreadCollector();
        const auto   samples   = Diagnostics::LastFrameSamples(collector);
        const double frameMs   = Diagnostics::LastFrameTotalMs(collector);

        // Double-clicking a row jumps to the scope's source line. (The chevron
        // still toggles open/close on single click.)
        if (!tableState.onRowDoubleClick)
            tableState.onRowDoubleClick = [](GUI::ID rowID) {
                if (rowID.GetMode() != GUI::IDMode::SubRange) return;
                const int vis = int(rowID.value.subRangeParts.index);
                if (vis < 0 || vis >= int(visibleIdx.size())) return;
                const ProfRow& r = rows[size_t(visibleIdx[size_t(vis)])];
                OpenSourceFile(r.file, r.line);
            };


        // ---- Rebuild the flat tree from the last finalized frame -----------
        rows.clear();
        char buf[32];
        for (const auto& s : samples)
        {
            ProfRow r;
            r.key      = s.key;
            r.name     = s.name;
            r.file     = s.file;
            r.line     = s.line;
            r.depth    = s.depth;
            r.totalMs  = s.totalMs;
            r.pctFrame = (frameMs > 0.0) ? 100.0 * s.totalMs / frameMs : 0.0;
            r.isWait   = s.isWait;

            std::snprintf(buf, sizeof(buf), "%.3f",  s.totalMs);   r.totalStr = buf;
            std::snprintf(buf, sizeof(buf), "%.3f",  s.selfMs);    r.selfStr  = buf;
            std::snprintf(buf, sizeof(buf), "%.1f%%", r.pctFrame); r.pctStr   = buf;
            std::snprintf(buf, sizeof(buf), "%d",    s.callCount); r.callsStr = buf;

            // Open state lives in the persistent map; TreeCell gets a pointer into
            // it, so a toggle fired during input survives the rebuild.
            r.isOpen = openState.try_emplace(s.key, true).first->second;
            rows.push_back(std::move(r));
        }
        TreeUtils::RebuildTreeIndex(rows);

        // ---- Visibility walk: skip closed subtrees in O(1) -----------------
        visibleIdx.clear();
        for (size_t i = 0; i < rows.size(); )
        {
            visibleIdx.push_back(int(i));
            const bool branch = rows[i].skipCount > 0;
            if (branch && !rows[i].isOpen) i += 1 + size_t(rows[i].skipCount);
            else                           i += 1;
        }

        // ---- Selection: capture a tree-row click -------------------------
        // The table writes selectedRowID during input (after this build). If it
        // changed since we last forced it, the user clicked a row — adopt it as
        // the shared selection. (Flame clicks set s_selectedKey directly.)
        if (tableState.selectedRowID != prevSelectedRow
            && tableState.selectedRowID.GetMode() == GUI::IDMode::SubRange)
        {
            const int vis = int(tableState.selectedRowID.value.subRangeParts.index);
            if (vis >= 0 && vis < int(visibleIdx.size()))
                s_selectedKey = rows[visibleIdx[vis]].key;
        }

        // The table's flex-fill row (flex-basis:0) must be a direct child of the
        // dock-content column to bound to its height; otherwise it overflows.

        // ---- Header: enable toggle + live frame total --------------------
        BeginHorizontal(LayoutOverride{}.NoGrow().NoShrink());
            const bool enabled = Diagnostics::IsProfilingEnabled();
            Toggle(enabled, enabled ? "Profiling: on" : "Profiling: off",
                   [enabled](bool value) { Diagnostics::SetProfilingEnabled(value); });
            char totalText[48];
            std::snprintf(totalText, sizeof(totalText), "Frame: %.2f ms", frameMs);
            Text(idManager.AllocateDynamicID(), totalText, theme.staticTextStyleRef);
        EndHorizontal();

        // ---- Frame graph: per-frame CPU build-time history ---------------
        // Bars = CPU work between presents; the faint line is full frame time
        // (incl. any vsync wait). The Y scale snaps to the next budget line above the peak.
        // Sampled in Run() only while profiling is on.
        {
            if (!frameGraphStylesInit)
            {
                Style bg;
                bg.SetOverflow(GUI::Overflow::Hidden);
                bg.padding = GUI::BoxEdges(0);   // inner height == kFrameGraphHeight
                bg.margin  = GUI::BoxEdges(2,0,4,0);
                for (int i = 0; i < Style::kStateCount; ++i)
                    bg.states[i].backgroundColor = ColorStyle::Create(theme.GetColor("--bg-view", color4f{ 0.12f, 0.12f, 0.13f, 1.0f }));
                frameGraphBgStyle       = styleManager.Register(bg);
                frameGraphBarUnderStyle = styleManager.RegisterColorStyle(ColorStyle::Create(color4f{ 0.35f, 0.70f, 0.40f, 1.0f }));
                frameGraphBarOverStyle  = styleManager.RegisterColorStyle(ColorStyle::Create(color4f{ 0.85f, 0.35f, 0.30f, 1.0f }));
                frameGraphStylesInit = true;
            }

            std::array<GUIApplication::FrameTimingSample, GUIApplication::kFrameHistoryCount> hist{};
            GetFrameTimingHistory(hist);
            constexpr int N = int(GUIApplication::kFrameHistoryCount);
            const int H = styleManager.Px(kFrameGraphHeight);   // bg padding is 0, so inner height == border height

            // Peak across both series drives the auto-scale.
            float dataMax = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                if (hist[size_t(i)].buildMs > dataMax) dataMax = hist[size_t(i)].buildMs;
                if (hist[size_t(i)].totalMs > dataMax) dataMax = hist[size_t(i)].totalMs;
            }

            // Snap the ceiling to the smallest budget above the peak; beyond the
            // coarsest reference, fall back to peak + headroom.
            const float target = dataMax * 1.10f;
            float scaleMax = kFrameRefs[kFrameRefCount - 1].ms;
            for (int i = 0; i < kFrameRefCount; ++i)
                if (kFrameRefs[i].ms >= target) { scaleMax = kFrameRefs[i].ms; break; }
            if (target > scaleMax) scaleMax = target;
            if (scaleMax < kFrameRefs[0].ms) scaleMax = kFrameRefs[0].ms;

            auto yAt = [&](float ms) { int y = H - int(ms * float(H) / scaleMax + 0.5f); return y < 0 ? 0 : (y > H ? H : y); };

            // Keep reference lines that fit, top-down, dropping any that would
            // crowd the previous one so labels stay legible at every scale.
            struct Grid { float ms; bool budget; GUI::TextID tid; int w; int h; };
            std::vector<Grid> grids;
            const int minGap = styleManager.Px(13);
            int lastY = -minGap;
            for (int i = kFrameRefCount - 1; i >= 0; --i)
            {
                if (kFrameRefs[i].ms > scaleMax + 0.01f) continue;
                const int y = yAt(kFrameRefs[i].ms);
                if (!grids.empty() && (y - lastY) < minGap) continue;
                const GUI::TextID tid = textSystem->RegisterText(frameGraphLabelIDs[i], kFrameRefs[i].label,
                                                                 theme.staticTextStyleRef, GUI::ElementState::None);
                const auto m = textSystem->GetTextMeasurement(tid);
                grids.push_back({ kFrameRefs[i].ms, kFrameRefs[i].ms == kFrameBudgetMs, tid, m.maxWidth, m.minHeight });
                lastY = y;
            }

            GUI::Element graphEl{};
            graphEl.id       = frameGraphID;
            graphEl.styleRef = frameGraphBgStyle;
            const auto graphRef = RegisterElement(std::move(graphEl),
                                                  LayoutOverride{}.Height(styleManager.Px(kFrameGraphHeight)).NoShrink());

            VERIFY(elementStorage.AddCustomDraw(graphRef,
                [this, hist, grids, scaleMax](GUI::CustomDrawContext& ctx)
                {
                    const IntRect inner = ctx.ElementInnerRect();   // local space, origin (0,0) top-left
                    const int W = inner.w, H = inner.h;
                    if (W <= 0 || H <= 0) return;
                    constexpr int N = int(GUIApplication::kFrameHistoryCount);
                    const float pxPerMs = float(H) / scaleMax;
                    auto yOf = [&](float ms) { int y = H - int(ms * pxPerMs + 0.5f); return y < 0 ? 0 : (y > H ? H : y); };

                    // Bars: CPU build time, colored against the 60fps budget.
                    for (int i = 0; i < N; ++i)
                    {
                        const int x0 = int(float(i)     / float(N) * float(W) + 0.5f);
                        int       x1 = int(float(i + 1) / float(N) * float(W) + 0.5f);
                        if (x1 > W) x1 = W;
                        int bw = x1 - x0; if (bw < 1) bw = 1;
                        const int top = yOf(hist[size_t(i)].buildMs);
                        const IntRect bar{ .position = { x0, top }, .size = { bw, H - top } };
                        ctx.RenderQuad(bar, (hist[size_t(i)].buildMs > kFrameBudgetMs) ? frameGraphBarOverStyle
                                                                                       : frameGraphBarUnderStyle, 0.0f);
                    }

                    // Faint overlay line: full frame time (incl. any vsync wait).
                    std::array<int2, N> pts{};
                    for (int i = 0; i < N; ++i)
                        pts[size_t(i)] = { int((float(i) + 0.5f) / float(N) * float(W) + 0.5f), yOf(hist[size_t(i)].totalMs) };
                    ctx.RenderPolyline(std::span<const int2>(pts.data(), pts.size()), false,
                                       GUI::LineStyle{ .colorStyleIndex = theme.lineGrayStyle, .lineWidth = styleManager.Px(1.0f) });

                    // Reference lines + ms labels; the 60fps budget line is brighter.
                    for (const auto& g : grids)
                    {
                        const int y = yOf(g.ms);
                        ctx.RenderLine({ 0, y }, { W, y },
                                       GUI::LineStyle{ .colorStyleIndex = g.budget ? theme.lineYellowStyle : theme.lineGrayStyle,
                                                       .lineWidth = styleManager.Px(1.0f), .dashSize = styleManager.Px(4.0f) });
                        const int ly = (y - g.h >= 0) ? (y - g.h) : (y + styleManager.Px(1));
                        ctx.RenderText(g.tid, IntRect{ .position = { styleManager.Px(3), ly }, .size = { g.w + styleManager.Px(2), g.h } });
                    }
                }));
        }

        // ---- Aggregated flame graph --------------------------------------
        if (!samples.empty())
        {
            if (!flameStylesInit)
            {
                const color4f hues[kPaletteN] = {
                    { 0.27f, 0.51f, 0.71f, 1.0f }, { 0.40f, 0.62f, 0.42f, 1.0f },
                    { 0.80f, 0.62f, 0.35f, 1.0f }, { 0.65f, 0.42f, 0.62f, 1.0f },
                    { 0.35f, 0.66f, 0.66f, 1.0f }, { 0.80f, 0.49f, 0.42f, 1.0f },
                    { 0.50f, 0.55f, 0.72f, 1.0f }, { 0.62f, 0.62f, 0.40f, 1.0f },
                };
                auto makeBox = [&](color4f bg) -> StyleRef {
                    Style st;
                    st.SetOverflow(GUI::Overflow::Hidden);
                    st.padding = GUI::BoxEdges(2);
                    st.margin  = GUI::BoxEdges(0);
                    for (int i = 0; i < Style::kStateCount; ++i)
                        st.states[i].backgroundColor = ColorStyle::Create(bg);
                    return styleManager.Register(st);
                };
                for (int k = 0; k < kPaletteN; ++k) barStyles[k] = makeBox(hues[k]);
                waitBarStyle = makeBox(color4f{ 0.42f, 0.42f, 0.46f, 1.0f });
                selBarStyle  = makeBox(color4f{ 0.95f, 0.75f, 0.25f, 1.0f });

                // Container that clips the icicle (overflow:hidden) so shrinking
                // the area clips the bottom rows rather than scaling them.
                Style clip;
                clip.SetOverflow(GUI::Overflow::Hidden);
                clip.layoutData.flex.direction = GUI::FlexDirection::Column;
                flameClipStyle = styleManager.Register(clip);

                flameStylesInit = true;
            }

            const int rowH     = theme.tableRowHeight > 0 ? theme.tableRowHeight : styleManager.Px(18);
            int       maxDepth = 0;
            for (const auto& s : samples) maxDepth = (s.depth > maxDepth) ? s.depth : maxDepth;
            const int W = s_flameWidth > 0 ? s_flameWidth : styleManager.Px(600);

            // Icicle layout: one forward pass over the depth-first samples,
            // bucketed per depth (DFS order is already left-to-right at every
            // depth). Each node's width is its share of its parent.
            std::vector<std::vector<FlameBar>> barsByDepth(size_t(maxDepth) + 1);
            struct Frame { float w; double total; float cursor; };
            std::vector<Frame> stack;
            stack.push_back({ 1.0f, frameMs > 0.0 ? frameMs : 1.0, 0.0f });
            for (const auto& s : samples)
            {
                while (int(stack.size()) > s.depth + 1) stack.pop_back();
                Frame& parent = stack.back();
                const float w = (parent.total > 0.0) ? parent.w * float(s.totalMs / parent.total) : 0.0f;
                const float x = parent.cursor;
                parent.cursor += w;
                barsByDepth[size_t(s.depth)].push_back(
                    { x, w, s.depth, int(s.key % kPaletteN), s.isWait, s.key, s.name, s.file, s.line });
                stack.push_back({ w, s.totalMs, x });
            }

            // A vertical canvas of per-depth rows; its custom draw captures the
            // inner width for next frame's pixel layout.
            GUI::Element flameRoot{};
            flameRoot.id       = flameRootID;
            flameRoot.styleRef = flameClipStyle;   // column + overflow:hidden → clips the bottom, no scaling
            const auto flameRef = RegisterElement(std::move(flameRoot),
                                                  LayoutOverride{}.Height(styleManager.Px(flameHeight)).FlexShrink(1));
            VERIFY(elementStorage.AddCustomDraw(flameRef,
                [](GUI::CustomDrawContext& ctx) { s_flameWidth = ctx.ElementInnerRect().w; }));

            VERIFY(elementStorage.BeginChildren());
            // Variable bar/label count: isolate the flame's internal ids so the
            // counter drift can't leak onto elements emitted after it.
            (void)idManager.PushParent(flameRootID);
            for (int d = 0; d <= maxDepth; ++d)
            {
                BeginHorizontal(idManager.AllocateDynamicID(), LayoutOverride{}.Height(rowH).NoShrink());
                int cursorPx = 0;
                for (const FlameBar& b : barsByDepth[size_t(d)])
                {
                    const int leftPx  = int(b.x * float(W) + 0.5f);
                    int       rightPx = int((b.x + b.w) * float(W) + 0.5f);
                    if (rightPx > W) rightPx = W;
                    int wpx = rightPx - leftPx;
                    if (wpx < 1) wpx = 1;

                    if (leftPx > cursorPx)
                    {
                        GUI::Element spacer{};
                        spacer.id       = idManager.AllocateDynamicID();
                        spacer.styleRef = theme.emptyHorzStyleRef;
                        RegisterElement(std::move(spacer),
                                        LayoutOverride{}.FixedWidth(leftPx - cursorPx).Height(rowH));
                    }

                    const uint64_t key  = b.key;
                    const char*    file = b.file;
                    const int      line = b.line;
                    GUI::Element box{};
                    box.id           = idManager.AllocateDynamicID();
                    box.eventHandler = [key, file, line](GUI::ElementEvent&& e) -> bool {
                        if (e.type == GUI::EventType::MouseDown && e.state.LeftPressed())
                        {
                            if (s_selectedKey == key) OpenSourceFile(file, line);             // re-click → jump
                            else { s_selectedKey = key; s_flameSelectPending = true; }    // first click → select
                            return true;
                        }
                        return false;
                    };
                    box.styleRef = b.isWait     ? waitBarStyle
                                 : (key == s_selectedKey) ? selBarStyle
                                 : barStyles[size_t(b.colorIdx)];
                    RegisterElement(std::move(box), LayoutOverride{}.FixedWidth(wpx).Height(rowH));

                    VERIFY(elementStorage.BeginChildren());
                    if (wpx > styleManager.Px(26))
                        Text(idManager.AllocateDynamicID(), b.name, theme.staticTextStyleRef);
                    VERIFY(elementStorage.EndChildren());

                    cursorPx = leftPx + wpx;
                }
                EndHorizontal();
            }
            (void)idManager.PopParent();
            VERIFY(elementStorage.EndChildren());

            // Draggable divider below the flame: drag it to resize the flame's
            // height. The icicle is clipped (overflow:hidden), not scaled.
            GUI::Element flameSizer{};
            flameSizer.id           = flameSizerID;
            flameSizer.styleRef     = theme.dockSizerVertStyleRef;
            flameSizer.state        = guiStateManager.GetCurrentState(flameSizer.id);
            const float uiScale = styleManager.GetUIScale();
            const int   rowUnits = int(std::ceil(float(rowH) / uiScale));   // rowH is pixels; flameHeight CSS px
            flameSizer.eventHandler = GUI::MakeVerticalSizerHandler(guiStateManager, flameHeight, rowUnits, 40 * rowUnits,
                                                                    nullptr, nullptr, false, uiScale);
            RegisterElement(std::move(flameSizer), LayoutOverride::FixedHeight(styleManager.Px(5)));
        }

        // ---- Columns -------------------------------------------------------
        static std::vector<VirtualTableColumn> columns =
        {
            {
                { .header = "Scope", .initialWidth = 240, .minWidth = 100, .maxWidth = 600, .locked = true },
                [this](int rowIdx) {
                    if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                    ProfRow& r = rows[visibleIdx[rowIdx]];
                    const bool branch = (r.skipCount > 0);
                    TreeCell(r.name, r.depth, branch ? &openState[r.key] : nullptr,
                             IconID::Invalid, r.ancestorLineMask, r.isLastSibling);
                }
            },
            {
                { .header = "Total ms", .initialWidth = 84, .minWidth = 60, .maxWidth = 160 },
                [this](int rowIdx) {
                    if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                    Cell(rows[visibleIdx[rowIdx]].totalStr);
                }
            },
            {
                { .header = "Self ms", .initialWidth = 84, .minWidth = 60, .maxWidth = 160 },
                [this](int rowIdx) {
                    if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                    Cell(rows[visibleIdx[rowIdx]].selfStr);
                }
            },
            {
                { .header = "% frame", .initialWidth = 72, .minWidth = 50, .maxWidth = 120 },
                [this](int rowIdx) {
                    if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                    Cell(rows[visibleIdx[rowIdx]].pctStr);
                }
            },
            {
                { .header = "Calls", .initialWidth = 60, .minWidth = 40, .maxWidth = 100 },
                [this](int rowIdx) {
                    if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                    Cell(rows[visibleIdx[rowIdx]].callsStr);
                }
            },
        };

        // The flex-fill row (flex-basis:0) bounds the table to the panel height
        // so its body scrolls internally instead of overflowing the panel.
        std::span<VirtualTableColumn> columnSpan(columns.data(), columns.size());
        BeginHorizontal(idManager.AllocateDynamicID("profilerFill"),
                        theme.flexFillRowStyleRef, LayoutOverride::Auto().FlexGrow(1));
        // A stable static id keeps the table's scroll state put: AllocateDynamicID
        // folds a per-parent counter, so the flame graph's varying element count
        // would otherwise shift the table id each frame and reset its scroll.
        VirtualTable(profilerTableID,
                     tableState, columnSpan,
                     int(visibleIdx.size()), 0, theme.tableStyleRef,
                     LayoutOverride{}.FlexGrow(1));
        EndHorizontal();

        // ---- Reflect a changed selection onto the tree -------------------
        // Only on change, so the table's own scroll is never fought.
        if (s_selectedKey != prevSelectedKey)
        {
            if (s_selectedKey != 0 && tableState._rowsRangeID != GUI::ID::Invalid())
            {
                int vis = -1;
                for (size_t i = 0; i < visibleIdx.size(); ++i)
                    if (rows[visibleIdx[i]].key == s_selectedKey) { vis = int(i); break; }
                if (vis >= 0)
                {
                    tableState.selectedRowID = idManager.GetSubID(tableState._rowsRangeID, uint32_t(vis));

                    const int rh     = tableState._actualRowHeight;
                    const int innerH = tableState._lastViewportH;
                    if (s_flameSelectPending && rh > 0 && innerH > 0)
                    {
                        // The rows actually scroll via the per-section bodies;
                        // _bodyID is only the synced wrapper. Drive every section
                        // (mirrors the table's own keyboard scroll-into-view).
                        const int headerH       = tableState._hasHeaders ? rh : 0;
                        const int rowTopContent = headerH + vis * rh;
                        const int rowBotContent = rowTopContent + rh;
                        auto&     scroll        = guiStateManager.GetScrollManagement();
                        int  writtenY  = 0;
                        bool anyChange = false;
                        for (int sIdx = 0; sIdx < int(tableState._sectionCount); ++sIdx)
                        {
                            const GUI::ID sid = tableState.SectionBodyID(sIdx);
                            if (sid == GUI::ID::Invalid()) continue;
                            auto       handle = scroll.GetOrCreateScrollOffsetHandle(sid);
                            const int2 cur    = scroll.GetScrollOffset(handle);
                            int newY = cur.y;
                            if (rowTopContent < -cur.y + headerH)     newY = -(rowTopContent - headerH);
                            else if (rowBotContent > -cur.y + innerH) newY = -(rowBotContent - innerH);
                            if (newY != cur.y) { scroll.SetScrollOffset(handle, int2{ cur.x, newY }); anyChange = true; }
                            writtenY = newY;
                        }
                        if (anyChange && tableState._bodyID != GUI::ID::Invalid())
                        {
                            auto bh = scroll.GetOrCreateScrollOffsetHandle(tableState._bodyID);
                            if (scroll.GetScrollOffset(bh).y != writtenY)
                                scroll.SetScrollOffset(bh, int2{ 0, writtenY });
                        }
                    }
                }
            }
            prevSelectedKey = s_selectedKey;
        }
        s_flameSelectPending = false;
        prevSelectedRow = tableState.selectedRowID;
    }
}
