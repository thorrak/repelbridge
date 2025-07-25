#include <Arduino.h>
#include <LittleFS.h>
#include "sniffer_mode.h"
#include "bus.h"

#ifdef MODE_ZIGBEE_CONTROLLER
#include "zigbee_controller.h"
#endif

#ifdef MODE_WIFI_CONTROLLER
#include "wifi_controller.h"
#endif

// Mode selection - change this to switch between modes
// #define MODE_SNIFFER 0
// #define MODE_CONTROLLER 1
// #define MODE_ZIGBEE_CONTROLLER 2
// #define MODE_WIFI_CONTROLLER 3

// Set the desired mode here
// #define CURRENT_MODE       MODE_WIFI_CONTROLLER

// Global bus objects
Bus bus0(0);
Bus bus1(1);

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("Initializing...");
  
  // Initialize LittleFS filesystem
  if (!LittleFS.begin(true)) {
    Serial.println("Failed to initialize LittleFS");
  } else {
    Serial.println("LittleFS initialized successfully");
  }
  
#ifdef MODE_SNIFFER
  Serial.println("Starting in SNIFFER mode...");
  sniffer_setup();
#endif

#ifdef MODE_CONTROLLER
  Serial.println("Starting in CONTROLLER mode...");
  Serial.println("Initializing controller emulation...");
  
  delay(1000);

  // Initialize bus 0
  bus0.init();
  bus0.activate();  // Activate bus 0
  
  Serial.println("Running full startup sequence...");
  delay(1000);  // Give time to see the message
  bus0.discover_repellers();
  Serial.println("Sent set as address...");
  delay(10000);  // Wait for the command to be processed
  
  bus0.discover_repellers();
  bus0.retrieve_serial_for_all();
  bus0.warm_up_all();
  
  Serial.println("Full startup sequence completed!");
#endif


#ifdef MODE_ZIGBEE_CONTROLLER
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ZIGBEE_MODE_ED)
    // If Zigbee mode is selected but not on ESP32-C6, throw an error
#error "Zigbee mode requires ESP32-C6 target"
#endif
  Serial.println("Starting in ZIGBEE_CONTROLLER mode...");
  Serial.println("Initializing Zigbee controller...");

  delay(1000);

  // Initialize both buses for Zigbee control
  bus0.init();
  bus1.init();

  // Initialize Zigbee controller
  zigbee_controller_setup();

  Serial.println("Zigbee controller initialization completed!");
#endif

#ifdef MODE_WIFI_CONTROLLER
  Serial.println("Starting in WIFI_CONTROLLER mode...");
  Serial.println("Initializing WiFi controller...");
  
  delay(1000);

  // Initialize WiFi controller
  wifi_controller_setup();
  
  Serial.println("WiFi controller initialization completed!");
#endif
}

bool ran_once = false;
void loop() {
#ifdef MODE_SNIFFER
  sniffer_loop();
#endif

#ifdef MODE_CONTROLLER
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
#endif

#ifdef MODE_ZIGBEE_CONTROLLER
    zigbee_controller_loop();
    delay(100);
#endif

#ifdef MODE_WIFI_CONTROLLER
    wifi_controller_loop();
    yield();
    // delay(100);
#endif
}