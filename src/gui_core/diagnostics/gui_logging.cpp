#include <gui_core/diagnostics/gui_logging.h>

#include <gui_core/gui_base_types.h>   // SteadyClockNowNanos

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char*);
#endif

namespace tachyon::Diagnostics
{
    namespace
    {
        struct Sink { LogSinkFn fn; void* context; };

        struct Logger
        {
            std::mutex mutex;
            Sink       sinks[kMaxLogSinks] = {};
            int        sinkCount = 0;
            LogRecord  ring[kLogRingCapacity];
            uint64_t   sequence = 0;
        };

        Logger& GetLogger()
        {
            static Logger logger;
            return logger;
        }

        std::atomic<LogSeverity> g_minimumSeverity { LogSeverity::Info };
        std::atomic<bool>        g_consoleOutput   { true };

        void WriteToConsole(const LogRecord& record)
        {
            char message[1024];
            FormatLogMessage(record, message);
            char line[1200];
            std::snprintf(line, sizeof(line), "[%s] %.*s: %s\n", LogSeverityName(record.severity),
                          int(record.category.name.size()), record.category.name.data(), message);
            std::FILE* stream = (record.severity >= LogSeverity::Warning) ? stderr : stdout;
            std::fputs(line, stream);
#if defined(_WIN32)
            OutputDebugStringA(line);
#endif
        }

        size_t Append(std::span<char> out, size_t at, std::string_view text)
        {
            if (out.empty() || at + 1 >= out.size())
                return at;
            const size_t n = std::min(text.size(), out.size() - 1 - at);
            std::memcpy(out.data() + at, text.data(), n);
            return at + n;
        }
    }

    const char* LogSeverityName(LogSeverity severity)
    {
        switch (severity)
        {
            case LogSeverity::Trace:    return "trace";
            case LogSeverity::Debug:    return "debug";
            case LogSeverity::Info:     return "info";
            case LogSeverity::Warning:  return "warning";
            case LogSeverity::Error:    return "error";
            case LogSeverity::Critical: return "critical";
        }
        return "?";
    }

    std::string_view LogArgText(const LogRecord& record, const LogArg& arg)
    {
        if (arg.kind != LogArg::Kind::Text)
            return {};
        return std::string_view(record.text + arg.textOffset, arg.textLength);
    }

    size_t FormatLogMessage(const LogRecord& record, std::span<char> out)
    {
        if (out.empty())
            return 0;
        size_t at = 0;
        int    nextArg = 0;
        const std::string_view tmpl = record.messageTemplate;
        size_t i = 0;
        while (i < tmpl.size())
        {
            const size_t open = tmpl.find("{}", i);
            const size_t end  = (open == std::string_view::npos) ? tmpl.size() : open;
            at = Append(out, at, tmpl.substr(i, end - i));
            if (open == std::string_view::npos)
                break;
            i = open + 2;
            if (nextArg >= record.argCount)
            {
                at = Append(out, at, "{}");
                continue;
            }
            const LogArg& arg = record.args[nextArg++];
            char number[32];
            switch (arg.kind)
            {
                case LogArg::Kind::Int:   std::snprintf(number, sizeof(number), "%lld", (long long)arg.i);           at = Append(out, at, number); break;
                case LogArg::Kind::Uint:  std::snprintf(number, sizeof(number), "%llu", (unsigned long long)arg.u);  at = Append(out, at, number); break;
                case LogArg::Kind::Float: std::snprintf(number, sizeof(number), "%g", arg.f);                        at = Append(out, at, number); break;
                case LogArg::Kind::Text:  at = Append(out, at, LogArgText(record, arg));                              break;
                case LogArg::Kind::None:  break;
            }
        }
        out[at] = '\0';
        return at;
    }

    void SetMinimumLogSeverity(LogSeverity severity) { g_minimumSeverity.store(severity, std::memory_order_relaxed); }
    LogSeverity GetMinimumLogSeverity()              { return g_minimumSeverity.load(std::memory_order_relaxed); }
    void SetLogConsoleOutput(bool enabled)           { g_consoleOutput.store(enabled, std::memory_order_relaxed); }

    namespace
    {
        // The misuse messages already reported. Once full, a report is logged every time.
        std::mutex  g_misuseMutex;
        const char* g_misuseReported[64] = {};
        int         g_misuseReportedCount = 0;
    }

    void ReportMisuse(LogCategory category, const char* message)
    {
        {
            std::lock_guard lock(g_misuseMutex);
            for (int i = 0; i < g_misuseReportedCount; ++i)
                if (g_misuseReported[i] == message)
                    return;
            if (g_misuseReportedCount < int(std::size(g_misuseReported)))
                g_misuseReported[g_misuseReportedCount++] = message;
        }
        Log(category, LogSeverity::Error, "", 0, "{}", message);
    }

    void ResetMisuseReports()
    {
        std::lock_guard lock(g_misuseMutex);
        g_misuseReportedCount = 0;
    }

    bool AddLogSink(LogSinkFn sink, void* context)
    {
        Logger& logger = GetLogger();
        std::lock_guard lock(logger.mutex);
        if (!sink || logger.sinkCount == kMaxLogSinks)
            return false;
        for (int i = 0; i < logger.sinkCount; ++i)
            if (logger.sinks[i].fn == sink && logger.sinks[i].context == context)
                return false;
        logger.sinks[logger.sinkCount++] = { sink, context };
        return true;
    }

    void RemoveLogSink(LogSinkFn sink, void* context)
    {
        Logger& logger = GetLogger();
        std::lock_guard lock(logger.mutex);
        for (int i = 0; i < logger.sinkCount; ++i)
        {
            if (logger.sinks[i].fn == sink && logger.sinks[i].context == context)
            {
                logger.sinks[i] = logger.sinks[--logger.sinkCount];
                return;
            }
        }
    }

    void LogMessage(LogCategory category, LogSeverity severity, std::string_view messageTemplate,
                    std::span<const LogArg> args, const char* file, uint32_t line)
    {
        if (severity < GetMinimumLogSeverity())
            return;

        LogRecord record;
        record.timestampNanos  = SteadyClockNowNanos();
        record.threadId        = std::hash<std::thread::id>{}(std::this_thread::get_id());
        record.category        = category;
        record.severity        = severity;
        record.messageTemplate = messageTemplate;
        record.file            = file ? file : "";
        record.line            = line;
        record.argCount        = uint8_t(std::min<size_t>(args.size(), kMaxLogArgs));

        // Text arguments move into the record's own buffer; one that doesn't fit is truncated.
        size_t textUsed = 0;
        for (int i = 0; i < record.argCount; ++i)
        {
            LogArg arg = args[i];
            if (arg.kind == LogArg::Kind::Text)
            {
                const size_t n = std::min(arg.text.size(), size_t(kLogTextCapacity) - textUsed);
                std::memcpy(record.text + textUsed, arg.text.data(), n);
                arg.textOffset = uint16_t(textUsed);
                arg.textLength = uint16_t(n);
                arg.text       = {};
                textUsed += n;
            }
            record.args[i] = arg;
        }

        Logger& logger = GetLogger();
        std::lock_guard lock(logger.mutex);
        record.sequence = ++logger.sequence;
        logger.ring[(record.sequence - 1) % kLogRingCapacity] = record;
        if (g_consoleOutput.load(std::memory_order_relaxed))
            WriteToConsole(record);
        for (int i = 0; i < logger.sinkCount; ++i)
            logger.sinks[i].fn(record, logger.sinks[i].context);
    }

    uint64_t LatestLogSequence()
    {
        Logger& logger = GetLogger();
        std::lock_guard lock(logger.mutex);
        return logger.sequence;
    }

    uint32_t CopyRecentLogRecords(std::span<LogRecord> out)
    {
        Logger& logger = GetLogger();
        std::lock_guard lock(logger.mutex);
        const uint64_t available = std::min<uint64_t>(logger.sequence, kLogRingCapacity);
        const uint64_t count     = std::min<uint64_t>(available, out.size());
        const uint64_t first     = logger.sequence - count + 1;
        for (uint64_t i = 0; i < count; ++i)
            out[i] = logger.ring[(first + i - 1) % kLogRingCapacity];
        return uint32_t(count);
    }
}
