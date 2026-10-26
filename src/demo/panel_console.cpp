#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

//
// Console panel: the application log (interstice::Diagnostics) as a virtual table of
// time / level icon / message, plus a detail pane for the selected record.
// "Collapse duplicates" folds identical lines anywhere in the log into one row.
//

namespace interstice
{
    namespace
    {
        enum class LogLevel { Info, Warning, Error };

        constexpr Diagnostics::LogCategory kLogDemo = Diagnostics::MakeLogCategory("demo");

        struct ConsoleLogEntry
        {
            LogLevel    level;
            std::string timestamp;
            std::string message;  // single line shown in the list
            std::string details;  // full text shown in the detail pane (empty -> use message)
        };

        // Panel data - built by EnableLogPanel, torn down by DisableLogPanel.
        // logEntries mirrors the log records pulled so far (Clear empties only
        // the panel); logLastSequence is the newest record already pulled.
        std::vector<ConsoleLogEntry>            logEntries;
        uint64_t                                logLastSequence = 0;
        std::vector<interstice::VirtualTableColumn> logColumns;
        interstice::TableState              logTableState;
        std::string                             logSearch;

        // One entry per visible row, rebuilt only when the filter changes:
        //   logVisible[r] - logEntries index of the row's first matching entry.
        //   logRepeat[r]  - entries this row stands for; > 1 shows a count bubble.
        std::vector<int> logVisible;
        std::vector<int> logRepeat;
        bool             logCollapse    = false; // fold identical lines into one + count (off by default)
        bool             logFilterDirty = true;

        // Detail pane: the selected entry's text as rich-text runs with file:line
        // links. Runs point into the entry's stored text, valid until it is cleared.
        struct DetailLink { intptr_t id; std::string file; int line; };
        int                          logDetailSel    = -1;  // entry index the runs were built for
        int                          logDetailHeight = 132; // pane height (CSS px); driven by the sizer
        std::vector<interstice::ContentRun> logDetailRuns;
        std::vector<DetailLink>      logDetailLinks;
        interstice::ID                      logDetailTextID = interstice::ID::Invalid(); // stable id for the selectable detail text

        // Level-filter toggles and per-level totals over the whole log
        // (recomputed only when the log changes).
        bool        logShowLevel[3] = { true, true, true }; // indexed by LogLevel
        std::string logCountText[3];

        void RecountLogLevels()
        {
            int counts[3] = {};
            for (const auto& e : logEntries)
                counts[int(e.level)]++;
            for (int i = 0; i < 3; ++i)
                logCountText[i] = std::to_string(counts[i]);
        }

        // Timestamps show the time since start-up (static init is close enough).
        const int64_t logEpochNanos = SteadyClockNowNanos();

        LogLevel ToLogLevel(Diagnostics::LogSeverity severity)
        {
            if (severity >= Diagnostics::LogSeverity::Error)   return LogLevel::Error;
            if (severity == Diagnostics::LogSeverity::Warning) return LogLevel::Warning;
            return LogLevel::Info;
        }

        // Source path from the repository's src/ folder on, so the detail pane shows
        // a short, recognisable file:line.
        std::string_view ShortSourcePath(std::string_view file)
        {
            for (std::string_view marker : { "src/", "src\\" })
            {
                const size_t at = file.rfind(marker);
                if (at != std::string_view::npos)
                    return file.substr(at);
            }
            return file;
        }

        void AppendLogRecord(const Diagnostics::LogRecord& record)
        {
            const int64_t ms = std::max<int64_t>(0, (record.timestampNanos - logEpochNanos) / 1'000'000);
            char ts[48];
            std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d", int(ms / 3600000), int(ms / 60000 % 60),
                          int(ms / 1000 % 60), int(ms % 1000));

            char message[1024];
            Diagnostics::FormatLogMessage(record, message);

            std::string details = std::string(Diagnostics::LogSeverityName(record.severity)) + " ["
                                + std::string(record.category.name) + "] " + message;
            if (record.file[0] != '\0')
                details += "\n  at " + std::string(ShortSourcePath(record.file)) + ":" + std::to_string(record.line);
            logEntries.push_back({ ToLogLevel(record.severity), ts, message, std::move(details) });
        }

        // Appends the records logged since the last pull. Returns true if any arrived.
        bool PullNewLogRecords()
        {
            const uint64_t latest = Diagnostics::LatestLogSequence();
            if (latest == logLastSequence)
                return false;
            static std::vector<Diagnostics::LogRecord> records(Diagnostics::kLogRingCapacity);
            const uint32_t count = Diagnostics::CopyRecentLogRecords(records);
            for (uint32_t i = 0; i < count; ++i)
                if (records[i].sequence > logLastSequence)
                    AppendLogRecord(records[i]);
            logLastSequence = latest;
            return true;
        }

        // logEntries index the table's single selection points at (-1 if none).
        // The row id encodes the visible-row index, which maps through logVisible.
        int SelectedLogEntry()
        {
            if (logTableState.selectedRowID.GetMode() != interstice::IDMode::SubRange) return -1;
            const int visIdx = int(logTableState.selectedRowID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(logVisible.size())) return -1;
            return logVisible[visIdx];
        }

        // Splits the entry's text into runs; a path containing '.' followed by
        // ':<digits>' becomes a link run, mapped back via logDetailLinks.
        void RebuildLogDetail(int entryIdx)
        {
            logDetailRuns.clear();
            logDetailLinks.clear();
            logDetailSel = entryIdx;
            if (entryIdx < 0 || entryIdx >= int(logEntries.size())) return;

            const std::string& text = logEntries[entryIdx].details.empty()
                                    ? logEntries[entryIdx].message
                                    : logEntries[entryIdx].details;
            auto isPathChar = [](char c) {
                return std::isalnum((unsigned char)c) || c == '_' || c == '.'
                    || c == '/' || c == '\\' || c == '-' || c == '+';
            };
            const char* d = text.data();
            size_t segStart = 0, i = 0;
            intptr_t nextLinkId = 1;
            while (i < text.size())
            {
                if (text[i] == ':' && i + 1 < text.size() && std::isdigit((unsigned char)text[i + 1]))
                {
                    size_t p = i;                       // backtrack over the path
                    while (p > segStart && isPathChar(text[p - 1])) p--;
                    size_t e = i + 1;                   // forward over the line digits
                    while (e < text.size() && std::isdigit((unsigned char)text[e])) e++;
                    const std::string_view path(d + p, i - p);
                    if (i > p && path.find('.') != std::string_view::npos)
                    {
                        if (p > segStart)
                            logDetailRuns.push_back({ .type = interstice::ContentRun::Text,
                                                      .text = std::string_view(d + segStart, p - segStart) });
                        const intptr_t id = nextLinkId++;
                        logDetailRuns.push_back({ .type = interstice::ContentRun::Text,
                                                  .text = std::string_view(d + p, e - p),
                                                  .contentId = id, .isLink = true });
                        logDetailLinks.push_back({ id, std::string(path), std::atoi(d + i + 1) });
                        segStart = e;
                        i = e;
                        continue;
                    }
                }
                ++i;
            }
            if (segStart < text.size())
                logDetailRuns.push_back({ .type = interstice::ContentRun::Text,
                                          .text = std::string_view(d + segStart, text.size() - segStart) });
        }
    }

    void Application::EnableLogPanel()
    {
        // Static id so the detail text's focus and selection survive across frames.
        if (logDetailTextID == interstice::ID::Invalid())
            logDetailTextID = idManager.CreateStaticID();

        logEntries.clear();
        logLastSequence = 0;
        PullNewLogRecords();
        RecountLogLevels();
        // Follow the newest entry until the user scrolls up; resetting the follow
        // state re-runs the initial scroll-to-bottom on reopen.
        logTableState.followTail          = true;
        logTableState.RestartFollowTail();
        logTableState.selectedRowID       = interstice::ID::Invalid();
        logDetailSel                      = -1;
        logFilterDirty = true;

        // Columns read logEntries through logVisible, so they stay valid across
        // refilters; the lambdas only capture `this`.
        logColumns.clear();
        logColumns.resize(3);
        // Timestamp - fixed-width, first column.
        logColumns[0].column = { .initialWidth = 112, .minWidth = 112, .maxWidth = 112,
                                 .sizing = ColumnSizing::Fixed };
        logColumns[0].generateCell = [this](int rowIdx) {
            if (rowIdx < 0 || rowIdx >= int(logVisible.size())) return;
            Cell(logEntries[logVisible[rowIdx]].timestamp.c_str());
        };
        // Level icon - fixed-width landmark, after the timestamp.
        logColumns[1].column = { .initialWidth = 32, .minWidth = 32, .maxWidth = 32,
                                 .sizing = ColumnSizing::Fixed };
        logColumns[1].generateCell = [this](int rowIdx) {
            if (rowIdx < 0 || rowIdx >= int(logVisible.size())) return;
            const LogLevel level = logEntries[logVisible[rowIdx]].level;
            const IconID   icon  = (level == LogLevel::Error)   ? demo.errorIcon
                                 : (level == LogLevel::Warning) ? demo.warningIcon
                                 :                                demo.infoIcon;
            Cell([this, icon]() {
                const interstice::ID iconID = idManager.AllocateDynamicID();
                interstice::Element iconElem{};
                iconElem.id       = iconID;
                iconElem.iconID   = icon;
                iconElem.flags    = interstice::ElementFlags::NoInput;
                iconElem.state    = guiStateManager.GetCurrentState(iconID);
                iconElem.styleRef = logLevelIconStyleRef;
                auto res = elementStorage.Add(std::move(iconElem),
                                              interstice::LayoutOverride{});
                VERIFY(res.status);
                VERIFY(windowRegistry.Add(res.ref));
            });
        };
        // Message - stretches to fill the remaining width, single line. A
        // collapsed row (repeat > 1) trails a count bubble pushed to the right.
        logColumns[2].column = { .initialWidth = 240, .minWidth = 120,
                                 .sizing = ColumnSizing::Stretch };
        logColumns[2].generateCell = [this](int rowIdx) {
            if (rowIdx < 0 || rowIdx >= int(logVisible.size())) return;
            // Copy into the cell lambda - Cell may defer it for record/replay.
            Cell([this, msg = logEntries[logVisible[rowIdx]].message,
                        count = logRepeat[rowIdx]]() {
                // Message text fills the cell so the bubble lands at the right edge.
                {
                    const interstice::ID id = idManager.AllocateDynamicID();
                    interstice::Element el{};
                    el.id       = id;
                    el.flags    = interstice::ElementFlags::NoInput;
                    el.state    = guiStateManager.GetCurrentState(id);
                    el.styleRef = logMessageStyleRef;
                    el.textID   = textSystem->RegisterText(id, msg, el.styleRef, el.state);
                    auto res = elementStorage.Add(std::move(el), interstice::LayoutOverride::Auto().FlexGrow(1));
                    VERIFY(res.status);
                    VERIFY(windowRegistry.Add(res.ref));
                }
                if (count > 1)
                {
                    const interstice::ID id = idManager.AllocateDynamicID();
                    const std::string countText = std::to_string(count);
                    interstice::Element el{};
                    el.id       = id;
                    el.flags    = interstice::ElementFlags::NoInput;
                    el.state    = guiStateManager.GetCurrentState(id);
                    el.styleRef = logCountBadgeStyleRef;
                    el.textID   = textSystem->RegisterText(id, countText, el.styleRef, el.state);
                    auto res = elementStorage.Add(std::move(el), interstice::LayoutOverride::Auto());
                    VERIFY(res.status);
                    VERIFY(windowRegistry.Add(res.ref));
                }
            });
        };
    }

    void Application::DisableLogPanel()
    {
        logEntries.clear();
        logColumns.clear();
        logVisible.clear();
        logRepeat.clear();
        logDetailRuns.clear();
        logDetailLinks.clear();
        logDetailSel = -1;
    }

    void Application::RenderLogPanel()
    {
        if (PullNewLogRecords())
        {
            RecountLogLevels();
            logFilterDirty = true;
        }

        // Top row: clear button | search box | level toggles | collapse toggle.
        BeginHorizontal(idManager.AllocateDynamicID("logSearchRow"),
                        theme.searchRowStyleRef, interstice::LayoutOverride::Auto().NoShrink());
        {
            Button(idManager.AllocateDynamicID("logClear"), "Clear",
                []() {
                    logEntries.clear();
                    RecountLogLevels();
                    logFilterDirty = true;
                    // Drop the detail too - its runs reference the entries we
                    // just cleared, so leaving them would render freed memory.
                    logDetailRuns.clear();
                    logDetailLinks.clear();
                    logDetailSel   = -1;
                },
                interstice::LayoutOverride{}.NoGrow().NoShrink());
            SetTooltip("Clear the log");

            if (SearchField(logSearch, demo.findIcon,
                            TextEditBehavior::ContinuousUpdate,
                            interstice::LayoutOverride{}.FlexGrow(1)))
                            logFilterDirty = true;

            // Level toggles: a colored dot and the level's count, pressed while the
            // level shows. onToggle fires during end-of-frame input dispatch, so the
            // refilter picks the change up next frame.
            BeginHorizontal(idManager.AllocateDynamicID("logLevelToggles"),
                logLevelWellStyleRef,
                interstice::LayoutOverride{}.NoGrow().NoShrink());
            {
                const char*  levelTip[3] = { "Show or hide info messages",
                                             "Show or hide warnings",
                                             "Show or hide errors" };
                const int levelCount = int(sizeof(logShowLevel) / sizeof(logShowLevel[0])) - 1;
                for (int lvl = levelCount; lvl >= 0; --lvl) // error, warning, info - most severe first
                {
                    Toggle(idManager.AllocateDynamicID("logLevelToggle"),
                        logShowLevel[lvl], { demo.dotIcon, logCountText[lvl] },
                        [lvl](bool on) { logShowLevel[lvl] = on; logFilterDirty = true; },
                        logLevelPillStyleRefs[lvl],
                        interstice::LayoutOverride{}.NoGrow().NoShrink());
                    SetTooltip(levelTip[lvl]);
                }
            }
            EndHorizontal();

            // Collapse-duplicates toggle: fold identical lines into one + count.
            Toggle(idManager.AllocateDynamicID("logCollapse"),
                   logCollapse, demo.iconCollapseAllIcon,
                   [](bool on) { logCollapse = on; logFilterDirty = true; },
                   iconToggleStyleRef,
                   interstice::LayoutOverride{}.NoGrow().NoShrink());
            SetTooltip("Collapse duplicate lines");
        }
        EndHorizontal();

        // Refilter on demand. With collapse on, entries sharing level + message
        // fold into the first such row, which carries the total count.
        if (logFilterDirty)
        {
            const std::string q = SearchToLower(logSearch);
            logVisible.clear();
            logRepeat.clear();
            std::unordered_map<std::string, int> group; // level+message key -> logVisible index
            for (int i = 0; i < int(logEntries.size()); ++i)
            {
                if (!logShowLevel[int(logEntries[i].level)]
                    || !SearchMatches(logEntries[i].message, q))
                    continue;
                if (logCollapse)
                {
                    std::string key(1, char('0' + int(logEntries[i].level)));
                    key += logEntries[i].message;
                    auto it = group.find(key);
                    if (it != group.end())
                    {
                        logRepeat[it->second]++;
                        continue;
                    }
                    group.emplace(std::move(key), int(logVisible.size()));
                }
                logVisible.push_back(i);
                logRepeat.push_back(1);
            }
            logFilterDirty = false;
        }

        std::span<VirtualTableColumn> columnSpan(logColumns.data(), logColumns.size());

        // flex-fill-row (flex-basis:0) bounds the table to the panel height so
        // its body scrolls instead of overflowing.
        BeginHorizontal(idManager.AllocateDynamicID("consoleFill"),
                        theme.flexFillRowStyleRef,
                        LayoutOverride::Auto().FlexGrow(1));
        logTableState.rowStyle         = theme.listRowStyleRef;
        logTableState.cellStyle        = logCellStyleRef;
        if (!logColumns.empty())
            logColumns[0].column.cellStyle = logCellTimeStyleRef; // styles change on a theme reload
        logTableState.showHeaders      = false;
        logTableState.columnSeparators = false;
        VirtualTable(idManager.AllocateDynamicID("consoleLog"),
                     logTableState,
                     columnSpan, int(logVisible.size()), 0, logTableStyleRef,
                     LayoutOverride{}.FlexGrow(1));
        EndHorizontal();

        // The detail pane sits after the sizer, so the drag delta is inverted.
        {
            const interstice::ID sizerID = idManager.AllocateDynamicID("logDetailSizer");
            interstice::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerVertStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = interstice::MakeVerticalSizerHandler(
                guiStateManager, logDetailHeight, 48, 480, nullptr, nullptr, /*invert*/ true, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), interstice::LayoutOverride::FixedHeight(4));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Detail pane (bottom): the selected entry's full text, with clickable
        // file:line links. Height is driven by the sizer above.
        BeginVertical(idManager.AllocateDynamicID("logDetailPane"),
                      logDetailPaneStyleRef,
                      interstice::LayoutOverride::Auto().FixedHeight(CssPx(logDetailHeight)).NoGrow().NoShrink());
        {
            const int sel = SelectedLogEntry();
            if (sel != logDetailSel) RebuildLogDetail(sel);

            // No selection (or the entry has no text) -> leave the pane empty.
            if (!logDetailRuns.empty())
            {
                // Selectable + copyable read-only rich text, keeping the styled, clickable
                // file:line links. Clicking a link prints "Open file:line" (a stand-in for a
                // real editor jump). Uses a stable id so selection/focus survive across frames.
                SelectableText(logDetailTextID,
                    std::span<interstice::ContentRun>(logDetailRuns.data(), logDetailRuns.size()),
                    [](intptr_t contentId)
                    {
                        for (const auto& lk : logDetailLinks)
                            if (lk.id == contentId) { INTERSTICE_LOG_INFO(kLogDemo, "open {}:{}", lk.file, lk.line); break; }
                    },
                    logDetailTextStyleRef, interstice::LayoutOverride::Auto().FlexGrow(1));
            }
        }
        EndVertical();
    }

}
