#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <gui_core/style/gui_style.h>

#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_platform_input_handler.h>

#include <gui_core/windows/gui_window_flags.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <SDL3/SDL.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Undefine Windows macro that conflicts with our CreateWindow method
#undef CreateWindow
#endif

static constexpr GUI::Diagnostics::LogCategory kLogSDL = GUI::Diagnostics::MakeLogCategory("sdl");

namespace GUI
{
    namespace
    {
        // SDL's memory functions take no user data and are process-wide.
        AllocationHooks  s_sdlHooks;
        constexpr size_t kSDLAlignment = alignof(std::max_align_t);

        void* SDLCALL SDLMalloc(size_t size)
        {
            return s_sdlHooks.allocate(size != 0 ? size : 1, kSDLAlignment, s_sdlHooks.user);
        }
        void* SDLCALL SDLCalloc(size_t count, size_t size)
        {
            if (size != 0 && count > SIZE_MAX / size) return nullptr;
            const size_t bytes = count * size != 0 ? count * size : 1;
            void* ptr = s_sdlHooks.allocate(bytes, kSDLAlignment, s_sdlHooks.user);
            if (ptr != nullptr) std::memset(ptr, 0, bytes);
            return ptr;
        }
        void* SDLCALL SDLRealloc(void* ptr, size_t size)
        {
            return s_sdlHooks.reallocate(ptr, size != 0 ? size : 1, kSDLAlignment, s_sdlHooks.user);
        }
        void SDLCALL SDLFree(void* ptr)
        {
            s_sdlHooks.free(ptr, s_sdlHooks.user);
        }
    }

    static MouseButtons ToMouseButtons(Uint32 sdlbuttons)
    {
        // Map SDL button mask to simple bitflags:
        // 1 = left, 2 = right, 4 = middle
        MouseButtons buttons = MouseButtons::None;
        if (sdlbuttons & SDL_BUTTON_LMASK)   buttons |= MouseButtons::Left;
        if (sdlbuttons & SDL_BUTTON_MMASK)   buttons |= MouseButtons::Middle;
        if (sdlbuttons & SDL_BUTTON_RMASK)   buttons |= MouseButtons::Right;
        if (sdlbuttons & SDL_BUTTON_X1MASK)  buttons |= MouseButtons::X1;
        if (sdlbuttons & SDL_BUTTON_X2MASK)  buttons |= MouseButtons::X2;
        return buttons;
    }

    SDLWindowBackend::SDLWindowBackend(GUI::GraphicsContext* graphicsContext, const AllocationHooks* hooks)
        : graphicsContext(graphicsContext)
    {
        // Replacing SDL's allocator is only safe before SDL holds any memory.
        if (hooks != nullptr && hooks->IsSet())
        {
            if (!SDL_WasInit(0) && SDL_GetNumAllocations() == 0)
            {
                s_sdlHooks = *hooks;
                m_routesSDLMemory = SDL_SetMemoryFunctions(&SDLMalloc, &SDLCalloc, &SDLRealloc, &SDLFree);
            }
            if (!m_routesSDLMemory)
                LOG_WARNING(kLogSDL, "SDL is already in use, so its memory doesn't go through the allocation hooks");
        }

        // Text editors draw the IME composition themselves (OnTextComposition);
        // the OS still draws the candidate list, placed via SetTextInputArea.
        SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");

        if (!SDL_WasInit(SDL_INIT_VIDEO))
        {
            if (!SDL_Init(SDL_INIT_VIDEO))
            {
                LOG_ERROR(kLogSDL, "SDL_Init failed: {}", SDL_GetError());
                m_sdlInitialized = false;
                return;
            }
            m_ownsSDL = true;
        }
        m_sdlInitialized = true;

        SDL_EventFilter filter =
            [](void* userdata, SDL_Event* event) -> bool
            {
                SDLWindowBackend* backend = static_cast<SDLWindowBackend*>(userdata);
                if (!backend)
                {
                    return true;
                }

                // Enforce modal behavior: if a modal window is active, suppress
                // events targeting other windows and keep the modal window raised.
                if (!backend->HandleModalFilterForEvent(event))
                {
                    // Event was consumed due to modality; do not continue processing.
                    return false;
                }

                // Handle resize events immediately for smooth resizing.
                // Suppressed during programmatic window changes (SetWindowRect/SetWindowVisible)
                // to prevent re-entering OnFrame via RenderWindowFrame while already mid-frame.
                if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
                    !backend->m_suppressLiveResize)
                {
                    SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
                    if (win)
                    {
                        IWindowContext* windowContext =
                            (IWindowContext*)SDL_GetPointerProperty(
                                SDL_GetWindowProperties(win),
                                "WindowContext",
                                nullptr);
                        if (windowContext != nullptr)
                        {
                            int w = 0, h = 0;
                            SDL_GetWindowSizeInPixels(win, &w, &h);
                            int x = 0, y = 0;
                            SDL_GetWindowPosition(win, &x, &y);
                            windowContext->RenderWindowFrame(backend->graphicsContext, GUI::IntRect{ x, y, w, h }, true);
                        }
                    }
                }

                // Non-popup window moved/resized; fires during the OS modal loop, where the frame loop is blocked.
                if (event->type == SDL_EVENT_WINDOW_MOVED ||
                    event->type == SDL_EVENT_WINDOW_RESIZED)
                {
                    SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
                    if (win)
                    {
                        SDL_WindowFlags flags = SDL_GetWindowFlags(win);
                        if (!(flags & SDL_WINDOW_UTILITY))
                        {
                            if (backend->m_onWindowMoved) backend->m_onWindowMoved();
                        }
                    }
                }

                return true;
            };

        // The event filter runs even during OS modal loops (e.g. resize on Windows), so it
        // handles live-resize rendering and modal enforcement.
        SDL_SetEventFilter(filter, this);

        UpdateMouseState();
    }

    SDLWindowBackend::~SDLWindowBackend()
    {
        // Destroy any remaining windows
        for (auto sdl_window : m_windows)
        {
            if (!sdl_window) continue;
            SDL_RemoveEventWatch(m_eventWatch, sdl_window);
            SDL_DestroyWindow(sdl_window);
        }
        m_windows.clear();

        for (SDL_Cursor*& cursor : m_systemCursors)
        {
            if (cursor) SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }

        // The filter's userdata is this backend; don't leave it behind in a host's SDL.
        SDL_EventFilter filter   = nullptr;
        void*           userdata = nullptr;
        if (SDL_GetEventFilter(&filter, &userdata) && userdata == this)
            SDL_SetEventFilter(nullptr, nullptr);

        if (m_ownsSDL)
            SDL_Quit();
        m_sdlInitialized = false;

        // Back to SDL's own allocator, unless SDL still holds memory from the hooks.
        if (m_routesSDLMemory)
        {
            if (const int live = SDL_GetNumAllocations(); live == 0)
            {
                SDL_malloc_func m; SDL_calloc_func c; SDL_realloc_func r; SDL_free_func f;
                SDL_GetOriginalMemoryFunctions(&m, &c, &r, &f);
                SDL_SetMemoryFunctions(m, c, r, f);
                s_sdlHooks = {};
            }
            else
                LOG_WARNING(kLogSDL, "SDL still holds {} allocations after SDL_Quit and keeps using the allocation hooks", live);
        }
    }

    OSWindowHandle SDLWindowBackend::CreateBootstrapWindow()
    {
        if (!m_sdlInitialized) return nullptr;

        // Create a hidden window for Vulkan initialization
        SDL_Window* bootstrapWindow = SDL_CreateWindow(
            "Vulkan Bootstrap Window",
            800, 600,
            SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN
        );
        if (bootstrapWindow == nullptr)
            LOG_ERROR(kLogSDL, "SDL_CreateWindow failed: {}", SDL_GetError());

        return reinterpret_cast<OSWindowHandle>(bootstrapWindow);
    }

    void SDLWindowBackend::DestroyBootstrapWindow(OSWindowHandle bootstrapWindow)
    {
        if (bootstrapWindow != nullptr)
        {
            SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(bootstrapWindow));
        }
    }

    OSWindowHandle SDLWindowBackend::CreateWindow(std::string_view title, int2 size, GUI::WindowFlags windowFlags, OSWindowHandle parentOSHandle)
    {
        return CreateWindow(title, IntRect { SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, size.x, size.y },
                            windowFlags, parentOSHandle);
    }
    
    static int32_t RoundToInt(float v) { return int32_t(std::lround(v)); }

    // A mouse position inside `window` (OS window coordinates) in screen pixels.
    static GUI::int2 PixelPointOf(SDL_Window* window, float x, float y)
    {
        auto* backend = (SDLWindowBackend*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), "IntersticeBackend", nullptr);
        if (backend == nullptr)
        {
            int winX = 0, winY = 0;
            SDL_GetWindowPosition(window, &winX, &winY);
            return GUI::int2{ int32_t(x) + winX, int32_t(y) + winY };
        }
        return backend->WindowPointToPixels(window, x, y);
    }

    static IWindowContext* LocalGetWindowContext(OSWindowHandle handle)
    {
        return (IWindowContext*)SDL_GetPointerProperty(SDL_GetWindowProperties(SDLWindowBackend::ToSDL(handle)), "WindowContext", nullptr);
    }

    static IPlatformInputHandler* LocalGetInputHandler(OSWindowHandle handle)
    {
        return (IPlatformInputHandler*)SDL_GetPointerProperty(SDL_GetWindowProperties(SDLWindowBackend::ToSDL(handle)), "InputHandler", nullptr);
    }


    IWindowContext* SDLWindowBackend::GetWindowContext(OSWindowHandle handle)
    {
        return LocalGetWindowContext(handle);
    }

    static KeyCode ToKeyCode(SDL_Keycode key, SDL_Keymod modifiers)
    {
        // Handle numpad keys with numlock state
        bool numlockOn = (modifiers & SDL_KMOD_NUM) != 0;
        
        if (!numlockOn)
        {
            // When numlock is OFF, numpad keys act as navigation/editing keys
            switch (key)
            {
                case SDLK_KP_0:        return KeyCode::Insert;
                case SDLK_KP_1:        return KeyCode::End;
                case SDLK_KP_2:        return KeyCode::Down;
                case SDLK_KP_3:        return KeyCode::PageDown;
                case SDLK_KP_4:        return KeyCode::Left;
                case SDLK_KP_5:        return KeyCode::None; // Center key - no standard mapping
                case SDLK_KP_6:        return KeyCode::Right;
                case SDLK_KP_7:        return KeyCode::Home;
                case SDLK_KP_8:        return KeyCode::Up;
                case SDLK_KP_9:        return KeyCode::PageUp;
                case SDLK_KP_PERIOD:   return KeyCode::Delete;
            }
        }
        else
        {
            // When numlock is ON, numpad keys act as numeric keys
            switch (key)
            {
                case SDLK_KP_0:        return KeyCode::Key0;
                case SDLK_KP_1:        return KeyCode::Key1;
                case SDLK_KP_2:        return KeyCode::Key2;
                case SDLK_KP_3:        return KeyCode::Key3;
                case SDLK_KP_4:        return KeyCode::Key4;
                case SDLK_KP_5:        return KeyCode::Key5;
                case SDLK_KP_6:        return KeyCode::Key6;
                case SDLK_KP_7:        return KeyCode::Key7;
                case SDLK_KP_8:        return KeyCode::Key8;
                case SDLK_KP_9:        return KeyCode::Key9;
                case SDLK_KP_PERIOD:   return KeyCode::Period;
            }
        }

        switch (key)
        {
            case SDLK_RETURN:             return KeyCode::Return;
            case SDLK_ESCAPE:             return KeyCode::Escape;
            case SDLK_BACKSPACE:          return KeyCode::Backspace;
            case SDLK_TAB:                return KeyCode::Tab;
            case SDLK_SPACE:              return KeyCode::Space;
            case SDLK_EXCLAIM:            return KeyCode::Exclaim;
            case SDLK_DBLAPOSTROPHE:      return KeyCode::DoubleQuote;
            case SDLK_HASH:               return KeyCode::Hash;
            case SDLK_PERCENT:            return KeyCode::Percent;
            case SDLK_DOLLAR:             return KeyCode::Dollar;
            case SDLK_AMPERSAND:          return KeyCode::Ampersand;
            case SDLK_APOSTROPHE:         return KeyCode::Quote;
            case SDLK_LEFTPAREN:          return KeyCode::LeftParen;
            case SDLK_RIGHTPAREN:         return KeyCode::RightParen;
            case SDLK_ASTERISK:           return KeyCode::Asterisk;
            case SDLK_PLUS:               return KeyCode::Plus;
            case SDLK_COMMA:              return KeyCode::Comma;
            case SDLK_MINUS:              return KeyCode::Minus;
            case SDLK_PERIOD:             return KeyCode::Period;
            case SDLK_SLASH:              return KeyCode::Slash;
            case SDLK_0:                  return KeyCode::Key0;
            case SDLK_1:                  return KeyCode::Key1;
            case SDLK_2:                  return KeyCode::Key2;
            case SDLK_3:                  return KeyCode::Key3;
            case SDLK_4:                  return KeyCode::Key4;
            case SDLK_5:                  return KeyCode::Key5;
            case SDLK_6:                  return KeyCode::Key6;
            case SDLK_7:                  return KeyCode::Key7;
            case SDLK_8:                  return KeyCode::Key8;
            case SDLK_9:                  return KeyCode::Key9;
            case SDLK_COLON:              return KeyCode::Colon;
            case SDLK_SEMICOLON:          return KeyCode::Semicolon;
            case SDLK_LESS:               return KeyCode::Less;
            case SDLK_EQUALS:             return KeyCode::Equals;
            case SDLK_GREATER:            return KeyCode::Greater;
            case SDLK_QUESTION:           return KeyCode::Question;
            case SDLK_AT:                 return KeyCode::At;
                                           
            case SDLK_LEFTBRACKET:        return KeyCode::LeftBracket;
            case SDLK_BACKSLASH:          return KeyCode::Backslash;
            case SDLK_RIGHTBRACKET:       return KeyCode::RightBracket;
            case SDLK_CARET:              return KeyCode::Caret;
            case SDLK_UNDERSCORE:         return KeyCode::Underscore;
            case SDLK_GRAVE:              return KeyCode::Backquote;
            case SDLK_A:                  return KeyCode::KeyA;
            case SDLK_B:                  return KeyCode::KeyB;
            case SDLK_C:                  return KeyCode::KeyC;
            case SDLK_D:                  return KeyCode::KeyD;
            case SDLK_E:                  return KeyCode::KeyE;
            case SDLK_F:                  return KeyCode::KeyF;
            case SDLK_G:                  return KeyCode::KeyG;
            case SDLK_H:                  return KeyCode::KeyH;
            case SDLK_I:                  return KeyCode::KeyI;
            case SDLK_J:                  return KeyCode::KeyJ;
            case SDLK_K:                  return KeyCode::KeyK;
            case SDLK_L:                  return KeyCode::KeyL;
            case SDLK_M:                  return KeyCode::KeyM;
            case SDLK_N:                  return KeyCode::KeyN;
            case SDLK_O:                  return KeyCode::KeyO;
            case SDLK_P:                  return KeyCode::KeyP;
            case SDLK_Q:                  return KeyCode::KeyQ;
            case SDLK_R:                  return KeyCode::KeyR;
            case SDLK_S:                  return KeyCode::KeyS;
            case SDLK_T:                  return KeyCode::KeyT;
            case SDLK_U:                  return KeyCode::KeyU;
            case SDLK_V:                  return KeyCode::KeyV;
            case SDLK_W:                  return KeyCode::KeyW;
            case SDLK_X:                  return KeyCode::KeyX;
            case SDLK_Y:                  return KeyCode::KeyY;
            case SDLK_Z:                  return KeyCode::KeyZ;
                                           
            case SDLK_CAPSLOCK:           return KeyCode::Capslock;
                                           
            case SDLK_F1:                 return KeyCode::F1;
            case SDLK_F2:                 return KeyCode::F2;
            case SDLK_F3:                 return KeyCode::F3;
            case SDLK_F4:                 return KeyCode::F4;
            case SDLK_F5:                 return KeyCode::F5;
            case SDLK_F6:                 return KeyCode::F6;
            case SDLK_F7:                 return KeyCode::F7;
            case SDLK_F8:                 return KeyCode::F8;
            case SDLK_F9:                 return KeyCode::F9;
            case SDLK_F10:                return KeyCode::F10;
            case SDLK_F11:                return KeyCode::F11;
            case SDLK_F12:                return KeyCode::F12;
                                           
            case SDLK_PRINTSCREEN:        return KeyCode::Printscreen;
            case SDLK_SCROLLLOCK:         return KeyCode::Scrolllock;
            case SDLK_PAUSE:              return KeyCode::Pause;
            case SDLK_INSERT:             return KeyCode::Insert;
            case SDLK_HOME:               return KeyCode::Home;
            case SDLK_PAGEUP:             return KeyCode::PageUp;
            case SDLK_DELETE:             return KeyCode::Delete;
            case SDLK_END:                return KeyCode::End;
            case SDLK_PAGEDOWN:           return KeyCode::PageDown;
            case SDLK_RIGHT:              return KeyCode::Right;
            case SDLK_LEFT:               return KeyCode::Left;
            case SDLK_DOWN:               return KeyCode::Down;
            case SDLK_UP:                 return KeyCode::Up;
                                           
            case SDLK_NUMLOCKCLEAR:       return KeyCode::NUMLOCKCLEAR;

            case SDLK_KP_DIVIDE:          return KeyCode::KeyPadDivide;
            case SDLK_KP_MULTIPLY:        return KeyCode::KeyPadMultiply;
            case SDLK_KP_MINUS:           return KeyCode::KeyPadMinus;
            case SDLK_KP_PLUS:            return KeyCode::KeyPadPlus;
            case SDLK_KP_ENTER:           return KeyCode::KeyPadEnter;
            
            // Numpad 0-9 and period handled above based on numlock state
            
            case SDLK_KP_EQUALS:          return KeyCode::KeyPadEquals;
            case SDLK_KP_COMMA:           return KeyCode::KeyPadComma;
            case SDLK_KP_EQUALSAS400:     return KeyCode::KeyPadEqualsAs400;
            case SDLK_KP_00:              return KeyCode::KeyPad00;
            case SDLK_KP_000:             return KeyCode::KeyPad000;
            case SDLK_KP_LEFTPAREN:       return KeyCode::KeyPadLeftParen;
            case SDLK_KP_RIGHTPAREN:      return KeyCode::KeyPadRightParen;
            case SDLK_KP_LEFTBRACE:       return KeyCode::KeyPadLeftBrace;
            case SDLK_KP_RIGHTBRACE:      return KeyCode::KeyPadRightBrace;
            case SDLK_KP_TAB:             return KeyCode::KeyPadTab;
            case SDLK_KP_BACKSPACE:       return KeyCode::KeyPadBackspace;
            case SDLK_KP_A:               return KeyCode::KeyPadA;
            case SDLK_KP_B:               return KeyCode::KeyPadB;
            case SDLK_KP_C:               return KeyCode::KeyPadC;
            case SDLK_KP_D:               return KeyCode::KeyPadD;
            case SDLK_KP_E:               return KeyCode::KeyPadE;
            case SDLK_KP_F:               return KeyCode::KeyPadF;
            case SDLK_KP_XOR:             return KeyCode::KeyPadXor;
            case SDLK_KP_POWER:           return KeyCode::KeyPadPower;
            case SDLK_KP_PERCENT:         return KeyCode::KeyPadPercent;
            case SDLK_KP_LESS:            return KeyCode::KeyPadLess;
            case SDLK_KP_GREATER:         return KeyCode::KeyPadGreater;
            case SDLK_KP_AMPERSAND:       return KeyCode::KeyPadAmpersand;
            case SDLK_KP_DBLAMPERSAND:    return KeyCode::KeyPadDoubleAmpersand;
            case SDLK_KP_VERTICALBAR:     return KeyCode::KeyPadVerticalbar;
            case SDLK_KP_DBLVERTICALBAR:  return KeyCode::KeyPadDoubleVerticalbar;
            case SDLK_KP_COLON:           return KeyCode::KeyPadColon;
            case SDLK_KP_HASH:            return KeyCode::KeyPadHash;
            case SDLK_KP_SPACE:           return KeyCode::KeyPadSpace;
            case SDLK_KP_AT:              return KeyCode::KeyPadAt;
            case SDLK_KP_EXCLAM:          return KeyCode::KeyPadExclam;
            case SDLK_KP_MEMSTORE:        return KeyCode::KeyPadMemstore;
            case SDLK_KP_MEMRECALL:       return KeyCode::KeyPadMemrecall;
            case SDLK_KP_MEMCLEAR:        return KeyCode::KeyPadMemclear;
            case SDLK_KP_MEMADD:          return KeyCode::KeyPadMemadd;
            case SDLK_KP_MEMSUBTRACT:     return KeyCode::KeyPadMemsubtract;
            case SDLK_KP_MEMMULTIPLY:     return KeyCode::KeyPadMemmultiply;
            case SDLK_KP_MEMDIVIDE:       return KeyCode::KeyPadMemdivide;
            case SDLK_KP_PLUSMINUS:       return KeyCode::KeyPadPlusminus;
            case SDLK_KP_CLEAR:           return KeyCode::KeyPadClear;
            case SDLK_KP_CLEARENTRY:      return KeyCode::KeyPadClearentry;
            case SDLK_KP_BINARY:          return KeyCode::KeyPadBinary;
            case SDLK_KP_OCTAL:           return KeyCode::KeyPadOctal;
            case SDLK_KP_DECIMAL:         return KeyCode::KeyPadDecimal;
            case SDLK_KP_HEXADECIMAL:     return KeyCode::KeyPadHexadecimal;
                                           
            case SDLK_APPLICATION:        return KeyCode::Application;
            case SDLK_POWER:              return KeyCode::Power;
            case SDLK_F13:                return KeyCode::F13;
            case SDLK_F14:                return KeyCode::F14;
            case SDLK_F15:                return KeyCode::F15;
            case SDLK_F16:                return KeyCode::F16;
            case SDLK_F17:                return KeyCode::F17;
            case SDLK_F18:                return KeyCode::F18;
            case SDLK_F19:                return KeyCode::F19;
            case SDLK_F20:                return KeyCode::F20;
            case SDLK_F21:                return KeyCode::F21;
            case SDLK_F22:                return KeyCode::F22;
            case SDLK_F23:                return KeyCode::F23;
            case SDLK_F24:                return KeyCode::F24;
            case SDLK_EXECUTE:            return KeyCode::Execute;
            case SDLK_HELP:               return KeyCode::Help;
            case SDLK_MENU:               return KeyCode::Menu;
            case SDLK_SELECT:             return KeyCode::Select;
            case SDLK_STOP:               return KeyCode::Stop;
            case SDLK_AGAIN:              return KeyCode::Again;
            case SDLK_UNDO:               return KeyCode::Undo;
            case SDLK_CUT:                return KeyCode::Cut;
            case SDLK_COPY:               return KeyCode::Copy;
            case SDLK_PASTE:              return KeyCode::Paste;
            case SDLK_FIND:               return KeyCode::Find;
            case SDLK_MUTE:               return KeyCode::Mute;
            case SDLK_VOLUMEUP:           return KeyCode::Volumeup;
            case SDLK_VOLUMEDOWN:         return KeyCode::Volumedown;
                                           
            case SDLK_ALTERASE:           return KeyCode::Alterase;
            case SDLK_SYSREQ:             return KeyCode::Sysreq;
            case SDLK_CANCEL:             return KeyCode::Cancel;
            case SDLK_CLEAR:              return KeyCode::Clear;
            case SDLK_PRIOR:              return KeyCode::Prior;
            case SDLK_RETURN2:            return KeyCode::Return2;
            case SDLK_SEPARATOR:          return KeyCode::Separator;
            case SDLK_OUT:                return KeyCode::Out;
            case SDLK_OPER:               return KeyCode::Oper;
            case SDLK_CLEARAGAIN:         return KeyCode::Clearagain;
            case SDLK_CRSEL:              return KeyCode::Crsel;
            case SDLK_EXSEL:              return KeyCode::Exsel;
                                           
            case SDLK_THOUSANDSSEPARATOR: return KeyCode::ThousandsSeparator;
            case SDLK_DECIMALSEPARATOR:   return KeyCode::DecimalSeparator;
            case SDLK_CURRENCYUNIT:       return KeyCode::CurrencyUnit;
            case SDLK_CURRENCYSUBUNIT:    return KeyCode::CurrencySubUnit;
            case SDLK_LCTRL:              return KeyCode::LeftControl;
            case SDLK_LSHIFT:             return KeyCode::LeftShift;
            case SDLK_LALT:               return KeyCode::LeftAlt;
            case SDLK_LGUI:               return KeyCode::LeftCommand;
            case SDLK_RCTRL:              return KeyCode::RightControl;
            case SDLK_RSHIFT:             return KeyCode::RightShift;
            case SDLK_RALT:               return KeyCode::RightAlt;
            case SDLK_RGUI:               return KeyCode::RightCommand;
            
            case SDLK_MODE:               return KeyCode::Mode;
                                           
            case SDLK_MEDIA_NEXT_TRACK:   return KeyCode::AudioNext;
            case SDLK_MEDIA_PREVIOUS_TRACK: return KeyCode::AudioPrev;
            case SDLK_MEDIA_STOP:         return KeyCode::AudioStop;
            case SDLK_MEDIA_PLAY:         return KeyCode::AudioPlay;
            // SDLK_MUTE handled above
            case SDLK_MEDIA_SELECT:       return KeyCode::MediaSelect;
            // WWW, MAIL, CALCULATOR, COMPUTER, DISPLAYSWITCH don't exist in SDL3
            case SDLK_AC_SEARCH:          return KeyCode::ACSearch;
            case SDLK_AC_HOME:            return KeyCode::ACHome;
            case SDLK_AC_BACK:            return KeyCode::ACBack;
            case SDLK_AC_FORWARD:         return KeyCode::ACForward;
            case SDLK_AC_STOP:            return KeyCode::ACStop;
            case SDLK_AC_REFRESH:         return KeyCode::ACRefresh;
            case SDLK_AC_BOOKMARKS:       return KeyCode::ACBookmarks;
            
            // Brightness and keyboard illumination keys don't exist in SDL3
            case SDLK_MEDIA_EJECT:        return KeyCode::Eject;
            // SDLK_SLEEP already handled above
            // APP1, APP2 don't exist in SDL3
            
            case SDLK_MEDIA_REWIND:       return KeyCode::AudioRewind;
            case SDLK_MEDIA_FAST_FORWARD: return KeyCode::AudioFastforward;
            
            case SDLK_SOFTLEFT:           return KeyCode::SoftLeft;
            case SDLK_SOFTRIGHT:          return KeyCode::SoftRight;
            case SDLK_CALL:               return KeyCode::Call;
            case SDLK_ENDCALL:            return KeyCode::EndCall;
        }
        return KeyCode::None;
    }

    static KeyModifier ToKeyModifier(SDL_Keymod modifiers)
    {
        KeyModifier modifierFlags = KeyModifier::None;
        if (modifiers & SDL_KMOD_SHIFT) modifierFlags |= KeyModifier::Shift;
        if (modifiers & SDL_KMOD_CTRL)  modifierFlags |= KeyModifier::Control;
        if (modifiers & SDL_KMOD_GUI)   modifierFlags |= KeyModifier::Command;
        if (modifiers & SDL_KMOD_ALT)
        {
#ifdef __APPLE__
            modifierFlags |= KeyModifier::Option;
#else
            modifierFlags |= KeyModifier::Alt;
#endif
        }
        return modifierFlags;
    }

    void SDLWindowBackend::AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inInputHandler)
    {
        if (window == nullptr)
            return;

        SDL_PropertiesID props = SDL_GetWindowProperties(ToSDL(window));
        SDL_SetPointerProperty(props, "InputHandler", (void*)inInputHandler);
    }

    void SDLWindowBackend::AttachContextToWindow(OSWindowHandle window, IWindowContext* context)
    {
        if (window == nullptr)
            return;

        SDL_PropertiesID props = SDL_GetWindowProperties(ToSDL(window));
        SDL_SetPointerProperty(props, "WindowContext", (void*)context);
    }

    // Show a window, applying the classic Win32 popup shadow when it asked for
    // one ("WantsDropShadow" property, set at creation). CS_DROPSHADOW is a
    // CLASS style evaluated at show time, and SDL windows share one class — so
    // the bit is set just for the Show call and restored after; the shadow the
    // window-manager created persists for this window while it stays visible.
    static void ShowSDLWindowWithShadow(SDL_Window* window)
    {
#ifdef _WIN32
        HWND hwnd = nullptr;
        if (SDL_GetBooleanProperty(SDL_GetWindowProperties(window), "WantsDropShadow", false))
            hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        ULONG_PTR classStyle = 0;
        bool      toggled    = false;
        if (hwnd != nullptr)
        {
            classStyle = GetClassLongPtr(hwnd, GCL_STYLE);
            if ((classStyle & CS_DROPSHADOW) == 0)
            {
                SetClassLongPtr(hwnd, GCL_STYLE, classStyle | CS_DROPSHADOW);
                toggled = true;
            }
        }
        SDL_ShowWindow(window);
        if (toggled)
            SetClassLongPtr(hwnd, GCL_STYLE, classStyle);
#else
        SDL_ShowWindow(window);
#endif
    }

    OSWindowHandle SDLWindowBackend::CreateWindow(std::string_view title,
                                                  IntRect rect,
                                                  GUI::WindowFlags windowFlags,
                                                  OSWindowHandle parentOSHandle)
    {
        if (!m_sdlInitialized) return nullptr;
        
        bool isTransparent = (windowFlags & GUI::WindowFlags::Transparent) != GUI::WindowFlags::None;
        bool isModal = (windowFlags & GUI::WindowFlags::Modal) != GUI::WindowFlags::None;


        Uint64 api_flags = 0;
        api_flags = SDL_WINDOW_VULKAN;
        
        Uint64 sd_window_flags = api_flags;
        
        // Transparent windows on Windows can't use Vulkan surfaces (they need WS_EX_LAYERED)
        // We'll render offscreen and use UpdateLayeredWindow instead
#ifndef _WIN32
        // On non-Windows platforms, we can use Vulkan surfaces with transparency
        sd_window_flags |= api_flags;
        if (isTransparent) sd_window_flags |= SDL_WINDOW_TRANSPARENT;
#else
        // On Windows, only create Vulkan surface for non-transparent windows
        if (isTransparent)
            sd_window_flags |= SDL_WINDOW_TRANSPARENT;
        else
            sd_window_flags |= api_flags;
#endif

        // Check individual flag bits (WindowFlags is a bitfield)
        if ((windowFlags & GUI::WindowFlags::AllowHighDPI)      != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if ((windowFlags & GUI::WindowFlags::Borderless)        != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_BORDERLESS;
        if ((windowFlags & GUI::WindowFlags::Resizable)         != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_RESIZABLE;
        if ((windowFlags & GUI::WindowFlags::AlwaysOnTop)       != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
        if ((windowFlags & GUI::WindowFlags::SkipTaskbar)       != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_UTILITY;
        if ((windowFlags & GUI::WindowFlags::Utility)           != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_UTILITY;
        if ((windowFlags & GUI::WindowFlags::NotFocusable)      != GUI::WindowFlags::None) sd_window_flags |= SDL_WINDOW_NOT_FOCUSABLE;
		
        // Always create hidden, to avoid a native caption flash while Win32 styles are applied;
        // shown below unless requested hidden.
        bool wasCreatedHidden = (windowFlags & GUI::WindowFlags::Hidden) != GUI::WindowFlags::None;
        sd_window_flags |= SDL_WINDOW_HIDDEN;
        
        SDL_Window* sdlWindow = SDL_CreateWindow(std::string(title).c_str(),
                                                 std::max(0, rect.w),
                                                 std::max(0, rect.h),
                                                 sd_window_flags);

        if (!sdlWindow)
            return nullptr;

        // Set window position after creation (SDL3 way)
        SDL_SetWindowPosition(sdlWindow, rect.x, rect.y);
        SDL_SetPointerProperty(SDL_GetWindowProperties(sdlWindow), "IntersticeBackend", this);
        
        SDL_PropertiesID props = SDL_GetWindowProperties(sdlWindow);

        // Track whether this SDL window is modal.
        SDL_SetBooleanProperty(props, "IsModal", isModal);

#ifdef _WIN32
        HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

        if (hwnd)
        {
            // Borderless: strip caption/sizing/system-menu styles and use WS_POPUP while
            // still hidden, or some Windows versions briefly show a native caption.
            if ((windowFlags & GUI::WindowFlags::Borderless) != GUI::WindowFlags::None)
            {
                LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
                LONG_PTR desiredStyle = (style & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
                if (desiredStyle != style)
                {
                    SetWindowLongPtr(hwnd, GWL_STYLE, desiredStyle);
                    // Force a frame update so the style change takes effect.
                    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
            }

            // For transparent windows on Windows, manually set WS_EX_LAYERED
            if (isTransparent)
            {
                LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
                SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            }

            // A caption-less WS_POPUP never gets a DWM shadow, so borderless shadows use
            // CS_DROPSHADOW, applied by ShowSDLWindowWithShadow on every Show.
            SDL_SetBooleanProperty(props, "WantsDropShadow",
                ((windowFlags & GUI::WindowFlags::Borderless) != GUI::WindowFlags::None) &&
                ((windowFlags & GUI::WindowFlags::Shadow) != GUI::WindowFlags::None));
        }
#endif

        // OS parenting makes child popups receive input and position relative to the parent.
        if (parentOSHandle != nullptr)
        {
            SDL_Window* parent = ToSDL(parentOSHandle);
            if (parent != nullptr)
            {
                SDL_SetWindowParent(sdlWindow, parent);
                if (isModal)
                {
                    SDL_SetWindowModal(sdlWindow, true);
                }
            }

            // Ensure the child/modal window stays above its parent.
            SDL_SetWindowAlwaysOnTop(sdlWindow, true);
        }

        SDL_StartTextInput(sdlWindow);

        // Shown only after the Win32 style adjustments above.
        if (!wasCreatedHidden)
        {
            ShowSDLWindowWithShadow(sdlWindow);
        }
        
        static GUI::MouseButtons lastMouseButtons = GUI::MouseButtons::None;

        m_eventWatch = [](OSWindowHandle data, SDL_Event* event) -> bool
            {
                // For not-focusable windows (e.g. tooltips), reroute all input
                // events to the last focusable window so the tooltip never
                // consumes input.
                static SDL_Window* s_lastFocusableWindow = nullptr;
                {
                    SDL_Window* focused = SDL_GetKeyboardFocus();
                    if (focused && !(SDL_GetWindowFlags(focused) & SDL_WINDOW_NOT_FOCUSABLE))
                        s_lastFocusableWindow = focused;
                }

                auto GetInputContext = [](OSWindowHandle windowHandle) -> GUI::IPlatformInputHandler*
                {
                    SDL_Window* sdlWin = (SDL_Window*)windowHandle;
                    if (SDL_GetWindowFlags(sdlWin) & SDL_WINDOW_NOT_FOCUSABLE)
                    {
                        if (s_lastFocusableWindow && s_lastFocusableWindow != sdlWin)
                        {
                            return LocalGetInputHandler((OSWindowHandle)s_lastFocusableWindow);
                        }
                    }
                    return LocalGetInputHandler(windowHandle);
                };
                auto GetWindowID = [](OSWindowHandle windowHandle) -> GUI::ID
                    {
                        SDL_Window* sdlWin = (SDL_Window*)windowHandle;
                        if (SDL_GetWindowFlags(sdlWin) & SDL_WINDOW_NOT_FOCUSABLE)
                        {
                            if (s_lastFocusableWindow && s_lastFocusableWindow != sdlWin)
                            {
                                auto windowContext = LocalGetWindowContext((OSWindowHandle)s_lastFocusableWindow);
                                return windowContext ? windowContext->GetID() : ID::Invalid();
                            }
                        }
                        auto windowContext = LocalGetWindowContext(windowHandle);
                        return windowContext ? windowContext->GetID() : ID::Invalid();
                    };

                switch(event->type)
                {
                    case SDL_EVENT_KEY_DOWN:
                    {
                        if (SDL_GetWindowFromID(event->key.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;
                        
                        bool repeat = event->key.repeat;
                        SDL_Keymod modifiers = event->key.mod;
                        GUI::KeyCode keyCode = ToKeyCode(event->key.key, modifiers);

                        GUI::ID windowID = GetWindowID(data);
                        inputHandler->OnKeyboard(windowID, keyCode, repeat ? 1 : 0, ToKeyModifier(modifiers), GUI::KeyAction::KeyDown);
                        break;
                    }
                    case SDL_EVENT_KEY_UP:
                    {
                        if (SDL_GetWindowFromID(event->key.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;

                        bool repeat = event->key.repeat;
                        SDL_Keymod modifiers = event->key.mod;
                        GUI::KeyCode keyCode = ToKeyCode(event->key.key, modifiers);

                        GUI::ID windowID = GetWindowID(data);
                        inputHandler->OnKeyboard(windowID, keyCode, repeat ? 1 : 0, ToKeyModifier(modifiers), GUI::KeyAction::KeyUp);
                        break;
                    }
                    case SDL_EVENT_TEXT_INPUT:
                    {
                        if (SDL_GetWindowFromID(event->text.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;

                        GUI::ID windowID = GetWindowID(data);
                        // SDL delivers UTF-8; decode to Unicode codepoints so a multi-byte
                        // character arrives as a single KeyChar, not one bogus one per byte.
                        const char* text = event->text.text;
                        for (Uint32 codepoint; (codepoint = SDL_StepUTF8(&text, nullptr)) != 0; )
                        {
                            inputHandler->OnKeyboard(windowID, (GUI::KeyCode)codepoint, 0, GUI::KeyModifier::None, GUI::KeyAction::KeyChar);
                        }
                        break;
                    }
                    case SDL_EVENT_TEXT_EDITING:
                    {
                        if (SDL_GetWindowFromID(event->edit.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;

                        // IME pre-edit text; an empty string ends the composition.
                        const char* text = event->edit.text != nullptr ? event->edit.text : "";
                        inputHandler->OnTextComposition(GetWindowID(data), text, event->edit.start);
                        break;
                    }
                    case SDL_EVENT_MOUSE_MOTION:
                    {
                        if (SDL_GetWindowFromID(event->motion.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;

                        const GUI::int2 pixelPos = PixelPointOf((SDL_Window*)data, event->motion.x, event->motion.y);
                        const float x = float(pixelPos.x);
                        const float y = float(pixelPos.y);
                        GUI::ID windowID = GetWindowID(data);
                        inputHandler->OnMouseMove(windowID, GUI::int2{ int32_t(x), int32_t(y) }, lastMouseButtons, ToKeyModifier(SDL_GetModState()));
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                    {
                        if (SDL_GetWindowFromID(event->button.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;

                        GUI::ID windowID = GetWindowID(data);

                        const GUI::int2 pixelPos = PixelPointOf((SDL_Window*)data, event->button.x, event->button.y);
                        const float x = float(pixelPos.x);
                        const float y = float(pixelPos.y);
                        Uint8 button = event->button.button;
                        switch (event->type)
                        {
                            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                            {
                                if (button == SDL_BUTTON_LEFT)   lastMouseButtons |= GUI::MouseButtons::Left;
                                if (button == SDL_BUTTON_RIGHT)  lastMouseButtons |= GUI::MouseButtons::Right;
                                if (button == SDL_BUTTON_MIDDLE) lastMouseButtons |= GUI::MouseButtons::Middle;
                                if (button == SDL_BUTTON_X1)     lastMouseButtons |= GUI::MouseButtons::X1;
                                if (button == SDL_BUTTON_X2)     lastMouseButtons |= GUI::MouseButtons::X2;

                                inputHandler->OnMouseButton(windowID, GUI::int2{ int32_t(x), int32_t(y) }, lastMouseButtons, ToKeyModifier(SDL_GetModState()), GUI::MouseAction::MouseDown);
                                break;
                            }
                            case SDL_EVENT_MOUSE_BUTTON_UP:
                            {
                                if (button == SDL_BUTTON_LEFT)   lastMouseButtons &= ~GUI::MouseButtons::Left;
                                if (button == SDL_BUTTON_RIGHT)  lastMouseButtons &= ~GUI::MouseButtons::Right;
                                if (button == SDL_BUTTON_MIDDLE) lastMouseButtons &= ~GUI::MouseButtons::Middle;
                                if (button == SDL_BUTTON_X1)     lastMouseButtons &= ~GUI::MouseButtons::X1;
                                if (button == SDL_BUTTON_X2)     lastMouseButtons &= ~GUI::MouseButtons::X2;

                                inputHandler->OnMouseButton(windowID, GUI::int2{ int32_t(x), int32_t(y) }, lastMouseButtons, ToKeyModifier(SDL_GetModState()), GUI::MouseAction::MouseUp);
                                break;
                            }
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_WHEEL:
                    {
                        if (SDL_GetWindowFromID(event->wheel.windowID) != (SDL_Window*)data)
                            break;

                        GUI::IPlatformInputHandler* inputHandler = GetInputContext(data);
                        if (inputHandler == nullptr)
                            break;

                        const GUI::int2 pixelPos = PixelPointOf((SDL_Window*)data, event->wheel.mouse_x, event->wheel.mouse_y);
                        const float x = float(pixelPos.x);
                        const float y = float(pixelPos.y);
                        float mx = event->wheel.x;
                        float my = event->wheel.y;
                        GUI::ID windowID = GetWindowID(data);
                        inputHandler->OnMouseScroll(windowID, GUI::int2{ int32_t(x), int32_t(y) }, GUI::float2{ mx, my }, ToKeyModifier(SDL_GetModState()));
                        break;
                    }
                    case SDL_EVENT_WINDOW_EXPOSED:
                    {
                        if (SDL_GetWindowFromID(event->window.windowID) != (SDL_Window*)data)
                            break;

                        IWindowContext* windowContext = LocalGetWindowContext(data);
                        if (windowContext == nullptr)
                            break;

                        // Skip rendering for transparent windows - they render through the normal frame loop
                        if (!windowContext->IsTransparent())
                            break;
                        
                        int w = 0, h = 0;
                        SDL_GetWindowSizeInPixels((SDL_Window*)data, &w, &h);
                        int x = 0, y = 0;
                        SDL_GetWindowPosition((SDL_Window*)data, &x, &y);
                        auto* gc = (GUI::GraphicsContext*)SDL_GetPointerProperty(
                            SDL_GetWindowProperties((SDL_Window*)data), "GraphicsContext", nullptr);
                        windowContext->RenderWindowFrame(gc, GUI::IntRect{ x, y, w, h }, true);
                        break;
                    }
                }
                return false;
            };
        SDL_AddEventWatch(m_eventWatch, sdlWindow);

        m_windows.insert(sdlWindow);

        // Maintain modal stack (creation order) to support nested modals.
        if (isModal)
        {
            m_modalWindows.push_back(sdlWindow);
        }

        return FromSDL(sdlWindow);
    }

    bool SDLWindowBackend::DestroyWindow(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return false;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return false;
        auto it = m_windows.find(sdl_window);
        if (it == m_windows.end()) return false;
		m_pendingDestroy.insert(sdl_window);
        return true;
    }

    void SDLWindowBackend::DetachWindowContext(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return;
        if (m_windows.find(sdl_window) == m_windows.end()) return;
        SDL_SetPointerProperty(SDL_GetWindowProperties(sdl_window), "WindowContext", nullptr);
        SDL_SetPointerProperty(SDL_GetWindowProperties(sdl_window), "InputHandler", nullptr);
    }

    bool SDLWindowBackend::IsWindow(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return false;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return false;
        return m_windows.find(sdl_window) != m_windows.end();
    }

    void SDLWindowBackend::BringToFront(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return;
        SDL_RaiseWindow(sdl_window);
    }

    // Get the window currently under the mouse
    OSWindowHandle SDLWindowBackend::GetWindowUnderMouse()
    {
        if (!m_sdlInitialized) return nullptr;
        SDL_Window* mouseFocus = SDL_GetMouseFocus();
        if (mouseFocus != nullptr && m_windows.find(mouseFocus) != m_windows.end())
        {
            return FromSDL(mouseFocus);
        }
        return nullptr;
    }

    OSWindowHandle SDLWindowBackend::GetActiveWindow()
    {
        if (!m_sdlInitialized) return nullptr;
        SDL_Window* focus = SDL_GetKeyboardFocus();
        if (!focus) return nullptr;
        if (m_windows.find(focus) == m_windows.end()) return nullptr;
        return FromSDL(focus);
    }

    void SDLWindowBackend::PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup)
    {
        if (!m_sdlInitialized) return;

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            switch (ev.type)
            {
                case SDL_EVENT_QUIT:
                    // schedule destruction of all windows
                    for (auto sdl_window : m_windows) m_pendingDestroy.insert(sdl_window);
                    break;

                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    {
                        SDL_Window* sdl_window = SDL_GetWindowFromID(ev.window.windowID);
                        if (sdl_window) m_pendingDestroy.insert(sdl_window);
                    }
                    break;

                default:
                    break;
            }
        }

        // destroy windows requested by events
        for (auto sdl_window : m_pendingDestroy)
        {
            if (m_windows.find(sdl_window) != m_windows.end())
            {
                // Also maintain modal stack when windows are destroyed via events.
                auto itModal = std::find(m_modalWindows.begin(), m_modalWindows.end(), sdl_window);
                if (itModal != m_modalWindows.end())
                {
                    m_modalWindows.erase(itModal);
                }

                IWindowContext* context =
                    (IWindowContext*)SDL_GetPointerProperty(
                        SDL_GetWindowProperties(sdl_window),
                        "WindowContext",
                        nullptr);

                // The context's surface must go before the window it belongs to.
                if (cleanup != nullptr)
                {
                    OSWindowHandle handle = FromSDL(sdl_window);
                    cleanup(handle, context);
                }

                // SDL destroys a window's children with it; un-parent ours so each
                // child lives or dies by its own policy (closed ones are queued too).
                for (SDL_Window* child : m_windows)
                {
                    if (child == sdl_window || SDL_GetWindowParent(child) != sdl_window)
                        continue;
                    if (SDL_GetWindowFlags(child) & SDL_WINDOW_MODAL)
                        SDL_SetWindowModal(child, false);
                    SDL_SetWindowParent(child, nullptr);
                }

                SDL_StopTextInput(sdl_window);
                SDL_RemoveEventWatch(m_eventWatch, sdl_window);
                SDL_DestroyWindow(sdl_window);

                m_windows.erase(sdl_window);
            }
        }
        m_pendingDestroy.clear();

        UpdateMouseState();
    }

    void SDLWindowBackend::UpdateMouseState()
    {
        if (!m_sdlInitialized) return;
        float x = 0, y = 0;
        Uint32 btnstate = SDL_GetGlobalMouseState(&x, &y);
        m_mousePos.x = static_cast<int>(x);
        m_mousePos.y = static_cast<int>(y);
        m_mouseButtons = ToMouseButtons(btnstate);

        // Track the last focusable window for input rerouting from tooltips.
        SDL_Window* focused = SDL_GetKeyboardFocus();
        if (focused && !(SDL_GetWindowFlags(focused) & SDL_WINDOW_NOT_FOCUSABLE))
            m_lastFocusableWindow = focused;
    }

    int2 SDLWindowBackend::GetScreenMousePos()
    {
        return m_mousePos;
    }

    MouseButtons SDLWindowBackend::GetMouseButtons()
    {
        return m_mouseButtons;
    }
    
    static_assert(SDL_SYSTEM_CURSOR_COUNT <= 32, "grow SDLWindowBackend::kSystemCursorSlots");

    float SDLWindowBackend::GetWindowDisplayScale(OSWindowHandle window)
    {
        SDL_Window* sdlWindow = ToSDL(window);
        if (!m_sdlInitialized || sdlWindow == nullptr) return 0.0f;
        return SDL_GetWindowDisplayScale(sdlWindow); // 0 on failure
    }

    IntRect SDLWindowBackend::GetWindowPixelRect(OSWindowHandle window)
    {
        SDL_Window* sdlWindow = ToSDL(window);
        if (!m_sdlInitialized || sdlWindow == nullptr) return {};
        IntRect rect {};
        SDL_GetWindowPosition(sdlWindow, &rect.x, &rect.y);
        SDL_GetWindowSizeInPixels(sdlWindow, &rect.w, &rect.h);
        return rect;
    }

    float SDLWindowBackend::OsPerPixel(SDL_Window* window) const
    {
        const float density = SDL_GetWindowPixelDensity(window);
        return density > 0.0f ? 1.0f / density : 1.0f;
    }

    int2 SDLWindowBackend::WindowPointToPixels(SDL_Window* window, float x, float y) const
    {
        int osX = 0, osY = 0;
        SDL_GetWindowPosition(window, &osX, &osY);
        const float k = OsPerPixel(window);
        return int2{ osX + int32_t(std::floor(x / k)), osY + int32_t(std::floor(y / k)) };
    }

    void SDLWindowBackend::SetTextInputArea(OSWindowHandle window, IntRect rect)
    {
        SDL_Window* sdlWindow = ToSDL(window);
        if (!m_sdlInitialized || sdlWindow == nullptr) return;
        const float k = OsPerPixel(sdlWindow);
        const SDL_Rect area{ RoundToInt(rect.x * k), RoundToInt(rect.y * k),
                             std::max(1, RoundToInt(rect.w * k)), std::max(1, RoundToInt(rect.h * k)) };
        SDL_SetTextInputArea(sdlWindow, &area, 0);
    }

    void SDLWindowBackend::SetCursor(MouseCursor cursor)
    {
        if (!m_sdlInitialized) return;
        
        SDL_SystemCursor sdlCursor;
        switch (cursor)
        {
		    case MouseCursor::None:       if (m_cursorVisible) SDL_HideCursor(); m_cursorVisible = false; return; // No cursor, hide and return early
            case MouseCursor::Default:    sdlCursor = SDL_SYSTEM_CURSOR_DEFAULT; break;
            case MouseCursor::Text:       sdlCursor = SDL_SYSTEM_CURSOR_TEXT; break;
            case MouseCursor::Wait:       sdlCursor = SDL_SYSTEM_CURSOR_WAIT; break;
            case MouseCursor::Crosshair:  sdlCursor = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
            case MouseCursor::Pointer:    sdlCursor = SDL_SYSTEM_CURSOR_POINTER; break;
            case MouseCursor::ColResize:  sdlCursor = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case MouseCursor::RowResize:  sdlCursor = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case MouseCursor::EwResize:   sdlCursor = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case MouseCursor::NsResize:   sdlCursor = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case MouseCursor::NwseResize: sdlCursor = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
            case MouseCursor::NeswResize: sdlCursor = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
            case MouseCursor::Move:       sdlCursor = SDL_SYSTEM_CURSOR_MOVE; break;
            case MouseCursor::NotAllowed: sdlCursor = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
            default:                      sdlCursor = SDL_SYSTEM_CURSOR_DEFAULT; break;
        }
        
        if (!m_cursorVisible)
            SDL_ShowCursor();
        m_cursorVisible = true;
        
        SDL_Cursor*& sdlCursorHandle = m_systemCursors[sdlCursor];
        if (sdlCursorHandle == nullptr)
            sdlCursorHandle = SDL_CreateSystemCursor(sdlCursor);
        if (sdlCursorHandle && SDL_GetCursor() != sdlCursorHandle)
            SDL_SetCursor(sdlCursorHandle);
    }

    void SDLWindowBackend::SwapBuffers(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return;
        // If using OpenGL context, swap. If not, no-op.
        SDL_GL_SwapWindow(sdl_window);
    }

    IntRect SDLWindowBackend::GetWindowRect(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return { 0,0,0,0 };
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return { 0,0,0,0 };
        SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_window);
        if (flags & SDL_WINDOW_MINIMIZED) return { 0,0,0,0 };
        int2 osPos {}, osSize {};
        SDL_GetWindowPosition(sdl_window, &osPos.x, &osPos.y);
        SDL_GetWindowSize(sdl_window, &osSize.x, &osSize.y);
        const float k = OsPerPixel(sdl_window);
        return IntRect{ .position = osPos, .size = { RoundToInt(osSize.x / k), RoundToInt(osSize.y / k) } };
    }

    int2 SDLWindowBackend::GetWindowPos(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return { 0,0 };
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return { 0,0 };
        SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_window);
        if (flags & SDL_WINDOW_MINIMIZED) return { 0,0 };
        int2 position {};
        SDL_GetWindowPosition(sdl_window, &position.x, &position.y);
        return position;
    }

    int2 SDLWindowBackend::GetWindowSize(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return { 0,0 };
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return { 0,0 };
        SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_window);
        if (flags & SDL_WINDOW_MINIMIZED) return { 0,0 };
        int2 size {};
        SDL_GetWindowSize(sdl_window, &size.x, &size.y);
        const float k = OsPerPixel(sdl_window);
        return int2{ RoundToInt(size.x / k), RoundToInt(size.y / k) };
    }

    bool SDLWindowBackend::SetWindowRect(OSWindowHandle h, const IntRect& clientRect)
    {
        if (!m_sdlInitialized) return false;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return false;
        const float k = OsPerPixel(sdl_window);
        m_suppressLiveResize = true;
        SDL_SetWindowPosition(sdl_window, clientRect.x, clientRect.y);
        SDL_SetWindowSize(sdl_window, std::max(0, RoundToInt(clientRect.w * k)), std::max(0, RoundToInt(clientRect.h * k)));
        m_suppressLiveResize = false;
        return true;
    }
    
    void SDLWindowBackend::SetWindowVisible(OSWindowHandle h, bool visible)
    {
        if (!m_sdlInitialized) return;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return;
        m_suppressLiveResize = true;
        if (visible)
            ShowSDLWindowWithShadow(sdl_window);
        else
            SDL_HideWindow(sdl_window);
        m_suppressLiveResize = false;
    }

    void* SDLWindowBackend::GetNativeHandle(OSWindowHandle h)
    {
        if (!m_sdlInitialized) return nullptr;
        SDL_Window* sdl_window = ToSDL(h);
        if (!sdl_window) return nullptr;
        
        SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
#ifdef _WIN32
        return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
        return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__linux__)
        // X11 or Wayland window handle
        void* x11_window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_WINDOW_POINTER, nullptr);
        if (x11_window) return x11_window;
        return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
#else
        return nullptr;
#endif
    }

    std::vector<MonitorInfo> SDLWindowBackend::GetMonitors() const
    {
        std::vector<MonitorInfo> out;
        if (!m_sdlInitialized) return out;
        
        int num = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&num);
        if (!displays || num <= 0) return out;

        for (int i = 0; i < num; ++i)
        {
            SDL_Rect bounds = {0,0,0,0};
            SDL_Rect usable = {0,0,0,0};
            
            if (!SDL_GetDisplayBounds(displays[i], &bounds)) continue;
            SDL_GetDisplayUsableBounds(displays[i], &usable);
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displays[i]);

            MonitorInfo mi
            {
                .rect = IntRect{ bounds.x, bounds.y, bounds.w, bounds.h },
                .workArea = IntRect{ usable.x, usable.y, usable.w, usable.h },
                .isPrimary = (i == 0),
                .contentScale = SDL_GetDisplayContentScale(displays[i]) > 0.0f ? SDL_GetDisplayContentScale(displays[i]) : 1.0f,
                .refreshRate  = mode != nullptr ? mode->refresh_rate : 0.0f
            };
            out.push_back(mi);
        }
        
        SDL_free(displays);
        return out;
    }

    int SDLWindowBackend::GetMonitorAt(int2 pos) const
    {
        if (!m_sdlInitialized) return -1;
        
        int num = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&num);
        if (!displays || num <= 0) return -1;
        
        for (int i = 0; i < num; ++i)
        {
            SDL_Rect bounds;
            if (SDL_GetDisplayBounds(displays[i], &bounds))
            {
                if (pos.x >= bounds.x && pos.y >= bounds.y &&
                    pos.x <= bounds.x + bounds.w && pos.y <= bounds.y + bounds.h)
                {
                    SDL_free(displays);
                    return i;
                }
            }
        }
        
        SDL_free(displays);
        return 0; // fallback to primary
    }

    int SDLWindowBackend::GetMonitorFromWindow(OSWindowHandle handle) const
    {
        if (!m_sdlInitialized) return -1;
        SDL_Window* sdl_window = ToSDL(handle);
        if (!sdl_window) return -1;
        
        SDL_DisplayID displayID = SDL_GetDisplayForWindow(sdl_window);
        if (!displayID) return 0;
        
        // Convert display ID to index
        int num = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&num);
        if (!displays) return 0;
        
        for (int i = 0; i < num; ++i)
        {
            if (displays[i] == displayID)
            {
                SDL_free(displays);
                return i;
            }
        }
        
        SDL_free(displays);
        return 0;
    }

    uint32_t SDLWindowBackend::GetTicks() const
    {
        if (!m_sdlInitialized) return 0;
        return static_cast<uint32_t>(SDL_GetTicks());
    }

    std::string SDLWindowBackend::GetPreferredLanguage() const
    {
        int count = 0;
        SDL_Locale** locales = SDL_GetPreferredLocales(&count);
        std::string tag;
        if (locales && count > 0 && locales[0]->language)
        {
            tag = locales[0]->language;
            if (locales[0]->country)
                tag += std::string("-") + locales[0]->country;
        }
        SDL_free(locales);
        return tag;
    }

    SDL_Window* SDLWindowBackend::GetTopModalWindowSDL() const
    {
        if (m_modalWindows.empty())
        {
            return nullptr;
        }

        // Last entry is the top-most (most recently created) modal window.
        return m_modalWindows.back();
    }

    bool SDLWindowBackend::HandleModalFilterForEvent(SDL_Event* event)
    {
        if (!m_sdlInitialized)
        {
            return true;
        }

        SDL_Window* topModal = GetTopModalWindowSDL();
        if (topModal == nullptr)
        {
            // No modal windows active; nothing to filter.
            return true;
        }

        // Helper to extract the SDL_Window* for this event, if any.
        SDL_Window* target = nullptr;
        switch (event->type)
        {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_EXPOSED:
            {
                target = SDL_GetWindowFromID(event->window.windowID);
                break;
            }

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                target = SDL_GetWindowFromID(event->key.windowID);
                break;
            }

            case SDL_EVENT_TEXT_INPUT:
            {
                target = SDL_GetWindowFromID(event->text.windowID);
                break;
            }

            case SDL_EVENT_TEXT_EDITING:
            {
                target = SDL_GetWindowFromID(event->edit.windowID);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            {
                target = SDL_GetWindowFromID(event->motion.windowID);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                target = SDL_GetWindowFromID(event->button.windowID);
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                target = SDL_GetWindowFromID(event->wheel.windowID);
                break;
            }

            default:
            {
                break;
            }
        }

        if (target == nullptr)
        {
            // Non-window-specific event; let it through.
            return true;
        }

        // If the event targets a different window than the top modal, suppress it
        // and raise the modal window to keep it in front.
        if (target != topModal)
        {
            SDL_RaiseWindow(topModal);
            return false;
        }

        return true;
    }

} // namespace GUI
