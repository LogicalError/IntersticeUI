#pragma once

// Files compiled into the library, as cmake/EmbedFiles.cmake writes them.

#include <cstddef>
#include <span>
#include <string_view>

namespace tachyon::detail
{
    /// A file compiled into the library, named by a path relative to the resources
    /// ("themes/styles.css", "fonts/Inter/Inter-Regular.ttf", "box_shader.frag.spv").
    struct EmbeddedFile
    {
        std::string_view           key;
        std::span<const std::byte> data;
    };

    /// The default themes and fonts (empty with TACHYON_EMBED_RESOURCES off).
    std::span<const EmbeddedFile> EmbeddedResourceFiles();
    /// The Vulkan backend's compiled shaders.
    std::span<const EmbeddedFile> EmbeddedShaderFiles();

    /// The file whose key ends `path` at a folder boundary: "themes/styles.css" and
    /// "C:/app/resources/themes/../themes/styles.css" both find "themes/styles.css". Case is
    /// ignored on Windows, as Windows file names do. Empty when none does.
    std::span<const std::byte> FindEmbeddedFile(std::span<const EmbeddedFile> files, std::string_view path);
}
