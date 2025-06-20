#include "tx_controller.h"
#include "known_packets.h"

static uint8_t rs485_direction_pin = 4;
static char packet_name_buffer[32];

// Packet identification function (address-aware)
static const char* identifyPacket(const uint8_t* data, size_t length) {
  if(length != 11) return "UNKNOWN";
  
  // Check for LED brightness messages (addressing aware)
  // TX LED: AA XX 05 YY 00 00 00 00 00 00 00 (XX=address, YY=brightness)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0x00 && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led:%02X (%d)", data[1], data[3]);
    return packet_name_buffer;
  }
  
  // RX LED: AA 80 05 XX 00 00 00 00 00 00 00 (XX=brightness)
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0x00 && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_led (%d)", data[3]);
    return packet_name_buffer;
  }
  
  // TX LED Startup: AA XX 05 YY 00 FF 00 00 00 00 00 (XX=address, YY=brightness)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x05 && 
     data[4] == 0x00 && data[5] == 0xFF && data[6] == 0x00 && 
     data[7] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led_startup:%02X (%d)", data[1], data[3]);
    return packet_name_buffer;
  }
  
  // RX LED Startup: AA 80 05 XX 00 FF 00 00 00 00 00 (XX=brightness)
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

  // RX Warmup Complete : AA 80 01 05 XX YY 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x01 && 
     data[3] == 0x05 &&  
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_warmup_comp (%02X%02X)", data[4], data[5]);
    return packet_name_buffer;
  }

  // TX Color: AA 8E 06 XX YY ZZ 00 00 00 00 00 (XX=R, YY=G, ZZ=B)
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
  
  // TX Color Startup: AA XX 06 YY ZZ WW 00 00 00 00 00 (XX=address, YY=R, ZZ=G, WW=B)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x06 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_color_startup:%02X (%02X%02X%02X)", data[1], data[3], data[4], data[5]);
    return packet_name_buffer;
  }
  
  // RX Color Startup: AA 80 06 XX YY ZZ 00 00 00 00 00
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x06 && 
     data[6] == 0x00 && data[7] == 0x00 && data[8] == 0x00 && 
     data[9] == 0x00 && data[10] == 0x00) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_color_startup (%02X%02X%02X)", data[3], data[4], data[5]);
    return packet_name_buffer;
  }
  
  // Check for address-aware TX packets (compare pattern ignoring address byte)
  // TX Startup: AA XX 07 00 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x07 && 
     memcmp(&data[3], &tx_startup[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_startup:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX Heartbeat: AA XX 01 00 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x01 && 
     memcmp(&data[3], &tx_heartbeat[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_heartbeat:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX LED On Conf: AA XX 03 08 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x03 && 
     memcmp(&data[3], &tx_led_on_conf[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_led_on_conf:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX Warmup 1: AA XX AF 01 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0xAF && 
     memcmp(&data[3], &tx_warmup_1[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_warmup_1:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX Warmup 2: AA XX B7 01 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0xB7 && 
     memcmp(&data[3], &tx_warmup_2[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_warmup_2:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX Warmup 3: AA XX BF 01 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0xBF && 
     memcmp(&data[3], &tx_warmup_3[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_warmup_3:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX Warmup Complete: AA XX 0C 00 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x0C && 
     memcmp(&data[3], &tx_warmup_complete[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_warmup_complete:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // TX Startup Complete: AA XX 0A 01 00 00 00 00 00 00 00 (XX=address)
  if(data[0] == 0xAA && data[1] <= 0x7E && data[2] == 0x0A && 
     memcmp(&data[3], &tx_startup_comp[3], 8) == 0) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "tx_startup_comp:%02X", data[1]);
    return packet_name_buffer;
  }
  
  // Check for non-addressable packets (broadcast/system level)
  if(memcmp(data, tx_powerup, 11) == 0) return "tx_powerup";
  if(memcmp(data, tx_powerdown, 11) == 0) return "tx_powerdown";
  
  // Check for RX packets (responses with device address in byte 3)
  // RX Startup: AA 80 07 XX 05 03 F2 00 0A 03 89 (XX=device address)
  if(data[0] == 0xAA && data[1] == 0x80 && data[2] == 0x07 && 
     data[4] == 0x05 && data[5] == 0x03 && data[6] == 0xF2 && 
     data[7] == 0x00 && data[8] == 0x0A && data[9] == 0x03 && data[10] == 0x89) {
    snprintf(packet_name_buffer, sizeof(packet_name_buffer), "rx_startup:%02X", data[3]);
    return packet_name_buffer;
  }
  
  // Other RX packets (exact matches for now)
  if(memcmp(data, rx_led_on_conf, 11) == 0) return "rx_led_on_conf";
  if(memcmp(data, rx_warmup_1, 11) == 0) return "rx_warmup_1";
  if(memcmp(data, rx_warmup_2, 11) == 0) return "rx_warmup_2";
  if(memcmp(data, rx_warmup_3, 11) == 0) return "rx_warmup_3";
  if(memcmp(data, rx_warmup_complete, 11) == 0) return "rx_warmup_complete";
  if(memcmp(data, rx_startup_comp, 11) == 0) return "rx_startup_comp";
  
  return "UNKNOWN";
}

// Helper function to print received packets
static void print_received_packet(const uint8_t* data, size_t length) {
  Serial.printf("[%08lu] RX: ", millis());
  
  // Print hex data
  for(size_t i = 0; i < length; i++) {
    Serial.printf("%02X ", data[i]);
  }
  
  // Identify packet type
  const char* packet_name = identifyPacket(data, length);
  Serial.printf("(%s) [%d bytes]\n", packet_name, length);
}

// Helper function to receive and print a packet with timeout
static bool receive_and_print(const char* expected_type, uint16_t timeout_ms = 500) {
  uint8_t received_packet[11];
  
  if (receive_packet(received_packet, timeout_ms)) {
    print_received_packet(received_packet, 11);
    return true;
  } else {
    Serial.printf("TIMEOUT: Expected %s but no response received\n", expected_type);
    return false;
  }
}

void tx_controller_init(uint8_t direction_pin) {
  rs485_direction_pin = direction_pin;
  pinMode(rs485_direction_pin, OUTPUT);
  digitalWrite(rs485_direction_pin, LOW);  // Start in receive mode
  
  // Initialize Serial2 for RS-485 communication
  Serial2.begin(19200, SERIAL_8N1, 16, 17);  // RX=16, TX=17
  
  // Clear any existing data
  while(Serial2.available()) {
    Serial2.read();
  }
}

static void transmit_packet(const uint8_t* packet, size_t length) {
  // Set RS-485 transceiver to transmit mode
  digitalWrite(rs485_direction_pin, HIGH);
  delayMicroseconds(10);  // Give DE time to enable
  
  // Send the packet
  Serial2.write(packet, length);
  Serial2.flush();  // Wait until transmission complete
  
  // Back to receive mode
  delayMicroseconds(10);  // Give time for transmission to complete
  digitalWrite(rs485_direction_pin, LOW);
}

// Fixed packet transmission functions
void send_tx_startup() {
  transmit_packet(tx_startup, sizeof(tx_startup));
}

void send_tx_startup_comp() {
  transmit_packet(tx_startup_comp, sizeof(tx_startup_comp));
}

void send_tx_powerup() {
  transmit_packet(tx_powerup, sizeof(tx_powerup));
}

void send_tx_powerdown() {
  transmit_packet(tx_powerdown, sizeof(tx_powerdown));
}

void send_tx_heartbeat() {
  transmit_packet(tx_heartbeat, sizeof(tx_heartbeat));
}

void send_tx_led_on_conf() {
  transmit_packet(tx_led_on_conf, sizeof(tx_led_on_conf));
}

void send_tx_warmup_1() {
  transmit_packet(tx_warmup_1, sizeof(tx_warmup_1));
}

void send_tx_warmup_2() {
  transmit_packet(tx_warmup_2, sizeof(tx_warmup_2));
}

void send_tx_warmup_3() {
  transmit_packet(tx_warmup_3, sizeof(tx_warmup_3));
}

void send_tx_warmup_complete() {
  transmit_packet(tx_warmup_complete, sizeof(tx_warmup_complete));
}

// Dynamic packet transmission functions
void send_tx_led(uint8_t brightness) {
  uint8_t packet[] = {0xAA, 0x05, 0x05, brightness, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  transmit_packet(packet, sizeof(packet));
}

void send_tx_led_startup(uint8_t brightness) {
  uint8_t packet[] = {0xAA, 0x05, 0x05, brightness, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
  transmit_packet(packet, sizeof(packet));
}

void send_tx_color(uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t packet[] = {0xAA, 0x8E, 0x06, red, green, blue, 0x00, 0x00, 0x00, 0x00, 0x00};
  transmit_packet(packet, sizeof(packet));
}

void send_tx_color_startup(uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t packet[] = {0xAA, 0x05, 0x06, red, green, blue, 0x00, 0x00, 0x00, 0x00, 0x00};
  transmit_packet(packet, sizeof(packet));
}

bool receive_packet(uint8_t* packet_buffer, uint16_t timeout_ms) {
  static uint8_t rx_buffer[11];
  static size_t buffer_index = 0;
  static unsigned long last_byte_time = 0;
  static bool packet_in_progress = false;
  
  const unsigned long PACKET_TIMEOUT_MS = 8;  // Same as sniffer
  unsigned long start_time = millis();
  unsigned long current_time;
  
  // Ensure we're in receive mode
  digitalWrite(rs485_direction_pin, LOW);
  
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
        memcpy(packet_buffer, rx_buffer, 11);
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
    while (Serial2.available()) {
      uint8_t byte_received = Serial2.read();
      current_time = millis();
      
      // If we haven't received data for a while, this might be a new packet
      if (!packet_in_progress || (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
        if (packet_in_progress && buffer_index == 11) {
          // Previous complete packet available
          memcpy(packet_buffer, rx_buffer, 11);
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
        memcpy(packet_buffer, rx_buffer, 11);
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
    delay(1);
  }
}


void full_startup() {
// [00018486] RX: AA 82 07 00 00 00 00 00 00 00 00 (tx_startup) [11 bytes]
// [00018501] RX: AA 80 07 05 05 03 F2 00 0A 03 89 (rx_startup) [11 bytes]

// [00018605] RX: AA 82 07 00 00 00 00 00 00 00 00 (tx_startup) [11 bytes]
// [00019455] RX: AA 82 07 00 00 00 00 00 00 00 00 (tx_startup) [11 bytes]
// [00020306] RX: AA 82 07 00 00 00 00 00 00 00 00 (tx_startup) [11 bytes]

// [00020906] RX: AA 8E 09 01 00 00 00 00 00 00 00 (tx_powerup) [11 bytes]
// [00021006] RX: AA 05 AF 01 00 00 00 00 00 00 00 (tx_warmup_1) [11 bytes]
// [00021028] RX: AA 80 AF 52 45 50 31 41 46 30 35 (rx_warmup_1) [11 bytes]
// [00021135] RX: AA 05 B7 01 00 00 00 00 00 00 00 (tx_warmup_2) [11 bytes]
// [00021159] RX: AA 80 B7 42 41 41 37 33 30 33 00 (rx_warmup_2) [11 bytes]
// [00021265] RX: AA 05 BF 01 00 00 00 00 00 00 00 (tx_warmup_3) [11 bytes]
// [00021290] RX: AA 80 BF FF FF FF FF FF FF FF FF (rx_warmup_3) [11 bytes]

  send_tx_startup();
  receive_and_print("rx_startup", 500);

  delay(100);
  send_tx_startup();
  delay(900);
  send_tx_startup();
  delay(900);
  send_tx_startup();
  delay(600);
  send_tx_powerup();
  delay(100);  // Wait for powerup response
  send_tx_warmup_1();
  receive_and_print("rx_warmup_1", 500);
  delay(100);
  send_tx_warmup_2();
  receive_and_print("rx_warmup_2", 500);
  delay(100);
  send_tx_warmup_3();
  receive_and_print("rx_warmup_3", 500);
  delay(100);

  send_tx_heartbeat();
  receive_and_print("rx_warmup", 500);
  delay(200);

  send_tx_heartbeat();
  receive_and_print("rx_warmup", 500);
  delay(500);

  send_tx_heartbeat();
  delay(1000);

  send_tx_color_startup(0xFF, 0x00, 0x00); // Red
  receive_and_print("rx_color_startup", 500);
  delay(100);

  send_tx_led_startup(100); // Full brightness
  receive_and_print("rx_led_startup", 500);
  delay(100);

  send_tx_startup_comp();
  receive_and_print("rx_startup_comp", 500);
  delay(100);

  send_tx_heartbeat();
  receive_and_print("rx_warmup", 500);

}






void full_poweron() {
  send_tx_startup();
  receive_and_print("rx_startup", 500);
  delay(100);

  send_tx_startup();
  delay(900);

  send_tx_startup();
  delay(900);

  send_tx_startup();
  delay(600);

}



