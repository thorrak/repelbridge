#include "tx_controller.h"
#include "known_packets.h"
#include "repeller.h"
#include "packet.h"
#include "receive.h"

void tx_controller_init(uint8_t bus) {
  pinMode(BUS_0_DIR_PIN, OUTPUT);
  digitalWrite(BUS_0_DIR_PIN, LOW);  // Start in receive mode
  
  // Initialize Serial1 for RS-485 communication
#ifdef BUS_1_TX_PIN
  pinMode(BUS_1_DIR_PIN, OUTPUT);
  digitalWrite(BUS_1_DIR_PIN, LOW);  // Start in receive mode

  if(bus == 2) 
    Serial1.begin(19200, SERIAL_8N1, BUS_1_RX_PIN, BUS_1_TX_PIN);
  else
#endif
    Serial1.begin(19200, SERIAL_8N1, BUS_0_RX_PIN, BUS_0_TX_PIN);
  // Clear any existing data
  while(Serial1.available()) {
    Serial1.read();
  }
}


// Fixed packet transmission functions
void send_tx_discover() {
  Packet packet;
  packet.setAsTxDiscover();
  packet.transmit();
}

void send_tx_powerup() {
  Packet packet;
  packet.setAsTxPowerup();
  packet.transmit();
}

void send_tx_powerdown() {
  Packet packet;
  packet.setAsTxPowerdown();
  packet.transmit();
}

void send_tx_heartbeat(uint8_t address) {
  Packet packet;
  packet.setAsTxHeartbeat(address);
  packet.transmit();
}

void send_tx_led_on_conf(uint8_t address) {
  Packet packet;
  packet.setAsTxLEDOnConf(address);
  packet.transmit();
}

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

void send_tx_warmup_complete(uint8_t address) {
  Packet packet;
  packet.setAsTxWarmupComp(address);
  packet.transmit();
}

void send_tx_led_brightness(uint8_t address, uint8_t brightness) {
  Packet packet;
  packet.setAsTxLED(address, brightness);
  packet.transmit();
}

void send_tx_led_brightness_startup(uint8_t address, uint8_t brightness) {
  Packet packet;
  packet.setAsTxLEDStartup(address, brightness);
  packet.transmit();
}

void send_tx_color(uint8_t red, uint8_t green, uint8_t blue) {
  // Note that this one broadcasts the color to all devices
  Packet packet;
  packet.setAsTxColor(red, green, blue);
  packet.transmit();
}

void send_tx_color_confirm(uint8_t green, uint8_t blue) {
  // Send the color confirmation packet: AA 8E 03 08 YY ZZ 00 00 00 00 00
  // No idea why the green and blue values are in YY and ZZ, (and the red is missing) but that's how it is
  Packet packet;
  packet.setAsTxColorConfirm(green, blue);
  packet.transmit();
}

void send_tx_color_startup(uint8_t address, uint8_t red, uint8_t green, uint8_t blue) {
  Packet packet;
  packet.setAsTxColorStartup(address, red, green, blue);
  packet.transmit();
}

void send_tx_startup_comp(uint8_t address) {
  Packet packet;
  packet.setAsTxStartupComp(address);
  packet.transmit();
}


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

void send_startup_led_params(Repeller* repeller) {
  if (!repeller) {
    Serial.println("Invalid repeller pointer");
    return;
  }
  
  Serial.printf("Setting startup LED parameters for repeller 0x%02X...\n", repeller->address);
  
  // There are three parts to this:
  // 1. Send tx_color_startup with red, green, blue values and then look for rx_color_startup
  Serial.printf("Setting startup color for repeller 0x%02X...\n", repeller->address);
  // TODO - Come back later when we productionize this, and actually cache the controller color somewhere
  send_tx_color_startup(repeller->address, 0x42, 0x90, 0xf5); // Soft blue color
  
  Packet response_packet;
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_COLOR_STARTUP) {
      // Note - The response contains the color values as well (I think??), but the colors do not necessarily match what we sent
      // For now, I'm just ignoring them.
      Serial.printf("Repeller 0x%02X confirmed startup color\n", repeller->address);
    } else {
      Serial.printf("Repeller 0x%02X sent unexpected color response: ", repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("No color startup response from repeller 0x%02X\n", repeller->address);
  }
  
  // 2. Send tx_led_brightness_startup with brightness value and then look for rx_led_brightness_startup
  Serial.printf("Setting startup brightness for repeller 0x%02X...\n", repeller->address);
  // TODO - Come back later when we productionize this, and actually cache the controller brightness somewhere
  send_tx_led_brightness_startup(repeller->address, 100); // Full brightness (100 = 0x64 = 100%)
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_LED_BRIGHTNESS_STARTUP) {
      Serial.printf("Repeller 0x%02X confirmed startup brightness\n", repeller->address);
    } else {
      Serial.printf("Repeller 0x%02X sent unexpected brightness response: ", repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("No LED startup response from repeller 0x%02X\n", repeller->address);
  }
  
  // 3. Send tx_startup_comp and then look for rx_startup_comp
  Serial.printf("Sending startup complete to repeller 0x%02X...\n", repeller->address);
  send_tx_startup_comp(repeller->address);
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_STARTUP_COMP) {
      Serial.printf("Repeller 0x%02X confirmed startup complete\n", repeller->address);
    } else {
      Serial.printf("Repeller 0x%02X sent unexpected startup complete response: ", repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("No startup complete response from repeller 0x%02X\n", repeller->address);
  }
  
  Serial.printf("LED parameter setup complete for repeller 0x%02X\n", repeller->address);
}


void send_led_on_to_repeller(Repeller *repeller) {
  Packet response_packet;
  // 2. send_tx_led_on_conf and look for RX_LED_ON_CONF
  Serial.printf("Sending LED on confirmation to repeller 0x%02X...\n", repeller->address);
  send_tx_led_on_conf(repeller->address);
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_LED_ON_CONF) {
      Serial.printf("Repeller 0x%02X confirmed LED activation\n", repeller->address);
    } else {
      Serial.printf("Repeller 0x%02X sent unexpected LED on response: ", repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("No LED on confirmation response from repeller 0x%02X\n", repeller->address);
  }
}


void send_activate_at_end_of_warmup(Repeller *repeller) {
  if (!repeller) {
    Serial.println("Invalid repeller pointer");
    return;
  }

  Serial.printf("Activating repeller 0x%02X at end of warmup...\n", repeller->address);

  // This function is called after every repeller has been warmed up to actually activate the repeller
  // Not sure if this does anything other than turn on the LEDs, but that's enough!

  // 1. send_tx_warmup_complete and look for RX_WARMUP_COMPLETE
  Serial.printf("Sending warmup complete to repeller 0x%02X...\n", repeller->address);
  send_tx_warmup_complete(repeller->address);
  
  Packet response_packet;
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_WARMUP_COMPLETE) {
      Serial.printf("Repeller 0x%02X confirmed warmup complete\n", repeller->address);
    } else {
      Serial.printf("Repeller 0x%02X sent unexpected warmup complete response: ", repeller->address);
      response_packet.print();
    }
  } else {
    Serial.printf("No warmup complete response from repeller 0x%02X\n", repeller->address);
  }
  
  // 2. send_tx_led_on_conf and look for RX_LED_ON_CONF
  send_led_on_to_repeller(repeller);
  
  Serial.printf("Activation complete for repeller 0x%02X\n", repeller->address);
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
    repeller.state = WARMING_UP;
    repeller.turned_on_at = esp_timer_get_time();  // Record the time when the repeller was turned on
    Serial.printf("Sending warmup instruction to repeller at address 0x%02X...\n", repeller.address);
    send_tx_warmup(&repeller);  // Not sure entirely what this does
  }
  
  delay(4000);  // Wait for 4 seconds to allow warmup to start

  for (auto& repeller : repellers) {
    Serial.printf("Sending LED parameters to repeller at address 0x%02X...\n", repeller.address);
    send_startup_led_params(&repeller);
  }

  Serial.println("Sent warmup & initial LED instructions to all repellers.");
}

void end_warm_up_all() {
  Serial.println("Activating all repellers (ending warm up)...");

  send_tx_powerup();  // This is the command that actually powers up the repellers
  
  for (auto& repeller : repellers) {
    repeller.state = ACTIVE;
    Serial.printf("Activating (ending warm up) repeller at address 0x%02X...\n", repeller.address);
    send_activate_at_end_of_warmup(&repeller);
  }

  Serial.println("Activated all repellers.");
}


bool heartbeat_poll() {
  // Send the heartbeat command (`send_tx_heartbeat`) to each repeller, and read the response. Update the status of each repeller based on the response.
  // If the response is RX_WARMUP, the state is WARMING_UP.
  // If the response is RX_WARMUP_COMP, the state is WARMED_UP
  // If the response is RX_ACTIVE, the state is ACTIVE
  // If no response is received, the state is OFFLINE. We'll need to add handling for this later. 

  Serial.println("Starting heartbeat poll...");
  
  for (auto& repeller : repellers) {
    Serial.printf("Sending heartbeat to repeller 0x%02X...\n", repeller.address);
    
    // Send heartbeat command
    send_tx_heartbeat(repeller.address);
    
    // Wait for response
    Packet response_packet;
    if (receive_packet(response_packet, 1000)) {
      PacketType packet_type = response_packet.identifyPacket();

      // For now, output the packet itself to the console
      Serial.printf("Received response from repeller 0x%02X: ", repeller.address);
      response_packet.print();
      
      switch (packet_type) {
        case RX_WARMUP:
          repeller.state = WARMING_UP;
          Serial.printf("Repeller 0x%02X is warming up\n", repeller.address);
          break;
          
        case RX_WARMUP_COMP:
          repeller.state = WARMED_UP;
          Serial.printf("Repeller 0x%02X is warmed up\n", repeller.address);
          break;
          
        case RX_HEARTBEAT_RUNNING:  // Assuming RX_HEARTBEAT_RUNNING means ACTIVE
          repeller.state = ACTIVE;
          Serial.printf("Repeller 0x%02X is active\n", repeller.address);
          break;
          
        default:
          Serial.printf("Repeller 0x%02X sent unexpected response: ", repeller.address);
          response_packet.print();
          break;
      }
    } else {
      // No response received - state remains as is for now
      Serial.printf("No response from repeller 0x%02X\n", repeller.address);
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
    Serial.println("All repellers warmed up, activating...");
    end_warm_up_all();
  } else{
    Serial.printf("Heartbeat poll complete. Warming up: %s, Warmed up: %s\n", 
                  any_warming_up ? "true" : "false", 
                  any_warmed_up ? "true" : "false");
  }

  return false; 

}


void change_led_brightness(uint8_t brightness_pct) {
  // This function broadcasts the LED brightness change to all devices
  Serial.printf("Changing LED brightness to %d%% for all devices...\n", brightness_pct);

  // 1. send_tx_led_brightness for each repeller with the specified brightness, and look for RX_LED_BRIGHTNESS
  for (auto& repeller : repellers) {
    if (repeller.state == ACTIVE) {
      Serial.printf("Setting brightness to %d%% for repeller 0x%02X...\n", brightness_pct, repeller.address);
      send_tx_led_brightness(repeller.address, brightness_pct);
      
      Packet response_packet;
      if (receive_packet(response_packet, 1000)) {
        if (response_packet.identifyPacket() == RX_LED_BRIGHTNESS) {
          Serial.printf("Repeller 0x%02X confirmed brightness change\n", repeller.address);
          send_led_on_to_repeller(&repeller);  // Trigger the repeller actually using the new brightness
        } else {
          Serial.printf("Repeller 0x%02X sent unexpected brightness response: ", repeller.address);
          response_packet.print();
        }
      } else {
        Serial.printf("No brightness response from repeller 0x%02X\n", repeller.address);
      }
    } else {
      Serial.printf("Skipping repeller 0x%02X (not active, state: %s)\n", repeller.address, repeller.getStateString());
    }
  }
  
  Serial.println("LED brightness change complete.");
}

void change_led_color(uint8_t red, uint8_t green, uint8_t blue) {
  // This function broadcasts the LED color change to all devices
  Serial.printf("Changing LED color to R:%d G:%d B:%d for all devices...\n", red, green, blue);

  // 1. Send the broadcast color command
  send_tx_color(red, green, blue);
  Serial.println("Sent broadcast color change command");
  
  // 2. Send the color confirmation command (AA 8E 03 08 YY ZZ ...)
  send_tx_color_confirm(green, blue);
  Serial.println("Sent color confirmation command");
  
  Serial.println("Color change complete.");
}

void shutdown_all() {
  Serial.println("Shutting down all repellers...");
  
  // 1. Send send_tx_powerdown
  send_tx_powerdown();
  Serial.println("Sent powerdown command to all repellers");
  
  // 2. Loop through all repellers and set their state to OFFLINE
  for (auto& repeller : repellers) {
    repeller.state = OFFLINE;
    Serial.printf("Set repeller 0x%02X to OFFLINE state\n", repeller.address);
  }
  
  Serial.println("All repellers shut down.");
}