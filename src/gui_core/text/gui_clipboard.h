#pragma once

#include <string>
#include <string_view>

namespace interstice
{
    class IClipboard
    {
    public:
        virtual ~IClipboard() = default;

        virtual std::string Get() const = 0;
        virtual void Set(std::string_view text) = 0;
    };

    // A clipboard inside the process, not shared with other applications.
    class LocalClipboard final : public IClipboard
    {
    public:
        std::string Get() const override { return m_text; }
        void Set(std::string_view text) override { m_text = text; }

    private:
        std::string m_text;
    };
}
