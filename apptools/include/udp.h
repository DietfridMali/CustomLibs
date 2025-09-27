#pragma once 

#include <stdint.h>

#include "SDL_net.h"
#include "string.hpp"
#include "networkmessage.h"
#include "networkendpoint.h"

// =================================================================================================
// UDP based networking

class UDPSocket {
    public:
        NetworkEndpoint     m_localAddress;
        UDPsocket           m_socket;
        UDPpacket*          m_packet;
        //IPaddress           m_address;
        int                 m_channel;
        bool                m_isValid;

    private:
        int Bind (NetworkEndpoint receiver);

        inline void Unbind(void) {
            SDLNet_UDP_Unbind(m_socket, m_channel);
        }

    public:
        UDPSocket() 
            : m_localAddress(String("127.0.0.1", 0))
            , m_packet(nullptr)
            , m_channel(0)
            , m_isValid(false)
        {
            memset(&m_socket, 0, sizeof(m_socket));
        }

        ~UDPSocket() {
            if (m_packet) {
                SDLNet_FreePacket(m_packet);
                m_packet = nullptr;
            }
        }

        bool Open(NetworkEndpoint& localAddress);

        void Close(void);

        bool Send(NetworkEndpoint& receiver, String message);


        String Receive(NetworkEndpoint& sender);

};

// =================================================================================================

class UDP {
    public:

        NetworkEndpoint m_localAddress;
        UDPSocket       m_sockets[2];

        UDP() 
            : m_localAddress("127.0.0.1", 0) 
        { }


        bool OpenSocket(int type) {     // 0: read, 1: write
            return m_sockets[type].Open(m_localAddress.IpAddress(), m_localAddress.Port() + type); // 0: in port, 1: out port
        }


        inline uint16_t InPort(void) noexcept {
            return m_localAddress.InPort();
        }

        inline uint16_t OutPort(void) noexcept {
            return m_localAddress.OutPort();
        }

        bool Transmit(String message, NetworkEndpoint& address) {
            return m_sockets[1].Send(address, String("SMIBAT") + message);
        }


        Message& Receive(Message& message);

};

// =================================================================================================
