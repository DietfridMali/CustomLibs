
#include "udp.h"
#include "networkendpoint.h"

// =================================================================================================
// UDP based networking

bool UDPSocket::Open(String localAddress, uint16_t localPort) {
    m_localAddress = localAddress;
    m_localPort = localPort;
    if (localAddress == "127.0.0.1") {
        fprintf(stderr, "UDP OpenSocket: Please specify a valid local network or internet address in the command line or ini file\n");
        return false;
    }
    if (not (m_socket = SDLNet_UDP_Open(localPort)))
        return false;
    m_packet = SDLNet_AllocPacket(1500);
    return m_isValid = true;
}


void UDPSocket::Close(void) {
    if (m_isValid) {
        m_isValid = false;
        // Unbind ();
        SDLNet_UDP_Close(m_socket);
    }
}


int UDPSocket::Bind (NetworkEndPoint receiver) {
#if 0
    if (0 > (m_channel = SDLNet_ResolveHost (&m_address, (char*) address, port)))
        fprintf (stderr, "Failed to resolve host '%s:%d'\n", (char*) address, port);
    else
#endif
    if (0 > (m_channel = SDLNet_UDP_Bind (m_socket, -1, &receiver.Address())))
        fprintf (stderr, "Failed to bind '%s:%d' to UDP socket\n", (char*) address.IpAddress(), address.Port());
    else 
        return 0;
    return m_channel;
}



bool UDPSocket::Send(NetworkEndPoint& receiver, String message) {
    if (not m_isValid)
        return false;
    UDPpacket packet = { Bind(receiver), (Uint8*)message.Data(), int(message.Length()), int(message.Length()), 0, m_address};
    if (m_channel < 0)
        return false;
    int n = SDLNet_UDP_Send(m_socket, m_channel, &packet);
    Unbind ();
    return (n > 0);
}


String UDPSocket::Receive(NetworkEndPoint& sender) {
    if (not m_isValid)
        return String ("");
    if (0 > (m_packet->channel = Bind(m_localAddress)))
        return String("");
    int n = SDLNet_UDP_Recv(m_socket, m_packet);
    Unbind();
    if (n <= 0)
        return String("");
    uint8_t* p = (uint8_t*)&m_packet->address.host;
    char s[16];
    sprintf_s(s, sizeof (s), "%hu.%hu.%hu.%hu", p[0], p[1], p[2], p[3]);
    sender.Set(s, uint16_t(m_packet->address.port));
    return String((const char*)m_packet->data, m_packet->len);
}


Message& UDP::Receive(Message& message) {
    message.m_payload = m_sockets[0].Receive(message.address);
    if (message.m_payload.Find("SMIBAT") == 0)
        message.m_payload = message.m_payload.Replace("SMIBAT", "", 1);
    return message;
}


// =================================================================================================
