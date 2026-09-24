#pragma once

#include <stdint.h>
#include <stddef.h>
#include "basesingleton.hpp"

#ifdef _WIN32
struct _EXCEPTION_POINTERS;
#endif

// =================================================================================================

class CrashHandler
    : public BaseSingleton<CrashHandler> {
public:
    void Init(const char* appName, const char* appVersion);

    void SetFolder(const char* folder);

    bool WriteCallStack(const char* reason, char* fileName, size_t fileNameSize);

private:
    char                    m_folder[1024]{};
    char                    m_appName[64]{};
    char                    m_appVersion[256]{};
    bool                    m_isInitialized{ false };

#ifdef _WIN32
    using ExceptionFilter = long(__stdcall*)(_EXCEPTION_POINTERS*);
    using SignalHandler = void(__cdecl*)(int);
    using PureCallHandler = void(__cdecl*)(void);
    using InvalidParameterHandler = void(__cdecl*)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);

    void*                   m_process{ nullptr };
    void*                   m_watcher{ nullptr };
    void*                   m_crashEvent{ nullptr };
    void*                   m_doneEvent{ nullptr };
    bool                    m_haveSymbols{ false };
    volatile long           m_isReporting{ 0 };
    _EXCEPTION_POINTERS*    m_exceptionPointers{ nullptr };
    unsigned long           m_threadId{ 0 };
    const char*             m_reason{ nullptr };
    ExceptionFilter         m_previousFilter{ nullptr };
    SignalHandler           m_previousAbortHandler{ nullptr };
    PureCallHandler         m_previousPureCallHandler{ nullptr };
    InvalidParameterHandler m_previousInvalidParameterHandler{ nullptr };

    static long __stdcall OnUnhandledException(_EXCEPTION_POINTERS* exceptionPointers);

    static void __cdecl OnAbortSignal(int signalNumber);

    static void __cdecl OnPureCall(void);

    static void __cdecl OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved);

    static unsigned long __stdcall WatcherThread(void* param);

    bool BeginReport(void);

    void FinishReport(_EXCEPTION_POINTERS* exceptionPointers, const char* reason);

    void ReportException(_EXCEPTION_POINTERS* exceptionPointers, const char* reason);

    void ReportCurrentThread(const char* reason, unsigned long code);

    void WriteReport(void);

    void WriteTrace(void* file, const char* kind, _EXCEPTION_POINTERS* exceptionPointers, unsigned long threadId, const char* reason);

    void WriteDump(void* file);

    bool MakeFileName(char* fileName, size_t size, const char* kind, const char* extension);
#endif
};

#define crashHandler CrashHandler::Instance()

// =================================================================================================
