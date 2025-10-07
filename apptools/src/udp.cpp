
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
    if (not (m_socket = SDLNet_UDP_Open(port)))
        return false;
    m_packet = SDLNet_AllocPacket(MaxPacketSize);
    return m_packet != nullptr;
}


void UDPSocket::Close(void) {
    if (m_socket) {
        SDLNet_FreePacket(m_packet);
        SDLNet_UDP_Close(m_socket);
        m_socket = nullptr;
    }
}


bool UDPSocket::Bind(void) {
    if (SDLNet_ResolveHost(&m_socketAddress, (char*)m_ipAddress, m_port))
        return false;
    m_channel = SDLNet_UDP_Bind(m_socket, -1, &m_socketAddress);
    if (0 > m_channel)
        return false;
    return true;
}


void UDPSocket::Unbind(void) {
    if (m_socket and (m_channel >= 0)) {
        SDLNet_UDP_Unbind(m_socket, m_channel);
        m_channel = -1;
    }
}


bool UDPSocket::Send(const uint8_t* data, int dataLen, NetworkEndpoint& receiver) {
    if (not m_socket)
        return false;
#if 1
    m_packet->channel = -1;
    m_packet->len = dataLen;
    m_packet->maxlen = MaxPacketSize;
    std::memcpy(m_packet->data, data, dataLen);
    m_packet->address = receiver.SocketAddress();
    return SDLNet_UDP_Send(m_socket, -1, m_packet) > 0;
#else
    int l = int(message.Length());
    UDPpacket packet = { -1, reinterpret_cast<Uint8*>(message.Data()), l, MaxPacketSize, 0, receiver.SocketAddress() };
    return SDLNet_UDP_Send(m_socket, -1, &packet) > 0;
#endif
}


UDPData UDPSocket::Receive(int minLength) { // return sender address in message.Address()
    if (not (m_socket and m_packet))
        return { nullptr, 0 };
    int n = SDLNet_UDP_Recv(m_socket, m_packet);
    if ((n <= 0) or (m_packet->len > MaxPacketSize))
        return { nullptr, 0 };
    if (SocketAddress().host == m_packet->address.host)
        return { nullptr, 0 };
    if ((minLength > 0) and (m_packet->len < minLength))
        return { nullptr, 0 };
    return { m_packet->data, m_packet->len };
}


bool UDPSocket::Receive(NetworkMessage& message, int portOffset) { // return sender address in message.Address()
    UDPData data = Receive();
    if (not data.length)
        return false;
    message.Address() = m_packet->address;
    message.SetPort(uint16_t(int(message.GetPort()) + portOffset));
    message.Payload() = String((const char*)m_packet->data, m_packet->len);
    return true;
}


bool UDPSocket::SendBroadcast(const uint8_t* data, int dataLen, uint16_t destPort, bool subnetOnly) {
    // Empfänger bestimmen
    NetworkEndpoint destination = subnetOnly ? DirectedBroadcast(destPort) : NetworkEndpoint::LimitedBroadcast(destPort);
    // Hinweis: SDL_net setzt i.d.R. SO_BROADCAST beim UDP-Open. Falls eine Plattform das nicht tut,
    // wäre ein Raw-Socket-Workaround nötig. In der Praxis funktioniert 255.255.255.255 & x.y.z.255 mit SDL_net.
    return Send(data, dataLen, destination);
}

// =================================================================================================
