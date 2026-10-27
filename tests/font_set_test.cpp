// The font set (#61): built-in fonts (Inter, JetBrains Mono) are enough on their own,
// the Noto fallbacks are optional, and families can come from memory.

#include "skribidi_test_fixture.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace tachyon
{
    namespace
    {
        // Initializes the text system itself, with the directories each test picks.
        class FontSetTest : public GUITest::SkribidiHeadlessTest
        {
        protected:
            void SetUp() override { textSystem = std::make_unique<SkribidiTextSystem>(); }

            bool Init(std::string_view builtIn, std::string_view fallback)
            {
                textSystem->SetFontDirectory(builtIn);
                textSystem->SetFallbackFontDirectory(fallback);
                return textSystem->Initialize(&graphicsContext, &styleManager, &atlasManager, &stateManager, &clipboard);
            }

            StyleRef TextStyle(FontID font = FontID::Invalid, FontWeight weight = FontWeight::Normal)
            {
                Style s{};
                s.borderWidths    = BoxEdges(0);
                s.padding         = BoxEdges(0);
                s.textLayoutStyle = {};
                for (int i = 0; i < Style::kStateCount; ++i)
                {
                    s.states[i].textStyle.fontSize = 13.0f;
                    s.states[i].textStyle.fontId   = font;
                    s.states[i].textStyle.weight   = weight;
                }
                return styleManager.Register(s);
            }

            int32_t Width(std::string_view text, StyleRef style)
            {
                const TextID id = textSystem->RegisterText(ID{ .value{ .integer = ++m_nextID } }, IconID::Invalid, text, style, ElementState::None);
                return id == TextID::Invalid ? 0 : textSystem->GetTextMeasurement(id).maxWidth;
            }

            static std::vector<std::byte> ReadFile(const char* path)
            {
                std::ifstream file(path, std::ios::binary);
                std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::vector<std::byte> result(bytes.size());
                std::memcpy(result.data(), bytes.data(), bytes.size());
                return result;
            }

        private:
            uint64_t m_nextID = 0;
        };

        TEST_F(FontSetTest, BuiltInFontsAloneAreEnough)
        {
            ASSERT_TRUE(Init("fonts", ""));
            EXPECT_GT(Width("Hello", TextStyle()), 0);
        }

        // Inter's Medium face is built in: weight 500 sits between Regular and SemiBold
        // rather than falling back to either.
        TEST_F(FontSetTest, MediumWeightHasItsOwnFace)
        {
            ASSERT_TRUE(Init("fonts", ""));
            const std::string_view text = "Inspector Profiler Hierarchy";
            const int32_t regular  = Width(text, TextStyle(FontID::Invalid, FontWeight::Normal));
            const int32_t medium   = Width(text, TextStyle(FontID::Invalid, FontWeight::Medium));
            const int32_t semiBold = Width(text, TextStyle(FontID::Invalid, FontWeight::DemiBold));
            EXPECT_GT(medium, regular);
            EXPECT_LT(medium, semiBold);
        }

        TEST_F(FontSetTest, AMissingFallbackSetIsSkipped)
        {
            ASSERT_TRUE(Init("fonts", "fonts/no-such-folder"));
            EXPECT_GT(Width("Hello", TextStyle()), 0);
        }

        TEST_F(FontSetTest, MissingBuiltInFontsFailInitialization)
        {
            EXPECT_FALSE(Init("fonts/no-such-folder", ""));
        }

        TEST_F(FontSetTest, NoBuiltInFontsLeavesTheFontsToTheApp)
        {
            ASSERT_TRUE(Init("", ""));
            const std::vector<std::byte> inter = ReadFile("fonts/Inter/Inter-Regular.ttf");
            const FontFaceSource faces[] = { { .data = inter } };
            const FontID font = textSystem->RegisterFontFamily("App Font", faces);
            ASSERT_NE(font, FontID::Invalid);
            EXPECT_GT(Width("Hello", TextStyle(font)), 0);
        }

        TEST_F(FontSetTest, AFamilyRegistersFromMemory)
        {
            ASSERT_TRUE(Init("fonts", ""));
            const std::vector<std::byte> mono = ReadFile("fonts/JetBrainsMono/JetBrainsMono-Regular.ttf");
            ASSERT_FALSE(mono.empty());
            const FontFaceSource faces[] = { { .data = mono } };
            const FontID font = textSystem->RegisterFontFamily("Mono From Memory", faces);
            ASSERT_NE(font, FontID::Invalid);
            const StyleRef style = TextStyle(font);
            EXPECT_EQ(Width("iiii", style), Width("WWWW", style)) << "a monospaced face";
        }

        TEST_F(FontSetTest, DataThatIsNoFontIsRefused)
        {
            ASSERT_TRUE(Init("fonts", ""));
            const std::byte garbage[] = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };
            const FontFaceSource faces[] = { { .data = garbage } };
            EXPECT_EQ(textSystem->RegisterFontFamily("Garbage", faces), FontID::Invalid);
        }

        // Changing a family's faces rebuilds the font collection; families from memory
        // come back with it.
        TEST_F(FontSetTest, FamiliesFromMemorySurviveARebuild)
        {
            ASSERT_TRUE(Init("fonts", ""));
            const std::vector<std::byte> mono = ReadFile("fonts/JetBrainsMono/JetBrainsMono-Regular.ttf");
            const FontFaceSource memoryFaces[] = { { .data = mono } };
            const FontID font = textSystem->RegisterFontFamily("Mono From Memory", memoryFaces);
            ASSERT_NE(font, FontID::Invalid);

            const std::string_view regular[] = { "fonts/Inter/Inter-Regular.ttf" };
            const std::string_view bold[]    = { "fonts/Inter/Inter-Bold.ttf" };
            ASSERT_NE(textSystem->RegisterFontFamily("Other", regular), FontID::Invalid);
            ASSERT_NE(textSystem->RegisterFontFamily("Other", bold), FontID::Invalid);   // rebuilds

            const StyleRef style = TextStyle(font);
            EXPECT_GT(Width("iiii", style), 0);
            EXPECT_EQ(Width("iiii", style), Width("WWWW", style));
        }
    }
}
