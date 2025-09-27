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
        UDPpacket*          m_packet;

        static constexpr int MaxPacketSize = 1500;

    public:
        UDPSocket() 
            : NetworkEndpoint()
            , m_packet(nullptr)
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

        void Close(void);

        bool Send(const String& message, NetworkEndpoint& receiver);


        bool Receive(NetworkMessage& message);

};

// =================================================================================================
