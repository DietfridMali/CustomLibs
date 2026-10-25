#pragma once

#include <stdint.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26819)
#endif
#include "SDL3_net/SDL_net.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "string.hpp"
#include "dictionary.hpp"
#include "networkmessage.h"
#include "networkendpoint.h"

// =================================================================================================
// UDP based networking

struct UDPData {
	uint8_t*	buffer;
	int			length;
};

class UDPSocket
	: public NetworkEndpoint {
public:
	static constexpr int MaxPacketSize = 1500;

	NET_DatagramSocket*					m_socket;
	Dictionary<uint32_t, NET_Address*>	m_peerAddresses;
	IPv4Address							m_sender;
	int									m_dataLength;
	uint8_t								m_data[MaxPacketSize];

public:
	UDPSocket()
		: NetworkEndpoint()
		, m_socket(nullptr)
		, m_dataLength(0)
	{}

	~UDPSocket() {
		Close(true);
	}

	static bool ResolveIPv4(const char* hostName, uint32_t& host);

	bool Open(const String& localAddress, uint16_t port);

	bool Bind(void);

	void Close(bool destroy = false);

	bool Send(const uint8_t* data, int dataLen, const NetworkEndpoint& receiver);

	inline bool Send(String& message, NetworkEndpoint& receiver) {
		return Send(reinterpret_cast<const uint8_t*>(message.Data()), int(message.Length()), receiver);
	}

	inline bool Send(NetworkMessage& message) {
		return Send(message.Payload(), message.Address());
	}

	UDPData Receive(int minLength = 0);

	bool Receive(NetworkMessage& message);

	bool SendBroadcast(const uint8_t* data, int dataLen, uint16_t destPort, bool subnetOnly);

	inline bool SendBroadcast(const String& msg, uint16_t destPort, bool subnetOnly = true) {
		return SendBroadcast(reinterpret_cast<const uint8_t*>(msg.Data()), int(msg.Length()), destPort, subnetOnly);
	}

private:
	NET_Address* PeerAddress(const NetworkEndpoint& receiver);
};

// =================================================================================================
