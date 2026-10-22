// Prints a symbolized stack trace when a GPU test crashes. Display-driver faults
// otherwise kill the process with no output at all, which makes them very hard
// to attribute. Windows only; installed by a static initializer so every GPU
// test benefits without changes.

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    LONG WINAPI PrintCrashTrace(EXCEPTION_POINTERS* info)
    {
        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);

        std::fprintf(stderr, "\n*** CRASH: exception 0x%08lX at 0x%p\n",
            info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
            && info->ExceptionRecord->NumberParameters >= 2)
        {
            std::fprintf(stderr, "    access violation: %s address 0x%p\n",
                info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading"
                    : info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "executing",
                reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
        }

        STACKFRAME64 frame{};
        CONTEXT context = *info->ContextRecord;
        frame.AddrPC.Offset    = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

        alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + 256];
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        for (int depth = 0; depth < 40; ++depth)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
                    &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (frame.AddrPC.Offset == 0) break;

            DWORD64 displacement = 0;
            const char* name = "?";
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) name = symbol->Name;

            IMAGEHLP_MODULE64 module{};
            module.SizeOfStruct = sizeof(module);
            const char* moduleName = SymGetModuleInfo64(process, frame.AddrPC.Offset, &module)
                ? module.ModuleName : "?";

            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
                std::fprintf(stderr, "  #%02d %s!%s  (%s:%lu)\n", depth, moduleName, name, line.FileName, line.LineNumber);
            else
                std::fprintf(stderr, "  #%02d %s!%s +0x%llX\n", depth, moduleName, name,
                    static_cast<unsigned long long>(displacement));
        }
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const bool installed = []
    {
        SetUnhandledExceptionFilter(&PrintCrashTrace);
        return true;
    }();
}

#endif
