#include "bus.h"
#include "known_packets.h"
#include <LittleFS.h>


uint8_t active_bus_id = -1;  // Global variable to track the currently active bus ID


// Constructor - initialize bus with ID and set pin assignments
Bus::Bus(uint8_t id) : bus_id(id), bus_state(BUS_OFFLINE), 
                       warm_on_at(0), active_seconds_last_save_at(0),
                       last_polled(0),
                       red(0x03), green(0xd5), blue(0xff), brightness(100), cartridge_active_seconds(0),
                       cartridge_warn_at_seconds(349200), auto_shut_off_after_seconds(18000) {
  // Set pin assignments based on bus ID
  if (bus_id == 0) {
    tx_pin = BUS_0_TX_PIN;
    rx_pin = BUS_0_RX_PIN;
    dir_pin = BUS_0_DIR_PIN;
    pow_pin = BUS_0_POW_PIN;
  } else if (bus_id == 1) {
#ifdef BUS_1_TX_PIN
    tx_pin = BUS_1_TX_PIN;
    rx_pin = BUS_1_RX_PIN;
    dir_pin = BUS_1_DIR_PIN;
    pow_pin = BUS_1_POW_PIN;
#else
    // Bus 1 not defined, set to invalid
    tx_pin = -1;
    rx_pin = -1;
    dir_pin = -1;
    pow_pin = -1;
#endif
  } else {
    // Invalid bus ID
    tx_pin = -1;
    rx_pin = -1;
    dir_pin = -1;
    pow_pin = -1;
  }
}

// Initialize the bus (set the initial state for the pins)
void Bus::init() {
  if (dir_pin == -1) {
    Serial.printf("Bus %d: Invalid pin configuration\n", bus_id);
    bus_state = BUS_ERROR;
    return;
  }

  bus_state = BUS_OFFLINE;
  
  pinMode(dir_pin, OUTPUT);
  digitalWrite(dir_pin, LOW);  // Start in receive mode

  if(pow_pin != -1) {
    Serial.printf("Bus %d: output pow pin low\n", bus_id);
    pinMode(pow_pin, OUTPUT);
    digitalWrite(pow_pin, LOW);  // Start the bus off powered off
  }
  
  // Load settings from filesystem
  load_settings();  
}

// Activate the bus (Power on the bus (if unpowered) and set as Serial1)
void Bus::activate() {
  if(bus_state == BUS_ERROR) {
    powerdown();  // Ensure we power down if in error state
    Serial.printf("Bus %d: Cannot activate bus in error state\n", bus_id);
    return;
  }

  digitalWrite(dir_pin, LOW);  // Start in receive mode

  if(bus_state == BUS_OFFLINE) {
    if(pow_pin != -1) {
      digitalWrite(pow_pin, HIGH);  // Power on the bus
      delay(1000); // Allow 1s for bus to power up
    }
    bus_state = BUS_POWERED;
  }

  // Initialize Serial1 for RS-485 communication
  if(active_bus_id != bus_id) {
    if (bus_id == 0 || (bus_id == 1 && tx_pin != -1)) {
      Serial1.begin(19200, SERIAL_8N1, rx_pin, tx_pin);  // Will detatch the previous pins if set
      // Clear any existing data
      while(Serial1.available()) {
        Serial1.read();
      }
      Serial.printf("Bus %d initialized successfully\n", bus_id);
      active_bus_id = bus_id;  // Set this bus as the active one
    } else {
      Serial.printf("Bus %d: Failed to initialize\n", bus_id);
      bus_state = BUS_ERROR;
    }
  }
}

// Power down (deactivate) the bus
// NOTE - Does not send the powerdown command to the repellers first, so if there is no power pin, the repellers will keep running. 
void Bus::powerdown() {
  if(bus_state != BUS_OFFLINE) {
    if(pow_pin != -1) {
      digitalWrite(pow_pin, LOW);  // Power on the bus
      Serial.printf("Bus %d: powerdown: pin set low\n", bus_id);
    } else {
      Serial.printf("Bus %d: powerdown: no power pin\n", bus_id);
    }
    bus_state = BUS_OFFLINE;
  } else {
    Serial.printf("Bus %d: powerdown: bus already offline\n", bus_id);
  }

  if(active_bus_id == bus_id) {
    active_bus_id = -1;
  }
}

// Transmit packet on this bus
void Bus::transmit(Packet *packet) {
  activate();  // Ensure the bus is active before transmitting
  
  if (!packet) {
    Serial.printf("Bus %d: Cannot transmit null packet\n", bus_id);
    return;
  }
  
  // Set RS-485 transceiver to transmit mode for this bus
  digitalWrite(dir_pin, HIGH);
  delayMicroseconds(20);  // Give DE time to enable
  
  // Send the packet
  Serial1.write(packet->data, sizeof(packet->data));
  Serial1.flush();  // Wait until transmission complete
  
  // Back to receive mode
  delayMicroseconds(20);  // Give time for transmission to complete
  digitalWrite(dir_pin, LOW);
  delay(100);  // Allow some time before next operation
}

// Helper function to receive and print a packet with timeout
bool Bus::receive_and_print(const char* expected_type, uint16_t timeout_ms) {
  Packet received_packet;
  
  if (receive_packet(received_packet, timeout_ms)) {
    received_packet.print();
    return true;
  } else {
    Serial.printf("Bus %d TIMEOUT: Expected %s but no response received\n", bus_id, expected_type);
    return false;
  }
}

// Packet-based receive function
bool Bus::receive_packet(Packet& packet, uint16_t timeout_ms) {
  static uint8_t rx_buffer[11];
  static size_t buffer_index = 0;
  static unsigned long last_byte_time = 0;
  static bool packet_in_progress = false;
  
  const unsigned long PACKET_TIMEOUT_MS = 8;  // Same as sniffer
  unsigned long start_time = millis();
  unsigned long current_time;
  
  // Ensure this bus is active and we're in receive mode
  activate();
  digitalWrite(dir_pin, LOW);
  
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
      
      // Check if this byte is 0xAA (sync byte) and we're not at the start of a packet
      if (byte_received == 0xAA && buffer_index > 0) {
        // We found a sync byte but we already have data in the buffer
        // This indicates extra bytes before the real packet
        Serial.printf("Bus %d: Found 0xAA at position %d, discarding %d bytes: ", bus_id, buffer_index, buffer_index);
        for (size_t i = 0; i < buffer_index; i++) {
          Serial.printf("%02X ", rx_buffer[i]);
        }
        Serial.println();
        
        // Reset buffer and start fresh with this 0xAA byte
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

// Fixed packet transmission functions
void Bus::send_tx_discover() {
  Packet packet;
  packet.setAsTxDiscover();
  transmit(&packet);
}

void Bus::send_tx_powerup() {
  Packet packet;
  packet.setAsTxPowerup();
  transmit(&packet);
}

void Bus::send_tx_powerdown() {
  Packet packet;
  packet.setAsTxPowerdown();
  transmit(&packet);
}

void Bus::send_tx_heartbeat(uint8_t address) {
  Packet packet;
  packet.setAsTxHeartbeat(address);
  transmit(&packet);
}

void Bus::send_tx_led_on_conf(uint8_t address) {
  Packet packet;
  packet.setAsTxLEDOnConf(address);
  transmit(&packet);
}

void Bus::send_tx_ser_no_1(uint8_t address) {
  Packet packet;
  packet.setAsTxSerNo1(address);
  transmit(&packet);
}

void Bus::send_tx_ser_no_2(uint8_t address) {
  Packet packet;
  packet.setAsTxSerNo2(address);
  transmit(&packet);
}

void Bus::send_tx_warmup(uint8_t address) {
  Packet packet;
  packet.setAsTxWarmup(address);
  transmit(&packet);
}

void Bus::send_tx_warmup_complete(uint8_t address) {
  Packet packet;
  packet.setAsTxWarmupComp(address);
  transmit(&packet);
}

void Bus::send_tx_led_brightness(uint8_t address, uint8_t brightness) {
  Packet packet;
  packet.setAsTxLED(address, brightness);
  transmit(&packet);
}

void Bus::send_tx_led_brightness_startup(uint8_t address, uint8_t brightness) {
  Packet packet;
  packet.setAsTxLEDStartup(address, brightness);
  transmit(&packet);
}

void Bus::send_tx_color(uint8_t red, uint8_t green, uint8_t blue) {
  // Note that this one broadcasts the color to all devices
  Packet packet;
  packet.setAsTxColor(red, green, blue);
  transmit(&packet);
}

void Bus::send_tx_color_confirm(uint8_t green, uint8_t blue) {
  // Send the color confirmation packet: AA 8E 03 08 YY ZZ 00 00 00 00 00
  // No idea why the green and blue values are in YY and ZZ, (and the red is missing) but that's how it is
  Packet packet;
  packet.setAsTxColorConfirm(green, blue);
  transmit(&packet);
}

void Bus::send_tx_color_startup(uint8_t address, uint8_t red, uint8_t green, uint8_t blue) {
  Packet packet;
  packet.setAsTxColorStartup(address, red, green, blue);
  transmit(&packet);
}

void Bus::send_tx_startup_comp(uint8_t address) {
  Packet packet;
  packet.setAsTxStartupComp(address);
  transmit(&packet);
}

void Bus::send_set_address(uint8_t address) {
  Packet packet;
  packet.setAsSetAddress(address);
  transmit(&packet);
}

// Repeller management functions
Repeller* Bus::get_repeller(uint8_t address) {
  for (auto& repeller : repellers) {
    if (repeller.address == address) {
      return &repeller;
    }
  }
  return nullptr;
}

Repeller* Bus::get_or_create_repeller(uint8_t address) {
  // First try to find existing repeller
  Repeller* existing = get_repeller(address);
  if (existing != nullptr) {
    return existing;
  }
  
  // Create new repeller and add to list
  repellers.emplace_back(address);
  return &repellers.back();
}

uint8_t Bus::find_next_address() {
  uint8_t next_address = 0x01;  // Start looking for available address from 0x01
  for(next_address = 0x01; next_address <= 0x1F; next_address++) {
    if (get_repeller(next_address) == nullptr) {
      // Found an available address
      return next_address;
    }
  }
  return 0x20; // Technically, this should be an error.
}


// Discover all repellers on the bus by sending broadcast tx_startup commands
void Bus::discover_repellers() {
  Serial.printf("Bus %d: Discovering repellers on the bus...\n", bus_id);
  
  Packet received_packet;
  int consecutive_no_response = 0;
  int total_discovered = 0;
  
  while (consecutive_no_response < 3) {
    // Send broadcast tx_startup
    Serial.printf("Bus %d: Sending tx_discover broadcast...\n", bus_id);
    send_tx_discover();
    
    // Wait for response with 100ms timeout
    if (receive_packet(received_packet, 100)) {
      if(received_packet.identifyPacket() == RX_STARTUP) {
        uint8_t device_address = received_packet.getAddress();
        Serial.printf("Bus %d: Discovered repeller at address 0x%02X\n", bus_id, device_address);
        
        // Create or get the repeller
        Repeller* repeller = get_or_create_repeller(device_address);
        repeller->state = INACTIVE;
        
        total_discovered++;
        consecutive_no_response = 0;  // Reset counter
        
      } else if(received_packet.identifyPacket() == RX_STARTUP_00) {
        // Special case for RX_STARTUP_00, which indicates the repeller is not set up yet
        Serial.printf("Bus %d: Received RX_STARTUP_00, indicating no address set yet\n", bus_id);


        // Find an address that isn't already taken
        uint8_t available_address = find_next_address();

        if(available_address == 0x20) {
          Serial.printf("Bus %d: No available addresses found for new repeller\n", bus_id);
          consecutive_no_response++;
          continue;  // Skip to next iteration
        }

        // Once we've found an available address, we can set it on the repeller
        Serial.printf("Bus %d: Setting repeller address to 0x%02X\n", bus_id, available_address);
        send_set_address(available_address);

        // I THINK there is a response here that I could read, which I THINK is an incomplete packet 
        if (receive_packet(received_packet, 500)) {
          Serial.printf("Bus %d: Received set response packet\n", bus_id);
          received_packet.print();
        }

        // The repeller should theoretically respond to the next tx_discover - but let's add it to the list now
        // so we don't try to create a duplicate.
        // Create a new repeller with the available address
        Repeller* repeller = get_or_create_repeller(available_address);
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
      Serial.printf("Bus %d: No response to tx_discover\n", bus_id);
      consecutive_no_response++;
    }
  }
  
  Serial.printf("Bus %d: Repeller discovery complete. Found %d devices.\n", bus_id, total_discovered);
  
  // Print discovered repellers
  if (total_discovered > 0) {
    Serial.printf("Bus %d: Discovered repellers:\n", bus_id);
    for (const auto& repeller : repellers) {
      Serial.printf("  Address: 0x%02X, State: %s\n", repeller.address, repeller.getStateString());
    }
  }
}

void Bus::retrieve_serial(Repeller* repeller) {
  if (!repeller) {
    Serial.printf("Bus %d: Invalid repeller pointer\n", bus_id);
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
          Serial.printf("Bus %d: Retrieved serial number: %s\n", bus_id, repeller->serial);
        } else {
          Serial.printf("Bus %d: Failed to retrieve serial number part 2\n", bus_id);
        }
      } else {
        Serial.printf("Bus %d: No response for tx_ser_no_2\n", bus_id);
      }
    } else {
      Serial.printf("Bus %d: Failed to retrieve serial number part 1\n", bus_id);
    }
  } else {
    Serial.printf("Bus %d: No response for tx_ser_no_1\n", bus_id);
  }
}

void Bus::send_tx_warmup(Repeller* repeller) {
  if (!repeller) {
    Serial.printf("Bus %d: Invalid repeller pointer\n", bus_id);
    return;
  }
  
  // Send tx_warmup and wait for response
  send_tx_warmup(repeller->address);
  Packet response_packet;
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_WARMUP) {
      Serial.printf("Bus %d: Repeller 0x%02X warming up\n", bus_id, repeller->address);
    } else {
      // TODO - Print the packet here
      Serial.printf("Bus %d: Invalid response packet received\n", bus_id);
    }
  } else {
    Serial.printf("Bus %d: Repeller 0x%02X failed to respond to TX_WARMUP\n", bus_id, repeller->address);
  }
}

void Bus::send_startup_led_params(Repeller* repeller) {
  if (!repeller) {
    Serial.printf("Bus %d: Invalid repeller pointer\n", bus_id);
    return;
  }
  
  Serial.printf("Bus %d: Setting startup LED parameters for repeller 0x%02X...\n", bus_id, repeller->address);
  
  // There are three parts to this:
  // 1. Send tx_color_startup with red, green, blue values and then look for rx_color_startup
  Serial.printf("Bus %d: Setting startup color for repeller 0x%02X...\n", bus_id, repeller->address);
  // Use the configured color from settings
  send_tx_color_startup(repeller->address, repeller_red(), repeller_green(), repeller_blue());
  
  Packet response_packet;
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_COLOR_STARTUP) {
      // Note - The response contains the color values as well (I think??), but the colors do not necessarily match what we sent
      // For now, I'm just ignoring them.
      Serial.printf("Bus %d: Repeller 0x%02X confirmed startup color\n", bus_id, repeller->address);
    } else {
      Serial.printf("Bus %d: Repeller 0x%02X sent unexpected color response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("Bus %d: No color startup response from repeller 0x%02X\n", bus_id, repeller->address);
  }
  
  // 2. Send tx_led_brightness_startup with brightness value and then look for rx_led_brightness_startup
  Serial.printf("Bus %d: Setting startup brightness for repeller 0x%02X...\n", bus_id, repeller->address);
  // Use the configured brightness from settings
  send_tx_led_brightness_startup(repeller->address, repeller_brightness());
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_LED_BRIGHTNESS_STARTUP) {
      Serial.printf("Bus %d: Repeller 0x%02X confirmed startup brightness\n", bus_id, repeller->address);
    } else {
      Serial.printf("Bus %d: Repeller 0x%02X sent unexpected brightness response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("Bus %d: No LED startup response from repeller 0x%02X\n", bus_id, repeller->address);
  }
  
  // 3. Send tx_startup_comp and then look for rx_startup_comp
  Serial.printf("Bus %d: Sending startup complete to repeller 0x%02X...\n", bus_id, repeller->address);
  send_tx_startup_comp(repeller->address);
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_STARTUP_COMP) {
      Serial.printf("Bus %d: Repeller 0x%02X confirmed startup complete\n", bus_id, repeller->address);
    } else {
      Serial.printf("Bus %d: Repeller 0x%02X sent unexpected startup complete response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("Bus %d: No startup complete response from repeller 0x%02X\n", bus_id, repeller->address);
  }
  
  Serial.printf("Bus %d: LED parameter setup complete for repeller 0x%02X\n", bus_id, repeller->address);
}

void Bus::send_led_on_to_repeller(Repeller *repeller) {
  Packet response_packet;
  // 2. send_tx_led_on_conf and look for RX_LED_ON_CONF
  Serial.printf("Bus %d: Sending LED on confirmation to repeller 0x%02X...\n", bus_id, repeller->address);
  send_tx_led_on_conf(repeller->address);
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_LED_ON_CONF) {
      Serial.printf("Bus %d: Repeller 0x%02X confirmed LED activation\n", bus_id, repeller->address);
    } else {
      Serial.printf("Bus %d: Repeller 0x%02X sent unexpected LED on response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("Bus %d: No LED on confirmation response from repeller 0x%02X\n", bus_id, repeller->address);
  }
}

void Bus::send_activate_at_end_of_warmup(Repeller *repeller) {
  if (!repeller) {
    Serial.printf("Bus %d: Invalid repeller pointer\n", bus_id);
    return;
  }

  Serial.printf("Bus %d: Activating repeller 0x%02X at end of warmup...\n", bus_id, repeller->address);

  // This function is called after every repeller has been warmed up to actually activate the repeller
  // Not sure if this does anything other than turn on the LEDs, but that's enough!

  // 1. send_tx_warmup_complete and look for RX_WARMUP_COMPLETE
  Serial.printf("Bus %d: Sending warmup complete to repeller 0x%02X...\n", bus_id, repeller->address);
  send_tx_warmup_complete(repeller->address);
  
  Packet response_packet;
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_WARMUP_COMPLETE) {
      Serial.printf("Bus %d: Repeller 0x%02X confirmed warmup complete\n", bus_id, repeller->address);
    } else {
      Serial.printf("Bus %d: Repeller 0x%02X sent unexpected warmup complete response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("Bus %d: No warmup complete response from repeller 0x%02X\n", bus_id, repeller->address);
  }
  
  // 2. send_tx_led_on_conf and look for RX_LED_ON_CONF
  send_led_on_to_repeller(repeller);
  
  Serial.printf("Bus %d: Activation complete for repeller 0x%02X\n", bus_id, repeller->address);
}

void Bus::retrieve_serial_for_all() {
  Serial.printf("Bus %d: Retrieving serial numbers for all discovered repellers...\n", bus_id);
  
  for (auto& repeller : repellers) {
    if(strlen(repeller.serial) > 0) {
      // We only need to retrieve serial once
      Serial.printf("Bus %d: Repeller at address 0x%02X already has serial: %s\n", bus_id, repeller.address, repeller.serial);
      continue;  // Skip if serial already retrieved
    } else {
      Serial.printf("Bus %d: Retrieving serial for repeller at address 0x%02X...\n", bus_id, repeller.address);
      retrieve_serial(&repeller);
    }
  }
  
  Serial.printf("Bus %d: Serial retrieval complete.\n", bus_id);
}

void Bus::warm_up_all() {
  Serial.printf("Bus %d: Warming up all repellers...\n", bus_id);

  send_tx_powerup();  // This is the command that actually powers up the repellers

  warm_on_at = esp_timer_get_time();  // Record the time when the repellers were turned on
  active_seconds_last_save_at = warm_on_at;  // Initialize the last save time to the warm on time
  bus_state = BUS_WARMING_UP;
  
  for (auto& repeller : repellers) {
    repeller.state = WARMING_UP;
    repeller.turned_on_at = esp_timer_get_time();  // Record the time when the repeller was turned on
    Serial.printf("Bus %d: Sending warmup instruction to repeller at address 0x%02X...\n", bus_id, repeller.address);
    send_tx_warmup(&repeller);  // Not sure entirely what this does
  }
  
  delay(4000);  // Wait for 4 seconds to allow warmup to start

  for (auto& repeller : repellers) {
    Serial.printf("Bus %d: Sending LED parameters to repeller at address 0x%02X...\n", bus_id, repeller.address);
    send_startup_led_params(&repeller);
  }

  Serial.printf("Bus %d: Sent warmup & initial LED instructions to all repellers.\n", bus_id);
}

void Bus::end_warm_up_all() {
  Serial.printf("Bus %d: Activating all repellers (ending warm up)...\n", bus_id);

  send_tx_powerup();  // This is the command that actually powers up the repellers
  
  for (auto& repeller : repellers) {
    repeller.state = ACTIVE;
    Serial.printf("Bus %d: Activating (ending warm up) repeller at address 0x%02X...\n", bus_id, repeller.address);
    send_activate_at_end_of_warmup(&repeller);
  }

  bus_state = BUS_REPELLING;
  Serial.printf("Bus %d: Activated all repellers.\n", bus_id);
}

bool Bus::heartbeat_poll() {
  // Send the heartbeat command (`send_tx_heartbeat`) to each repeller, and read the response. Update the status of each repeller based on the response.
  // If the response is RX_WARMUP, the state is WARMING_UP.
  // If the response is RX_WARMUP_COMP, the state is WARMED_UP
  // If the response is RX_ACTIVE, the state is ACTIVE
  // If no response is received, the state is OFFLINE. We'll need to add handling for this later. 

  Serial.printf("Bus %d: Starting heartbeat poll...\n", bus_id);
  
  for (auto& repeller : repellers) {
    Serial.printf("Bus %d: Sending heartbeat to repeller 0x%02X...\n", bus_id, repeller.address);
    
    // Send heartbeat command
    send_tx_heartbeat(repeller.address);
    
    // Wait for response
    Packet response_packet;
    if (receive_packet(response_packet, 1000)) {
      PacketType packet_type = response_packet.identifyPacket();

      // For now, output the packet itself to the console
      Serial.printf("Bus %d: Received response from repeller 0x%02X: ", bus_id, repeller.address);
      response_packet.print();
      
      switch (packet_type) {
        case RX_WARMUP:
          repeller.state = WARMING_UP;
          Serial.printf("Bus %d: Repeller 0x%02X is warming up\n", bus_id, repeller.address);
          break;
          
        case RX_WARMUP_COMP:
          repeller.state = WARMED_UP;
          Serial.printf("Bus %d: Repeller 0x%02X is warmed up\n", bus_id, repeller.address);
          break;
          
        case RX_HEARTBEAT_RUNNING:  // Assuming RX_HEARTBEAT_RUNNING means ACTIVE
          repeller.state = ACTIVE;
          Serial.printf("Bus %d: Repeller 0x%02X is active\n", bus_id, repeller.address);
          break;
          
        default:
          Serial.printf("Bus %d: Repeller 0x%02X sent unexpected response: ", bus_id, repeller.address);
          response_packet.print();
          break;
      }
    } else {
      // No response received - state remains as is for now
      Serial.printf("Bus %d: No response from repeller 0x%02X\n", bus_id, repeller.address);
    }
  }

  // Once we have finished the heartbeat poll, loop over each repeller in the list. If any repeller is in the WARMING_UP state, set any_warming_up to true.
  // If any repeller is in the WARMED_UP state, set any_warmed_up to true.
  // Once this is done, we'll need to handle actually setting the controllers to active when any_warmed_up is true and any_warming_up is false. We'll do this later.
  bool any_warming_up = false;
  bool any_warmed_up = false;
  bool any_active = false;
  
  for (const auto& repeller : repellers) {
    if (repeller.state == WARMING_UP) {
      any_warming_up = true;
    } else if (repeller.state == ACTIVE) {
      any_active = true;
    } else if (repeller.state == WARMED_UP) {
      any_warmed_up = true;
    }
  }

  if(!any_warming_up && !any_warmed_up) {
    return true;
  } else if(!any_warming_up && any_warmed_up) {
    // All repellers are warmed up and none are warming up, so we can activate them
    Serial.printf("Bus %d: All repellers warmed up, activating...\n", bus_id);
    end_warm_up_all();
  } else{
    Serial.printf("Bus %d: Heartbeat poll complete. Warming up: %s, Warmed up: %s\n", bus_id,
                  any_warming_up ? "true" : "false", 
                  any_warmed_up ? "true" : "false");
  }

  return false; 
}

void Bus::poll() {
  bool heartbeat = false;  // Track if we successfully polled the heartbeat
  unsigned long current_time = millis();
    
  if (current_time - last_polled > BUS_POLLING_INTERVAL_MS) {
    Serial.println("Sending periodic heartbeat...");
    heartbeat = heartbeat_poll();  // Poll the heartbeat status of all repellers on the bus
    last_polled = current_time;
  }

}


void Bus::change_led_brightness(uint8_t brightness_pct) {
  // This function broadcasts the LED brightness change to all devices
  Serial.printf("Bus %d: Changing LED brightness to %d%% for all devices...\n", bus_id, brightness_pct);

  // 1. send_tx_led_brightness for each repeller with the specified brightness, and look for RX_LED_BRIGHTNESS
  for (auto& repeller : repellers) {
    if (repeller.state == ACTIVE) {
      Serial.printf("Bus %d: Setting brightness to %d%% for repeller 0x%02X...\n", bus_id, brightness_pct, repeller.address);
      send_tx_led_brightness(repeller.address, brightness_pct);
      
      Packet response_packet;
      if (receive_packet(response_packet, 1000)) {
        if (response_packet.identifyPacket() == RX_LED_BRIGHTNESS) {
          Serial.printf("Bus %d: Repeller 0x%02X confirmed brightness change\n", bus_id, repeller.address);
          send_led_on_to_repeller(&repeller);  // Trigger the repeller actually using the new brightness
        } else {
          Serial.printf("Bus %d: Repeller 0x%02X sent unexpected brightness response: ", bus_id, repeller.address);
          response_packet.print();
        }
      } else {
        Serial.printf("Bus %d: No brightness response from repeller 0x%02X\n", bus_id, repeller.address);
      }
    } else {
      Serial.printf("Bus %d: Skipping repeller 0x%02X (not active, state: %s)\n", bus_id, repeller.address, repeller.getStateString());
    }
  }
  
  Serial.printf("Bus %d: LED brightness change complete.\n", bus_id);
}

void Bus::change_led_color(uint8_t red, uint8_t green, uint8_t blue) {
  // This function broadcasts the LED color change to all devices
  Serial.printf("Bus %d: Changing LED color to R:%d G:%d B:%d for all devices...\n", bus_id, red, green, blue);

  // 1. Send the broadcast color command
  send_tx_color(red, green, blue);
  Serial.printf("Bus %d: Sent broadcast color change command\n", bus_id);
  
  // 2. Send the color confirmation command (AA 8E 03 08 YY ZZ ...)
  send_tx_color_confirm(green, blue);
  Serial.printf("Bus %d: Sent color confirmation command\n", bus_id);
  
  Serial.printf("Bus %d: Color change complete.\n", bus_id);
}

void Bus::shutdown_all() {
  Serial.printf("Bus %d: Shutting down all repellers...\n", bus_id);
  
  // 1. Send send_tx_powerdown
  send_tx_powerdown();
  Serial.printf("Bus %d: Sent powerdown command to all repellers\n", bus_id);
  
  // 2. Loop through all repellers and set their state to OFFLINE
  for (auto& repeller : repellers) {
    repeller.state = OFFLINE;
    Serial.printf("Bus %d: Set repeller 0x%02X to OFFLINE state\n", bus_id, repeller.address);
  }

  // 3. Power down the bus itself
  powerdown();
  
  Serial.printf("Bus %d: All repellers shut down.\n", bus_id);
}

// Load settings from filesystem
void Bus::load_settings() {
  String filename = "/bus" + String(bus_id) + "_settings.dat";
  
  if (!LittleFS.exists(filename)) {
    Serial.printf("Bus %d: Settings file not found, using defaults\n", bus_id);
    return; // Use default values set in constructor
  }
  
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("Bus %d: Failed to open settings file, using defaults\n", bus_id);
    return;
  }
  
  // Read settings in order
  if (file.available() >= sizeof(uint8_t)) {
    file.read((uint8_t*)&red, sizeof(uint8_t));
  }
  
  if (file.available() >= sizeof(uint8_t)) {
    file.read((uint8_t*)&green, sizeof(uint8_t));
  }
  
  if (file.available() >= sizeof(uint8_t)) {
    file.read((uint8_t*)&blue, sizeof(uint8_t));
  }
  
  if (file.available() >= sizeof(uint8_t)) {
    file.read((uint8_t*)&brightness, sizeof(uint8_t));
    if (brightness > 254) brightness = 254; // Validate range
  }
  
  if (file.available() >= sizeof(uint32_t)) {
    file.read((uint8_t*)&cartridge_active_seconds, sizeof(uint32_t));
  }
  
  if (file.available() >= sizeof(uint32_t)) {
    file.read((uint8_t*)&cartridge_warn_at_seconds, sizeof(uint32_t));
  }
  
  if (file.available() >= sizeof(uint16_t)) {
    file.read((uint8_t*)&auto_shut_off_after_seconds, sizeof(uint16_t));
    if (auto_shut_off_after_seconds > 57600) auto_shut_off_after_seconds = 18000; // Validate range
  }
  
  file.close();
  Serial.printf("Bus %d: Settings loaded from filesystem\n", bus_id);
}

// Save settings to filesystem
void Bus::save_settings() {
  String filename = "/bus" + String(bus_id) + "_settings.dat";
  
  File file = LittleFS.open(filename, "w");
  if (!file) {
    Serial.printf("Bus %d: Failed to open settings file for writing\n", bus_id);
    return;
  }
  
  // Write settings in order
  file.write((uint8_t*)&red, sizeof(uint8_t));
  file.write((uint8_t*)&green, sizeof(uint8_t));
  file.write((uint8_t*)&blue, sizeof(uint8_t));
  file.write((uint8_t*)&brightness, sizeof(uint8_t));
  file.write((uint8_t*)&cartridge_active_seconds, sizeof(uint32_t));
  file.write((uint8_t*)&cartridge_warn_at_seconds, sizeof(uint32_t));
  file.write((uint8_t*)&auto_shut_off_after_seconds, sizeof(uint16_t));
  
  file.close();
  Serial.printf("Bus %d: Settings saved to filesystem\n", bus_id);
}

// Zigbee interface methods
void Bus::ZigbeeSetRGB(uint8_t zb_red, uint8_t zb_green, uint8_t zb_blue) {
  if(zb_red != red || zb_green != green || zb_blue != blue) {
    red = zb_red;
    green = zb_green;
    blue = zb_blue;
    save_settings();
    Serial.printf("Bus %d: RGB set to (%d, %d, %d)\n", bus_id, red, green, blue);
  } else {
    Serial.printf("Bus %d: RGB already set to (%d, %d, %d), no changes made\n", bus_id, red, green, blue);
  }
}

void Bus::ZigbeeSetBrightness(uint8_t new_brightness) {
  if (new_brightness > 254) {
    Serial.printf("Bus %d: Invalid brightness value %d, must be 0-254\n", bus_id, new_brightness);
    return;
  }
  if(brightness != new_brightness) {
    brightness = new_brightness;
    save_settings();
    Serial.printf("Bus %d: Brightness set to %d\n", bus_id, brightness);
  } else {
    Serial.printf("Bus %d: Brightness already set to %d, no changes made", bus_id, brightness);
  }
}

void Bus::ZigbeeResetCartridge() {
  cartridge_active_seconds = 0;
  save_settings();
  Serial.printf("Bus %d: Cartridge reset, active seconds set to 0\n", bus_id);
}

void Bus::ZigbeeSetCartridgeWarnAtSeconds(uint32_t seconds) {
  if(cartridge_warn_at_seconds != seconds) {
    cartridge_warn_at_seconds = seconds;
    save_settings();
    Serial.printf("Bus %d: Cartridge warn time set to %lu seconds\n", bus_id, seconds);
  } else {
    Serial.printf("Bus %d: Cartridge warn time already set to %lu seconds, no changes made\n", bus_id, cartridge_warn_at_seconds);
  }
}

void Bus::ZigbeeSetAutoShutOffAfterSeconds(uint16_t seconds) {
  if (seconds > 57600) {
    Serial.printf("Bus %d: Invalid auto shut-off value %d, must be 0-57600\n", bus_id, seconds);
    return;
  }

  if(auto_shut_off_after_seconds != seconds) {
    auto_shut_off_after_seconds = seconds;
    save_settings();
    Serial.printf("Bus %d: Auto shut-off set to %d seconds\n", bus_id, seconds);
  } else {
    Serial.printf("Bus %d: Auto shut-off already set to %d seconds, no changes made\n", bus_id, auto_shut_off_after_seconds);
  }
}

// Helper conversion methods
uint8_t Bus::repeller_brightness() {
  // Convert Zigbee brightness (0-254) to repeller brightness (0-100)
  return (uint8_t)round((brightness * 100.0) / 254.0);
}

uint8_t Bus::repeller_red() {
  return red;
}

uint8_t Bus::repeller_green() {
  return green;
}

uint8_t Bus::repeller_blue() {
  return blue;
}

void Bus::save_active_seconds() {
  if (bus_state == BUS_WARMING_UP || bus_state == BUS_REPELLING) {
    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_microseconds = current_time - active_seconds_last_save_at;
    uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
    
    cartridge_active_seconds += elapsed_seconds;
    active_seconds_last_save_at = current_time;
    save_settings();
    
    Serial.printf("Bus %d: Active seconds updated: %lu total\n", bus_id, cartridge_active_seconds);
  }
}

bool Bus::past_automatic_shutoff() {
  if (auto_shut_off_after_seconds == 0) {
    return false; // Auto shut-off disabled
  }
  
  if (warm_on_at == 0) {
    return false; // Not warmed up yet
  }
  
  uint64_t current_time = esp_timer_get_time();
  uint64_t elapsed_microseconds = current_time - warm_on_at;
  uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
  
  return elapsed_seconds >= auto_shut_off_after_seconds;
}

void Bus::ZigbeePowerOn() {
  Serial.printf("Bus %d: Zigbee power on command received\n", bus_id);
  
  // Activate the bus if it's offline
  if (bus_state == BUS_OFFLINE) {
    activate();
  }
  
  // If bus is just powered, then discover repellers, retrieve serial numbers, and warm up
  if (bus_state == BUS_POWERED) {
    discover_repellers();
    retrieve_serial_for_all();
    warm_up_all();
  }
  
  Serial.printf("Bus %d: Zigbee power on sequence initiated\n", bus_id);
}

void Bus::ZigbeePowerOff() {
  Serial.printf("Bus %d: Zigbee power off command received\n", bus_id);
  
  // Save any remaining active seconds before shutdown
  save_active_seconds();
  
  // Shutdown all repellers and the bus
  shutdown_all();
  
  Serial.printf("Bus %d: Zigbee power off completed\n", bus_id);
}

// Cartridge monitoring methods
uint16_t Bus::get_cartridge_runtime_hours() {
  // Update active seconds if currently running
  if (bus_state == BUS_WARMING_UP || bus_state == BUS_REPELLING) {
    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_microseconds = current_time - active_seconds_last_save_at;
    uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
    uint32_t total_seconds = cartridge_active_seconds + elapsed_seconds;
    return total_seconds / 3600; // Convert to hours
  }
  
  return cartridge_active_seconds / 3600; // Convert to hours
}

uint8_t Bus::get_cartridge_percent_left() {
  uint32_t total_active_seconds = cartridge_active_seconds;
  
  // Add current session time if running
  if (bus_state == BUS_WARMING_UP || bus_state == BUS_REPELLING) {
    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_microseconds = current_time - active_seconds_last_save_at;
    uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
    total_active_seconds += elapsed_seconds;
  }
  
  // Calculate percentage remaining
  if (cartridge_warn_at_seconds == 0) {
    return 100; // No limit set, always 100%
  }
  
  if (total_active_seconds >= cartridge_warn_at_seconds) {
    return 0; // Cartridge expired
  }
  
  uint32_t remaining_seconds = cartridge_warn_at_seconds - total_active_seconds;
  uint8_t percent_left = (remaining_seconds * 100) / cartridge_warn_at_seconds;
  
  return percent_left;
}
