#include "crashhandler.h"
#include "loghandler.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdlib.h>
#include <cstdarg>
#pragma comment(lib, "dbghelp.lib")
#elif defined(__linux__)
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <cxxabi.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#endif

// =================================================================================================

static void CopyText(char* dest, size_t size, const char* src)
{
	snprintf(dest, size, "%s", src ? src : "");
}


void CrashHandler::SetFolder(const char* folder)
{
	CopyText(m_folder, sizeof(m_folder), folder);
	size_t length = strlen(m_folder);
	if ((length > 0) and (m_folder[length - 1] != '/') and (m_folder[length - 1] != '\\') and (length + 1 < sizeof(m_folder))) {
		m_folder[length] = '/';
		m_folder[length + 1] = '\0';
	}
}

#ifdef _WIN32

// =================================================================================================

static constexpr unsigned long	abortCode = 0xE0000001;
static constexpr unsigned long	pureCallCode = 0xE0000002;
static constexpr unsigned long	invalidParameterCode = 0xE0000003;
static constexpr unsigned long	callStackCode = 0xE0000004;
static constexpr DWORD			reportTimeout = 60000;
static constexpr int			maxFrames = 256;
static constexpr DWORD			symbolOptions = SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS;

#if defined(_M_X64)
static constexpr DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
static constexpr DWORD machineType = IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM64)
static constexpr DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
#endif

struct ExceptionName {
	unsigned long	code;
	const char*		name;
};

static const ExceptionName exceptionNames[] = {
	{ EXCEPTION_ACCESS_VIOLATION, "access violation" },
	{ EXCEPTION_STACK_OVERFLOW, "stack overflow" },
	{ EXCEPTION_INT_DIVIDE_BY_ZERO, "integer division by zero" },
	{ EXCEPTION_INT_OVERFLOW, "integer overflow" },
	{ EXCEPTION_ILLEGAL_INSTRUCTION, "illegal instruction" },
	{ EXCEPTION_PRIV_INSTRUCTION, "privileged instruction" },
	{ EXCEPTION_ARRAY_BOUNDS_EXCEEDED, "array bounds exceeded" },
	{ EXCEPTION_DATATYPE_MISALIGNMENT, "datatype misalignment" },
	{ EXCEPTION_IN_PAGE_ERROR, "in page error" },
	{ EXCEPTION_FLT_DIVIDE_BY_ZERO, "floating point division by zero" },
	{ EXCEPTION_FLT_INVALID_OPERATION, "floating point invalid operation" },
	{ EXCEPTION_FLT_OVERFLOW, "floating point overflow" },
	{ EXCEPTION_FLT_UNDERFLOW, "floating point underflow" },
	{ EXCEPTION_FLT_DENORMAL_OPERAND, "floating point denormal operand" },
	{ EXCEPTION_FLT_INEXACT_RESULT, "floating point inexact result" },
	{ EXCEPTION_FLT_STACK_CHECK, "floating point stack check" },
	{ EXCEPTION_BREAKPOINT, "breakpoint" },
	{ EXCEPTION_NONCONTINUABLE_EXCEPTION, "noncontinuable exception" },
	{ EXCEPTION_INVALID_DISPOSITION, "invalid disposition" },
	{ 0xC0000374, "heap corruption" },
	{ 0xC0000409, "stack buffer overrun" },
	{ 0xE06D7363, "unhandled C++ exception" },
	{ abortCode, "abort" },
	{ pureCallCode, "pure virtual function call" },
	{ invalidParameterCode, "invalid parameter passed to a C runtime function" },
};

struct TraceFrame {
	DWORD64	address;
	DWORD	inlineContext;
};

static CONTEXT				capturedContext;
static EXCEPTION_RECORD		capturedRecord;
static EXCEPTION_POINTERS	capturedPointers;
static CONTEXT				walkContext;
static SYSTEMTIME			crashTime;
static TraceFrame			traceFrames[maxFrames];
static char					textBuffer[4096];
static char					modulePath[MAX_PATH];
alignas(SYMBOL_INFO) static char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
static SRWLOCK reportLock = SRWLOCK_INIT;

// =================================================================================================

static const char* GetExceptionName(unsigned long code)
{
	for (const ExceptionName& entry : exceptionNames) {
		if (entry.code == code)
			return entry.name;
	}
	return "unknown exception";
}


static DWORD64 GetContextPC(const CONTEXT& context)
{
#if defined(_M_X64)
	return context.Rip;
#elif defined(_M_IX86)
	return context.Eip;
#elif defined(_M_ARM64)
	return context.Pc;
#endif
}


static void InitStackFrame(STACKFRAME_EX& frame, const CONTEXT& context)
{
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


static void WriteText(void* file, const char* format, ...)
{
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


static const char* GetModuleName(DWORD64 address, DWORD64& moduleBase)
{
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

void CrashHandler::Init(const char* appName, const char* appVersion)
{
	if (m_isInitialized)
		return;
	CopyText(m_appName, sizeof(m_appName), appName);
	CopyText(m_appVersion, sizeof(m_appVersion), appVersion);

	char	exeFolder[MAX_PATH];
	DWORD	length = GetModuleFileNameA(nullptr, exeFolder, MAX_PATH);
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


long __stdcall CrashHandler::OnUnhandledException(_EXCEPTION_POINTERS* exceptionPointers)
{
	CrashHandler& self = crashHandler;
	self.ReportException(exceptionPointers, GetExceptionName(exceptionPointers->ExceptionRecord->ExceptionCode));
	if (self.m_previousFilter)
		return self.m_previousFilter(exceptionPointers);
	return EXCEPTION_CONTINUE_SEARCH;
}


void __cdecl CrashHandler::OnAbortSignal(int signalNumber)
{
	CrashHandler& self = crashHandler;
	self.ReportCurrentThread(GetExceptionName(abortCode), abortCode);
	SignalHandler previous = self.m_previousAbortHandler;
	if ((previous != SIG_DFL) and (previous != SIG_IGN) and (previous != SIG_ERR))
		previous(signalNumber);
}


void __cdecl CrashHandler::OnPureCall(void)
{
	CrashHandler& self = crashHandler;
	self.ReportCurrentThread(GetExceptionName(pureCallCode), pureCallCode);
	if (self.m_previousPureCallHandler)
		self.m_previousPureCallHandler();
}


void __cdecl CrashHandler::OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
											  unsigned int line, uintptr_t reserved)
{
	CrashHandler& self = crashHandler;
	self.ReportCurrentThread(GetExceptionName(invalidParameterCode), invalidParameterCode);
	if (self.m_previousInvalidParameterHandler) {
		self.m_previousInvalidParameterHandler(expression, function, file, line, reserved);
		return;
	}
	_invoke_watson(expression, function, file, line, reserved);
}


unsigned long __stdcall CrashHandler::WatcherThread(void* param)
{
	CrashHandler* self = static_cast<CrashHandler*>(param);
	WaitForSingleObject(self->m_crashEvent, INFINITE);
	self->WriteReport();
	SetEvent(self->m_doneEvent);
	return 0;
}


bool CrashHandler::BeginReport(void)
{
	if (InterlockedCompareExchange(&m_isReporting, 1, 0) == 0)
		return true;
	if (GetThreadId(m_watcher) != GetCurrentThreadId())
		WaitForSingleObject(m_doneEvent, reportTimeout);
	return false;
}


void CrashHandler::FinishReport(_EXCEPTION_POINTERS* exceptionPointers, const char* reason)
{
	m_exceptionPointers = exceptionPointers;
	m_threadId = GetCurrentThreadId();
	m_reason = reason;
	SetEvent(m_crashEvent);
	WaitForSingleObject(m_doneEvent, reportTimeout);
}


void CrashHandler::ReportException(_EXCEPTION_POINTERS* exceptionPointers, const char* reason)
{
	if (BeginReport())
		FinishReport(exceptionPointers, reason);
}


void CrashHandler::ReportCurrentThread(const char* reason, unsigned long code)
{
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


bool CrashHandler::WriteCallStack(const char* reason, char* fileName, size_t fileNameSize)
{
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


bool CrashHandler::MakeFileName(char* fileName, size_t size, const char* kind, const char* extension)
{
	int length = snprintf(fileName, size, "%s%s-%s.%s", m_folder, m_appName, kind, extension);
	return (length > 0) and (static_cast<size_t>(length) < size);
}


void CrashHandler::WriteReport(void)
{
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


void CrashHandler::WriteTrace(void* file, const char* kind, _EXCEPTION_POINTERS* exceptionPointers, unsigned long threadId,
							  const char* reason)
{
	const EXCEPTION_RECORD*	record = exceptionPointers->ExceptionRecord;
	unsigned long			code = record->ExceptionCode;

	WriteText(file, "%s %s %s report\r\n\r\n", m_appName, m_appVersion, kind);
	WriteText(file, "time:      %04u-%02u-%02u %02u:%02u:%02u\r\n",
			  crashTime.wYear, crashTime.wMonth, crashTime.wDay, crashTime.wHour, crashTime.wMinute, crashTime.wSecond);
	WriteText(file, "build:     %s\r\n", logHandler.BuildStamp());
	WriteText(file, "reason:    %s (0x%08lX)\r\n", reason, code);
	if (((code == EXCEPTION_ACCESS_VIOLATION) or (code == EXCEPTION_IN_PAGE_ERROR)) and (record->NumberParameters >= 2)) {
		ULONG_PTR	access = record->ExceptionInformation[0];
		const char*	accessName = (access == 0) ? "reading" : (access == 1) ? "writing"
																		   : "executing";
		WriteText(file, "access:    %s address 0x%016llX\r\n", accessName,
				  static_cast<unsigned long long>(record->ExceptionInformation[1]));
	}
	WriteText(file, "thread:    %lu\r\n", threadId);
	const char* context = logHandler.Context();
	if (*context)
		WriteText(file, "context:   %s\r\n", context);

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
	DWORD64			faultAddress = GetContextPC(walkContext);
	STACKFRAME_EX	frame;
	InitStackFrame(frame, walkContext);
	HANDLE	thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
	int		frameCount = 0;
	while (frameCount < maxFrames) {
		if (not StackWalkEx(machineType, m_process, thread, &frame, &walkContext, nullptr, SymFunctionTableAccess64,
							SymGetModuleBase64, nullptr, SYM_STKWALK_DEFAULT))
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
		DWORD64	address = traceFrames[i].address;
		DWORD64	lookup = (address == faultAddress) ? address : address - 1;
		DWORD	inlineContext = traceFrames[i].inlineContext;

		memset(symbolBuffer, 0, sizeof(symbolBuffer));
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = MAX_SYM_NAME;
		DWORD64	displacement = 0;
		bool	haveName = m_haveSymbols and (SymFromInlineContext(m_process, lookup, inlineContext, &displacement, symbol) != FALSE);

		IMAGEHLP_LINE64 line;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);
		DWORD	lineDisplacement = 0;
		bool	haveLine = m_haveSymbols and
			(SymGetLineFromInlineContext(m_process, lookup, inlineContext, 0, &lineDisplacement, &line) != FALSE);

		DWORD64		moduleBase = 0;
		const char*	moduleName = GetModuleName(address, moduleBase);

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


void CrashHandler::WriteDump(void* file)
{
	MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
	exceptionInfo.ThreadId = m_threadId;
	exceptionInfo.ExceptionPointers = m_exceptionPointers;
	exceptionInfo.ClientPointers = FALSE;
	MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
														MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules);
	MiniDumpWriteDump(m_process, GetCurrentProcessId(), file, dumpType, &exceptionInfo, nullptr, nullptr);
}

#elif defined(__linux__)

// =================================================================================================

static constexpr int	maxFrames = 256;
static constexpr time_t	reportTimeout = 60;
static constexpr size_t	signalStackSize = 64 * 1024;
static constexpr int	crashSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
static constexpr int	crashSignalCount = static_cast<int>(sizeof(crashSignals) / sizeof(crashSignals[0]));
static constexpr int	lineToolOptionCount = 7;
static constexpr int	lineToolMissing = 127;
static constexpr size_t	lineToolOutputSize = 64 * 1024;
static constexpr size_t	addressTextSize = 20;
#if defined(__x86_64__) or defined(__i386__)
static constexpr unsigned long pageFaultWrite = 0x02;
static constexpr unsigned long pageFaultExecute = 0x10;
#endif

struct SignalName {
	int			signalNumber;
	int			code;
	const char*	name;
};

static const SignalName signalNames[] = {
	{ SIGSEGV, SEGV_MAPERR, "segmentation fault, address not mapped" },
	{ SIGSEGV, SEGV_ACCERR, "segmentation fault, access not permitted" },
	{ SIGSEGV, 0, "segmentation fault" },
	{ SIGBUS, BUS_ADRALN, "bus error, invalid address alignment" },
	{ SIGBUS, BUS_ADRERR, "bus error, nonexistent physical address" },
	{ SIGBUS, 0, "bus error" },
	{ SIGFPE, FPE_INTDIV, "integer division by zero" },
	{ SIGFPE, FPE_INTOVF, "integer overflow" },
	{ SIGFPE, FPE_FLTDIV, "floating point division by zero" },
	{ SIGFPE, FPE_FLTOVF, "floating point overflow" },
	{ SIGFPE, FPE_FLTUND, "floating point underflow" },
	{ SIGFPE, FPE_FLTRES, "floating point inexact result" },
	{ SIGFPE, FPE_FLTINV, "floating point invalid operation" },
	{ SIGFPE, 0, "arithmetic exception" },
	{ SIGILL, 0, "illegal instruction" },
	{ SIGABRT, 0, "abort" },
};

struct TraceInfo {
	const char*	reason;
	int			signalNumber;
	int			signalCode;
	bool		hasAccess;
	const char*	accessName;
	uintptr_t	accessAddress;
	long		threadId;
	int			frameCount;
	int			firstFrame;
	uintptr_t	frames[maxFrames];
};

struct FrameInfo {
	const char*	moduleName;
	char*		modulePath;
	uintptr_t	moduleOffset;
	const char*	symbolName;
	uintptr_t	symbolOffset;
	char*		lines;
	bool		isResolved;
};

static TraceInfo		crashInfo;
static struct sigaction	previousActions[crashSignalCount];
static char				signalStack[signalStackSize];
static char				textBuffer[4096];
static char				exePath[1024];
static FrameInfo		frameInfos[maxFrames];
static int				moduleFrames[maxFrames];
static char				addressTexts[maxFrames][addressTextSize];
static char*			lineToolArguments[maxFrames + lineToolOptionCount + 1];
static char*			lineToolOutputs[maxFrames];
static char				lineToolName[] = "addr2line";
static char				lineToolOptions[][3] = { "-a", "-C", "-f", "-i", "-e" };
static sem_t			crashEvent;
static sem_t			doneEvent;
static pthread_t		watcherThread;
static pthread_mutex_t	reportLock = PTHREAD_MUTEX_INITIALIZER;
static std::atomic_flag	isReporting = ATOMIC_FLAG_INIT;

// =================================================================================================

static const char* GetSignalName(int signalNumber, int code)
{
	for (const SignalName& entry : signalNames) {
		if ((entry.signalNumber == signalNumber) and ((entry.code == code) or (entry.code == 0)))
			return entry.name;
	}
	return "unknown signal";
}


static uintptr_t GetContextPC(const ucontext_t& context)
{
#if defined(__x86_64__)
	return static_cast<uintptr_t>(context.uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
	return static_cast<uintptr_t>(context.uc_mcontext.gregs[REG_EIP]);
#elif defined(__aarch64__)
	return static_cast<uintptr_t>(context.uc_mcontext.pc);
#endif
}


static const char* GetAccessName(const ucontext_t& context)
{
#if defined(__x86_64__) or defined(__i386__)
	unsigned long faultFlags = static_cast<unsigned long>(context.uc_mcontext.gregs[REG_ERR]);
	if (faultFlags & pageFaultExecute)
		return "executing";
	if (faultFlags & pageFaultWrite)
		return "writing";
	return "reading";
#else
	return "accessing";
#endif
}


static void WriteText(int file, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int length = vsnprintf(textBuffer, sizeof(textBuffer), format, args);
	va_end(args);
	if (length <= 0)
		return;
	if (length >= static_cast<int>(sizeof(textBuffer)))
		length = static_cast<int>(sizeof(textBuffer)) - 1;
	const char* text = textBuffer;
	while (length > 0) {
		ssize_t written = write(file, text, static_cast<size_t>(length));
		if (written <= 0) {
			if ((written < 0) and (errno == EINTR))
				continue;
			return;
		}
		text += written;
		length -= static_cast<int>(written);
	}
}


static void GetFrameInfo(uintptr_t address, FrameInfo& frame)
{
	frame = FrameInfo();
	Dl_info				symbolInfo;
	struct link_map*	module = nullptr;
	if (not dladdr1(reinterpret_cast<void*>(address), &symbolInfo, reinterpret_cast<void**>(&module), RTLD_DL_LINKMAP))
		return;
	frame.modulePath = *module->l_name ? module->l_name : exePath;
	const char* separator = strrchr(frame.modulePath, '/');
	frame.moduleName = separator ? separator + 1 : frame.modulePath;
	frame.moduleOffset = address - static_cast<uintptr_t>(module->l_addr);
	if (symbolInfo.dli_sname) {
		frame.symbolName = symbolInfo.dli_sname;
		frame.symbolOffset = address - reinterpret_cast<uintptr_t>(symbolInfo.dli_saddr);
	}
}


static _Unwind_Reason_Code CollectFrame(struct _Unwind_Context* context, void* data)
{
	TraceInfo* info = static_cast<TraceInfo*>(data);
	if (info->frameCount == maxFrames)
		return _URC_END_OF_STACK;
	int			isBeforeInstruction = 0;
	uintptr_t	address = static_cast<uintptr_t>(_Unwind_GetIPInfo(context, &isBeforeInstruction));
	if (address == 0)
		return _URC_END_OF_STACK;
	if (not isBeforeInstruction)
		--address;
	info->frames[info->frameCount] = address;
	++info->frameCount;
	return _URC_NO_REASON;
}


static char* RunLineTool(int& exitCode)
{
	exitCode = -1;
	int pipeEnds[2];
	if (pipe(pipeEnds) != 0)
		return nullptr;
	pid_t child = fork();
	if (child < 0) {
		close(pipeEnds[0]);
		close(pipeEnds[1]);
		return nullptr;
	}
	if (child == 0) {
		int nullFile = open("/dev/null", O_WRONLY);
		dup2(pipeEnds[1], STDOUT_FILENO);
		dup2(nullFile, STDERR_FILENO);
		close(pipeEnds[0]);
		close(pipeEnds[1]);
		execvp(lineToolName, lineToolArguments);
		_exit(lineToolMissing);
	}
	close(pipeEnds[1]);
	size_t	capacity = lineToolOutputSize;
	size_t	length = 0;
	char*	output = static_cast<char*>(malloc(capacity));
	while (output) {
		if (length + 1 == capacity) {
			capacity *= 2;
			char* resized = static_cast<char*>(realloc(output, capacity));
			if (not resized)
				break;
			output = resized;
		}
		ssize_t count = read(pipeEnds[0], output + length, capacity - 1 - length);
		if (count > 0)
			length += static_cast<size_t>(count);
		else if ((count == 0) or (errno != EINTR))
			break;
	}
	if (output)
		output[length] = '\0';
	close(pipeEnds[0]);
	int		status = 0;
	pid_t	result = 0;
	do {
		result = waitpid(child, &status, 0);
	} while ((result < 0) and (errno == EINTR));
	if ((result == child) and WIFEXITED(status))
		exitCode = WEXITSTATUS(status);
	return output;
}


static void AssignLines(char* output, int frameCount)
{
	int		frameIndex = 0;
	char*	text = output;
	while (*text) {
		char* end = strchr(text, '\n');
		if ((text[0] == '0') and (text[1] == 'x')) {
			*text = '\0';
			if (frameIndex == frameCount)
				return;
			frameInfos[moduleFrames[frameIndex]].lines = end ? end + 1 : text;
			++frameIndex;
		}
		if (not end)
			return;
		text = end + 1;
	}
}


static bool ResolveFrames(const TraceInfo& info, int& outputCount)
{
	outputCount = 0;
	for (int i = info.firstFrame; i < info.frameCount; ++i)
		GetFrameInfo(info.frames[i], frameInfos[i]);
	bool haveLineTool = false;
	lineToolArguments[0] = lineToolName;
	for (int i = 0; i < lineToolOptionCount - 2; ++i)
		lineToolArguments[i + 1] = lineToolOptions[i];
	for (int i = info.firstFrame; i < info.frameCount; ++i) {
		FrameInfo& frame = frameInfos[i];
		if (frame.isResolved or not frame.modulePath)
			continue;
		int frameCount = 0;
		for (int j = i; j < info.frameCount; ++j) {
			FrameInfo& other = frameInfos[j];
			if (other.isResolved or not other.modulePath or (strcmp(other.modulePath, frame.modulePath) != 0))
				continue;
			other.isResolved = true;
			snprintf(addressTexts[frameCount], addressTextSize, "0x%llx", static_cast<unsigned long long>(other.moduleOffset));
			lineToolArguments[lineToolOptionCount + frameCount] = addressTexts[frameCount];
			moduleFrames[frameCount] = j;
			++frameCount;
		}
		lineToolArguments[lineToolOptionCount - 1] = frame.modulePath;
		lineToolArguments[lineToolOptionCount + frameCount] = nullptr;
		int		exitCode = 0;
		char*	output = RunLineTool(exitCode);
		if (not output)
			return haveLineTool;
		lineToolOutputs[outputCount] = output;
		++outputCount;
		if (exitCode == lineToolMissing)
			return haveLineTool;
		if (exitCode == 0) {
			haveLineTool = true;
			AssignLines(output, frameCount);
		}
	}
	return haveLineTool;
}


static bool WriteResolvedFrame(int file, const FrameInfo& frame, int& lineIndex)
{
	bool	isWritten = false;
	char*	text = frame.lines;
	while (text and *text) {
		char* function = text;
		char* end = strchr(text, '\n');
		if (not end)
			break;
		*end = '\0';
		char* location = end + 1;
		end = strchr(location, '\n');
		text = end ? end + 1 : nullptr;
		if (end)
			*end = '\0';
		if (function[0] == '?')
			continue;
		WriteText(file, "  #%02d  %s!%s", lineIndex, frame.moduleName, function);
		if (location[0] != '?')
			WriteText(file, "  %s", location);
		WriteText(file, "\n");
		++lineIndex;
		isWritten = true;
	}
	return isWritten;
}


static void WriteTrace(int file, const char* appName, const char* appVersion, const char* kind, const TraceInfo& info)
{
	time_t		now = time(nullptr);
	struct tm	localTime;
	localtime_r(&now, &localTime);
	int		outputCount = 0;
	bool	haveLineTool = ResolveFrames(info, outputCount);

	WriteText(file, "%s %s %s report\n\n", appName, appVersion, kind);
	WriteText(file, "time:      %04d-%02d-%02d %02d:%02d:%02d\n",
			  localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday, localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
	WriteText(file, "build:     %s\n", logHandler.BuildStamp());
	if (info.signalNumber != 0)
		WriteText(file, "reason:    %s (signal %d, code %d)\n", info.reason, info.signalNumber, info.signalCode);
	else
		WriteText(file, "reason:    %s\n", info.reason);
	if (info.hasAccess)
		WriteText(file, "access:    %s address 0x%016llX\n", info.accessName, static_cast<unsigned long long>(info.accessAddress));
	WriteText(file, "thread:    %ld\n", info.threadId);
	const char* context = logHandler.Context();
	if (*context)
		WriteText(file, "context:   %s\n", context);

	if (haveLineTool)
		WriteText(file, "symbols:   resolved with addr2line\n");
	else
		WriteText(file, "symbols:   addr2line not available, only exported function names\n");

	WriteText(file, "\ncall stack:\n");

	int lineIndex = 0;
	for (int i = info.firstFrame; i < info.frameCount; ++i) {
		const FrameInfo& frame = frameInfos[i];
		if (WriteResolvedFrame(file, frame, lineIndex))
			continue;
		if (frame.symbolName) {
			char* demangled = abi::__cxa_demangle(frame.symbolName, nullptr, nullptr, nullptr);
			WriteText(file, "  #%02d  %s!%s + 0x%llX\n", lineIndex, frame.moduleName, demangled ? demangled : frame.symbolName,
					  static_cast<unsigned long long>(frame.symbolOffset));
			free(demangled);
		}
		else if (frame.moduleName)
			WriteText(file, "  #%02d  %s+0x%llX\n", lineIndex, frame.moduleName, static_cast<unsigned long long>(frame.moduleOffset));
		else
			WriteText(file, "  #%02d  0x%016llX\n", lineIndex, static_cast<unsigned long long>(info.frames[i]));
		++lineIndex;
	}
	if (info.frameCount == maxFrames)
		WriteText(file, "  ... truncated after %d frames\n", maxFrames);
	for (int i = 0; i < outputCount; ++i)
		free(lineToolOutputs[i]);
}


static void WaitForReport(void)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += reportTimeout;
	int result = 0;
	do {
		result = sem_timedwait(&doneEvent, &deadline);
	} while ((result != 0) and (errno == EINTR));
	if (result == 0)
		sem_post(&doneEvent);
}

// =================================================================================================
// CrashHandler

void CrashHandler::Init(const char* appName, const char* appVersion)
{
	if (m_isInitialized)
		return;
	CopyText(m_appName, sizeof(m_appName), appName);
	CopyText(m_appVersion, sizeof(m_appVersion), appVersion);

	ssize_t length = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
	exePath[(length > 0) ? length : 0] = '\0';
	if (not *m_folder) {
		char exeFolder[sizeof(exePath)];
		CopyText(exeFolder, sizeof(exeFolder), exePath);
		char* separator = strrchr(exeFolder, '/');
		if (separator) {
			separator[1] = '\0';
			SetFolder(exeFolder);
		}
	}

	sem_init(&crashEvent, 0, 0);
	sem_init(&doneEvent, 0, 0);
	pthread_create(&watcherThread, nullptr, WatcherThread, this);
	pthread_detach(watcherThread);

	stack_t stackInfo;
	stackInfo.ss_sp = signalStack;
	stackInfo.ss_size = sizeof(signalStack);
	stackInfo.ss_flags = 0;
	sigaltstack(&stackInfo, nullptr);

	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = OnSignal;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	for (int signalNumber : crashSignals)
		sigaddset(&action.sa_mask, signalNumber);
	for (int i = 0; i < crashSignalCount; ++i)
		sigaction(crashSignals[i], &action, &previousActions[i]);
	m_isInitialized = true;
}


void CrashHandler::OnSignal(int signalNumber, siginfo_t* signalInfo, void* context)
{
	if (not isReporting.test_and_set()) {
		const ucontext_t*	signalContext = static_cast<const ucontext_t*>(context);
		uintptr_t			faultAddress = GetContextPC(*signalContext);
		crashInfo.reason = GetSignalName(signalNumber, signalInfo->si_code);
		crashInfo.signalNumber = signalNumber;
		crashInfo.signalCode = signalInfo->si_code;
		crashInfo.hasAccess = (signalInfo->si_code > 0) and ((signalNumber == SIGSEGV) or (signalNumber == SIGBUS));
		crashInfo.accessName = GetAccessName(*signalContext);
		crashInfo.accessAddress = reinterpret_cast<uintptr_t>(signalInfo->si_addr);
		crashInfo.threadId = static_cast<long>(syscall(SYS_gettid));
		crashInfo.frameCount = 0;
		_Unwind_Backtrace(CollectFrame, &crashInfo);
		crashInfo.firstFrame = 0;
		for (int i = 0; i < crashInfo.frameCount; ++i) {
			if (crashInfo.frames[i] == faultAddress) {
				crashInfo.firstFrame = i;
				break;
			}
		}
		sem_post(&crashEvent);
		WaitForReport();
	}
	else if (not pthread_equal(pthread_self(), watcherThread))
		WaitForReport();

	for (int i = 0; i < crashSignalCount; ++i)
		sigaction(crashSignals[i], &previousActions[i], nullptr);
	if (signalInfo->si_code <= 0)
		raise(signalNumber);
}


void* CrashHandler::WatcherThread(void* param)
{
	CrashHandler*	self = static_cast<CrashHandler*>(param);
	int				result = 0;
	do {
		result = sem_wait(&crashEvent);
	} while ((result != 0) and (errno == EINTR));
	self->WriteReport();
	sem_post(&doneEvent);
	return nullptr;
}


bool CrashHandler::WriteCallStack(const char* reason, char* fileName, size_t fileNameSize)
{
	if (fileName and (fileNameSize > 0))
		fileName[0] = '\0';
	if (not m_isInitialized)
		return false;

	TraceInfo info{};
	info.reason = reason ? reason : "call stack";
	info.threadId = static_cast<long>(syscall(SYS_gettid));
	_Unwind_Backtrace(CollectFrame, &info);

	pthread_mutex_lock(&reportLock);
	bool isWritten = false;
	char traceFile[sizeof(m_folder) + 128];
	if (MakeFileName(traceFile, sizeof(traceFile), "trace", "txt")) {
		int file = open(traceFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (file >= 0) {
			WriteTrace(file, m_appName, m_appVersion, "trace", info);
			close(file);
			isWritten = true;
		}
	}
	pthread_mutex_unlock(&reportLock);

	if (isWritten and fileName and (fileNameSize > 0))
		CopyText(fileName, fileNameSize, traceFile);
	return isWritten;
}


bool CrashHandler::MakeFileName(char* fileName, size_t size, const char* kind, const char* extension)
{
	int length = snprintf(fileName, size, "%s%s-%s.%s", m_folder, m_appName, kind, extension);
	return (length > 0) and (static_cast<size_t>(length) < size);
}


void CrashHandler::WriteReport(void)
{
	pthread_mutex_lock(&reportLock);
	char fileName[sizeof(m_folder) + 128];
	if (MakeFileName(fileName, sizeof(fileName), "crash", "txt")) {
		int file = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (file >= 0) {
			WriteTrace(file, m_appName, m_appVersion, "crash", crashInfo);
			close(file);
		}
	}
	pthread_mutex_unlock(&reportLock);
}

#else

// =================================================================================================

void CrashHandler::Init(const char* appName, const char* appVersion)
{
	CopyText(m_appName, sizeof(m_appName), appName);
	CopyText(m_appVersion, sizeof(m_appVersion), appVersion);
	m_isInitialized = true;
}


bool CrashHandler::WriteCallStack(const char*, char* fileName, size_t fileNameSize)
{
	if (fileName and (fileNameSize > 0))
		fileName[0] = '\0';
	return false;
}

#endif

// =================================================================================================
