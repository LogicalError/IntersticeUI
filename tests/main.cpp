#include "gtest/gtest.h"
#include <gmock/gmock.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <cstdio>
#include <filesystem>
#include <system_error>

// ============================================================================
// Main
// ============================================================================
//
// The harness loads `themes/dark.css` relative to the CWD. ctest / Visual Studio
// set it to workdirectory/; when the exe is launched from elsewhere, find that
// directory by walking up from the executable. If none is found the CWD is left
// alone, so tests fail loudly (InvalidStyle) instead of using another theme.
static void EnsureWorkingDirectory(const char* argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists("themes/dark.css", ec) && !ec) return;

    // Walk up looking for `workdirectory/themes/dark.css` (build tree) or
    // `themes/dark.css` (installed layout).
    fs::path exePath = argv0 ? fs::absolute(argv0, ec) : fs::path{};
    if (ec || exePath.empty()) return;
    fs::path dir = exePath.parent_path();

    for (int i = 0; i < 8 && !dir.empty(); ++i)
    {
        if (fs::exists(dir / "workdirectory" / "themes" / "dark.css", ec) && !ec)
        {
            fs::current_path(dir / "workdirectory", ec);
            return;
        }
        if (fs::exists(dir / "themes" / "dark.css", ec) && !ec)
        {
            fs::current_path(dir, ec);
            return;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
}

int main(int argc, char** argv)
{
    EnsureWorkingDirectory(argc > 0 ? argv[0] : nullptr);
    // Every test boots a theme; only problems are worth printing.
    interstice::Diagnostics::SetMinimumLogSeverity(interstice::Diagnostics::LogSeverity::Warning);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
