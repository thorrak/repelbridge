#include <Arduino.h>
#include <LittleFS.h>
#include "sniffer_mode.h"
#include "bus.h"

// Mode selection - change this to switch between modes
#define MODE_SNIFFER 0
#define MODE_CONTROLLER 1

// Set the desired mode here
#define CURRENT_MODE       MODE_CONTROLLER

// Global bus objects
Bus bus0(0);
Bus bus1(1);

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("Initializing...");
  
  // Initialize LittleFS filesystem
  if (!LittleFS.begin()) {
    Serial.println("Failed to initialize LittleFS");
  } else {
    Serial.println("LittleFS initialized successfully");
  }
  
  if (CURRENT_MODE == MODE_SNIFFER) {
    Serial.println("Starting in SNIFFER mode...");
    sniffer_setup();
  } else if (CURRENT_MODE == MODE_CONTROLLER) {
    Serial.println("Starting in CONTROLLER mode...");
    Serial.println("Initializing controller emulation...");
    
    delay(1000);

    // Initialize bus 0
    bus0.init();
    
    Serial.println("Running full startup sequence...");
    delay(1000);  // Give time to see the message
    
    bus0.discover_repellers();
    bus0.retrieve_serial_for_all();
    bus0.warm_up_all();
    
    Serial.println("Full startup sequence completed!");
  }
}

bool ran_once = false;
void loop() {
  if (CURRENT_MODE == MODE_SNIFFER) {
    sniffer_loop();
  } else if (CURRENT_MODE == MODE_CONTROLLER) {
    // Controller mode - could add ongoing operations here
    // For now, just a heartbeat every 15 seconds
    static unsigned long last_heartbeat = 0;
    unsigned long current_time = millis();
    
    bool heartbeat = false;
    if(!ran_once) {
      if (current_time - last_heartbeat > 15000) {
        Serial.println("Sending periodic heartbeat...");
        heartbeat = bus0.heartbeat_poll();  // Poll the heartbeat status of all repellers
        last_heartbeat = current_time;
      }

      if(heartbeat) {
        // All repellers are active. Wait for 30 seconds, then shut them down
        delay(10000);
        bus0.change_led_color(0xA0, 0x00, 0x00);  // Set to red color
        delay(10000);        // Change the LED color/brightness
        bus0.change_led_brightness(10);  // Set to 50% brightness
        delay(10000); 



        Serial.println("Shutting down all repellers...");
        bus0.shutdown_all();
        ran_once = true;  // Ensure this runs only once
      }
    }
    
    delay(100);
  }
}