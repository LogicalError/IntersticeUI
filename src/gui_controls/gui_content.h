#pragma once

#include <gui_core/style/gui_style_text.h>

#include <string>
#include <string_view>

namespace interstice
{
    // What a control shows: a text, an icon or both, and an optional tooltip. It converts
    // from a string or an icon, so a control showing one of them takes it as is:
    //     Button("Save", onSave);
    //     Button(saveIcon, onSave);
    //     Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
    // It refers to its strings, which only need to live for the call.
    struct GUIContent
    {
        IconID           icon = IconID::Invalid;
        std::string_view text;
        std::string_view tooltip;   // shown on hover when not empty

        constexpr GUIContent() = default;
        constexpr GUIContent(const char* text) : text(text) {}
        constexpr GUIContent(std::string_view text) : text(text) {}
        GUIContent(const std::string& text) : text(text) {}
        constexpr GUIContent(std::string_view text, std::string_view tooltip) : text(text), tooltip(tooltip) {}
        constexpr GUIContent(IconID icon, std::string_view text = {}, std::string_view tooltip = {})
            : icon(icon), text(text), tooltip(tooltip) {}
    };
}
