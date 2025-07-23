#include "sniffer_mode.h"
#include "packet.h"

// MAX3485 Pin Connections for ESP32-C6
// RX pin (DI) - connects to ESP32-C6 TX (GPIO19)
// TX pin (RO) - connects to ESP32-C6 RX (GPIO22)  
// DE and RE pins (tied together) - connects to GPIO21 (BUS_0_DIR_PIN)

// Buffer for incoming data
static const size_t BUFFER_SIZE = 256;
static uint8_t rx_buffer[BUFFER_SIZE];
static size_t buffer_index = 0;

// Timing for packet detection
static unsigned long last_byte_time = 0;
static const unsigned long PACKET_TIMEOUT_MS = 8;  // 8ms gap indicates new packet
static bool packet_in_progress = false;

static void processPacket() {
  if(buffer_index > 0) {
    // Only process 11-byte packets
    if(buffer_index == 11) {
      Packet packet(rx_buffer);
      packet.print();
    } else {
      // Print partial packet for debugging
      Serial.printf("[%08lu] RX: ", millis());
      for(size_t i = 0; i < buffer_index; i++) {
        Serial.printf("%02X ", rx_buffer[i]);
      }
      Serial.printf("(PARTIAL) [%d bytes]\n", buffer_index);
    }
    buffer_index = 0;
  }
  packet_in_progress = false;
}

void sniffer_setup() {
  Serial.begin(115200);
  Serial.println("RS-485 Sniffer Starting...");
  Serial.println("Monitoring communications between controller and Liv Repeller...");
  Serial.println("Format: [TIMESTAMP] DIR: HEX_DATA (PACKET_NAME)");
  Serial.println("DIR: RX=Received, TX=Transmitted");
  Serial.println("----------------------------------------");

  // Set DE/RE control pin as output and keep in receive mode
  pinMode(BUS_0_DIR_PIN, OUTPUT);
  digitalWrite(BUS_0_DIR_PIN, LOW);  // Always in receive mode for sniffer


  // Initialize Serial1 for RS-485 communication
  // ESP32-C6 Serial1 configured for custom pins
  Serial1.begin(19200, SERIAL_8N1, BUS_0_RX_PIN, BUS_0_TX_PIN);  // RX=22, TX=19
  
  // Clear any existing data
  while(Serial1.available()) {
    Serial1.read();
  }
  
  delay(100);
  Serial.println("Ready to capture RS-485 data...");
}

void sniffer_loop() {
  unsigned long current_time = millis();
  
  // Check for packet timeout
  if(packet_in_progress && (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
    processPacket();
  }
  
  // Read available data
  while(Serial1.available()) {
    uint8_t byte_received = Serial1.read();
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