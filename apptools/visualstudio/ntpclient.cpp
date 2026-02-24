
// minimal: SNTP über UDP, NTS nicht implementiert (Fallback Plain-NTP)

#include <cstdint>
#include <cstring>
#include <ctime>
#include "udp.h"
#include "networkendpoint.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_net.h"
#pragma warning(pop)

#define NTP_UNIX_EPOCH_DIFF 2208988800u

uint32_t QueryCurrentDateNTP(int timeoutSeconds) {
    if (timeoutSeconds <= 0) timeoutSeconds = 1;
    UDPSocket sock;
    if (not sock.Open("0.0.0.0", 0)) return 0;
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
    uint32_t start = SDL_GetTicks();
    UDPData resp{};
    do {
        resp = sock.Receive(48);
        if (resp.length >= 48) break;
        SDL_Delay(1);
    } while ((SDL_GetTicks() - start) < uint32_t(timeoutSeconds) * 1000u);
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
