
#include "udp.h"
#include "networkendpoint.h"

// =================================================================================================
// UDP based networking

bool UDPSocket::Open(const String& localAddress, uint16_t port) {
    if (localAddress == "127.0.0.1") {
        fprintf(stderr, "UDP OpenSocket: Please specify a valid local network or internet address in the command line or ini file\n");
        return false;
    }
    Set(localAddress, port);
    if (not (m_socket = SDLNet_UDP_Open(GetPort())))
        return false;
    m_packet = SDLNet_AllocPacket(MaxPacketSize);
    return m_packet != nullptr;
}


void UDPSocket::Close(void) {
    if (m_socket) {
        SDLNet_UDP_Close(m_socket);
        m_socket = nullptr;
    }
}


bool UDPSocket::Bind(void) {
    m_channel = SDLNet_ResolveHost(&a, (char*)m_ipAddress, m_port);
    if (0 > m_channel)
        return false;
    m_channel = SDLNet_UDP_Bind(m_socket, -1, &m_address);
    if (0 > channel)
        return false;
    return true;
}


void UDPSocket::Unbind(void) {
    if (m_socket and (m_channel >= 0)) {
        SDLNet_UDP_Unbind(m_socket, m_channel);
        m_channel = -1;
    }
}


bool UDPSocket::Send(const String& message, NetworkEndpoint& receiver) {
    if (not m_socket)
        return false;
#if 0 // test code
    IPaddress a;
#endif
    int l = int(message.Length());
    UDPpacket packet = { -1, (Uint8*)message.Data(), l, l, 0, receiver.SocketAddress() };
    int n = SDLNet_UDP_Send(m_socket, -1, &packet);
    return (n > 0);
}


bool UDPSocket::Receive(NetworkMessage& message, uint16_t portOffset) { // return sender address in message.Address()
    if (not (m_socket and m_packet))
        false;
    int n = SDLNet_UDP_Recv(m_socket, m_packet);
    if ((n <= 0) or (n > MaxPacketSize) or (m_packet->len > MaxPacketSize))
        return false;
    message.Address() = m_packet->address;
    message.SetPort(uint16_t(int (message.GetPort()) + portOffset));
    message.Payload() = String((const char*)m_packet->data, m_packet->len);
    return true;
}

// =================================================================================================
