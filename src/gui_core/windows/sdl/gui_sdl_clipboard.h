#pragma once

#include <gui_core/text/gui_clipboard.h>

namespace tachyon
{
    /// @addtogroup windows
    /// @{

    /// The system clipboard, through SDL.
    class SDLClipboard : public IClipboard
    {
    public:
        std::string Get() const override;
        void        Set(std::string_view text) override;
    };
    /// @}
}
