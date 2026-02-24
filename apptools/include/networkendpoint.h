#pragma once

#include "string.hpp"
#include "array.hpp"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_net.h"
#pragma warning(pop)

// =================================================================================================

enum class ByteOrder { Host, Network };

#pragma warning(push)
#pragma warning(disable:4201)
union NetworkID {
        uint64_t    id{ 0 };
    struct {
        uint32_t    host;
        uint16_t    port;
    };
};
#pragma warning(pop)

typedef enum {
	ntIPv4,
    ntSteam
} eNetworkType;

class NetworkEndpoint {
public:
	NetworkID       m_id{ 0 };
    eNetworkType    m_type{ ntIPv4 };
    String          m_ipAddress{ "" };
    uint16_t        m_port{ 0 };
    IPaddress       m_socketAddress{};

    bool UpdateSocketAddress(const String& ipAddress, int32_t port) noexcept;

    void UpdateFromSocketAddress(void);

    NetworkEndpoint(String ipAddress = "0.0.0.0", uint16_t port = 0) {
        UpdateSocketAddress(ipAddress, port);
    }

    NetworkEndpoint(const IPaddress& socketAddress) noexcept { 
        m_socketAddress = socketAddress;
        UpdateFromSocketAddress();
    }

    NetworkEndpoint(uint32_t host, uint16_t port, ByteOrder byteOrder = ByteOrder::Host);

    NetworkEndpoint(const NetworkEndpoint& other) {
		*this = other;
    }

    NetworkEndpoint(NetworkEndpoint&& other) noexcept {
        if (UpdateSocketAddress(other.m_ipAddress, other.m_port)) {
			m_id = std::move(other.m_id);
            m_ipAddress = std::move(other.m_ipAddress);
        }
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

    inline bool SetPort(int32_t port) noexcept {
        String ipAddress("");
        return UpdateSocketAddress(ipAddress, port);
    }

    inline uint64_t& NetworkID(void) noexcept {
        return m_id.id;
	}

    inline uint64_t GetNetworkID(void) noexcept {
        return m_id.id;
    }

    inline void SetNetworkID(uint64_t id) noexcept {
        m_id.id = id;
    }

    inline eNetworkType GetType(void) const noexcept {
        return m_type;
	}

    inline void SetType(eNetworkType type) noexcept {
        m_type = type;
	}

    void UpdateNetworkID(uint64_t networkID, eNetworkType networkType = ntSteam) noexcept;

    inline const IPaddress& SocketAddress(void) const noexcept {
        return m_socketAddress;
    }

    inline IPaddress& SocketAddress(void) noexcept {
        return m_socketAddress;
    }

    bool operator==(const NetworkEndpoint& other) const {
        if (this == &other)
            return true;
        if (m_type != other.m_type)
            return false;
        return (m_type == ntIPv4) ? (m_ipAddress == other.m_ipAddress) and (m_port == other.m_port) : (m_id.id == other.m_id.id);
    }

    bool operator!=(const NetworkEndpoint& other) const {
        return not (*this == other);
    }

    NetworkEndpoint& operator=(const NetworkEndpoint& other) {
        m_id = other.m_id;
        m_ipAddress = other.m_ipAddress;
        m_port = other.m_port;
        m_socketAddress = other.m_socketAddress;
		m_type = other.m_type;
        return *this;
    }

    NetworkEndpoint& operator=(NetworkEndpoint&& other) noexcept {
		*this = static_cast<const NetworkEndpoint&>(other);
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

    inline void Clear(void) noexcept {
		m_id.id = 0;
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
