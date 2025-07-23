#include "packet.h"
#include "known_packets.h"

static char packet_name_buffer[32];

bool rest_zero(const uint8_t* data, uint8_t start_index) {
  for(uint8_t i = start_index; i<11; i++)
    if(data[i] != 0x00)
      return false;
  return true;
}

// Identify packet type based on packet data
PacketType Packet::identifyPacket() const {

  // All packets must start with 0xAA
  if(data[0] != 0xAA) return UNKNOWN;

  uint8_t address = data[1];

  // Check for LED brightness messages (addressing aware)
  // TX LED: AA XX 05 YY 00 00 00 00 00 00 00 (XX=address, YY=brightness)
  if(address <= 0x7E && data[2] == 0x05 && rest_zero(data, 4)) {
    return TX_LED_BRIGHTNESS;
  }
  
  // RX LED: AA 80 05 XX 00 00 00 00 00 00 00 (XX=brightness)
  if(address == 0x80 && data[2] == 0x05 && rest_zero(data, 4)) {
    return RX_LED_BRIGHTNESS;
  }
  
  // TX LED Startup: AA XX 05 YY 00 FF 00 00 00 00 00 (XX=address, YY=brightness)
  if(address <= 0x7E && data[2] == 0x05 && data[4] == 0x00 && 
     data[5] == 0xFF && rest_zero(data, 6)) {
    return TX_LED_BRIGHTNESS_STARTUP;
  }
  
  // RX LED Startup: AA 80 05 XX 00 FF 00 00 00 00 00 (XX=brightness)
  if(address == 0x80 && data[2] == 0x05 && data[4] == 0x00 && 
    data[5] == 0xFF && rest_zero(data, 6)) {
    return RX_LED_BRIGHTNESS_STARTUP;
  }
  
  // RX Running: AA 80 01 04 03 XX 00 00 00 00 00
  if(address == 0x80 && data[2] == 0x01 && 
     data[3] == 0x04 && data[4] == 0x03 && rest_zero(data, 6)) {
    return RX_HEARTBEAT_RUNNING;
  }

  // RX Warming Up: AA 80 01 02 XX YY 00 00 00 00 00
  if(address == 0x80 && data[2] == 0x01 && 
     data[3] == 0x02 &&  rest_zero(data, 6)) {
    return RX_WARMUP;
  }

  // RX Warmup Complete : AA 80 01 05 XX YY 00 00 00 00 00
  if(address == 0x80 && data[2] == 0x01 && 
     data[3] == 0x05 && rest_zero(data, 6)) {
    return RX_WARMUP_COMP;
  }

  // TX Color: AA 8E 06 XX YY ZZ 00 00 00 00 00 (XX=R, YY=G, ZZ=B)
  if(address == 0x8E && data[2] == 0x06 && rest_zero(data, 6)) {
    return TX_COLOR;
  }
  
  // TX Color Confirm: AA 8E 03 08 YY ZZ 00 00 00 00 00 (08 + green + blue from TX_COLOR)
  if(address == 0x8E && data[2] == 0x03 && data[3] == 0x08 && rest_zero(data, 6)) {
    return TX_COLOR_CONFIRM;
  }
  
  // TX Color Startup: AA XX 06 YY ZZ WW 00 00 00 00 00 (XX=address, YY=R, ZZ=G, WW=B)
  if(address <= 0x7E && data[2] == 0x06 && rest_zero(data, 6)) {
    return TX_COLOR_STARTUP;
  }
  
  // RX Color Startup: AA 80 06 XX YY ZZ 00 00 00 00 00
  if(address == 0x80 && data[2] == 0x06 && rest_zero(data, 6)) {
    return RX_COLOR_STARTUP;
  }
  
  // Check for address-aware TX packets (compare pattern ignoring address byte)
  // TX Startup: AA XX 07 00 00 00 00 00 00 00 00 (XX=address)
  // if(address <= 0x7E && data[2] == 0x07 && 
  //    memcmp(&data[3], &tx_startup[3], 8) == 0) {
  //   return TX_DISCOVER;
  // }  

  // TX Heartbeat: AA XX 01 00 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0x01 && rest_zero(data, 3)) {
    return TX_HEARTBEAT;
  }
  
  // TX LED On Conf: AA XX 03 08 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0x03 && data[3] == 0x08 && rest_zero(data, 4)) {
    return TX_LED_ON_CONF;
  }
  
  // TX Serial Number 1: AA XX AF 01 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0xAF && data[3] == 0x01 && rest_zero(data, 4)) {
    return TX_SER_NO_1;
  }
  
  // TX Serial Number 2: AA XX B7 01 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0xB7 && data[3] == 0x01 && rest_zero(data, 4)) {
    return TX_SER_NO_2;
  }
  
  // TX Warmup: AA XX BF 01 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0xBF && data[3] == 0x01 && rest_zero(data, 4)) {
    return TX_WARMUP;
  }
  
  // TX Warmup Complete: AA XX 0C 00 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0x0C && rest_zero(data, 3)) {
    return TX_WARMUP_COMPLETE;
  }
  
  // TX Startup Complete: AA XX 0A 01 00 00 00 00 00 00 00 (XX=address)
  if(address <= 0x7E && data[2] == 0x0A && data[3] == 0x01 && rest_zero(data, 4)) {
    return TX_STARTUP_COMP;
  }
  
  // Check for non-addressable packets (broadcast/system level)
  if(memcmp(data, tx_discover, 11) == 0) return TX_DISCOVER;
  if(memcmp(data, tx_powerup, 11) == 0) return TX_POWERUP;
  if(memcmp(data, tx_powerdown, 11) == 0) return TX_POWERDOWN;
  
  // Check for RX packets (responses with device address in byte 3)
  // RX Startup: AA 80 07 XX 05 03 F2 00 0A 03 89 (XX=device address)
  if(address == 0x80 && data[2] == 0x07 && 
     data[4] == 0x05 && data[5] == 0x03 && data[6] == 0xF2 && 
     data[7] == 0x00 && data[8] == 0x0A && data[9] == 0x03 && data[10] == 0x89) {
    // There is a special case for RX Startup if 0x00 is the address. In this case, the repeller needs to have an
    // address set before it can respond to any other packets.
    if(data[3] == 0x00) {
      return RX_STARTUP_00;
    } else {
      return RX_STARTUP;
    }
  }
  
  // RX Serial Number 1: AA 80 AF XX XX XX XX XX XX XX XX (serial number part 1)
  if(address == 0x80 && data[2] == 0xAF) {
    return RX_SER_NO_1;
  }
  
  // RX Serial Number 2: AA 80 B7 XX XX XX XX XX XX XX XX (serial number part 2)
  if(address == 0x80 && data[2] == 0xB7) {
    return RX_SER_NO_2;
  }
  
  // RX Warmup: AA 80 BF FF FF FF FF FF FF FF FF (warmup acknowledgment)
  // NOTE - Ignoring the rest of the packet (FFs) for now as they appear to be placeholders
  if(address == 0x80 && data[2] == 0xBF) {
    return RX_WARMUP;
  }
  
  // Other RX packets (exact matches for now)
  if(memcmp(data, rx_led_on_conf, 11) == 0) return RX_LED_ON_CONF;
  if(memcmp(data, rx_warmup_complete, 11) == 0) return RX_WARMUP_COMPLETE;
  if(memcmp(data, rx_startup_comp, 11) == 0) return RX_STARTUP_COMP;
  
  return UNKNOWN;
}

// Get formatted packet name with extracted data
const char* Packet::packetName() const {
  PacketType type = identifyPacket();
  
  switch(type) {
    case TX_LED_BRIGHTNESS:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led_brightness:%02X (%d)", data[1], data[3]);
      break;
    case RX_LED_BRIGHTNESS:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_led_brightness (%d)", data[3]);
      break;
    case TX_LED_BRIGHTNESS_STARTUP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led_brightness_startup:%02X (%d)", data[1], data[3]);
      break;
    case RX_LED_BRIGHTNESS_STARTUP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_led_brightness_startup (%d)", data[3]);
      break;
    case RX_HEARTBEAT_RUNNING:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_heartbeat_running (%d)", data[5]);
      break;
    case RX_WARMUP:
      if(data[1] == 0x80 && data[2] == 0x01) {
        // RX Warming Up: AA 80 01 02 XX YY 00 00 00 00 00
        snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_warmup (%02X%02X)", data[4], data[5]);
      } else {
        // RX Warmup acknowledgment: AA 80 BF FF FF FF FF FF FF FF FF
        strcpy(packet_name_buffer, "rx_warmup");
      }
      break;
    case RX_WARMUP_COMP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_warmup_comp (%02X%02X)", data[4], data[5]);
      break;
    case TX_COLOR:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_color (%02X%02X%02X)", data[3], data[4], data[5]);
      break;
    case TX_COLOR_CONFIRM:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_color_confirm (%02X%02X)", data[4], data[5]);
      break;
    case TX_COLOR_STARTUP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_color_startup:%02X (%02X%02X%02X)", data[1], data[3], data[4], data[5]);
      break;
    case RX_COLOR_STARTUP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_color_startup (%02X%02X%02X)", data[3], data[4], data[5]);
      break;
    case TX_DISCOVER:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_startup:%02X", data[1]);
      break;
    case TX_HEARTBEAT:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_heartbeat:%02X", data[1]);
      break;
    case TX_LED_ON_CONF:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led_on_conf:%02X", data[1]);
      break;
    case TX_SER_NO_1:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_ser_no_1:%02X", data[1]);
      break;
    case TX_SER_NO_2:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_ser_no_2:%02X", data[1]);
      break;
    case TX_WARMUP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_warmup:%02X", data[1]);
      break;
    case TX_WARMUP_COMPLETE:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_warmup_complete:%02X", data[1]);
      break;
    case TX_STARTUP_COMP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_startup_comp:%02X", data[1]);
      break;
    case TX_POWERUP:
      strcpy(packet_name_buffer, "tx_powerup");
      break;
    case TX_POWERDOWN:
      strcpy(packet_name_buffer, "tx_powerdown");
      break;
    case RX_STARTUP:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_startup:%02X", data[3]);
      break;
    case RX_STARTUP_00:
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_startup_00", data[3]);
      break;
    case RX_SER_NO_1: {
      char serial_part[9];
      for(int i = 0; i < 8; i++) {
        serial_part[i] = (data[i+3] >= 32 && data[i+3] <= 126) ? data[i+3] : '.';
      }
      serial_part[8] = '\0';
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_ser_no_1 (%s)", serial_part);
      break;
    }
    case RX_SER_NO_2: {
      char serial_part[9];
      for(int i = 0; i < 8; i++) {
        serial_part[i] = (data[i+3] >= 32 && data[i+3] <= 126) ? data[i+3] : '.';
      }
      serial_part[8] = '\0';
      snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_ser_no_2 (%s)", serial_part);
      break;
    }
    case RX_LED_ON_CONF:
      strcpy(packet_name_buffer, "rx_led_on_conf");
      break;
    case RX_WARMUP_COMPLETE:
      strcpy(packet_name_buffer, "rx_warmup_complete");
      break;
    case RX_STARTUP_COMP:
      strcpy(packet_name_buffer, "rx_startup_comp");
      break;
    case UNKNOWN:
    default:
      strcpy(packet_name_buffer, "UNKNOWN");
      break;
  }
  
  return packet_name_buffer;
}

// Print packet in standard format with identification
void Packet::print() const {
  Serial.printf("[%08lu] RX: ", millis());
  
  // Print hex data
  for(size_t i = 0; i < 11; i++) {
    Serial.printf("%02X ", data[i]);
  }
  
  // Get packet name with extracted data
  const char* packet_name = packetName();
  Serial.printf("(%s) [11 bytes]\n", packet_name);
}


