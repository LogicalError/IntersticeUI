#pragma once

#include <string>
#include <string_view>

namespace interstice
{
    /// The clipboard text editors copy to and paste from; GUIApplication::SetClipboard sets
    /// one (default: SDL's, or LocalClipboard without SDL).
    class IClipboard
    {
    public:
        virtual ~IClipboard() = default;

        /// The clipboard's text, UTF-8; empty when it holds none.
        virtual std::string Get() const = 0;
        /// Puts UTF-8 text on the clipboard.
        virtual void Set(std::string_view text) = 0;
    };

    /// A clipboard inside the process, not shared with other applications.
    class LocalClipboard final : public IClipboard
    {
    public:
        std::string Get() const override { return m_text; }
        void Set(std::string_view text) override { m_text = text; }

    private:
        std::string m_text;
    };
}
