
#include "networkendpoint.h"

// =================================================================================================

void NetworkEndpoint::UpdateFromSocketAddress(void) {
    uint32_t host = SDL_SwapBE32(m_socketAddress.host);
    m_port = SDL_SwapBE16(m_socketAddress.port);
    uint8_t* p = reinterpret_cast<uint8_t*>(&host);
    m_ipAddress.Format("{:d}.{:d}.{:d}.{:d}", (unsigned) ((host >> 24) & 0xFF), (unsigned)((host >> 16) & 0xFF), (unsigned)((host >> 8) & 0xFF), (unsigned)(host & 0xFF));
}


NetworkEndpoint::NetworkEndpoint(uint32_t host, uint16_t port, ByteOrder byteOrder) {
    if (byteOrder == ByteOrder::Host) {
        m_socketAddress.host = SDL_SwapBE32(host);
        m_socketAddress.port = SDL_SwapBE16(port);
    }
    else {
        m_socketAddress.host = host;
        m_socketAddress.port = port;
    }
    UpdateFromSocketAddress();
}


void NetworkEndpoint::UpdateSocketAddress(void) {
    ManagedArray<String> fields = m_ipAddress.Split('.');
    unsigned fieldValues[4] = { 0,0,0,0 };
    int i = 0, l = fields.Length();
    for (; i < l; ++i)
        fieldValues[i] = fields[i].IsEmpty() ? 0 : uint16_t(fields[i]);
    uint32_t host = (fieldValues[0] << 24) | (fieldValues[1] << 16) | (fieldValues[2] << 8) | fieldValues[3];
    m_socketAddress.host = SDL_SwapBE32(host);
    m_socketAddress.port = SDL_SwapBE16(m_port);
}


void NetworkEndpoint::Set(String ipAddress, uint16_t port) noexcept {
    if (not ipAddress.IsEmpty())
        m_ipAddress = ipAddress;
    if (port > 0)
        m_port = port;
    UpdateSocketAddress();
}


NetworkEndpoint NetworkEndpoint::DirectedBroadcast(uint16_t port) noexcept {
    // nutzt vorhandene IP-String-Repräsentation
    ManagedArray<String> f = m_ipAddress.Split('.');
    if (f.Length() != 4) 
        return LimitedBroadcast(port);
    //String ipAddress;
    //ipAddress.Format("{}.{}.{}.255", (char*)(f[0]), (char*)(f[1]), (char*)(f[2]));
    return NetworkEndpoint(String::Concat(f[0], ".", f[1], ".", f[2], ".255"), port);
}

// =================================================================================================

