#pragma once 

#include <stdint.h>

#include "SDL_net.h"
#include "string.hpp"
#include "networkmessage.h"
#include "networkendpoint.h"

// =================================================================================================
// UDP based networking

class UDPSocket
    : public NetworkEndpoint
{
public:
    UDPsocket           m_socket; // is a pointer type!
    UDPpacket* m_packet;
    IPaddress           m_address;
    int                 m_channel;

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

    bool Send(const String& message, NetworkEndpoint& receiver);

    bool Receive(NetworkMessage& message, int portOffset = -1);
};

// =================================================================================================
