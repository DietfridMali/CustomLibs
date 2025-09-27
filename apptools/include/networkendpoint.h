#pragma once

#include "string.hpp"
#include "SDL_net.h"

// =================================================================================================

class NetworkEndPoint {
public:
    String      m_ipAddress;
    uint16_t    m_port;
    IPAddress   m_address;

    NetworkEndPoint(String ipAddress = "127.0.0.1", uint16_t port = 9100)
        : m_ipAddress(ipAddress), m_port(port)
    {
        UpdateAddress();
    }

    void UpdateAddress(void) {
        unsigned a, b, c, d; std::sscanf(ipAddress, "%u.%u.%u.%u", &a, &b, &c, &d);
        uint32_t m_address.host = (a << 24) | (b << 16) | (c << 8) | d; // Big-Endian zusammensetzen
        m_address.port = SDL_SwapBE16(m_port);
    }

    NetworkEndPoint(const NetworkEndPoint& other) = default;

    NetworkEndPoint(NetworkEndPoint&& other) = default;

    ~NetworkEndPoint() = default;

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

    inline IPAddress& Adress(void) const noexcept {
        return m_address;
    }

    inline void Set(String ipAddress, uint16_t port = 0) noexcept {
        if (not ipAddress.IsEmpty())
            m_ipAddress = ipAddress;
        if (port > 0)
            m_port = port;
        UpdateAddress();
    }

    bool operator==(const NetworkEndPoint& other) const {
        return (m_ipAddress == other.m_ipAddress) and (m_port == other.m_port);
    }

    bool operator!=(const NetworkEndPoint& other) const {
        return not (*this == other);
    }

    NetworkEndPoint& operator=(const NetworkEndPoint& other) = default;

    NetworkEndPoint& operator=(NetworkEndPoint&& other) = default;
};

// =================================================================================================

