#pragma once

#include <string>
#include <vector>
#include <list>
#include <string_view>
#include <charconv>
#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <ranges>
#include <limits>

namespace StringUtils {

    // ---------- Konvertierungsfunktionen ----------

    inline int ToInt(std::string_view s) {
        int value{};
        auto res = std::from_chars(s.data(), s.data() + s.size(), value);
        if (res.ec != std::errc()) throw std::invalid_argument("Ungültiger Integer");
        return value;
    }

    inline size_t ToSizeT(std::string_view s) {
        size_t value{};
        auto res = std::from_chars(s.data(), s.data() + s.size(), value);
        if (res.ec != std::errc()) throw std::invalid_argument("Ungültiges size_t");
        return value;
    }

    inline float ToFloat(std::string_view s) {
        try {
            return std::stof(std::string{ s });
        }
        catch (...) {
            throw std::invalid_argument("Ungültiger Float");
        }
    }

    inline uint16_t ToUInt16(std::string_view s) {
        unsigned long val = ToSizeT(s);
        if (val > std::numeric_limits<uint16_t>::max())
            throw std::out_of_range("Wert überschreitet uint16_t");
        return static_cast<uint16_t>(val);
    }

    inline bool ToBool(std::string_view s) {
        return ToInt(s) != 0;
    }

    // ---------- Umwandlung in Klein-/Großbuchstaben ----------

    inline std::string ToLower(std::string_view s) {
        std::string result(s);
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    inline std::string ToUpper(std::string_view s) {
        std::string result(s);
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return std::toupper(c); });
        return result;
    }

    // ---------- Prüffunktionen ----------

    inline bool IsLower(std::string_view s) {
        return std::ranges::all_of(s, [](unsigned char c) { return std::islower(c) or !std::isalpha(c); });
    }

    inline bool IsUpper(std::string_view s) {
        return std::ranges::all_of(s, [](unsigned char c) { return std::isupper(c) or !std::isalpha(c); });
    }

    // ---------- Split ----------

    std::list<std::string> Split(std::string_view s, char delimiter) {
        std::list<std::string> parts;
        size_t start = 0;
        size_t end = 0;
        while ((end = s.find(delimiter, start)) != std::string_view::npos) {
            parts.emplace_back(s.substr(start, end - start));
            start = end + 1;
        }
        parts.emplace_back(s.substr(start));
        return parts;
    }

    // ---------- Replace mit Limitierung ----------

    std::string Replace(std::string_view s, std::string_view oldPattern, std::string_view newPattern, int repetitions = 0) {
        std::string result;
        size_t start = 0;
        int count = 0;

        while (true) {
            size_t pos = s.find(oldPattern, start);
            if (pos == std::string_view::npos or (repetitions > 0 and count >= repetitions)) {
                result.append(s.substr(start));
                break;
            }
            result.append(s.substr(start, pos - start));
            result.append(newPattern);
            start = pos + oldPattern.size();
            ++count;
        }
        return result;
    }

    // ---------- Concat (variadic) ----------

    template <typename... Args>
    std::string Concat(const Args&... args) {
        std::ostringstream oss;
        (oss << ... << args);
        return oss.str();
    }

    // ---------- Concat mit Initializer-List ----------

    std::string Concat(std::initializer_list<std::string_view> list) {
        size_t totalSize = 0;
        for (auto& str : list)
            totalSize += str.size();

        std::string result;
        result.reserve(totalSize);

        for (auto& str : list)
            result.append(str);

        return result;
    }

}
