#if defined(COMPILE_STRING)

#include "string.hpp"
#include <string>
#include <algorithm>

String String::Replace(const char* oldPattern, const char* newPattern, int repetitions) const {
    std::string result = m_str;
    std::string oldStr(oldPattern);
    std::string newStr(newPattern);

    size_t pos = 0;
    int count = 0;

    while ((pos = result.find(oldStr, pos)) != std::string::npos) {
        result.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
        if (repetitions > 0 and ++count >= repetitions)
            break;
    }

    return String(result);
}


ManagedArray<String> String::Split(const String& str, char delimiter) {
    ManagedArray<String> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        result.Append(item);
    }
    return result;
}

#endif //COMPILE_STRING