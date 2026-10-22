#include <gui_core/windows/sdl/gui_sdl_clipboard.h>

#include <SDL3/SDL.h>

namespace GUI
{
    std::string SDLClipboard::Get() const
    {
        // SDL_GetClipboardText returns a heap-allocated copy that must be freed.
        char* raw = SDL_GetClipboardText();
        if (raw == nullptr)
            return {};
        std::string out(raw);
        SDL_free(raw);
        return out;
    }

    void SDLClipboard::Set(std::string_view text)
    {
        std::string buffer(text);
        SDL_SetClipboardText(buffer.c_str());
    }
} // namespace GUI
