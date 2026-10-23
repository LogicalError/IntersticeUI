#pragma once

// Structured logging (engine design docs 02 and 28): each message is a fixed-size
// record — category, severity, a static message template with `{}` placeholders and
// up to kMaxLogArgs typed arguments. Formatting happens lazily, at the sink.
//
// Records land in an in-memory ring of recent history (read by the editor's console,
// newest-last, with a sequence number for incremental reads) and are handed to every
// registered sink. A built-in console sink mirrors them to stdout / stderr and, on
// Windows, the debugger output.
//
// Emitting never allocates: text arguments are copied into the record's own fixed
// buffer (truncated if they don't fit), so dynamic strings such as file paths are safe
// to pass. The message template and category name must be static strings.

#include <stdint.h>
#include <span>
#include <string_view>
#include <type_traits>

#include <gui_core/gui_hash.h>

namespace interstice::Diagnostics
{
    enum class LogSeverity : uint8_t { Trace = 0, Debug, Info, Warning, Error, Critical };

    const char* LogSeverityName(LogSeverity severity);

    // A subsystem's log category. `name` must be a static string; `id` is its hash,
    // which is what filters and queries compare.
    struct LogCategory
    {
        uint64_t         id   = 0;
        std::string_view name;
    };

    constexpr LogCategory MakeLogCategory(std::string_view staticName)
    {
        return { Hash::CalculateConst(staticName), staticName };
    }

    // One argument. While passed to LogMessage, `text` views the caller's string; inside
    // a LogRecord it is empty and the text lives in the record (use LogArgText).
    struct LogArg
    {
        enum class Kind : uint8_t { None = 0, Int, Uint, Float, Text };

        Kind kind = Kind::None;
        union
        {
            int64_t  i = 0;
            uint64_t u;
            double   f;
        };
        std::string_view text;
        uint16_t         textOffset = 0; // record only: where the text starts in LogRecord::text
        uint16_t         textLength = 0;
    };

    template<typename T>
    LogArg MakeLogArg(const T& value)
    {
        LogArg arg;
        if constexpr (std::is_same_v<T, bool>)
        {
            arg.kind = LogArg::Kind::Text;
            arg.text = value ? "true" : "false";
        }
        else if constexpr (std::is_enum_v<T>)
        {
            arg.kind = LogArg::Kind::Int;
            arg.i    = static_cast<int64_t>(value);
        }
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
        {
            arg.kind = LogArg::Kind::Int;
            arg.i    = value;
        }
        else if constexpr (std::is_integral_v<T>)
        {
            arg.kind = LogArg::Kind::Uint;
            arg.u    = value;
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            arg.kind = LogArg::Kind::Float;
            arg.f    = static_cast<double>(value);
        }
        else
        {
            static_assert(std::is_convertible_v<const T&, std::string_view>, "unsupported log argument type");
            arg.kind = LogArg::Kind::Text;
            arg.text = std::string_view(value);
        }
        return arg;
    }

    inline constexpr int kMaxLogArgs        = 4;
    inline constexpr int kLogTextCapacity   = 384; // bytes shared by a record's text arguments
    inline constexpr int kLogRingCapacity   = 1024;
    inline constexpr int kMaxLogSinks       = 8;

    struct LogRecord
    {
        uint64_t         sequence       = 0; // 1-based, increases by one per record
        int64_t          timestampNanos = 0; // steady clock, see SteadyClockNowNanos
        uint64_t         threadId       = 0;
        LogCategory      category;
        LogSeverity      severity       = LogSeverity::Info;
        std::string_view messageTemplate;
        const char*      file           = "";
        uint32_t         line           = 0;
        uint8_t          argCount       = 0;
        LogArg           args[kMaxLogArgs];
        char             text[kLogTextCapacity];
    };

    std::string_view LogArgText(const LogRecord& record, const LogArg& arg);

    // Writes the template with its arguments substituted, NUL-terminated and truncated
    // to `out`. Returns the length written (excluding the NUL).
    size_t FormatLogMessage(const LogRecord& record, std::span<char> out);

    // Records below the minimum are dropped before any work is done. [AnyThread]
    void        SetMinimumLogSeverity(LogSeverity severity);
    LogSeverity GetMinimumLogSeverity();

    // Turns the built-in stdout/stderr/debugger sink on or off (on by default). [AnyThread]
    void SetLogConsoleOutput(bool enabled);

    // Sinks run on the emitting thread, under the logger's lock. Returns false when
    // the sink is already registered or kMaxLogSinks are in use. [MainThread]
    using LogSinkFn = void (*)(const LogRecord& record, void* context);
    bool AddLogSink(LogSinkFn sink, void* context);
    void RemoveLogSink(LogSinkFn sink, void* context);

    // Emits one record. Prefer the LOG_* macros, which add the source location. [AnyThread]
    void LogMessage(LogCategory category, LogSeverity severity, std::string_view messageTemplate,
                    std::span<const LogArg> args, const char* file = "", uint32_t line = 0);

    // Sequence number of the newest record (0 before the first). [AnyThread]
    uint64_t LatestLogSequence();

    // Copies up to out.size() of the most recent records, oldest first. Returns the
    // number copied. [AnyThread]
    uint32_t CopyRecentLogRecords(std::span<LogRecord> out);

    template<typename... Args>
    void Log(LogCategory category, LogSeverity severity, const char* file, uint32_t line,
             std::string_view messageTemplate, const Args&... args)
    {
        static_assert(sizeof...(Args) <= kMaxLogArgs, "too many log arguments");
        if (severity < GetMinimumLogSeverity())
            return;
        const LogArg packed[sizeof...(Args) + 1] = { MakeLogArg(args)..., LogArg{} };
        LogMessage(category, severity, messageTemplate, std::span<const LogArg>(packed, sizeof...(Args)), file, line);
    }
}

#define INTERSTICE_LOG_TRACE(category, ...)    ::interstice::Diagnostics::Log(category, ::interstice::Diagnostics::LogSeverity::Trace,    __FILE__, __LINE__, __VA_ARGS__)
#define INTERSTICE_LOG_DEBUG(category, ...)    ::interstice::Diagnostics::Log(category, ::interstice::Diagnostics::LogSeverity::Debug,    __FILE__, __LINE__, __VA_ARGS__)
#define INTERSTICE_LOG_INFO(category, ...)     ::interstice::Diagnostics::Log(category, ::interstice::Diagnostics::LogSeverity::Info,     __FILE__, __LINE__, __VA_ARGS__)
#define INTERSTICE_LOG_WARNING(category, ...)  ::interstice::Diagnostics::Log(category, ::interstice::Diagnostics::LogSeverity::Warning,  __FILE__, __LINE__, __VA_ARGS__)
#define INTERSTICE_LOG_ERROR(category, ...)    ::interstice::Diagnostics::Log(category, ::interstice::Diagnostics::LogSeverity::Error,    __FILE__, __LINE__, __VA_ARGS__)
#define INTERSTICE_LOG_CRITICAL(category, ...) ::interstice::Diagnostics::Log(category, ::interstice::Diagnostics::LogSeverity::Critical, __FILE__, __LINE__, __VA_ARGS__)
