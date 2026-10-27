#pragma once

// Collects the misuse reports (Diagnostics::ReportMisuse) and other errors logged while it
// lives. Starting one lets every misuse be reported again, so a test sees its own.

#include <gui_core/diagnostics/gui_logging.h>

#include <string>
#include <string_view>
#include <vector>

namespace GUITest
{
    class MisuseReports
    {
    public:
        MisuseReports()
        {
            tachyon::Diagnostics::ResetMisuseReports();
            tachyon::Diagnostics::AddLogSink(&Sink, this);
        }
        ~MisuseReports() { tachyon::Diagnostics::RemoveLogSink(&Sink, this); }
        MisuseReports(const MisuseReports&) = delete;
        MisuseReports& operator=(const MisuseReports&) = delete;

        size_t Count() const { return m_messages.size(); }
        bool   Contains(std::string_view text) const
        {
            for (const std::string& message : m_messages)
                if (message.find(text) != std::string::npos)
                    return true;
            return false;
        }
        std::string All() const
        {
            std::string all;
            for (const std::string& message : m_messages)
                all += message + "\n";
            return all;
        }
        void Clear() { m_messages.clear(); tachyon::Diagnostics::ResetMisuseReports(); }

    private:
        static void Sink(const tachyon::Diagnostics::LogRecord& record, void* context)
        {
            if (record.severity < tachyon::Diagnostics::LogSeverity::Error)
                return;
            char text[512];
            const size_t length = tachyon::Diagnostics::FormatLogMessage(record, text);
            static_cast<MisuseReports*>(context)->m_messages.emplace_back(text, length);
        }

        std::vector<std::string> m_messages;
    };
}
