
#include "stunclient.h"

// =================================================================================================

// Hilfsfunktionen für Netz-Byteorder ohne Alignment-Probleme
static inline uint16_t be16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | uint16_t(p[1]); }
static inline uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

std::optional<NetworkEndpoint> StunClient::StunQueryIPv4(const char* serverHost, uint16_t serverPort, uint32_t timeoutMs) {
    constexpr uint32_t MAGIC = 0x2112A442u;

    // STUN-Server über SDL_net auflösen, um einen sendefähigen Endpoint zu haben
    IPaddress socketAddress;
    if (SDLNet_ResolveHost(&socketAddress, serverHost, serverPort) != 0)
        return std::nullopt;
    NetworkEndpoint serverAddress = socketAddress;

    UDPSocket sock;
    if (!sock.Open("0.0.0.0", 0))
        return std::nullopt;

    // 20-Byte Binding Request
    uint8_t request[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };

    const uint32_t ticks = SDL_GetTicks();
    for (int i = 0; i < 12; ++i)
        request[8 + i] = uint8_t(ticks >> ((i % 4) * 8));

    if (!sock.Send(request, sizeof(request), serverAddress)) {
        sock.Close();
        return std::nullopt;
    }

    const uint32_t start = SDL_GetTicks();
    UDPData data;

    while ((SDL_GetTicks() - start) < timeoutMs) {
        data = sock.Receive(20);
        if (data.length) {
            const uint16_t msgType = SDLNet_Read16(data.buffer + 0);   // host-order
            const uint16_t msgLen = SDLNet_Read16(data.buffer + 2);
            const uint32_t cookie = SDLNet_Read32(data.buffer + 4);
            if (msgType != 0x0101 || cookie != MAGIC) // Binding Success
                continue;
            if (20 + msgLen > data.length)
                continue;
            if (std::memcmp(data.buffer + 8, request + 8, 12) != 0) // Transaction-ID
                continue;

            size_t pos = 20;
            while (pos + 4 <= size_t(20 + msgLen)) {
                const uint16_t at = SDLNet_Read16(data.buffer + pos + 0);
                const uint16_t al = SDLNet_Read16(data.buffer + pos + 2);
                pos += 4;
                if (pos + al > size_t(20 + msgLen))
                    break;

                if (at == 0x0020) { // XOR-MAPPED-ADDRESS
                    if (al >= 8 && data.buffer[pos + 1] == 0x01) { // IPv4
                        uint16_t port = SDLNet_Read16(data.buffer + pos + 2);
                        port ^= uint16_t(MAGIC >> 16);
                        uint32_t addr = SDLNet_Read32(data.buffer + pos + 4);
                        addr ^= MAGIC;
                        NetworkEndpoint result(addr, port, ByteOrder::Host);
                        sock.Close();
                        return result;
                    }
                }
                pos += al + ((4 - (al % 4)) % 4); // 32-bit Padding
            }
        }
        SDL_Delay(1);
    }
    sock.Close();
    return std::nullopt;
}

// =================================================================================================

#ifdef _WIN32

#include <string>
#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

String GetLocalAddress(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return String("");

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
        return String("");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return String("");
    }

    sockaddr_in local{};
    int len = sizeof(local);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return String("");
    }

    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));

    closesocket(sock);
    WSACleanup();
    return String(buf);
}

#else

#include <string>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

String GetLocalAddress() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return String("");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return String("");
    }

    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) < 0) {
        close(sock);
        return String("");
    }
    close(sock);

    char buf[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)))
        return String("");
    return String(buf);
}

#endif

// =================================================================================================
