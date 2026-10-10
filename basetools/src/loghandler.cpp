#include "loghandler.h"

#include <cstdint>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <share.h>
#else
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

// =================================================================================================

#ifdef _DEBUG
static constexpr bool kEchoToConsole = true;
#else
static constexpr bool kEchoToConsole = false;
#endif


static time_t ProgramBuildTime(void)
noexcept
{
#ifdef _WIN32
	const uint8_t*			image = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
	const IMAGE_DOS_HEADER*	dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	const IMAGE_NT_HEADERS*	ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dosHeader->e_lfanew);
	return time_t(ntHeaders->FileHeader.TimeDateStamp);
#else
	struct stat info{};
#ifdef __APPLE__
	char		path[4096]{};
	uint32_t	size = uint32_t(sizeof(path));
	_NSGetExecutablePath(path, &size);
	stat(path, &info);
#else
	stat("/proc/self/exe", &info);
#endif
	return info.st_mtime;
#endif
}


LogHandler::LogHandler()
noexcept
{
	const time_t	buildTime = ProgramBuildTime();
	struct tm		utc{};
#ifdef _WIN32
	gmtime_s(&utc, &buildTime);
#else
	gmtime_r(&buildTime, &utc);
#endif
	strftime(m_buildStamp, sizeof(m_buildStamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
}


LogHandler::~LogHandler()
{
	CloseFile();
}


bool LogHandler::Init(const char* folder, const char* fileName)
noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	CloseFile();
	const size_t	length = strlen(folder);
	const bool		needsSeparator = (length > 0) and (folder[length - 1] != '/') and (folder[length - 1] != '\\');
	snprintf(m_path, sizeof(m_path), "%s%s%s", folder, needsSeparator ? "/" : "", fileName);
#ifdef _WIN32
	m_file = _fsopen(m_path, "wt", _SH_DENYWR);
#else
	m_file = fopen(m_path, "wt");
#endif
	m_isLineStart = true;
	return m_file != nullptr;
}


void LogHandler::Close(void)
noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	CloseFile();
}


void LogHandler::CloseFile(void)
noexcept
{
	if (m_file != nullptr) {
		fclose(m_file);
		m_file = nullptr;
	}
}


void LogHandler::Write(const char* text)
noexcept
{
	if (m_file != nullptr) {
		fputs(text, m_file);
		fflush(m_file);
	}
	if (kEchoToConsole or (m_file == nullptr)) {
		fputs(text, stderr);
		fflush(stderr);
	}
}


void LogHandler::Print(const char* format, ...)
noexcept
{
	va_list args;
	va_start(args, format);
	PrintArgs(format, args);
	va_end(args);
}


void LogHandler::PrintArgs(const char* format, va_list args)
noexcept
{
	if ((format == nullptr) or (*format == '\0'))
		return;
	std::lock_guard<std::mutex>	lock(m_mutex);
	char*						line = m_lines[m_lineIndex];
	const size_t				indent = m_isLineStart ? size_t(m_indent) : 0;
	memset(line, ' ', indent);
	vsnprintf(line + indent, kLineSize - indent, format, args);
	if (line[indent] == '\0')
		return;
	const size_t	length = strlen(line);
	const bool		isCompleteLine = m_isLineStart and (line[length - 1] == '\n');
	if (isCompleteLine and (strcmp(line, m_lines[1 - m_lineIndex]) == 0))
		return;
	Write(line);
	m_isLineStart = (line[length - 1] == '\n');
	m_lineIndex = 1 - m_lineIndex;
}


void LogHandler::Indent(int delta)
noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_indent += delta;
	if ((m_indent < 0) or (m_indent > kMaxIndent)) {
		Write("Log indentation error!\n");
		m_indent = (m_indent < 0) ? 0 : kMaxIndent;
	}
}


int LogHandler::SetIndent(int indent)
noexcept
{
	std::lock_guard<std::mutex>	lock(m_mutex);
	const int					previous = m_indent;
	if (indent < 0)
		indent = 0;
	else if (indent > kMaxIndent)
		indent = kMaxIndent;
	m_indent = indent;
	return previous;
}


void LogHandler::SetContext(const char* format, ...)
noexcept
{
	std::lock_guard<std::mutex>	lock(m_mutex);
	va_list						args;
	va_start(args, format);
	vsnprintf(m_context, sizeof(m_context), format, args);
	va_end(args);
}


void LogHandler::ClearContext(void)
noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_context[0] = '\0';
}

// =================================================================================================
