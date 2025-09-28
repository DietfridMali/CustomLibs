#pragma once

#include "string.hpp"
#include "array.hpp"
#include "SDL_net.h"

// =================================================================================================

enum class ByteOrder { Host, Network };

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

    NetworkEndpoint(const IPaddress& socketAddress) { 
        *this = socketAddress; 
    }

    void UpdateFromSocketAddress(void) {
        uint32_t host = SDL_SwapBE32(m_socketAddress.host);
        m_port = SDL_SwapBE16(m_socketAddress.port);
        uint8_t* p = reinterpret_cast<uint8_t*>(&host);
        m_ipAddress.Format("{}.{}.{}.{}", p[0], p[1], p[2], p[3]);
    }

    NetworkEndpoint(uint32_t host, uint16_t port, ByteOrder byteOrder = ByteOrder::Host) {
        if (byteOrder == ByteOrder::Host) {
            m_socketAddress.host = SDL_SwapBE32(host);
            m_socketAddress.port = SDL_SwapBE16(port);
        }
        else {
            m_socketAddress.host = host;
            m_socketAddress.port = port;
        }
        UpdateFromSocketAddress();
    }

    void UpdateSocketAddress(void) {
        ManagedArray<String> fields = m_ipAddress.Split('.');
        unsigned fieldValues[4] = { 0,0,0,0 };
        int i = 0, l = fields.Length();
        for (; i < l; ++i)
            fieldValues[i] = fields[i].IsEmpty() ? 0 : uint16_t(fields[i]);
        uint32_t host = (fieldValues[0] << 24) | (fieldValues[1] << 16) | (fieldValues[2] << 8) | fieldValues[3];
        m_socketAddress.host = SDL_SwapBE32(host);
        m_socketAddress.port = SDL_SwapBE16(m_port);
    }

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

    NetworkEndpoint& operator=(const IPaddress& const socketAddress) {
        m_socketAddress = socketAddress;
        return *this;
    }
};

// =================================================================================================

