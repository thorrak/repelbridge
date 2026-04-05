#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "esp_http_server.h"
#include "ArduinoJson.h"
#include "bus.h"
#include "getGuid.h"

// WiFi Configuration
#define WIFI_STA_SSID "fakeap"
#define WIFI_STA_PASS "fakepassword"
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

    // REST API response generators (write JSON into buffer, return bytes written)
    int writeBusStatusJson(char* buf, size_t buf_size);
    int writeCartridgeStatusJson(char* buf, size_t buf_size);
};

// Global WiFi device instances
extern WiFiRepellerDevice* wifi_bus0_device;
extern WiFiRepellerDevice* wifi_bus1_device;

// Helper functions
WiFiRepellerDevice* getDeviceByBusId(uint8_t bus_id);

#endif // WIFI_CONTROLLER_H
