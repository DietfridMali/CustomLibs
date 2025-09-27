#pragma once

#include "string.hpp"
#include "array.hpp"
#include "SDL_net.h"

// =================================================================================================

class NetworkEndpoint 
{
public:
    String      m_ipAddress;
    uint16_t    m_port;
    IPaddress   m_socketAddress;

    NetworkEndpoint(String ipAddress = "127.0.0.1", uint16_t port = 9100)
        : m_ipAddress(ipAddress), m_port(port)
    {
        UpdateSocketAddress();
    }

    void UpdateSocketAddress(void) {
        ManagedArray<String> fields = m_ipAddress.Split('.');
        unsigned fieldValues[4];
        int i = 0, l = fields.Length();
        for (; i < l; ++i)
            fieldValues[i] = uint16_t(fields[i]);
        for (; i < 4; ++i)
            fieldValues[i] = 0;
        m_socketAddress.host = (fieldValues[0] << 24) | (fieldValues[1] << 16) | (fieldValues[2] << 8) | fieldValues[3];
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

    inline const IPaddress& SocketAddress(void) const noexcept {
        return m_socketAddress;
    }

    inline void Set(String ipAddress, uint16_t port = 0) noexcept {
        if (not ipAddress.IsEmpty())
            m_ipAddress = ipAddress;
        if (port > 0)
            m_port = port;
        UpdateSocketAddress();
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

