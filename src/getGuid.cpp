#include "getGuid.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include "esp_mac.h"

void getGuid(char *str)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    // Reconstruct the same two 32-bit halves that ESP.getEfuseMac() produced
    uint32_t int32_1 = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
    uint32_t int32_2 = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8);

    // Caller must supply a buffer of at least 17 bytes (16 hex digits + NUL)
    snprintf(str, 17, "%08" PRIX32 "%08" PRIX32, int32_1, int32_2);
}