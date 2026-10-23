#include <gtest/gtest.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <string>
#include <thread>
#include <vector>

namespace interstice::Diagnostics
{
    namespace
    {
        constexpr LogCategory kTestCategory = MakeLogCategory("test");

        // Captures what reaches a sink, formatted, so assertions don't depend on the ring.
        struct Captured
        {
            std::vector<LogRecord>   records;
            std::vector<std::string> messages;
        };

        void CaptureSink(const LogRecord& record, void* context)
        {
            auto* captured = static_cast<Captured*>(context);
            char message[512];
            FormatLogMessage(record, message);
            captured->records.push_back(record);
            captured->messages.push_back(message);
        }

        void CountSink(const LogRecord&, void* context) { ++*static_cast<int*>(context); }

        // The logger is process-wide: keep the console quiet and restore the filter.
        class LoggingTest : public ::testing::Test
        {
        protected:
            Captured    captured;
            LogSeverity previousMinimum = GetMinimumLogSeverity();

            void SetUp() override
            {
                SetLogConsoleOutput(false);
                SetMinimumLogSeverity(LogSeverity::Trace);
                ASSERT_TRUE(AddLogSink(CaptureSink, &captured));
            }

            void TearDown() override
            {
                RemoveLogSink(CaptureSink, &captured);
                SetMinimumLogSeverity(previousMinimum);
                SetLogConsoleOutput(true);
            }
        };

        TEST_F(LoggingTest, ArgumentsAreFormattedIntoTheTemplate)
        {
            INTERSTICE_LOG_INFO(kTestCategory, "loaded {} items ({} skipped) in {} ms from {}", 12, 3u, 1.5, "disk");
            ASSERT_EQ(captured.messages.size(), 1u);
            EXPECT_EQ(captured.messages[0], "loaded 12 items (3 skipped) in 1.5 ms from disk");

            const LogRecord& r = captured.records[0];
            EXPECT_EQ(r.category.id, kTestCategory.id);
            EXPECT_EQ(r.category.name, "test");
            EXPECT_EQ(r.severity, LogSeverity::Info);
            EXPECT_EQ(r.argCount, 4);
            EXPECT_EQ(r.args[0].kind, LogArg::Kind::Int);
            EXPECT_EQ(r.args[0].i, 12);
            EXPECT_EQ(r.args[1].kind, LogArg::Kind::Uint);
            EXPECT_EQ(r.args[2].kind, LogArg::Kind::Float);
            EXPECT_EQ(LogArgText(r, r.args[3]), "disk");
            EXPECT_GT(r.line, 0u);
            EXPECT_NE(std::string_view(r.file).find("logging_test"), std::string_view::npos);
        }

        TEST_F(LoggingTest, TextArgumentsAreCopiedIntoTheRecord)
        {
            {
                std::string path = "themes/styles.css";
                INTERSTICE_LOG_ERROR(kTestCategory, "{}:{}: {}", path, 7, std::string("bad value"));
                path.assign(path.size(), 'x'); // the record must not view the caller's string
            }
            ASSERT_EQ(captured.records.size(), 1u);
            char message[128];
            FormatLogMessage(captured.records[0], message);
            EXPECT_STREQ(message, "themes/styles.css:7: bad value");
        }

        TEST_F(LoggingTest, OversizedTextIsTruncated)
        {
            const std::string longText(kLogTextCapacity * 2, 'a');
            INTERSTICE_LOG_WARNING(kTestCategory, "[{}]", longText);
            ASSERT_EQ(captured.messages.size(), 1u);
            EXPECT_EQ(captured.messages[0], "[" + std::string(kLogTextCapacity, 'a') + "]");
        }

        TEST_F(LoggingTest, FormattingHandlesMissingArgumentsAndSmallBuffers)
        {
            INTERSTICE_LOG_INFO(kTestCategory, "{} and {}", 1);
            ASSERT_EQ(captured.messages.size(), 1u);
            EXPECT_EQ(captured.messages[0], "1 and {}");

            char tiny[5];
            EXPECT_EQ(FormatLogMessage(captured.records[0], tiny), 4u);
            EXPECT_STREQ(tiny, "1 an");
        }

        TEST_F(LoggingTest, RecordsBelowTheMinimumAreDropped)
        {
            SetMinimumLogSeverity(LogSeverity::Warning);
            const uint64_t before = LatestLogSequence();
            INTERSTICE_LOG_INFO(kTestCategory, "info");
            INTERSTICE_LOG_DEBUG(kTestCategory, "debug");
            INTERSTICE_LOG_WARNING(kTestCategory, "warning");
            INTERSTICE_LOG_CRITICAL(kTestCategory, "critical");
            ASSERT_EQ(captured.records.size(), 2u);
            EXPECT_EQ(captured.records[0].severity, LogSeverity::Warning);
            EXPECT_EQ(captured.records[1].severity, LogSeverity::Critical);
            EXPECT_EQ(LatestLogSequence(), before + 2) << "dropped records don't reach the ring either";
        }

        TEST_F(LoggingTest, RingKeepsTheMostRecentRecordsOldestFirst)
        {
            const uint64_t before = LatestLogSequence();
            for (int i = 0; i < kLogRingCapacity + 10; ++i)
                INTERSTICE_LOG_INFO(kTestCategory, "record {}", i);
            EXPECT_EQ(LatestLogSequence(), before + kLogRingCapacity + 10);

            std::vector<LogRecord> recent(kLogRingCapacity + 5);
            const uint32_t count = CopyRecentLogRecords(recent);
            ASSERT_EQ(count, uint32_t(kLogRingCapacity));
            EXPECT_EQ(recent[0].sequence, before + 11) << "the oldest 10 were overwritten";
            EXPECT_EQ(recent[count - 1].sequence, LatestLogSequence());
            EXPECT_EQ(recent[0].args[0].i, 10);

            std::vector<LogRecord> newest(3);
            ASSERT_EQ(CopyRecentLogRecords(newest), 3u);
            EXPECT_EQ(newest[2].sequence, LatestLogSequence());
            EXPECT_EQ(newest[0].sequence, LatestLogSequence() - 2);
        }

        TEST_F(LoggingTest, SinksAreUniqueAndRemovable)
        {
            int count = 0;
            EXPECT_TRUE(AddLogSink(CountSink, &count));
            EXPECT_FALSE(AddLogSink(CountSink, &count)) << "the same sink twice is rejected";
            INTERSTICE_LOG_INFO(kTestCategory, "one");
            RemoveLogSink(CountSink, &count);
            INTERSTICE_LOG_INFO(kTestCategory, "two");
            EXPECT_EQ(count, 1);
            EXPECT_EQ(captured.records.size(), 2u);
        }

        TEST_F(LoggingTest, ConcurrentEmittersLoseNothing)
        {
            int count = 0;
            ASSERT_TRUE(AddLogSink(CountSink, &count));
            constexpr int kPerThread = 500;
            auto emit = [] { for (int i = 0; i < kPerThread; ++i) INTERSTICE_LOG_DEBUG(kTestCategory, "tick {}", i); };
            std::thread a(emit), b(emit);
            a.join();
            b.join();
            RemoveLogSink(CountSink, &count);
            EXPECT_EQ(count, 2 * kPerThread);

            std::vector<LogRecord> recent(2 * kPerThread);
            ASSERT_EQ(CopyRecentLogRecords(recent), uint32_t(2 * kPerThread));
            for (size_t i = 1; i < recent.size(); ++i)
                ASSERT_EQ(recent[i].sequence, recent[i - 1].sequence + 1);
            EXPECT_NE(recent.front().threadId, 0u);
        }
    }
}
