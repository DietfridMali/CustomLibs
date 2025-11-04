
#include "timer.h"
#include "internetservices.h"
#include <cstdint>
#include <cstring>
#include <ctime>

// =================================================================================================

// Hilfsfunktionen für Netz-Byteorder ohne Alignment-Probleme
static inline uint16_t be16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | uint16_t(p[1]); }
static inline uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

std::optional<NetworkEndpoint> InternetServices::StunQueryIPv4(const char* serverHost, uint16_t serverPort, uint32_t timeoutMs) {
    constexpr uint32_t MAGIC = 0x2112A442u;

    // STUN-Server über SDL_net auflösen, um einen sendefähigen Endpoint zu haben
    IPaddress socketAddress;
    if (SDLNet_ResolveHost(&socketAddress, serverHost, serverPort) != 0)
        return std::nullopt;
    NetworkEndpoint serverAddress = socketAddress;

    UDPSocket sock;
    if (not sock.Open("0.0.0.0", 0))
        return std::nullopt;

    // 20-Byte Binding Request
    uint8_t request[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };

    const uint32_t ticks = Timer::GetTime();
    for (int i = 0; i < 12; ++i)
        request[8 + i] = uint8_t(ticks >> ((i % 4) * 8));

    if (!sock.Send(request, sizeof(request), serverAddress)) {
        sock.Close();
        return std::nullopt;
    }

    const uint32_t start = Timer::GetTime();
    UDPData data;

    while ((Timer::GetTime() - start) < timeoutMs) {
        data = sock.Receive(20);
        if (data.length) {
            const uint16_t msgType = SDLNet_Read16(data.buffer + 0);   // host-order
            const uint16_t msgLen = SDLNet_Read16(data.buffer + 2);
            const uint32_t cookie = SDLNet_Read32(data.buffer + 4);
            if (msgType != 0x0101 or cookie != MAGIC) // Binding Success
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
                    if ((al >= 8) and (data.buffer[pos + 1] == 0x01)) { // IPv4
                        uint16_t port = SDLNet_Read16(data.buffer + pos + 2);
                        port ^= uint16_t(MAGIC >> 16);
                        uint32_t addr = SDLNet_Read32(data.buffer + pos + 4);
                        addr ^= MAGIC;
                        sock.Close();
                        return NetworkEndpoint(addr, port, ByteOrder::Host);
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

String InternetServices::GetLocalAddress(void) {
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

String InternetServices::GetLocalAddress(void) {
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

// minimal: SNTP über UDP, NTS nicht implementiert (Fallback Plain-NTP)

#define NTP_UNIX_EPOCH_DIFF 2208988800u

uint32_t InternetServices::QueryDate(int timeout) {
    if (timeout <= 0) 
        timeout = 1;

    UDPSocket sock;
    if (not sock.Open("0.0.0.0", 0)) 
        return 0;

    IPaddress srv{};
    if (SDLNet_ResolveHost(&srv, "time.cloudflare.com", 123) != 0) {
        sock.Close(true);
        return 0;
    }

    NetworkEndpoint server(srv);
    uint8_t req[48];
    std::memset(req, 0, sizeof(req));
    req[0] = 0x23; // LI=0, VN=4, Mode=3
    if (not sock.Send(req, int(sizeof(req)), server)) {
        sock.Close(true);
        return 0;
    }

    Timer t(timeout);
    t.Start();
    UDPData resp{};
    do {
        resp = sock.Receive(48);
        if (resp.length >= 48) break;
        SDL_Delay(1);
    } while (not t.HasExpired(timeout, false));
    if (resp.length < 48) {
        sock.Close(true);
        return 0;
    }

    uint32_t secs_be = 0;
    std::memcpy(&secs_be, resp.buffer + 40, 4);
    uint32_t ntp_secs = SDL_SwapBE32(secs_be);
    if (ntp_secs < NTP_UNIX_EPOCH_DIFF) {
        sock.Close(true);
        return 0;
    }
    
    uint64_t unix_secs_u64 = uint64_t(ntp_secs) - uint64_t(NTP_UNIX_EPOCH_DIFF);
    time_t unix_secs = time_t(unix_secs_u64);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &unix_secs);
#elif defined(__unix__) || defined(__APPLE__)
    gmtime_r(&unix_secs, &tm_utc);
#else
    std::tm* tmp = std::gmtime(&unix_secs);
    if (tmp) tm_utc = *tmp;
#endif
    uint32_t day = uint32_t(tm_utc.tm_mday);
    uint32_t month = uint32_t(tm_utc.tm_mon + 1);
    uint32_t year = uint32_t(tm_utc.tm_year + 1900);
    sock.Close(true);
    return day * 100000u + month * 1000u + year;
}

// =================================================================================================

