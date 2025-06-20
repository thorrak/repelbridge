
#include <Arduino.h>
#include "known_packets.h"

// MAX3485 Pin Connections for ESP32
// RX pin (DI) - connects to ESP32 TX (GPIO17)
// TX pin (RO) - connects to ESP32 RX (GPIO16)  
// DE and RE pins (tied together) - connects to GPIO4
const uint8_t rs485_direction_pin = 4;  // DE/RE control pin

// Buffer for incoming data
const size_t BUFFER_SIZE = 256;
uint8_t rx_buffer[BUFFER_SIZE];
size_t buffer_index = 0;

// Timing for packet detection
unsigned long last_byte_time = 0;
const unsigned long PACKET_TIMEOUT_MS = 8;  // 10ms gap indicates new packet
bool packet_in_progress = false;

void setup() {
  Serial.begin(115200);
  Serial.println("RS-485 Sniffer Starting...");
  Serial.println("Monitoring communications between controller and Liv Repeller...");
  Serial.println("Format: [TIMESTAMP] DIR: HEX_DATA (PACKET_NAME)");
  Serial.println("DIR: RX=Received, TX=Transmitted");
  Serial.println("----------------------------------------");

  // Set DE/RE control pin as output and keep in receive mode
  pinMode(rs485_direction_pin, OUTPUT);
  digitalWrite(rs485_direction_pin, LOW);  // Always in receive mode for sniffer

  // Initialize Serial2 for RS-485 communication
  // ESP32 Serial2 uses GPIO16 (RX) and GPIO17 (TX) by default
  Serial2.begin(19200, SERIAL_8N1, 16, 17);  // RX=16, TX=17
  
  // Clear any existing data
  while(Serial2.available()) {
    Serial2.read();
  }
  
  delay(100);
  Serial.println("Ready to capture RS-485 data...");
}

static char packet_name_buffer[32];

const char* identifyPacket(const uint8_t* data, size_t length) {
  if(length != 11) return "UNKNOWN";
  
  // Check for LED brightness messages
  // TX LED: AA 05 05 XX 00 00 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x05 && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0x00 && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led (%d)", data[3]);
    return packet_name_buffer;
  }
  
  // RX LED: AA 80 05 XX 00 00 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0x00 && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_led (%d)", data[3]);
    return packet_name_buffer;
  }
  
  // TX LED Startup: AA 05 05 XX 00 FF 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x05 && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0xFF && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led_startup (%d)", data[3]);
    return packet_name_buffer;
  }
  
  // RX LED Startup: AA 80 05 XX 00 FF 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0xFF && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_led_startup (%d)", data[3]);
    return packet_name_buffer;
  }
  
  // RX Running: AA 80 01 04 03 XX 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x01 && 
     data[3] == 0x04 && data[4] == 0x03 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_running (%d)", data[5]);
    return packet_name_buffer;
  }

  // RX Warming Up: AA 80 01 02 XX YY 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x01 && 
     data[3] == 0x02 &&  
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_warmup (%02X%02X)", data[4], data[5]);
    return packet_name_buffer;
  }

  
  // RX Warmup Complete : AA 80 01 02 XX YY 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x01 && 
     data[3] == 0x05 &&  
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_warmup_comp (%02X%02X)", data[4], data[5]);
    return packet_name_buffer;
  }


  // TX Color: AA 8E 06 XX YY ZZ 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x8E && data[2] == 0x06 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_color (%02X%02X%02X)", data[3], data[4], data[5]);
    return packet_name_buffer;
  }
  
  // RX Color: AA 8E 03 XX YY ZZ 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x8E && data[2] == 0x03 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_color (%02X%02X%02X)", data[3], data[4], data[5]);
    return packet_name_buffer;
  }
  
  // TX Color Startup: AA 05 06 XX YY ZZ 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x05 && data[2] == 0x06 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_color_startup (%02X%02X%02X)", data[3], data[4], data[5]);
    return packet_name_buffer;
  }
  
  // RX Color Startup: AA 80 06 XX YY ZZ 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x06 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_color_startup (%02X%02X%02X)", data[3], data[4], data[5]);
    return packet_name_buffer;
  }
  
  // Check for fixed packets
  if(memcmp(data, tx_startup, 11) == 0) return "tx_startup";
  if(memcmp(data, rx_startup, 11) == 0) return "rx_startup";
  if(memcmp(data, tx_powerup, 11) == 0) return "tx_powerup";
  if(memcmp(data, tx_powerdown, 11) == 0) return "tx_powerdown";
  if(memcmp(data, tx_heartbeat, 11) == 0) return "tx_heartbeat";
  if(memcmp(data, tx_led_on_conf, 11) == 0) return "tx_led_on_conf";
  if(memcmp(data, rx_led_on_conf, 11) == 0) return "rx_led_on_conf";
  if(memcmp(data, tx_warmup_1, 11) == 0) return "tx_warmup_1";
  if(memcmp(data, rx_warmup_1, 11) == 0) return "rx_warmup_1";
  if(memcmp(data, tx_warmup_2, 11) == 0) return "tx_warmup_2";
  if(memcmp(data, rx_warmup_2, 11) == 0) return "rx_warmup_2";
  if(memcmp(data, tx_warmup_3, 11) == 0) return "tx_warmup_3";
  if(memcmp(data, rx_warmup_3, 11) == 0) return "rx_warmup_3";
  if(memcmp(data, tx_warmup_complete, 11) == 0) return "tx_warmup_complete";
  if(memcmp(data, rx_warmup_complete, 11) == 0) return "rx_warmup_complete";
  if(memcmp(data, tx_startup_comp, 11) == 0) return "tx_startup_comp";
  if(memcmp(data, rx_startup_comp, 11) == 0) return "rx_startup_comp";
  
  return "UNKNOWN";
}

void printHexData(const uint8_t* data, size_t length, const char* direction) {
  // Print timestamp
  Serial.printf("[%08lu] %s: ", millis(), direction);
  
  // Print hex data
  for(size_t i = 0; i < length; i++) {
    Serial.printf("%02X ", data[i]);
  }
  
  // Identify packet type
  const char* packet_name = identifyPacket(data, length);
  Serial.printf("(%s)", packet_name);
  
  // Print length
  Serial.printf(" [%d bytes]\n", length);
}

void processPacket() {
  if(buffer_index > 0) {
    printHexData(rx_buffer, buffer_index, "RX");
    buffer_index = 0;
  }
  packet_in_progress = false;
}

void loop() {
  unsigned long current_time = millis();
  
  // Check for packet timeout
  if(packet_in_progress && (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
    processPacket();
  }
  
  // Read available data
  while(Serial2.available()) {
    uint8_t byte_received = Serial2.read();
    current_time = millis();
    
    // If we haven't received data for a while, this might be a new packet
    if(!packet_in_progress || (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
      if(packet_in_progress) {
        processPacket();  // Process previous packet first
      }
      buffer_index = 0;
      packet_in_progress = true;
    }
    
    // Add byte to buffer if there's space
    if(buffer_index < BUFFER_SIZE - 1) {
      rx_buffer[buffer_index++] = byte_received;
    } else {
      // Buffer overflow - process what we have
      processPacket();
      rx_buffer[0] = byte_received;
      buffer_index = 1;
      packet_in_progress = true;
    }
    
    last_byte_time = current_time;
  }
  
  // Small delay to prevent overwhelming the processor
  delay(1);
}