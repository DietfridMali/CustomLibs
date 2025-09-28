#pragma once

#include "string.hpp"
#include "array.hpp"
#include "SDL_net.h"

// =================================================================================================

enum class ByteOrder { Host, Network };

class NetworkEndpoint 
{
public:
    String      m_ipAddress{ "" };
    uint16_t    m_port{ 0 };
    IPaddress   m_socketAddress{};

    NetworkEndpoint(String ipAddress = "127.0.0.1", uint16_t port = 9100)
        : m_ipAddress(ipAddress), m_port(port)
    {
        UpdateSocketAddress();
    }

    NetworkEndpoint(const IPaddress& socketAddress) noexcept { 
        *this = socketAddress; 
    }

    NetworkEndpoint(uint32_t host, uint16_t port, ByteOrder byteOrder = ByteOrder::Host);

    NetworkEndpoint(const NetworkEndpoint& other) = default;

    NetworkEndpoint(NetworkEndpoint&& other) = default;

    ~NetworkEndpoint() = default;

    inline String GetIpAddress(void) noexcept {
        return m_ipAddress;
    }

    inline const String& IpAddress(void) const noexcept {
        return m_ipAddress;
    }

    inline uint16_t GetPort(void) const noexcept {
        return m_port;
    }

    inline void SetPort(uint16_t port) noexcept {
        m_port = port;
        UpdateSocketAddress();
    }

    inline const IPaddress& SocketAddress(void) const noexcept {
        return m_socketAddress;
    }

    inline IPaddress& SocketAddress(void) noexcept {
        return m_socketAddress;
    }

    inline void Set(String ipAddress, uint16_t port = 0) noexcept;

    bool operator==(const NetworkEndpoint& other) const {
        return (m_ipAddress == other.m_ipAddress) and (m_port == other.m_port);
    }

    bool operator!=(const NetworkEndpoint& other) const {
        return not (*this == other);
    }

    NetworkEndpoint& operator=(const NetworkEndpoint& other) {
        m_ipAddress = other.m_ipAddress;
        m_port = other.m_port;
        UpdateSocketAddress();
        return *this;
    }

    NetworkEndpoint& operator=(NetworkEndpoint&& other) noexcept {
        m_ipAddress = other.m_ipAddress;
        m_port = other.m_port;
        UpdateSocketAddress();
        return *this;
    }

    NetworkEndpoint& operator=(const IPaddress& socketAddress) {
        m_socketAddress = socketAddress;
        UpdateFromSocketAddress();
        return *this;
    }

private:
    void UpdateFromSocketAddress(void);

    void UpdateSocketAddress(void);
};

// =================================================================================================

