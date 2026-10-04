#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <mutex>

#include "basesingleton.hpp"

// =================================================================================================

class LogHandler
    : public BaseSingleton<LogHandler>
{
public:
    static constexpr int kMaxIndent = 30;
    static constexpr size_t kLineSize = 100000;
    static constexpr size_t kContextSize = 256;
    static constexpr size_t kPathSize = 1024;

    ~LogHandler();

    bool Init(const char* folder, const char* fileName) noexcept;

    void Close(void) noexcept;

    inline bool IsOpen(void) const noexcept { return m_file != nullptr; }

    inline const char* Path(void) const noexcept { return m_path; }

    void Print(const char* format, ...) noexcept;

    void PrintArgs(const char* format, va_list args) noexcept;

    void Indent(int delta) noexcept;

    int SetIndent(int indent) noexcept;

    inline int Indentation(void) const noexcept { return m_indent; }

    void SetContext(const char* format, ...) noexcept;

    void ClearContext(void) noexcept;

    inline const char* Context(void) const noexcept { return m_context; }

private:
    std::mutex  m_mutex;
    FILE*       m_file { nullptr };
    int         m_indent { 0 };
    int         m_lineIndex { 0 };
    bool        m_isLineStart { true };
    char        m_path[kPathSize] { };
    char        m_context[kContextSize] { };
    char        m_lines[2][kLineSize] { };

    void CloseFile(void) noexcept;

    void Write(const char* text) noexcept;
};

#define logHandler LogHandler::Instance()

// =================================================================================================
