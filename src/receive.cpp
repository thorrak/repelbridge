#include "known_packets.h"
#include "repeller.h"
#include "packet.h"
#include "receive.h"

// Helper function to receive and print a packet with timeout
static bool receive_and_print(const char* expected_type, uint16_t timeout_ms) {
  Packet received_packet;
  
  if (receive_packet(received_packet, timeout_ms)) {
    received_packet.print();
    return true;
  } else {
    Serial.printf("TIMEOUT: Expected %s but no response received\n", expected_type);
    return false;
  }
}


// Packet-based receive function
bool receive_packet(Packet& packet, uint16_t timeout_ms) {
  static uint8_t rx_buffer[11];
  static size_t buffer_index = 0;
  static unsigned long last_byte_time = 0;
  static bool packet_in_progress = false;
  
  const unsigned long PACKET_TIMEOUT_MS = 8;  // Same as sniffer
  unsigned long start_time = millis();
  unsigned long current_time;
  
  // Ensure we're in receive mode
  digitalWrite(BUS_0_DIR_PIN, LOW);  // TODO - Detect the appropriate bus and only toggle that pin
  #ifdef BUS_1_DIR_PIN
  digitalWrite(BUS_1_DIR_PIN, LOW);  // TODO - Detect the appropriate bus and only toggle that pin
  #endif
  
  while (true) {
    current_time = millis();
    
    // Check for overall timeout
    if (timeout_ms > 0 && (current_time - start_time) >= timeout_ms) {
      return false;
    }
    
    // Check for packet timeout (gap between bytes)
    if (packet_in_progress && (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
      if (buffer_index == 11) {
        // Complete 11-byte packet received
        packet.setData(rx_buffer);
        buffer_index = 0;
        packet_in_progress = false;
        return true;
      } else {
        // Incomplete packet, reset
        buffer_index = 0;
        packet_in_progress = false;
      }
    }
    
    // Read available data
    while (Serial1.available()) {
      uint8_t byte_received = Serial1.read();
      current_time = millis();
      
      // If we haven't received data for a while, this might be a new packet
      if (!packet_in_progress || (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
        if (packet_in_progress && buffer_index == 11) {
          // Previous complete packet available
          packet.setData(rx_buffer);
          buffer_index = 0;
          packet_in_progress = false;
          return true;
        }
        buffer_index = 0;
        packet_in_progress = true;
      }
      
      // Add byte to buffer
      if (buffer_index < 11) {
        rx_buffer[buffer_index++] = byte_received;
      } else {
        // Buffer overflow, reset
        buffer_index = 0;
        packet_in_progress = false;
      }
      
      last_byte_time = current_time;
      
      // Check if we have a complete packet
      if (buffer_index == 11) {
        packet.setData(rx_buffer);
        buffer_index = 0;
        packet_in_progress = false;
        return true;
      }
    }
    
    // Non-blocking mode
    if (timeout_ms == 0) {
      return false;
    }
    
    // Small delay to prevent overwhelming the processor
    delay(100);
  }
}
