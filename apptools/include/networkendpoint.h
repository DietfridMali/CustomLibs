#pragma once

#include "string.hpp"
#include "SDL_net.h"

// =================================================================================================

class NetworkEndpoint {
public:
    String      m_ipAddress;
    uint16_t    m_port;
    IPaddress   m_socketAddress;

    NetworkEndpoint(String ipAddress = "127.0.0.1", uint16_t port = 9100)
        : m_ipAddress(ipAddress), m_port(port)
    {
        UpdateAddress();
    }

    void UpdateAddress(void) {
        unsigned a, b, c, d; std::sscanf(m_ipAddress, "%u.%u.%u.%u", &a, &b, &c, &d);
        m_socketAddress.host = (a << 24) | (b << 16) | (c << 8) | d; // Big-Endian zusammensetzen
        m_socketAddress.port = SDL_SwapBE16(m_port);
    }

    NetworkEndpoint(const NetworkEndpoint& other) = default;

    NetworkEndpoint(NetworkEndpoint&& other) = default;

    ~NetworkEndpoint() = default;

    inline String& IpAddress(void) noexcept {
        return m_ipAddress;
    }

    inline const String& IpAddress(void) const noexcept {
        return m_ipAddress;
    }

    inline uint16_t Port(void) const noexcept {
        return m_port;
    }

    inline void SetPort(uint16_t port) noexcept {
        m_port = port;
    }

    inline uint16_t InPort(void) const noexcept {
        return m_port;
    }

    inline uint16_t OutPort(void) const noexcept {
        return m_port + 1;
    }

    inline const IPaddress& SocketAdress(void) const noexcept {
        return m_socketAddress;
    }

    inline void Set(String ipAddress, uint16_t port = 0) noexcept {
        if (not ipAddress.IsEmpty())
            m_ipAddress = ipAddress;
        if (port > 0)
            m_port = port;
        UpdateAddress();
    }

    bool operator==(const NetworkEndpoint& other) const {
        return (m_ipAddress == other.m_ipAddress) and (m_port == other.m_port);
    }

    bool operator!=(const NetworkEndpoint& other) const {
        return not (*this == other);
    }

    NetworkEndpoint& operator=(const NetworkEndpoint& other) = default;

    NetworkEndpoint& operator=(NetworkEndpoint&& other) = default;
};

// =================================================================================================

