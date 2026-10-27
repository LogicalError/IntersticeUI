// FromChars reads floating point everywhere: std::from_chars where the standard
// library has it, a classic-locale stream where it doesn't (Apple's libc++).
#include <gtest/gtest.h>
#include <gui_core/gui_from_chars.h>

#include <string_view>

namespace tachyon
{
    namespace
    {
        template <typename T>
        std::pair<T, size_t> Read(std::string_view text, bool stream)
        {
            T value{};
            const auto r = stream ? FromCharsStream(text.data(), text.data() + text.size(), value)
                                  : FromChars(text.data(), text.data() + text.size(), value);
            if (r.ec != std::errc{}) return { T(-12345), size_t(-1) };
            return { value, size_t(r.ptr - text.data()) };
        }

        class FromCharsTest : public ::testing::TestWithParam<bool> {};

        TEST_P(FromCharsTest, ReadsNumbersAndStopsAfterThem)
        {
            const bool stream = GetParam();
            EXPECT_EQ(Read<float>("1.5", stream), std::make_pair(1.5f, size_t(3)));
            EXPECT_EQ(Read<float>("-0.25px", stream), std::make_pair(-0.25f, size_t(5)));
            EXPECT_EQ(Read<double>("1e3", stream), std::make_pair(1000.0, size_t(3)));
            EXPECT_EQ(Read<double>("0.5,", stream), std::make_pair(0.5, size_t(3)));
            EXPECT_EQ(Read<double>(".5", stream), std::make_pair(0.5, size_t(2)));
            EXPECT_EQ(Read<double>("2e", stream), std::make_pair(2.0, size_t(1))) << "an 'e' without digits isn't part of it";
        }

        TEST_P(FromCharsTest, RejectsWhatFromCharsRejects)
        {
            const bool stream = GetParam();
            EXPECT_EQ(Read<float>("", stream).second, size_t(-1));
            EXPECT_EQ(Read<float>(" 1", stream).second, size_t(-1));
            EXPECT_EQ(Read<float>("+1", stream).second, size_t(-1));
            EXPECT_EQ(Read<float>("px", stream).second, size_t(-1));
            EXPECT_EQ(Read<float>("-", stream).second, size_t(-1));
            EXPECT_EQ(Read<float>(".", stream).second, size_t(-1));
        }

        INSTANTIATE_TEST_SUITE_P(StandardAndStream, FromCharsTest, ::testing::Values(false, true));
    }
}
