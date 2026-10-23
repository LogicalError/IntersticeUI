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
}
