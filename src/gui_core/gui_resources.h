#pragma once

#include <gui_core/gui_result.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace tachyon
{
    /// @addtogroup utilities
    /// @{

    /// The copy of a resource compiled into the library: the default themes (dark.css,
    /// light.css, styles.css) and fonts (Inter, JetBrains Mono), unless the library was built
    /// with TACHYON_EMBED_RESOURCES off. Found by the end of the path, so "themes/styles.css"
    /// and "C:/app/resources/themes/styles.css" both find it. Empty when there is none.
    std::span<const std::byte> FindBuiltInResource(std::string_view path);

    /// A resource's contents: the file at `path` when it exists, else its built-in copy.
    /// The library reads its themes this way, so an app needs no resource files, and a file
    /// of the same name under the resource root replaces the built-in one.
    Result<std::string> ReadResource(std::string_view path);

    /// @}
}
