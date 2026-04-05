#include "getGuid.h"
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

    char first[9], secon[9];
    sprintf(first, "%08X", int32_1);
    sprintf(secon, "%08X", int32_2);

    strcpy(str, first);
    strcat(str, secon);
}