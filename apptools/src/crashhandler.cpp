#include "crashhandler.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#   include <dbghelp.h>
#   include <signal.h>
#   include <stdlib.h>
#   include <cstdarg>
#   pragma comment(lib, "dbghelp.lib")
#endif

// =================================================================================================

static void CopyText(char* dest, size_t size, const char* src) {
    snprintf(dest, size, "%s", src ? src : "");
}


void CrashHandler::SetFolder(const char* folder) {
    CopyText(m_folder, sizeof(m_folder), folder);
    size_t length = strlen(m_folder);
    if ((length > 0) and (m_folder[length - 1] != '/') and (m_folder[length - 1] != '\\') and (length + 1 < sizeof(m_folder))) {
        m_folder[length] = '/';
        m_folder[length + 1] = '\0';
    }
}

#ifdef _WIN32

// =================================================================================================

static constexpr unsigned long abortCode = 0xE0000001;
static constexpr unsigned long pureCallCode = 0xE0000002;
static constexpr unsigned long invalidParameterCode = 0xE0000003;
static constexpr unsigned long callStackCode = 0xE0000004;
static constexpr DWORD reportTimeout = 60000;
static constexpr int maxFrames = 256;
static constexpr DWORD symbolOptions = SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS;

#if defined(_M_X64)
static constexpr DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
static constexpr DWORD machineType = IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM64)
static constexpr DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
#endif

struct ExceptionName {
    unsigned long   code;
    const char*     name;
};

static const ExceptionName exceptionNames[] = {
    { EXCEPTION_ACCESS_VIOLATION,         "access violation" },
    { EXCEPTION_STACK_OVERFLOW,           "stack overflow" },
    { EXCEPTION_INT_DIVIDE_BY_ZERO,       "integer division by zero" },
    { EXCEPTION_INT_OVERFLOW,             "integer overflow" },
    { EXCEPTION_ILLEGAL_INSTRUCTION,      "illegal instruction" },
    { EXCEPTION_PRIV_INSTRUCTION,         "privileged instruction" },
    { EXCEPTION_ARRAY_BOUNDS_EXCEEDED,    "array bounds exceeded" },
    { EXCEPTION_DATATYPE_MISALIGNMENT,    "datatype misalignment" },
    { EXCEPTION_IN_PAGE_ERROR,            "in page error" },
    { EXCEPTION_FLT_DIVIDE_BY_ZERO,       "floating point division by zero" },
    { EXCEPTION_FLT_INVALID_OPERATION,    "floating point invalid operation" },
    { EXCEPTION_FLT_OVERFLOW,             "floating point overflow" },
    { EXCEPTION_FLT_UNDERFLOW,            "floating point underflow" },
    { EXCEPTION_FLT_DENORMAL_OPERAND,     "floating point denormal operand" },
    { EXCEPTION_FLT_INEXACT_RESULT,       "floating point inexact result" },
    { EXCEPTION_FLT_STACK_CHECK,          "floating point stack check" },
    { EXCEPTION_BREAKPOINT,               "breakpoint" },
    { EXCEPTION_NONCONTINUABLE_EXCEPTION, "noncontinuable exception" },
    { EXCEPTION_INVALID_DISPOSITION,      "invalid disposition" },
    { 0xC0000374,                         "heap corruption" },
    { 0xC0000409,                         "stack buffer overrun" },
    { 0xE06D7363,                         "unhandled C++ exception" },
    { abortCode,                          "abort" },
    { pureCallCode,                       "pure virtual function call" },
    { invalidParameterCode,               "invalid parameter passed to a C runtime function" },
};

struct TraceFrame {
    DWORD64 address;
    DWORD   inlineContext;
};

static CONTEXT              capturedContext;
static EXCEPTION_RECORD     capturedRecord;
static EXCEPTION_POINTERS   capturedPointers;
static CONTEXT              walkContext;
static SYSTEMTIME           crashTime;
static TraceFrame           traceFrames[maxFrames];
static char                 textBuffer[4096];
static char                 modulePath[MAX_PATH];
alignas(SYMBOL_INFO) static char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
static SRWLOCK              reportLock = SRWLOCK_INIT;

// =================================================================================================

static const char* GetExceptionName(unsigned long code) {
    for (const ExceptionName& entry : exceptionNames) {
        if (entry.code == code)
            return entry.name;
    }
    return "unknown exception";
}


static DWORD64 GetContextPC(const CONTEXT& context) {
#if defined(_M_X64)
    return context.Rip;
#elif defined(_M_IX86)
    return context.Eip;
#elif defined(_M_ARM64)
    return context.Pc;
#endif
}


static void InitStackFrame(STACKFRAME_EX& frame, const CONTEXT& context) {
    memset(&frame, 0, sizeof(frame));
    frame.StackFrameSize = sizeof(frame);
#if defined(_M_X64)
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64)
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
}


static void WriteText(void* file, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int length = vsnprintf(textBuffer, sizeof(textBuffer), format, args);
    va_end(args);
    if (length <= 0)
        return;
    if (length >= static_cast<int>(sizeof(textBuffer)))
        length = static_cast<int>(sizeof(textBuffer)) - 1;
    DWORD written = 0;
    WriteFile(file, textBuffer, static_cast<DWORD>(length), &written, nullptr);
}


static const char* GetModuleName(DWORD64 address, DWORD64& moduleBase) {
    moduleBase = 0;
    MEMORY_BASIC_INFORMATION memoryInfo;
    if (not VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(address)), &memoryInfo, sizeof(memoryInfo)))
        return nullptr;
    if (not memoryInfo.AllocationBase)
        return nullptr;
    DWORD length = GetModuleFileNameA(static_cast<HMODULE>(memoryInfo.AllocationBase), modulePath, MAX_PATH);
    if ((length == 0) or (length >= MAX_PATH))
        return nullptr;
    moduleBase = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(memoryInfo.AllocationBase));
    const char* separator = strrchr(modulePath, '\\');
    return separator ? separator + 1 : modulePath;
}

// =================================================================================================
// CrashHandler

void CrashHandler::Init(const char* appName, const char* appVersion) {
    if (m_isInitialized)
        return;
    CopyText(m_appName, sizeof(m_appName), appName);
    CopyText(m_appVersion, sizeof(m_appVersion), appVersion);

    char exeFolder[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, exeFolder, MAX_PATH);
    if ((length == 0) or (length >= MAX_PATH))
        exeFolder[0] = '\0';
    else {
        char* separator = strrchr(exeFolder, '\\');
        if (separator)
            separator[1] = '\0';
    }
    if (not *m_folder)
        SetFolder(exeFolder);

    char searchPath[MAX_PATH];
    CopyText(searchPath, sizeof(searchPath), exeFolder);
    size_t searchLength = strlen(searchPath);
    if (searchLength > 0)
        searchPath[searchLength - 1] = '\0';

    DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &m_process, 0, FALSE, DUPLICATE_SAME_ACCESS);
    SymSetOptions(symbolOptions);
    m_haveSymbols = SymInitialize(m_process, searchPath, TRUE) != FALSE;

    m_crashEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    m_doneEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    m_watcher = CreateThread(nullptr, 256 * 1024, WatcherThread, this, 0, nullptr);

    ULONG stackGuarantee = 64 * 1024;
    SetThreadStackGuarantee(&stackGuarantee);

    m_previousFilter = SetUnhandledExceptionFilter(OnUnhandledException);
    m_previousAbortHandler = signal(SIGABRT, OnAbortSignal);
    m_previousPureCallHandler = _set_purecall_handler(OnPureCall);
    m_previousInvalidParameterHandler = _set_invalid_parameter_handler(OnInvalidParameter);
    m_isInitialized = true;
}


long __stdcall CrashHandler::OnUnhandledException(_EXCEPTION_POINTERS* exceptionPointers) {
    CrashHandler& self = crashHandler;
    self.ReportException(exceptionPointers, GetExceptionName(exceptionPointers->ExceptionRecord->ExceptionCode));
    if (self.m_previousFilter)
        return self.m_previousFilter(exceptionPointers);
    return EXCEPTION_CONTINUE_SEARCH;
}


void __cdecl CrashHandler::OnAbortSignal(int signalNumber) {
    CrashHandler& self = crashHandler;
    self.ReportCurrentThread(GetExceptionName(abortCode), abortCode);
    SignalHandler previous = self.m_previousAbortHandler;
    if ((previous != SIG_DFL) and (previous != SIG_IGN) and (previous != SIG_ERR))
        previous(signalNumber);
}


void __cdecl CrashHandler::OnPureCall(void) {
    CrashHandler& self = crashHandler;
    self.ReportCurrentThread(GetExceptionName(pureCallCode), pureCallCode);
    if (self.m_previousPureCallHandler)
        self.m_previousPureCallHandler();
}


void __cdecl CrashHandler::OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
    CrashHandler& self = crashHandler;
    self.ReportCurrentThread(GetExceptionName(invalidParameterCode), invalidParameterCode);
    if (self.m_previousInvalidParameterHandler) {
        self.m_previousInvalidParameterHandler(expression, function, file, line, reserved);
        return;
    }
    _invoke_watson(expression, function, file, line, reserved);
}


unsigned long __stdcall CrashHandler::WatcherThread(void* param) {
    CrashHandler* self = static_cast<CrashHandler*>(param);
    WaitForSingleObject(self->m_crashEvent, INFINITE);
    self->WriteReport();
    SetEvent(self->m_doneEvent);
    return 0;
}


bool CrashHandler::BeginReport(void) {
    if (InterlockedCompareExchange(&m_isReporting, 1, 0) == 0)
        return true;
    if (GetThreadId(m_watcher) != GetCurrentThreadId())
        WaitForSingleObject(m_doneEvent, reportTimeout);
    return false;
}


void CrashHandler::FinishReport(_EXCEPTION_POINTERS* exceptionPointers, const char* reason) {
    m_exceptionPointers = exceptionPointers;
    m_threadId = GetCurrentThreadId();
    m_reason = reason;
    SetEvent(m_crashEvent);
    WaitForSingleObject(m_doneEvent, reportTimeout);
}


void CrashHandler::ReportException(_EXCEPTION_POINTERS* exceptionPointers, const char* reason) {
    if (BeginReport())
        FinishReport(exceptionPointers, reason);
}


void CrashHandler::ReportCurrentThread(const char* reason, unsigned long code) {
    if (not BeginReport())
        return;
    RtlCaptureContext(&capturedContext);
    memset(&capturedRecord, 0, sizeof(capturedRecord));
    capturedRecord.ExceptionCode = code;
    capturedRecord.ExceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(GetContextPC(capturedContext)));
    capturedPointers.ExceptionRecord = &capturedRecord;
    capturedPointers.ContextRecord = &capturedContext;
    FinishReport(&capturedPointers, reason);
}


bool CrashHandler::WriteCallStack(const char* reason, char* fileName, size_t fileNameSize) {
    if (fileName and (fileNameSize > 0))
        fileName[0] = '\0';
    if (not m_isInitialized)
        return false;

    CONTEXT context;
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = callStackCode;
    record.ExceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(GetContextPC(context)));
    EXCEPTION_POINTERS pointers;
    pointers.ExceptionRecord = &record;
    pointers.ContextRecord = &context;

    AcquireSRWLockExclusive(&reportLock);
    GetLocalTime(&crashTime);
    SymSetOptions(symbolOptions);
    if (m_haveSymbols)
        SymRefreshModuleList(m_process);

    bool isWritten = false;
    char traceFile[sizeof(m_folder) + 128];
    if (MakeFileName(traceFile, sizeof(traceFile), "trace", "txt")) {
        HANDLE file = CreateFileA(traceFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            WriteTrace(file, "trace", &pointers, GetCurrentThreadId(), reason ? reason : "call stack");
            CloseHandle(file);
            isWritten = true;
        }
    }
    ReleaseSRWLockExclusive(&reportLock);

    if (isWritten and fileName and (fileNameSize > 0))
        CopyText(fileName, fileNameSize, traceFile);
    return isWritten;
}


bool CrashHandler::MakeFileName(char* fileName, size_t size, const char* kind, const char* extension) {
    int length = snprintf(fileName, size, "%s%s-%s-%04u%02u%02u-%02u%02u%02u.%s",
                          m_folder, m_appName, kind,
                          crashTime.wYear, crashTime.wMonth, crashTime.wDay,
                          crashTime.wHour, crashTime.wMinute, crashTime.wSecond,
                          extension);
    return (length > 0) and (static_cast<size_t>(length) < size);
}


void CrashHandler::WriteReport(void) {
    AcquireSRWLockExclusive(&reportLock);
    GetLocalTime(&crashTime);
    SymSetOptions(symbolOptions);
    if (m_haveSymbols)
        SymRefreshModuleList(m_process);

    char fileName[sizeof(m_folder) + 128];
    if (MakeFileName(fileName, sizeof(fileName), "crash", "txt")) {
        HANDLE file = CreateFileA(fileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            WriteTrace(file, "crash", m_exceptionPointers, m_threadId, m_reason);
            CloseHandle(file);
        }
    }
    if (MakeFileName(fileName, sizeof(fileName), "crash", "dmp")) {
        HANDLE file = CreateFileA(fileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            WriteDump(file);
            CloseHandle(file);
        }
    }
    ReleaseSRWLockExclusive(&reportLock);
}


void CrashHandler::WriteTrace(void* file, const char* kind, _EXCEPTION_POINTERS* exceptionPointers, unsigned long threadId, const char* reason) {
    const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
    unsigned long code = record->ExceptionCode;

    WriteText(file, "%s %s %s report\r\n\r\n", m_appName, m_appVersion, kind);
    WriteText(file, "time:      %04u-%02u-%02u %02u:%02u:%02u\r\n",
              crashTime.wYear, crashTime.wMonth, crashTime.wDay, crashTime.wHour, crashTime.wMinute, crashTime.wSecond);
    WriteText(file, "reason:    %s (0x%08lX)\r\n", reason, code);
    if (((code == EXCEPTION_ACCESS_VIOLATION) or (code == EXCEPTION_IN_PAGE_ERROR)) and (record->NumberParameters >= 2)) {
        ULONG_PTR access = record->ExceptionInformation[0];
        const char* accessName = (access == 0) ? "reading" : (access == 1) ? "writing" : "executing";
        WriteText(file, "access:    %s address 0x%016llX\r\n", accessName, static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    WriteText(file, "thread:    %lu\r\n", threadId);

    if (not m_haveSymbols)
        WriteText(file, "symbols:   dbghelp could not be initialized, only addresses available\r\n");
    else {
        IMAGEHLP_MODULE64 moduleInfo;
        memset(&moduleInfo, 0, sizeof(moduleInfo));
        moduleInfo.SizeOfStruct = sizeof(moduleInfo);
        DWORD64 exeBase = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
        if (SymGetModuleInfo64(m_process, exeBase, &moduleInfo) and (moduleInfo.SymType == SymPdb) and moduleInfo.LineNumbers)
            WriteText(file, "symbols:   %s\r\n", moduleInfo.LoadedPdbName);
        else
            WriteText(file, "symbols:   no matching pdb found next to the exe, only addresses available\r\n");
    }

    WriteText(file, "\r\ncall stack:\r\n");

    walkContext = *exceptionPointers->ContextRecord;
    DWORD64 faultAddress = GetContextPC(walkContext);
    STACKFRAME_EX frame;
    InitStackFrame(frame, walkContext);
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
    int frameCount = 0;
    while (frameCount < maxFrames) {
        if (not StackWalkEx(machineType, m_process, thread, &frame, &walkContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr, SYM_STKWALK_DEFAULT))
            break;
        if (frame.AddrPC.Offset == 0)
            break;
        traceFrames[frameCount].address = frame.AddrPC.Offset;
        traceFrames[frameCount].inlineContext = frame.InlineFrameContext;
        ++frameCount;
    }
    if (thread)
        CloseHandle(thread);

    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    for (int i = 0; i < frameCount; ++i) {
        DWORD64 address = traceFrames[i].address;
        DWORD64 lookup = (address == faultAddress) ? address : address - 1;
        DWORD inlineContext = traceFrames[i].inlineContext;

        memset(symbolBuffer, 0, sizeof(symbolBuffer));
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        bool haveName = m_haveSymbols and (SymFromInlineContext(m_process, lookup, inlineContext, &displacement, symbol) != FALSE);

        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        bool haveLine = m_haveSymbols and (SymGetLineFromInlineContext(m_process, lookup, inlineContext, 0, &lineDisplacement, &line) != FALSE);

        DWORD64 moduleBase = 0;
        const char* moduleName = GetModuleName(address, moduleBase);

        if (haveName) {
            WriteText(file, "  #%02d  %s!%s", i, moduleName ? moduleName : "?", symbol->Name);
            if (not haveLine)
                WriteText(file, " + 0x%llX", static_cast<unsigned long long>(displacement));
        }
        else if (moduleName)
            WriteText(file, "  #%02d  %s+0x%llX", i, moduleName, static_cast<unsigned long long>(address - moduleBase));
        else
            WriteText(file, "  #%02d  0x%016llX", i, static_cast<unsigned long long>(address));
        if (haveLine)
            WriteText(file, "  %s(%lu)", line.FileName, line.LineNumber);
        WriteText(file, "\r\n");
    }
    if (frameCount == maxFrames)
        WriteText(file, "  ... truncated after %d frames\r\n", maxFrames);
}


void CrashHandler::WriteDump(void* file) {
    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
    exceptionInfo.ThreadId = m_threadId;
    exceptionInfo.ExceptionPointers = m_exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;
    MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules);
    MiniDumpWriteDump(m_process, GetCurrentProcessId(), file, dumpType, &exceptionInfo, nullptr, nullptr);
}

#else

// =================================================================================================

void CrashHandler::Init(const char* appName, const char* appVersion) {
    CopyText(m_appName, sizeof(m_appName), appName);
    CopyText(m_appVersion, sizeof(m_appVersion), appVersion);
    m_isInitialized = true;
}


bool CrashHandler::WriteCallStack(const char*, char* fileName, size_t fileNameSize) {
    if (fileName and (fileNameSize > 0))
        fileName[0] = '\0';
    return false;
}

#endif

// =================================================================================================
