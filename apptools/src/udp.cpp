
#include "std_defines.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "udp.h"
#include "networkendpoint.h"
#include "loghandler.h"

// =================================================================================================
// UDP based networking

static bool ParseIPv4(const char* address, uint32_t& host)
{
	unsigned	a, b, c, d;
	char		tail;
	if (not address or (SDL_sscanf(address, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) or (a > 255) or (b > 255) or (c > 255) or (d > 255))
		return false;
	host = SDL_Swap32BE((a << 24) | (b << 16) | (c << 8) | d);
	return true;
}


bool UDPSocket::ResolveIPv4(const char* hostName, uint32_t& host)
{
	addrinfo	hints{};
	addrinfo*	result = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if ((getaddrinfo(hostName, nullptr, &hints, &result) != 0) or not result)
		return false;
	host = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(result);
	return true;
}


bool UDPSocket::Open(const String& localAddress, uint16_t port)
{
#ifdef _DEBUG
	if (localAddress == "127.0.0.1") {
		logHandler.Print("UDP OpenSocket: Please specify a valid local network or internet address in the command line or ini file");
		return false;
	}
#endif
	UpdateSocketAddress(localAddress, port);
	SDL_PropertiesID properties = SDL_CreateProperties();
	SDL_SetBooleanProperty(properties, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, true);
	SDL_SetBooleanProperty(properties, NET_PROP_DATAGRAM_SOCKET_REUSEADDR_BOOLEAN, false);
	m_socket = NET_CreateDatagramSocket(nullptr, port, properties);
	SDL_DestroyProperties(properties);
	return m_socket != nullptr;
}


void UDPSocket::Close(bool destroy)
{
	if (destroy) {
		for (auto& peer : m_peerAddresses)
			NET_UnrefAddress(peer.second);
		m_peerAddresses.Destroy();
	}
	if (m_socket) {
		NET_DestroyDatagramSocket(m_socket);
		m_socket = nullptr;
	}
}


bool UDPSocket::Bind(void)
{
	uint32_t host;
	if (not ResolveIPv4((const char*)m_ipAddress, host))
		return false;
	NetworkEndpoint::operator=(IPv4Address{ .host = host, .port = SDL_Swap16BE(m_port) });
	return true;
}


NET_Address* UDPSocket::PeerAddress(const NetworkEndpoint& receiver)
{
	uint32_t		host = receiver.SocketAddress().host;
	NET_Address**	cachedAddress = m_peerAddresses.Find(host);
	if (cachedAddress)
		return *cachedAddress;
	NET_Address* address = NET_ResolveHostname((const char*)receiver.IpAddress());
	if (not address)
		return nullptr;
	if (NET_WaitUntilResolved(address, -1) != NET_SUCCESS) {
		NET_UnrefAddress(address);
		return nullptr;
	}
	m_peerAddresses.Insert(host, address);
	return address;
}


bool UDPSocket::Send(const uint8_t* data, int dataLen, const NetworkEndpoint& receiver)
{
	if (not m_socket)
		return false;
	if ((dataLen < 0) or (dataLen > MaxPacketSize))
		return false;
	NET_Address* address = PeerAddress(receiver);
	if (not address)
		return false;
	return NET_SendDatagram(m_socket, address, receiver.GetPort(), data, dataLen);
}


UDPData UDPSocket::Receive(int minLength)
{ // return sender address in message.Address()
	if (not m_socket)
		return { nullptr, 0 };
	NET_Datagram* datagram = nullptr;
	if (not NET_ReceiveDatagram(m_socket, &datagram) or not datagram)
		return { nullptr, 0 };
	uint32_t	host = 0;
	uint16_t	port = SDL_Swap16BE(datagram->port);
	bool		isValid = (datagram->buflen <= MaxPacketSize) and ParseIPv4(NET_GetAddressString(datagram->addr), host);
	if (isValid and (SocketAddress().host == host) and (SocketAddress().port == port))
		isValid = false;
	if (isValid and (minLength > 0) and (datagram->buflen < minLength))
		isValid = false;
	if (isValid) {
		if (not m_peerAddresses.Find(host))
			m_peerAddresses.Insert(host, NET_RefAddress(datagram->addr));
		std::memcpy(m_data, datagram->buf, datagram->buflen);
		m_dataLength = datagram->buflen;
		m_sender = IPv4Address{ .host = host, .port = port };
	}
	NET_DestroyDatagram(datagram);
	return isValid ? UDPData{ m_data, m_dataLength } : UDPData{ nullptr, 0 };
}


bool UDPSocket::Receive(NetworkMessage& message)
{ // return sender address in message.Address()
	UDPData data = Receive();
	if (not data.length)
		return false;
	message.Address() = m_sender;
	message.Payload() = String((const char*)m_data, m_dataLength);
	return true;
}


bool UDPSocket::SendBroadcast(const uint8_t* data, int dataLen, uint16_t destPort, bool subnetOnly)
{
	// Empfänger bestimmen
	NetworkEndpoint destination = subnetOnly ? DirectedBroadcast(destPort) : NetworkEndpoint::LimitedBroadcast(destPort);
	// Hinweis: SDL_net setzt i.d.R. SO_BROADCAST beim UDP-Open. Falls eine Plattform das nicht tut,
	// wäre ein Raw-Socket-Workaround nötig. In der Praxis funktioniert 255.255.255.255 & x.y.z.255 mit SDL_net.
	return Send(data, dataLen, destination);
}

// =================================================================================================
