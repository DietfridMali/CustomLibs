#include "platform.h"

#include "steam_api.h"
#ifdef _WIN32
#   define NOMINMAX
#   include <windows.h>
#   include <XGameRuntimeInit.h>
#   include <XUser.h>
#endif

#include <cstdio>

// =================================================================================================
// SteamInterface

bool SteamInterface::Login(void) {
    if (not SteamAPI_Init()) {
        fprintf(stderr, "SteamInterface::Login: SteamAPI_Init failed\n");
        return false;
    }
    if (not SteamUser()) {
        fprintf(stderr, "SteamInterface::Login: SteamUser not found\n");
        SteamAPI_Shutdown();
        return false;
    }
    if (not SteamUser()->BLoggedOn()) {
        fprintf(stderr, "SteamInterface::Login: not logged in\n");
        SteamAPI_Shutdown();
        return false;
    }
    m_userId = SteamUser()->GetSteamID().ConvertToUint64();
    fprintf(stderr, "SteamInterface::Login: logged in, SteamID=%llu\n", (unsigned long long)m_userId);
    return true;
}


void SteamInterface::Logout(void) {
    SteamAPI_Shutdown();
    m_userId = 0;
}


void SteamInterface::Update(void) {
    SteamAPI_RunCallbacks();
}


uint64_t SteamInterface::GetUserID(void) const {
    return m_userId;
}

// =================================================================================================
// XBoxInterface

#ifdef _WIN32

bool XBoxInterface::Login(void) {
    HRESULT hr = XGameRuntimeInitialize();
    if (FAILED(hr)) {
        fprintf(stderr, "XBoxInterface::Login: XGameRuntimeInitialize failed (0x%08X)\n", hr);
        return false;
    }
    XAsyncBlock asyncBlock{};
    asyncBlock.queue = nullptr;
    XUserHandle userHandle = nullptr;
    hr = XUserAddAsync(XUserAddOptions::AddDefaultUserSilently, &asyncBlock);
    if (FAILED(hr)) {
        fprintf(stderr, "XBoxInterface::Login: XUserAddAsync failed (0x%08X)\n", hr);
        XGameRuntimeUninitialize();
        return false;
    }
    hr = XAsyncGetStatus(&asyncBlock, true);
    if (FAILED(hr)) {
        fprintf(stderr, "XBoxInterface::Login: XAsyncGetStatus failed (0x%08X)\n", hr);
        XGameRuntimeUninitialize();
        return false;
    }
    hr = XUserAddResult(&asyncBlock, &userHandle);
    if (FAILED(hr)) {
        fprintf(stderr, "XBoxInterface::Login: XUserAddResult failed (0x%08X)\n", hr);
        XGameRuntimeUninitialize();
        return false;
    }
    hr = XUserGetId(userHandle, &m_xuid);
    if (FAILED(hr)) {
        fprintf(stderr, "XBoxInterface::Login: XUserGetId failed (0x%08X)\n", hr);
        XUserCloseHandle(userHandle);
        XGameRuntimeUninitialize();
        return false;
    }
    m_userHandle = userHandle;
    fprintf(stderr, "XBoxInterface::Login: signed in, XUID=%llu\n", (unsigned long long)m_xuid);
    return true;
}


void XBoxInterface::Logout(void) {
    if (m_userHandle) {
        XUserCloseHandle(static_cast<XUserHandle>(m_userHandle));
        m_userHandle = nullptr;
    }
    XGameRuntimeUninitialize();
    m_xuid = 0;
}


void XBoxInterface::Update(void) {
}


uint64_t XBoxInterface::GetUserID(void) const {
    return m_xuid;
}

#else

bool XBoxInterface::Login(void) { return false; }
void XBoxInterface::Logout(void) { }
void XBoxInterface::Update(void) { }
uint64_t XBoxInterface::GetUserID(void) const { return 0; }

#endif

// =================================================================================================
// PlatformHandler

bool PlatformHandler::Init(void) {
    if (m_steamItf.Login()) {
        m_platformType = PlatformType::Steam;
        m_activeItf = &m_steamItf;
        return true;
    }
#ifdef _WIN32
    if (m_xboxItf.Login()) {
        m_platformType = PlatformType::XBox;
        m_activeItf = &m_xboxItf;
        return true;
    }
    fprintf(stderr, "PlatformHandler::Init: no platform available\n");
#endif
    return false;
}


void PlatformHandler::Shutdown(void) {
    if (m_activeItf) {
        m_activeItf->Logout();
        m_activeItf = nullptr;
    }
    m_platformType = PlatformType::Unknown;
}


void PlatformHandler::Update(void) {
    if (m_activeItf)
        m_activeItf->Update();
}


uint64_t PlatformHandler::GetUserID(void) const {
    return m_activeItf ? m_activeItf->GetUserID() : 0;
}

// =================================================================================================
