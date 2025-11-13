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

    NetworkEndpoint(String ipAddress = "127.0.0.1", uint16_t port = 27015)
    {
        UpdateSocketAddress(ipAddress, port);
    }

    NetworkEndpoint(const IPaddress& socketAddress) noexcept { 
        *this = socketAddress; 
    }

    NetworkEndpoint(uint32_t host, uint16_t port, ByteOrder byteOrder = ByteOrder::Host);

    NetworkEndpoint(const NetworkEndpoint& other) {
        m_ipAddress = other.m_ipAddress;
        m_port = other.m_port;
        m_socketAddress = other.m_socketAddress;
    }

    NetworkEndpoint(NetworkEndpoint&& other) noexcept {
        if (UpdateSocketAddress(other.m_ipAddress, other.m_port))
            m_ipAddress = std::move(other.m_ipAddress);
    }

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

    inline bool SetPort(uint16_t port) noexcept {
        String ipAddress("");
        return UpdateSocketAddress(ipAddress, port);
    }

    inline const IPaddress& SocketAddress(void) const noexcept {
        return m_socketAddress;
    }

    inline IPaddress& SocketAddress(void) noexcept {
        return m_socketAddress;
    }

    void Set(String ipAddress, uint16_t port = 0) noexcept;

    bool operator==(const NetworkEndpoint& other) const {
        return (m_ipAddress == other.m_ipAddress) and (m_port == other.m_port);
    }

    bool operator!=(const NetworkEndpoint& other) const {
        return not (*this == other);
    }

    NetworkEndpoint& operator=(const NetworkEndpoint& other) {
        UpdateSocketAddress(other.m_ipAddress, other.m_port);
        return *this;
    }

    NetworkEndpoint& operator=(NetworkEndpoint&& other) noexcept {
        UpdateSocketAddress(other.m_ipAddress, other.m_port);
        return *this;
    }

    NetworkEndpoint& operator=(const IPaddress& socketAddress) {
        m_socketAddress = socketAddress;
        UpdateFromSocketAddress();
        return *this;
    }

    inline uint32_t SubNet(void) noexcept {
        return m_socketAddress.host & SDL_SwapBE32(0xFFFFFF00u);
    }

    // Limited Broadcast 255.255.255.255:port
    static NetworkEndpoint LimitedBroadcast(uint16_t port) noexcept {
        return NetworkEndpoint("255.255.255.255", port);
    }

    // Subnet-Directed Broadcast x.y.z.255:port (angenommen /24)
    NetworkEndpoint DirectedBroadcast(uint16_t port) noexcept;

    void UpdateFromSocketAddress(void);

    bool UpdateSocketAddress(const String& ipAddress, uint16_t port) noexcept;

    inline void Clear(void) noexcept {
        m_socketAddress.host = 0;
        m_socketAddress.port = 0;
        UpdateFromSocketAddress();
    }

    inline bool IsEmpty(void) noexcept {
        return (m_socketAddress.host == 0) and (m_socketAddress.port == 0);
    }
};

// =================================================================================================

using AddressPair = SimpleArray<NetworkEndpoint, 2>;

// =================================================================================================
