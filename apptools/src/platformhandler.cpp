#include "platformhandler.h"

// Platform SDKs. Value defines, not presence defines: the tree already tests the Xbox switch as
// `#if XBOX` (clientcommunication.cpp), and a `#ifdef` form would turn `XBOX=0` into "Xbox enabled".
// Defaults keep the current build unchanged - Steam on, Xbox off.
#ifndef STEAM
#   define STEAM 1
#endif
#ifndef XBOX
#   define XBOX 0
#endif

#if STEAM
#   include "steam_api.h"
#endif

#ifdef _WIN32
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#   include <string>
#else
#   include <cstdlib>
#endif

#if XBOX
#   ifdef _WIN32
#       define NOMINMAX
#       include <windows.h>
#   endif
#   include <XGameRuntimeInit.h>
#   include <XUser.h>
#endif

#include <cstdio>

#include "loghandler.h"

#ifdef LOG
#   undef LOG
#endif

#if 1 //def _DEBUG
#   define LOG(msg, ...) logHandler.Print(msg, ##__VA_ARGS__)
#else
#   define LOG(msg, ...) {}
#endif

// =================================================================================================
// PlatformHandler

bool PlatformHandler::Init(PlatformType type) {
    if (type == PlatformType::Unknown)
        return false;
    PlatformInterface* itf = m_interfaces[int(type)];
    if (not (itf and itf->Login()))
        return false;
    m_activeInterface = itf;
    m_platformType = type;
    return true;
}


bool PlatformHandler::Init(void) {
    if (Init(PlatformType::Steam) or Init(PlatformType::XBox))
        return true;
    LOG("PlatformHandler::Init: no platform available\n");
    return false;
}


void PlatformHandler::Shutdown(void) {
    if (m_activeInterface) {
        m_activeInterface->Logout();
        m_activeInterface = nullptr;
    }
    m_platformType = PlatformType::Unknown;
}


void PlatformHandler::Update(void) {
    if (m_activeInterface)
        m_activeInterface->Update();
}


uint64_t PlatformHandler::GetUserID(void) const {
    return m_activeInterface ? m_activeInterface->GetUserID() : 0;
}


String PlatformHandler::GetLanguage(void) const {
    if (m_activeInterface) {
        String language = m_activeInterface->GetLanguage();
        if (not language.IsEmpty())
            return language;
    }
    return SystemLanguage();
}


String PlatformHandler::SystemLanguage(void) {
#ifdef _WIN32
    ULONG count = 0;
    ULONG size = 0;
    if (not GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) or (size == 0))
        return String("");
    std::wstring buffer(size, L'\0');
    if (not GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size))
        return String("");
    std::string language;
    for (wchar_t c : buffer) {
        if (c == L'\0')
            break;
        language += char(c);
    }
    return String(language.c_str());
#else
    const char* variables[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
    for (const char* variable : variables) {
        const char* value = std::getenv(variable);
        if (value and *value) {
            String language(value);
            int end = language.Find('.');
            if (end >= 0)
                language = language.SubStr(0, end);
            return language.Replace("_", "-");
        }
    }
    return String("");
#endif
}


String PlatformHandler::SteamLanguageTag(const char* steamLanguage) {
    struct LanguageTag {
        const char* steamName;
        const char* tag;
    };

    static const LanguageTag tags[] = {
        { "arabic", "ar" },
        { "bulgarian", "bg" },
        { "schinese", "zh-CN" },
        { "tchinese", "zh-TW" },
        { "czech", "cs" },
        { "danish", "da" },
        { "dutch", "nl" },
        { "english", "en" },
        { "finnish", "fi" },
        { "french", "fr" },
        { "german", "de" },
        { "greek", "el" },
        { "hungarian", "hu" },
        { "indonesian", "id" },
        { "italian", "it" },
        { "japanese", "ja" },
        { "koreana", "ko" },
        { "malay", "ms" },
        { "norwegian", "no" },
        { "polish", "pl" },
        { "portuguese", "pt" },
        { "brazilian", "pt-BR" },
        { "romanian", "ro" },
        { "russian", "ru" },
        { "spanish", "es" },
        { "latam", "es-419" },
        { "swedish", "sv" },
        { "thai", "th" },
        { "turkish", "tr" },
        { "ukrainian", "uk" },
        { "vietnamese", "vi" }
    };

    if (not (steamLanguage and *steamLanguage))
        return String("");
    String name = String(steamLanguage).ToLowercase();
    for (const LanguageTag& t : tags) {
        if (name == t.steamName)
            return String(t.tag);
    }
    return String("");
}

// =================================================================================================
