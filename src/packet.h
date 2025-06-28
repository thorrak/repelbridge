#ifndef PACKET_H
#define PACKET_H

#include <Arduino.h>

enum PacketType {
  UNKNOWN,
  TX_LED,  // TODO - Change to TX_LED_BRIGHTNESS
  RX_LED,  // TODO - Change to RX_LED_BRIGHTNESS
  TX_LED_STARTUP,  // TODO - Change to TX_LED_BRIGHTNESS_STARTUP
  RX_LED_STARTUP,  // TODO - Change to RX_LED_BRIGHTNESS_STARTUP
  RX_RUNNING,  // TODO - Change to RX_HEARTBEAT_RUNNING
  RX_WARMUP,
  RX_WARMUP_COMP,
  TX_COLOR,
  TX_COLOR_CONFIRM,
  TX_COLOR_STARTUP,
  RX_COLOR_STARTUP,
  TX_DISCOVER,
  TX_HEARTBEAT,
  TX_LED_ON_CONF,
  TX_SER_NO_1,
  TX_SER_NO_2,
  TX_WARMUP,
  TX_WARMUP_COMPLETE,
  TX_STARTUP_COMP,
  TX_POWERUP,
  TX_POWERDOWN,
  RX_STARTUP,
  RX_SER_NO_1,
  RX_SER_NO_2,
  RX_WARMUP_COMPLETE,  // TODO - Figure out what to rename this to so it's less confusing vs. RX_WARMUP_COMP
  RX_STARTUP_COMP,
  RX_LED_ON_CONF
};


// Packet class to encapsulate RS-485 packet data and operations
class Packet {
public:
  uint8_t data[11];  // Raw 11-byte packet data
  
  // Default constructor - initializes packet to all zeros
  Packet() {
    memset(data, 0, sizeof(data));
  }
  
  // Constructor from raw data
  Packet(const uint8_t* raw_data) {
    memcpy(data, raw_data, sizeof(data));
  }
  
  // Get the address from the packet
  // For TX packets: address is in byte 1 (if <= 0x7E, otherwise broadcast)
  // For RX packets: address is in byte 3 (for startup responses) or derived from context
  uint8_t getAddress() const {
    // TX packets with device addressing (AA XX ...)
    if (data[0] == 0xAA && data[1] <= 0x7E) {
      return data[1];
    }
    
    // RX startup packets (AA 80 07 XX ...) - device address in byte 3
    if (data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x07) {
      return data[3];
    }
    
    // For other packets, return 0 to indicate no specific address or broadcast
    return 0;
  }
  
  // Check if packet has valid header
  bool isValid() const {
    return data[0] == 0xAA;
  }
  
  // Get packet type byte (byte 2)
  uint8_t getType() const {
    return data[2];
  }
  
  // Get raw data pointer for compatibility
  const uint8_t* getRawData() const {
    return data;
  }
  
  // Set data from raw array
  void setData(const uint8_t* raw_data) {
    memcpy(data, raw_data, sizeof(data));
  }
  
  // Identify packet type
  PacketType identifyPacket() const;
  
  // Get formatted packet name with extracted data
  const char* packetName() const;
  
  // Print packet in standard format with identification
  void print() const;

  // Transmit packet across the line
  void transmit();

  // If we want to override this packet with a transmit value, use the following
  void setAsTxLED(uint8_t address, uint8_t brightness_pct);
  void setAsTxLEDStartup(uint8_t address, uint8_t brightness_pct);
  void setAsTxColor(uint8_t red, uint8_t green, uint8_t blue);
  void setAsTxColorStartup(uint8_t address, uint8_t red, uint8_t green, uint8_t blue);
  void setAsTxDiscover();
  void setAsTxHeartbeat(uint8_t address);
  void setAsTxLEDOnConf(uint8_t address);
  void setAsTxSerNo1(uint8_t address);
  void setAsTxSerNo2(uint8_t address);
  void setAsTxWarmup(uint8_t address);
  void setAsTxWarmupComp(uint8_t address);
  void setAsTxStartupComp(uint8_t address);
  void setAsTxPowerup();
  void setAsTxPowerdown();
  void setAsTxColorConfirm(uint8_t green, uint8_t blue);
  
};

#endif