#pragma once

#include <stdint.h>
#include "basesingleton.hpp"

// =================================================================================================

enum class PlatformType { Unknown, Steam, XBox };

// =================================================================================================

class BasePlatformInterface {
public:
    virtual ~BasePlatformInterface() = default;
    
    virtual bool Login(void) = 0;
    
    virtual void Logout(void) = 0;
    
    virtual void Update(void) = 0;
    
    virtual uint64_t GetUserID(void) const = 0;
};

// =================================================================================================

class SteamInterface 
    : public BasePlatformInterface {
public:
    bool Login(void) override;
    
    void Logout(void) override;
    
    void Update(void) override;
    
    uint64_t GetUserID(void) const override;

private:
    uint64_t m_userId{ 0 };
};

// =================================================================================================

#ifdef _WIN32

class XBoxInterface
    : public BasePlatformInterface {
public:
    bool Login(void) override;

    void Logout(void) override;

    void Update(void) override;

    uint64_t GetUserID(void) const override;

    void* GetUserHandle(void) const noexcept { return m_userHandle; }

private:
    uint64_t m_xuid{ 0 };

    void* m_userHandle{ nullptr };
};

#endif

// =================================================================================================

class PlatformHandler 
    : public BaseSingleton<PlatformHandler> {
public:
    bool Init(void);

    void Shutdown(void);

    void Update(void);

    inline PlatformType GetPlatformType(void) const noexcept { 
        return m_platformType; 
    }

    inline bool HavePlatform(void) const noexcept { 
        return m_platformType != PlatformType::Unknown; 
    }

    uint64_t GetUserID(void) const;

    BasePlatformInterface* GetInterface(void) noexcept { 
        return m_activeItf; 
    }

private:
    PlatformType m_platformType{ PlatformType::Unknown };

    SteamInterface m_steamItf;
#ifdef _WIN32
    XBoxInterface m_xboxItf;
#endif
    BasePlatformInterface* m_activeItf{ nullptr };
};

#define platformHandler PlatformHandler::Instance()

// =================================================================================================
