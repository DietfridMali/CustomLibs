#pragma once

#include "filelist.h"

#include <regex>

// =================================================================================================

// C++20, plattformunabhängig (Windows/Linux). Unterstützt Muster wie "<ordner>/*.ext".
// Gibt nur Dateinamen (ohne Verzeichnisse) zurück. Leere Liste, wenn nichts passt.

namespace fs = std::filesystem;

// --- MakeRegexFromGlob mit operator[] ---
static String MakeRegexFromGlob(const String& glob) {
    String r;
    r.Reserve(glob.Length() * 2 + 4);
    r += '^';

    const int n = glob.Length();

    for (int i = 0; i < n; ++i) {
        char c = glob[i];
        switch (c) {
        case '*':
            r += ".*";
            break;

        case '?':
            r += '.';
            break;

        case '[': {
            int j = i + 1;
            r += '[';
            if (j < n and (glob[j] == '!' or glob[j] == '^')) {
                r += '^';
                ++j;
            }
            while (j < n and glob[j] != ']') {
                if (glob[j] != '\\') 
                    r += glob[j];
                else {
                    r += '\\';
                    r += '\\';
                }
                ++j;
            }
            if (j < n and glob[j] == ']') {
                r += ']';
                i = j;
            }
            else {
                r.Delete(r.Length() - 1, 1);
                r += "\\[";
            }
            break;
        }

        case '\\':
            r += '\\';
            r += '\\';
            break;

        case '.':
        case '+':
        case '(':
        case ')':
        case '{':
        case '}':
        case '^':
        case '$':
        case '|':
        case '/':
            r += '\\';
            r += c;
            break;

        default:
            r += c;
            break;
        }
    }

    r += '$';
    return r;
}


FileList FileLister::Get(const String& pattern) {
    FileList result;

    try {
        fs::path fullname((const char*)pattern);
        fs::path folder = fullname.parent_path();
        String mask = fullname.filename().string();

        if (not (fs::exists(folder) and fs::is_directory(folder)))
            return result;

        const String rxSource = MakeRegexFromGlob(mask);
#ifdef _WIN32
        const std::regex rx((const char*)rxSource, std::regex::ECMAScript | std::regex::icase);
#else
        const std::regex rx((const char*)rxSource, std::regex::ECMAScript);
#endif
        for (const auto& entry : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied)) {
            if (not entry.is_regular_file())
                continue;

            const fs::path& p = entry.path();
            const String name = String(p.filename().string());

            if (std::regex_match((const char*)name, rx)) {
                result.Append(name);
            }
        }
    }
    catch (...) {
    }
    return result;
}
// =================================================================================================

