#include <Arduino.h>
#include "sniffer_mode.h"
#include "tx_controller.h"

// Mode selection - change this to switch between modes
#define MODE_SNIFFER 0
#define MODE_CONTROLLER 1

// Set the desired mode here
#define CURRENT_MODE       MODE_CONTROLLER

void setup() {
  Serial.begin(115200);
  
  if (CURRENT_MODE == MODE_SNIFFER) {
    Serial.println("Starting in SNIFFER mode...");
    sniffer_setup();
  } else if (CURRENT_MODE == MODE_CONTROLLER) {
    Serial.println("Starting in CONTROLLER mode...");
    Serial.println("Initializing controller emulation...");
    
    delay(4000);

    // Initialize the transmitter
    tx_controller_init();  // GPIO4 for DE/RE control
    
    Serial.println("Running full startup sequence...");
    delay(1000);  // Give time to see the message
    
    discover_repellers();
    retrieve_serial_for_all();
    warm_up_all();
    // full_poweron();
    // delay(10000);

    // // Run the full startup sequence
    // full_startup();

    // delay(20000);
    // send_tx_powerdown();
    
    Serial.println("Full startup sequence completed!");
  }
}

void loop() {
  if (CURRENT_MODE == MODE_SNIFFER) {
    sniffer_loop();
  } else if (CURRENT_MODE == MODE_CONTROLLER) {
    // Controller mode - could add ongoing operations here
    // For now, just a heartbeat every 15 seconds
    static unsigned long last_heartbeat = 0;
    unsigned long current_time = millis();
    
    if (current_time - last_heartbeat > 60000) {
      // Serial.println("Sending periodic heartbeat...");
      // send_tx_heartbeat();
      last_heartbeat = current_time;
    }
    
    delay(100);
  }
}