// The default themes and fonts compiled into the library: found by the end of their
// path, kept in step with the files in workdirectory/, and enough for an app that has no
// resource files at all.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/gui_resources.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;

    // A folder that doesn't exist, so nothing can come from disk.
    std::string MissingFolder()
    {
        return (std::filesystem::temp_directory_path() / "tachyon-no-such-resources").generic_string();
    }

    bool Embedded() { return !detail::EmbeddedResourceFiles().empty(); }

    TEST(BuiltInResources, FoundByTheEndOfTheirPath)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        EXPECT_FALSE(FindBuiltInResource("themes/styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("C:/app/resources/themes/styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("/opt/app/themes/../fonts/Inter/Inter-Regular.ttf").empty());
#ifdef _WIN32
        // Backslashes separate folders only on Windows.
        EXPECT_FALSE(FindBuiltInResource("C:\\app\\resources\\themes\\styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("C:\\app\\Resources\\Themes\\Styles.css").empty()) << "Windows paths ignore case";
#endif
        EXPECT_FALSE(FindBuiltInResource("fonts/JetBrainsMono/JetBrainsMono-Bold.ttf").empty());
    }

    TEST(BuiltInResources, OnlyAtAFolderBoundary)
    {
        EXPECT_TRUE(FindBuiltInResource("styles.css").empty());
        EXPECT_TRUE(FindBuiltInResource("mythemes/styles.css").empty());
        EXPECT_TRUE(FindBuiltInResource("themes/showcase.css").empty());
    }

    // The tests run in workdirectory/, which holds the files the copies were made from.
    TEST(BuiltInResources, MatchTheFilesTheyWereMadeFrom)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        for (const detail::EmbeddedFile& file : detail::EmbeddedResourceFiles())
        {
            std::ifstream in(std::string(file.key), std::ios::binary);
            ASSERT_TRUE(in.good()) << file.key;
            const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ASSERT_EQ(bytes.size(), file.data.size()) << file.key;
            EXPECT_EQ(std::memcmp(bytes.data(), file.data.data(), bytes.size()), 0) << file.key;
        }
    }

    // A file on disk wins over the built-in copy of the same name.
    TEST(BuiltInResources, ReadResourcePrefersTheFileOnDisk)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "tachyon-resource-test" / "themes";
        std::filesystem::create_directories(folder);
        const std::filesystem::path file = folder / "dark.css";
        {
            std::ofstream out(file, std::ios::binary);
            out << ":root { --accent: #123456; }\n";
        }
        const Result<std::string> read = ReadResource(file.string());
        std::error_code ec;
        std::filesystem::remove(file, ec);
        ASSERT_TRUE(read) << read.GetError().message;
        EXPECT_EQ(*read, ":root { --accent: #123456; }\n");
    }

    TEST(BuiltInResources, ReadResourceReportsAFileWithNoBuiltInCopy)
    {
        const Result<std::string> missing = ReadResource(MissingFolder() + "/themes/showcase.css");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.GetError().code, ErrorCode::NotFound);
    }

    TEST(BuiltInResources, ReadResourceFallsBackToTheBuiltInCopy)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        const Result<std::string> builtIn = ReadResource(MissingFolder() + "/themes/dark.css");
        ASSERT_TRUE(builtIn) << builtIn.GetError().message;
        const Result<std::string> onDisk = ReadResource("themes/dark.css");
        ASSERT_TRUE(onDisk);
        EXPECT_EQ(*builtIn, *onDisk);
    }

    // An app with no resource files loads the default theme: palette, styles.css and the
    // @font-face fonts it names.
    TEST(BuiltInResources, AThemeLoadsWithoutFiles)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        GUITest::HeadlessGUIApp app(MissingFolder() + "/themes/dark.css");
        ASSERT_TRUE(app.Initialize());
        EXPECT_TRUE(app.theme.FindStyle("button")) << "styles.css is loaded";
        EXPECT_TRUE(app.theme.FindStyle("window"));
        EXPECT_NE(app.theme.GetColor("--accent", color4f{ 0, 0, 0, 0 }).a, 0.0f) << "the palette is loaded";
    }
}
