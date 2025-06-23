#include "tx_controller.h"
#include "known_packets.h"
#include "repeller.h"
#include "packet.h"
#include "receive.h"

void tx_controller_init() {
  pinMode(RS485_DIRECTION_PIN, OUTPUT);
  digitalWrite(RS485_DIRECTION_PIN, LOW);  // Start in receive mode
  
  // Initialize Serial2 for RS-485 communication
  Serial2.begin(19200, SERIAL_8N1, 16, 17);  // RX=16, TX=17
  
  // Clear any existing data
  while(Serial2.available()) {
    Serial2.read();
  }
}


// Fixed packet transmission functions
void send_tx_discover() {
  Packet packet;
  packet.setAsTxDiscover();
  packet.transmit();
}

// void send_tx_startup_comp() {
//   transmit_packet(tx_startup_comp, sizeof(tx_startup_comp));
// }

void send_tx_powerup() {
  Packet packet;
  packet.setAsTxPowerup();
  packet.transmit();
}

// void send_tx_powerdown() {
//   Packet(tx_powerdown).transmit();
// }

// void send_tx_heartbeat() {
//   transmit_packet(tx_heartbeat, sizeof(tx_heartbeat));
// }

// void send_tx_led_on_conf() {
//   transmit_packet(tx_led_on_conf, sizeof(tx_led_on_conf));
// }

void send_tx_ser_no_1(uint8_t address) {
  Packet packet;
  packet.setAsTxSerNo1(address);
  packet.transmit();
}

void send_tx_ser_no_2(uint8_t address) {
  Packet packet;
  packet.setAsTxSerNo2(address);
  packet.transmit();
}

void send_tx_warmup(uint8_t address) {
  Packet packet;
  packet.setAsTxWarmup(address);
  packet.transmit();
}

// void send_tx_warmup_complete() {
//   transmit_packet(tx_warmup_complete, sizeof(tx_warmup_complete));
// }

// // Dynamic packet transmission functions
// void send_tx_led(uint8_t brightness) {
//   uint8_t packet[] = {0xAA, 0x05, 0x05, brightness, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
//   transmit_packet(packet, sizeof(packet));
// }

// void send_tx_led_startup(uint8_t brightness) {
//   uint8_t packet[] = {0xAA, 0x05, 0x05, brightness, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
//   transmit_packet(packet, sizeof(packet));
// }

// void send_tx_color(uint8_t red, uint8_t green, uint8_t blue) {
//   uint8_t packet[] = {0xAA, 0x8E, 0x06, red, green, blue, 0x00, 0x00, 0x00, 0x00, 0x00};
//   transmit_packet(packet, sizeof(packet));
// }

// void send_tx_color_startup(uint8_t red, uint8_t green, uint8_t blue) {
//   uint8_t packet[] = {0xAA, 0x05, 0x06, red, green, blue, 0x00, 0x00, 0x00, 0x00, 0x00};
//   transmit_packet(packet, sizeof(packet));
// }



// void full_startup() {
//   send_tx_powerup();
//   delay(100);  // Wait for powerup response
//   send_tx_ser_no_1();
//   receive_and_print("rx_ser_no_1", 500);
//   delay(100);
//   send_tx_ser_no_2();
//   receive_and_print("rx_ser_no_2", 500);
//   delay(100);
//   send_tx_warmup();
//   receive_and_print("rx_warmup", 500);
//   delay(100);

//   send_tx_heartbeat();
//   receive_and_print("rx_warmup", 500);
//   delay(200);

//   send_tx_heartbeat();
//   receive_and_print("rx_warmup", 500);
//   delay(500);

//   send_tx_heartbeat();
//   delay(1000);

//   send_tx_color_startup(0xFF, 0x00, 0x00); // Red
//   receive_and_print("rx_color_startup", 500);
//   delay(100);

//   send_tx_led_startup(100); // Full brightness
//   receive_and_print("rx_led_startup", 500);
//   delay(100);

//   send_tx_startup_comp();
//   receive_and_print("rx_startup_comp", 500);
//   delay(100);

//   send_tx_heartbeat();
//   receive_and_print("rx_warmup", 500);

// }




// Discover all repellers on the bus by sending broadcast tx_startup commands
void discover_repellers() {
  Serial.println("Discovering repellers on the bus...");
  
  Packet received_packet;
  int consecutive_no_response = 0;
  int total_discovered = 0;
  
  while (consecutive_no_response < 3) {
    // Send broadcast tx_startup
    Serial.println("Sending tx_discover broadcast...");
    send_tx_discover();
    
    // Wait for response with 100ms timeout
    if (receive_packet(received_packet, 100)) {
      if(received_packet.identifyPacket() == RX_STARTUP) {
        uint8_t device_address = received_packet.getAddress();
        Serial.printf("Discovered repeller at address 0x%02X\n", device_address);
        
        // Create or get the repeller
        Repeller* repeller = get_or_create_repeller(device_address);
        repeller->state = INACTIVE;
        
        total_discovered++;
        consecutive_no_response = 0;  // Reset counter
        
      } else {
        // Received packet but not rx_startup, print it for debugging
        received_packet.print();
        consecutive_no_response++;
      }
    } else {
      // No response received
      Serial.println("No response to tx_discover");
      consecutive_no_response++;
    }
  }
  
  Serial.printf("Repeller discovery complete. Found %d devices.\n", total_discovered);
  
  // Print discovered repellers
  if (total_discovered > 0) {
    Serial.println("Discovered repellers:");
    for (const auto& repeller : repellers) {
      Serial.printf("  Address: 0x%02X, State: %s\n", repeller.address, repeller.getStateString());
    }
  }
}


void retrieve_serial(Repeller* repeller) {
  if (!repeller) {
    Serial.println("Invalid repeller pointer");
    return;
  }
  
  // Send tx_ser_no_1 and wait for response
  send_tx_ser_no_1(repeller->address);
  Packet response_packet;
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_SER_NO_1) {
      // Extract serial number part 1
      char serial_part1[9];
      for (int i = 0; i < 8; i++) {
        serial_part1[i] = (response_packet.data[i + 3] >= 32 && response_packet.data[i + 3] <= 126) ? response_packet.data[i + 3] : '.';
      }
      serial_part1[8] = '\0'; // Null-terminate the string
      
      // Send tx_ser_no_2 and wait for response
      send_tx_ser_no_2(repeller->address);
      
      if (receive_packet(response_packet, 1000)) {
        if (response_packet.identifyPacket() == RX_SER_NO_2) {
          // Extract serial number part 2
          char serial_part2[9];
          for (int i = 0; i < 8; i++) {
            serial_part2[i] = (response_packet.data[i + 3] >= 32 && response_packet.data[i + 3] <= 126) ? response_packet.data[i + 3] : '.';
          }
          serial_part2[8] = '\0'; // Null-terminate the string
          
          // Combine both parts into the repeller's serial
          repeller->setSerial(serial_part1, serial_part2);
          Serial.printf("Retrieved serial number: %s\n", repeller->serial);
        } else {
          Serial.println("Failed to retrieve serial number part 2");
        }
      } else {
        Serial.println("No response for tx_ser_no_2");
      }
    } else {
      Serial.println("Failed to retrieve serial number part 1");
    }
  } else {
    Serial.println("No response for tx_ser_no_1");
  }

}

void send_tx_warmup(Repeller* repeller) {
  if (!repeller) {
    Serial.println("Invalid repeller pointer");
    return;
  }
  
  // Send tx_ser_no_1 and wait for response
  send_tx_warmup(repeller->address);
  Packet response_packet;
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_WARMUP) {
      Serial.printf("Repeller 0x%02X warming up\n", repeller->address);
    } else {
      // TODO - Print the packet here
      Serial.printf("Invalid response packet received\n");
    }
  } else {
    Serial.printf("Repeller 0x%02X failed to respond to TX_WARMUP\n", repeller->address);
  }

}

void retrieve_serial_for_all() {
  Serial.println("Retrieving serial numbers for all discovered repellers...");
  
  for (auto& repeller : repellers) {
    if(strlen(repeller.serial) > 0) {
      // We only need to retrieve serial once
      Serial.printf("Repeller at address 0x%02X already has serial: %s\n", repeller.address, repeller.serial);
      continue;  // Skip if serial already retrieved
    } else {
      Serial.printf("Retrieving serial for repeller at address 0x%02X...\n", repeller.address);
      retrieve_serial(&repeller);
    }
  }
  
  Serial.println("Serial retrieval complete.");
}


void warm_up_all() {
  Serial.println("Warming up all repellers...");

  send_tx_powerup();  // This is the command that actually powers up the repellers
  
  for (auto& repeller : repellers) {
    Serial.printf("Sending warmup instruction to repeller at address 0x%02X...\n", repeller.address);
    send_tx_warmup(&repeller);  // Not sure entirely what this does
  }
  
  Serial.println("Sent warmup to all repellers.");
}

