#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "bus.h"
#include "getGuid.h"

// WiFi Configuration
#define WIFI_AP_NAME "RepelBridgeAP"
#define WIFI_AP_PASSWORD "repelbridge"
#define WIFI_MDNS_SERVICE "_repelbridge"
#define WIFI_WEB_PORT 80

// External references to global bus objects
extern Bus bus0;
extern Bus bus1;

// WiFi controller functions
void wifi_controller_setup();
void wifi_controller_loop();

// WiFi device wrapper class for bus control
class WiFiRepellerDevice {
private:
    Bus* controlled_bus;
    uint8_t bus_id;
    
public:
    WiFiRepellerDevice(uint8_t id, Bus* bus);
    ~WiFiRepellerDevice();
    
    Bus* getBus() { return controlled_bus; }
    uint8_t getBusId() { return bus_id; }
    
    // REST API response generators
    String getBusStatusJson();
    String getCartridgeStatusJson();
    String getSystemStatusJson();
};

// Global WiFi device instances
extern WiFiRepellerDevice* wifi_bus0_device;
extern WiFiRepellerDevice* wifi_bus1_device;

// Global web server instance
extern WebServer* web_server;

// REST API endpoint handlers
void handleBusStatus();
void handleBusPower();
void handleBusBrightness();
void handleBusColor();
void handleBusCartridgeStatus();
void handleBusCartridgeReset();
void handleBusAutoShutoff();
void handleBusCartridgeWarnAt();
void handleSystemStatus();
void handleNotFound();

// Helper functions
WiFiRepellerDevice* getDeviceByBusId(uint8_t bus_id);
void setupWebServerRoutes();
void sendJsonResponse(int status_code, const String& json);
void sendErrorResponse(int status_code, const String& error_message);

#endif // WIFI_CONTROLLER_H