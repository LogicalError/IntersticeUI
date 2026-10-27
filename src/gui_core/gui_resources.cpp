#include <gui_core/gui_resources.h>
#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tachyon
{
    namespace
    {
        constexpr Diagnostics::LogCategory kLogResources = Diagnostics::MakeLogCategory("resources");

        // Windows file names ignore case, so the built-in copies do too there.
        bool SameName(std::string_view a, std::string_view b)
        {
#ifdef _WIN32
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
                { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
#else
            return a == b;
#endif
        }
    }

    namespace detail
    {
        std::span<const std::byte> FindEmbeddedFile(std::span<const EmbeddedFile> files, std::string_view path)
        {
            const std::string normal = std::filesystem::path(path).lexically_normal().generic_string();
            const std::string_view p = normal;
            for (const EmbeddedFile& file : files)
            {
                if (p.size() < file.key.size())
                    continue;
                const std::string_view tail = p.substr(p.size() - file.key.size());
                const bool atBoundary = p.size() == file.key.size() || p[p.size() - file.key.size() - 1] == '/';
                if (atBoundary && SameName(tail, file.key))
                    return file.data;
            }
            return {};
        }
    }

    std::span<const std::byte> FindBuiltInResource(std::string_view path)
    {
        return detail::FindEmbeddedFile(detail::EmbeddedResourceFiles(), path);
    }

    Result<std::string> ReadResource(std::string_view path)
    {
        const std::filesystem::path file(path);
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec))
        {
            std::ifstream in(file, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            if (in.good() || in.eof())
                return text.str();
            return Error{ ErrorCode::Failed, "could not read " + std::string(path) };
        }
        if (const std::span<const std::byte> data = FindBuiltInResource(path); !data.empty())
        {
            TACHYON_LOG_INFO(kLogResources, "{}: no such file, using the built-in copy", path);
            return std::string(reinterpret_cast<const char*>(data.data()), data.size());
        }
        return Error{ ErrorCode::NotFound, std::string(path) + " not found, and the library has no built-in copy" };
    }
}
