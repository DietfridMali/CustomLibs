#pragma once 

#include <stdint.h>

#include "SDL_net.h"
#include "string.hpp"
#include "networkmessage.h"

// =================================================================================================
// UDP based networking

class UDPSocket {
    public:
        String      m_localAddress;
        uint16_t    m_localPort;
        UDPsocket   m_socket;
        UDPpacket*  m_packet;
        IPaddress   m_address;
        int         m_channel;
        bool        m_isValid;

    private:
        int Bind (String& address, uint16_t port);

        inline void Unbind(void) {
            SDLNet_UDP_Unbind(m_socket, m_channel);
        }

    public:
        UDPSocket() : m_localAddress(String("127.0.0.1")), m_localPort(0), m_packet(nullptr), m_channel(0), m_isValid(false) {
            memset(&m_address, 0, sizeof(m_address));
            memset(&m_socket, 0, sizeof(m_socket));
        }

        ~UDPSocket() {
            if (m_packet) {
                SDLNet_FreePacket(m_packet);
                m_packet = nullptr;
            }
        }

        bool Open(String localAddress, uint16_t localPort);

        void Close(void);

        bool Send(String message, String address, uint16_t port);


        String Receive(String& address, uint16_t& port);

};

// =================================================================================================

class UDP {
    public:

        String     m_localAddress;
        UDPSocket  m_sockets[2];

        UDP() : m_localAddress(String("127.0.0.1")) {}


        bool OpenSocket(uint16_t port, int type) {     // 0: read, 1: write
            return m_sockets[type].Open(m_localAddress, port);
        }


        inline uint16_t InPort(void) noexcept {
            return m_sockets[0].m_localPort;
        }

        inline uint16_t OutPort(void) noexcept {
            return m_sockets[1].m_localPort;
        }

        bool Transmit(String message, String address, uint16_t port) {
            return m_sockets[1].Send(String("SMIBAT") + message, address, port);
        }


        Message Receive(void);

};

// =================================================================================================
