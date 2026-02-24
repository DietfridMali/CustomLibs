#pragma once

// =================================================================================================

// stun_client.hpp
#pragma once
#include <stdint.h>
#include <optional>
#include <string>
#include <cstring>

#include "string.hpp"
#include "udp.h"

class InternetServices {
public:
    std::optional<NetworkEndpoint> StunQueryIPv4(const char* serverHost = "stun.l.google.com", uint16_t serverPort = 19302, uint32_t timeoutMs = 1500);

    String GetLanAddress(void);

    uint32_t QueryDate(int timeout);
};


// =================================================================================================

