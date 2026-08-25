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

#if XBOX
#   ifdef _WIN32
#       define NOMINMAX
#       include <windows.h>
#   endif
#   include <XGameRuntimeInit.h>
#   include <XUser.h>
#endif

#include <cstdio>

#ifdef LOG
#   undef LOG
#endif

#if 1 //def _DEBUG
#   define LOG(msg, ...) fprintf(stderr, msg, ##__VA_ARGS__)
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

// =================================================================================================
