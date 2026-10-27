// The tutorial (docs/tutorial.md and docs/tutorial/*.md) shows the code of examples/tutorial,
// so the two can't drift apart. A page marks each code block with the region it shows:
//
//     <!-- snippet: examples/tutorial/my_app.cpp [counter] -->
//     ```cpp
//     ...
//     ```
//
// and the source marks the region with a line "// [counter]" before and after it.
// The block must equal the region (indentation and line endings aside), and every region in
// examples/tutorial must be shown somewhere in the tutorial.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // The repository root: the tests run in workdirectory/ or below it.
    fs::path RepositoryRoot()
    {
        for (fs::path dir = fs::current_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (fs::exists(dir / "docs" / "tutorial.md"))
                return dir;
            if (dir == dir.parent_path())
                break;
        }
        return {};
    }

    std::vector<std::string> ReadLines(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        std::vector<std::string> lines;
        for (std::string line; std::getline(in, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            lines.push_back(line);
        }
        return lines;
    }

    // Lines without the indentation they all share, and without leading and trailing blank lines.
    std::string Normalize(std::vector<std::string> lines)
    {
        while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
        size_t indent = std::string::npos;
        for (const std::string& line : lines)
            if (!line.empty())
                indent = std::min(indent, line.find_first_not_of(' '));
        std::string text;
        for (const std::string& line : lines)
            text += (line.size() > indent ? line.substr(indent) : std::string{}) + "\n";
        return text;
    }

    bool IsMarker(const std::string& line, std::string* region)
    {
        static const std::regex marker(R"(^\s*(//|#)\s*\[([a-z0-9-]+)\]\s*$)");
        std::smatch m;
        if (!std::regex_match(line, m, marker)) return false;
        if (region) *region = m[2];
        return true;
    }

    // Every region of a source file, by name.
    std::map<std::string, std::string> Regions(const fs::path& file)
    {
        std::map<std::string, std::string> regions;
        const std::vector<std::string> lines = ReadLines(file);
        std::string open;
        std::vector<std::string> body;
        for (const std::string& line : lines)
        {
            std::string name;
            if (IsMarker(line, &name))
            {
                if (open.empty()) { open = name; body.clear(); }
                else if (name == open) { regions[open] = Normalize(body); open.clear(); }
                else ADD_FAILURE() << file << ": region [" << name << "] starts inside [" << open << "]";
                continue;
            }
            if (!open.empty())
                body.push_back(line);
        }
        EXPECT_TRUE(open.empty()) << file << ": region [" << open << "] is not closed";
        return regions;
    }

    struct Snippet
    {
        std::string page;
        int         line = 0;
        std::string file;
        std::string region;
        std::string code;
    };

    // The marked code blocks of a tutorial page.
    std::vector<Snippet> Snippets(const fs::path& page)
    {
        static const std::regex marker(R"(^<!--\s*snippet:\s*(\S+)\s+\[([a-z0-9-]+)\]\s*-->$)");
        std::vector<Snippet> snippets;
        const std::vector<std::string> lines = ReadLines(page);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::smatch m;
            if (!std::regex_match(lines[i], m, marker))
                continue;
            Snippet s{ page.filename().string(), int(i + 1), m[1], m[2], {} };
            size_t start = i + 1;
            if (start >= lines.size() || lines[start].rfind("```", 0) != 0)
            {
                ADD_FAILURE() << page << ":" << s.line << ": no code block right after the snippet marker";
                continue;
            }
            std::vector<std::string> body;
            size_t end = start + 1;
            for (; end < lines.size() && lines[end].rfind("```", 0) != 0; ++end)
                body.push_back(lines[end]);
            s.code = Normalize(body);
            snippets.push_back(std::move(s));
            i = end;
        }
        return snippets;
    }

    class TutorialSnippets : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            root = RepositoryRoot();
            ASSERT_FALSE(root.empty()) << "docs/tutorial.md not found above " << fs::current_path();
            pages.push_back(root / "docs" / "tutorial.md");
            for (const auto& entry : fs::directory_iterator(root / "docs" / "tutorial"))
                if (entry.path().extension() == ".md")
                    pages.push_back(entry.path());
            std::sort(pages.begin(), pages.end());
        }

        fs::path              root;
        std::vector<fs::path> pages;
    };

    TEST_F(TutorialSnippets, EveryCodeBlockMatchesTheSource)
    {
        int checked = 0;
        for (const fs::path& page : pages)
            for (const Snippet& s : Snippets(page))
            {
                const fs::path source = root / s.file;
                ASSERT_TRUE(fs::exists(source)) << s.page << ":" << s.line << ": " << s.file << " doesn't exist";
                const auto regions = Regions(source);
                const auto it = regions.find(s.region);
                ASSERT_NE(it, regions.end()) << s.page << ":" << s.line << ": " << s.file << " has no region [" << s.region << "]";
                EXPECT_EQ(s.code, it->second) << s.page << ":" << s.line << " shows a different [" << s.region << "] than " << s.file;
                ++checked;
            }
        EXPECT_GT(checked, 0) << "the tutorial has no marked code blocks";
    }

    TEST_F(TutorialSnippets, EveryRegionIsShownInTheTutorial)
    {
        std::set<std::string> shown;
        for (const fs::path& page : pages)
            for (const Snippet& s : Snippets(page))
                shown.insert(s.file + " [" + s.region + "]");

        for (const auto& entry : fs::directory_iterator(root / "examples" / "tutorial"))
        {
            const std::string file = "examples/tutorial/" + entry.path().filename().string();
            for (const auto& [region, code] : Regions(entry.path()))
                EXPECT_TRUE(shown.contains(file + " [" + region + "]"))
                    << file << " [" << region << "] is marked but no tutorial page shows it";
        }
    }
}
