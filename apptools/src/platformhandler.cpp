#include "platformhandler.h"

#include "steam_api.h"
#ifdef _WIN32
#   define NOMINMAX
#   include <windows.h>
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
    PlatformInterface* itf = m_interfaces[int(type)];
    if (not (itf and itf->Login()))
        return false;
    m_activeInterface = itf;
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
