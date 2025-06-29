#ifndef ZIGBEE_CONTROLLER_H
#define ZIGBEE_CONTROLLER_H

#include <Arduino.h>

// #ifdef ZIGBEE_MODE_ED


#include "ZigbeeCore.h"
// #include "ep/ZigbeeColorDimmableLight.h"
#include "ZBCDL.h"  // Include the near clone of ZigbeeColorDimmableLight.h

#include "bus.h"

// Zigbee Configuration
#define ZIGBEE_MANUFACTURER_CODE 0x1234
#define ZIGBEE_DEVICE_NAME "Liv Repeller Controller"
#define ZIGBEE_DEVICE_MODEL "LRC-ESP32C6"

// Custom Manufacturer Cluster
#define CLUSTER_ID_CUSTOM_MANUFACTURER 0xFC00

// Custom Cluster Attributes
#define ATTR_ID_RUNTIME_HOURS 0xF001
#define ATTR_ID_PERCENT_LEFT 0xF002

// Custom Cluster Commands  
#define CMD_ID_RESET_CARTRIDGE 0x01

// External references to global bus objects
extern Bus bus0;
extern Bus bus1;

// Zigbee device endpoints - one for each bus
class ZigbeeRepellerDevice {
private:
  ZigbeeColorDimmableLight* zigbee_light;

  Bus* controlled_bus;
  uint8_t endpoint_id;
  
public:
  ZigbeeRepellerDevice(uint8_t ep_id, Bus* bus);
  ~ZigbeeRepellerDevice();
  
  void init();
  Bus* getBus() { return controlled_bus; }
  uint8_t getEndpointId() { return endpoint_id; }
  ZigbeeColorDimmableLight* getZigbeeLight() { return zigbee_light; }
};

// Global Zigbee device instances
extern ZigbeeRepellerDevice* zigbee_bus0_device;
extern ZigbeeRepellerDevice* zigbee_bus1_device;

// Zigbee controller functions
void zigbee_controller_setup();
void zigbee_controller_loop();

// Zigbee callback functions
void zigbee_light_change_callback(ZigbeeRepellerDevice* device, bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level);
void zigbee_light_change_callback_bus0(bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level);
void zigbee_light_change_callback_bus1(bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level);

// Custom manufacturer cluster callbacks
void zigbee_custom_cluster_command_callback(uint8_t endpoint, uint16_t cluster_id, uint8_t command_id, esp_zb_zcl_command_t *command);
esp_err_t zigbee_custom_cluster_read_callback(uint8_t endpoint, uint16_t cluster_id, uint16_t attribute_id, uint8_t *data, uint16_t max_len);

// Helper functions
ZigbeeRepellerDevice* get_zigbee_device_by_endpoint(uint8_t endpoint);
void update_zigbee_attributes_from_bus(ZigbeeRepellerDevice* device);

// #endif // ZIGBEE_MODE_ED

#endif // ZIGBEE_CONTROLLER_H