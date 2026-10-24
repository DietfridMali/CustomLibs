#pragma once

#include <string>
#include <cstring>
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

// ---------- Conversion functions ----------

inline int ToInt(std::string_view s)
{
	int		value{};
	auto	res = std::from_chars(s.data(), s.data() + s.size(), value);
	if (res.ec != std::errc())
		throw std::invalid_argument("Invalid integer");
	return value;
}

inline size_t ToSizeT(std::string_view s)
{
	size_t	value{};
	auto	res = std::from_chars(s.data(), s.data() + s.size(), value);
	if (res.ec != std::errc())
		throw std::invalid_argument("Invalid size_t");
	return value;}

inline float ToFloat(std::string_view s)
{
	try {
		return std::stof(std::string{ s });
	}
	catch (...) {
		throw std::invalid_argument("Invalid float");
	}
}

inline uint16_t ToUInt16(std::string_view s)
{
	size_t val = ToSizeT(s);
	if (val > (std::numeric_limits<uint16_t>::max)())
		throw std::out_of_range("Value exceeds uint16_t");
	return static_cast<uint16_t>(val);
}

inline bool ToBool(std::string_view s) {
	return ToInt(s) != 0;
}

// ---------- Lower and upper case ----------

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

// ---------- Test functions ----------

inline bool IsLower(std::string_view s) {
	return std::ranges::all_of(s, [](unsigned char c) { return std::islower(c) or !std::isalpha(c); });
}

inline bool IsUpper(std::string_view s) {
	return std::ranges::all_of(s, [](unsigned char c) { return std::isupper(c) or !std::isalpha(c); });
}

// ---------- Split ----------

inline std::list<std::string> Split(std::string_view s, char delimiter)
{
	std::list<std::string>	parts;
	size_t					start = 0;
	size_t					end = 0;
	while ((end = s.find(delimiter, start)) != std::string_view::npos) {
		parts.emplace_back(s.substr(start, end - start));
		start = end + 1;
	}
	parts.emplace_back(s.substr(start));
	return parts;
}

// ---------- Replace with limit ----------

inline std::string Replace(std::string_view s, std::string_view oldPattern, std::string_view newPattern, int repetitions = 0)
{
	std::string	result;
	size_t		start = 0;
	int			count = 0;

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

// ---------- Concat with initializer list ----------

inline std::string Concat(std::initializer_list<std::string_view> list)
{
	size_t totalSize = 0;
	for (auto& str : list)
		totalSize += str.size();

	std::string result;
	result.reserve(totalSize);

	for (auto& str : list)
		result.append(str);

	return result;
}

inline char* CopyString(char* dest, size_t destSize, const char* src, size_t count = (std::numeric_limits<size_t>::max)()) {
	size_t	l = 0;
	while ((l < count) and (l + 1 < destSize) and (src[l] != '\0'))
		++l;
	std::memcpy(dest, src, l);
	dest[l] = '\0';
	return dest;
}

inline char* AppendString(char* dest, size_t destSize, const char* src) {
	size_t	l = std::strlen(dest);
	CopyString(dest + l, destSize - l, src);
	return dest;
}

inline int CompareNoCase(const char* s1, const char* s2, size_t count = (std::numeric_limits<size_t>::max)()) {
	for (size_t i = 0; i < count; ++i) {
		int	c1 = std::tolower(static_cast<unsigned char>(s1[i]));
		int	c2 = std::tolower(static_cast<unsigned char>(s2[i]));
		if ((c1 != c2) or (c1 == 0))
			return c1 - c2;
	}
	return 0;
}

inline char* FindNoCase(char* s, const char* sub) {
	size_t	l = std::strlen(sub);
	for (char* p = s; ; ++p) {
		if (CompareNoCase(p, sub, l) == 0)
			return p;
		if (*p == '\0')
			return nullptr;
	}
}

inline const char* FindNoCase(const char* s, const char* sub) {
	return FindNoCase(const_cast<char*>(s), sub);
}

inline char* MakeLower(char* s) {
	for (char* p = s; *p != '\0'; ++p)
		*p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
	return s;
}

inline char* MakeUpper(char* s) {
	for (char* p = s; *p != '\0'; ++p)
		*p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
	return s;
}

}
