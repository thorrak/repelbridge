#include "packet.h"
#include "known_packets.h"


void fill_zero(uint8_t* data, uint8_t start_index) {
  for(uint8_t i = start_index; i<11; i++)
    data[i] = 0x00;
}

void Packet::setAsTxLED(uint8_t address, uint8_t brightness_pct) {
  // TX LED: AA XX 05 YY 00 00 00 00 00 00 00 (XX=address, YY=brightness)
  uint8_t packet_data[11] = {0xAA, address, 0x05, brightness_pct};
  fill_zero(packet_data, 4);
  memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxLEDStartup(uint8_t address, uint8_t brightness_pct) {
  // TX LED Startup: AA XX 05 YY 00 FF 00 00 00 00 00 (XX=address, YY=brightness)
  uint8_t packet_data[11] = {0xAA, address, 0x05, brightness_pct, 0x00, 0xFF};
  fill_zero(packet_data, 6);
  memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxColor(uint8_t red, uint8_t green, uint8_t blue) {
  // TX Color: AA 8E 06 XX YY ZZ 00 00 00 00 00 (XX=R, YY=G, ZZ=B)
    setAsTxColorStartup(0x8e, red, green, blue);
}

void Packet::setAsTxColorStartup(uint8_t address, uint8_t red, uint8_t green, uint8_t blue) {
    // TX Color Startup: AA XX 06 YY ZZ WW 00 00 00 00 00 (XX=address, YY=R, ZZ=G, WW=B)
    uint8_t packet_data[11] = {0xAA, address, 0x06, red, green, blue};
    fill_zero(packet_data, 6);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxDiscover() {
    // TX Discover: AA 82 07 00 00 00 00 00 00 00 00
    memcpy(data, tx_discover, sizeof(data));
}

void Packet::setAsTxHeartbeat(uint8_t address) {
      // TX Heartbeat: AA XX 01 00 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0x01};
    fill_zero(packet_data, 3);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxLEDOnConf(uint8_t address) {
    // TX LED On Conf: AA XX 03 08 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0x03, 0x08};
    fill_zero(packet_data, 4);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxSerNo1(uint8_t address) {
    // TX Serial Number 1: AA XX AF 01 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0xAF, 0x01};
    fill_zero(packet_data, 4);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxSerNo2(uint8_t address) {
    // TX Serial Number 2: AA XX B7 01 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0xB7, 0x01};
    fill_zero(packet_data, 4);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxWarmup(uint8_t address) {
    // TX Warmup: AA XX BF 01 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0xBF, 0x01};
    fill_zero(packet_data, 4);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxWarmupComp(uint8_t address) {
    // TX Warmup Complete: AA XX 0C 00 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0x0C};
    fill_zero(packet_data, 3);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxStartupComp(uint8_t address) {
    // TX Startup Complete: AA XX 0A 01 00 00 00 00 00 00 00 (XX=address)
    uint8_t packet_data[11] = {0xAA, address, 0x0A, 0x01};
    fill_zero(packet_data, 4);
    memcpy(data, packet_data, sizeof(data));
}

void Packet::setAsTxPowerup() {
    // TX Discover: AA 82 07 00 00 00 00 00 00 00 00
    memcpy(data, tx_powerup, sizeof(data));
}

void Packet::setAsTxPowerdown() {
    // TX Discover: AA 82 07 00 00 00 00 00 00 00 00
    memcpy(data, tx_powerdown, sizeof(data));
}

void Packet::setAsTxColorConfirm(uint8_t green, uint8_t blue) {
  // TX Color Confirm: AA 8E 03 08 YY ZZ 00 00 00 00 00 (08 + green + blue from TX_COLOR)
  uint8_t packet_data[11] = {0xAA, 0x8E, 0x03, 0x08, green, blue};
  fill_zero(packet_data, 6);
  memcpy(data, packet_data, sizeof(data));
}
