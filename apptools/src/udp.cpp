
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


bool UDPSocket::Send(const String& message, NetworkEndpoint& receiver) {
    if (not m_socket)
        return false;
    int l = int(message.Length());
    UDPpacket packet = { -1, (Uint8*)message.Data(), l, l, 0, receiver.SocketAddress() };
    int n = SDLNet_UDP_Send(m_socket, -1, &packet);
    return (n > 0);
}


bool UDPSocket::Receive(NetworkMessage& message) { // return sender address in message.Address()
    if (not (m_socket and m_packet))
        false;
    int n = SDLNet_UDP_Recv(m_socket, m_packet);
    if ((n <= 0) or (n > MaxPacketSize))
        false;
    uint8_t* p = reinterpret_cast<uint8_t*>(&m_packet->address.host);
    String s;
    s.Format("{}.{}.{}.{}", p[0], p[1], p[2], p[3]);
    message.Address().Set(s, uint16_t(m_packet->address.port - 1)); // the port is the sender's out port; we need the in port which by convention of this app is out port - 1
    message.Payload() = String((const char*)m_packet->data, m_packet->len);
    return true;
}

// =================================================================================================
