#pragma once

#include <string>
#include <format>
#include <list>
#include <initializer_list>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <cctype>
#include <cstring>
#include "array.hpp"

// =================================================================================================

class String {
private:
    std::string m_str;

public:
    // Konstruktoren
    String() = default;

    String(const char* s) : m_str(s ? s : "") {}

    String(const std::string& s) : m_str(s) {}

    String(const char* s, size_t l) : m_str(s, l) {}

    explicit String(char c) : m_str(1, c) {}

    explicit String(int n) : m_str(std::to_string(n)) {}

    explicit String(size_t n) : m_str(std::to_string(n)) {}

    explicit String(float f) : m_str(std::to_string(f)) {}

    String(const String&) = default;

    String(String&&) noexcept = default;

    String& operator=(const String&) = default;

    String& operator=(String&&) noexcept = default;

    String& operator=(const char* s);

    String& operator=(size_t n);

    String& operator=(float f);

    // Operatoren
    operator std::string() const noexcept {
        return m_str;
    }

    String& operator+=(const String& other);
    String& operator+=(const char* s);
    String& operator+=(const char c);
    String operator+(const String& other) const;
    String operator+(const char* s) const;
    String operator+(const char c) const;

    bool operator==(const String& other) const noexcept;
    bool operator!=(const String& other) const noexcept;
    bool operator<(const String& other) const noexcept;
    bool operator>(const String& other) const noexcept;
    bool operator<=(const String& other) const noexcept;
    bool operator>=(const String& other) const noexcept;
    bool operator==(const char* s) const noexcept;
    bool operator!=(const char* s) const noexcept;

    // Typecasts
    operator const char* () const noexcept;
    operator char* () noexcept;
    explicit operator int() const;
    explicit operator size_t() const;
    explicit operator uint16_t() const;
    explicit operator float() const;
    explicit operator bool() const noexcept;

    // Eigenschaften
    inline int Length(void) const noexcept { return static_cast<int>(m_str.length()); }

    inline bool IsEmpty(void) const noexcept { return m_str.empty(); }

    inline void Reserve(size_t capacity) { m_str.reserve(capacity); }

    inline void Resize(size_t capacity) { m_str.resize(capacity); }

    inline void _Reset(void) noexcept {
        m_str.clear();
    }

    inline void Destroy(void) {
        m_str.clear();
        m_str.shrink_to_fit();
    }

    inline char* Data(void) noexcept { return m_str.data(); }

    inline const char* Data(void) const noexcept { return m_str.data(); }

    // Methoden
    inline String SubStr(int offset, int length) const {
        if (offset < 0 or offset > Length()) return String("");
        return String(m_str.substr(offset, length));
    }

    inline String& Delete(int offset, int length) noexcept {
        if (offset < 0 or offset >= Length()) return *this;
        m_str.erase(static_cast<size_t>(offset), static_cast<size_t>(length));
        return *this;
    }

    inline int Find(const char* pattern) const noexcept {
        auto pos = m_str.find(pattern);
        return pos != std::string::npos ? static_cast<int>(pos) : -1;
    }

    String Replace(const char* oldPattern, const char* newPattern = "", int repetitions = 0) const;

    static ManagedArray<String> Split(const String& str, char delim);

    inline ManagedArray<String> Split(char delim) const {
        return Split(m_str, delim);
    }

    inline auto begin() noexcept {
        return m_str.begin();
    }

    inline auto end() noexcept {
        return m_str.end();
    }

    inline bool IsLowercase() const noexcept {
        return std::all_of(m_str.begin(), m_str.end(), [](unsigned char c) {
            return not std::isalpha(c) or std::islower(c);
            });
    }

    inline bool IsUppercase() const noexcept {
        return std::all_of(m_str.begin(), m_str.end(), [](unsigned char c) {
            return not std::isalpha(c) or std::isupper(c);
            });
    }

    inline String ToLowercase() const {
        std::string tmp = m_str;
        std::transform(tmp.begin(), tmp.end(), tmp.begin(), [](unsigned char c) { return std::tolower(c); });
        return String(tmp);
    }

    inline String ToUppercase() const {
        std::string tmp = m_str;
        std::transform(tmp.begin(), tmp.end(), tmp.begin(), [](unsigned char c) { return std::toupper(c); });
        return String(tmp);
    }

    // Statische Helfer
    template <typename... Args>
    static String Concat(const Args&... args);

    static String Concat(std::initializer_list<String> values);

    static int Compare(void* context, const String& s1, const String& s2) noexcept {
        return (s1.Length() or s2.Length()) ? std::strcmp(static_cast<const char*>(s1), static_cast<const char*>(s2)) : 0;
    }

    template <typename... Args>
    void Format(std::string_view fmt, Args&&... args) {
        m_str = std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
    }
};

// ---------- Inline-Funktionen (kurze Operatoren & Zuweisungen) ----------

inline String& String::operator=(const char* s) {
    m_str = s ? s : "";
    return *this;
}

inline String& String::operator=(size_t n) {
    m_str = std::to_string(n);
    return *this;
}

inline String& String::operator=(float f) {
    m_str = std::to_string(f);
    return *this;
}

inline String& String::operator+=(const String& other) {
    m_str += other.m_str;
    return *this;
}

inline String& String::operator+=(const char* s) {
    m_str += s;
    return *this;
}

inline String& String::operator+=(const char c) {
    m_str += c;
    return *this;
}

inline String String::operator+(const String& other) const {
    return String(m_str + other.m_str);
}

inline String String::operator+(const char* s) const {
    return String(m_str + s);
}

inline String String::operator+(const char c) const {
    return String(m_str + c);
}

inline bool String::operator==(const String& other) const noexcept {
    return m_str == other.m_str;
}

inline bool String::operator!=(const String& other) const noexcept {
    return m_str != other.m_str;
}

inline bool String::operator<(const String& other) const noexcept {
    return m_str < other.m_str;
}

inline bool String::operator>(const String& other) const noexcept {
    return m_str > other.m_str;
}

inline bool String::operator<=(const String& other) const noexcept {
    return m_str <= other.m_str;
}

inline bool String::operator>=(const String& other) const noexcept {
    return m_str >= other.m_str;
}

inline bool String::operator==(const char* s) const noexcept {
    return m_str == (s ? s : "");
}

inline bool String::operator!=(const char* s) const noexcept {
    return m_str != (s ? s : "");
}

inline String::operator const char* () const noexcept {
    return m_str.c_str();
}

inline String::operator char* () noexcept {
    return m_str.data();
}

inline String::operator int() const {
    return std::stoi(m_str);
}

inline String::operator size_t() const {
    return static_cast<size_t>(std::stoll(m_str));
}

inline String::operator uint16_t() const {
    auto val = static_cast<size_t>(*this);
    if (val > std::numeric_limits<uint16_t>::max())
        throw std::out_of_range("Wert zu groß für uint16_t");
    return static_cast<uint16_t>(val);
}

inline String::operator float() const {
    return std::stof(m_str);
}

inline String::operator bool() const noexcept {
    return !m_str.empty() and m_str != "0";
}

// ---------- Template-Funktionen ----------

template <typename... Args>
String String::Concat(const Args&... args) {
    std::ostringstream oss;
    (oss << ... << args);
    return String(oss.str());
}

inline String String::Concat(std::initializer_list<String> values) {
    std::ostringstream oss;
    for (const auto& v : values)
        oss << static_cast<const char*>(v);
    return String(oss.str());   // BUGFIX: eine überflüssige Klammer entfernt
}

// =================================================================================================
