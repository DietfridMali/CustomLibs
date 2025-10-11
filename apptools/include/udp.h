#pragma once 

#include <stdint.h>

#include "SDL_net.h"
#include "string.hpp"
#include "networkmessage.h"
#include "networkendpoint.h"

// =================================================================================================
// UDP based networking

struct UDPData {
    uint8_t*    buffer;
    int         length;
};

class UDPSocket
    : public NetworkEndpoint
{
public:
    UDPsocket   m_socket; // is a pointer type!
    UDPpacket*  m_packet;
    IPaddress   m_address;
    int         m_channel;

    static constexpr int MaxPacketSize = 1500;

public:
    UDPSocket()
        : NetworkEndpoint()
        , m_packet(nullptr)
        , m_channel(-1)
    {
        memset(&m_socket, 0, sizeof(m_socket));
    }

    ~UDPSocket() {
        if (m_packet) {
            SDLNet_FreePacket(m_packet);
            m_packet = nullptr;
        }
    }

    bool Open(const String& localAddress, uint16_t port);

    bool Bind(void);

    void Unbind(void);

    void Close(void);

    bool Send(const uint8_t* data, int dataLen, const NetworkEndpoint& receiver);

    inline bool Send(const String& message, NetworkEndpoint& receiver) {
        return Send(reinterpret_cast<const uint8_t*>(message.Data()), int(message.Length()), receiver);
    }

    inline bool Send(NetworkMessage& const message) {
        return Send(message.Payload(), message.Address());
    }

    UDPData Receive(int minLength = 0);

    bool Receive(NetworkMessage& message);

    bool SendBroadcast(const uint8_t* data, int dataLen, uint16_t destPort, bool subnetOnly);

    inline bool SendBroadcast(const String& msg, uint16_t destPort, bool subnetOnly = true) {
        return SendBroadcast(reinterpret_cast<const uint8_t*>(msg.Data()), int(msg.Length()), destPort, subnetOnly);
    }

};

// =================================================================================================
