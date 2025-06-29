#include "zigbee_controller.h"

// #ifdef ZIGBEE_MODE_ED 

#include "esp_zigbee_core.h"


// Global Zigbee device instances
ZigbeeRepellerDevice* zigbee_bus0_device = nullptr;
ZigbeeRepellerDevice* zigbee_bus1_device = nullptr;

// ZigbeeRepellerDevice implementation
ZigbeeRepellerDevice::ZigbeeRepellerDevice(uint8_t ep_id, Bus* bus) 
  : endpoint_id(ep_id), controlled_bus(bus), zigbee_light(nullptr) {
}

ZigbeeRepellerDevice::~ZigbeeRepellerDevice() {

  if (zigbee_light) {
    delete zigbee_light;
  }

}

void ZigbeeRepellerDevice::init() {
  if (!controlled_bus) {
    Serial.printf("ERROR: Bus not assigned for endpoint %d\n", endpoint_id);
    return;
  }
  
  // Create Zigbee light device for this endpoint
  zigbee_light = new ZigbeeColorDimmableLight(endpoint_id);
  
  // Set device information
  zigbee_light->setManufacturerAndModel(ZIGBEE_DEVICE_NAME, ZIGBEE_DEVICE_MODEL);
  
  // Register callback for light changes
  // Note: We'll use a static callback function and identify the device by endpoint
  if (endpoint_id == 1) {
    zigbee_light->onLightChange(zigbee_light_change_callback_bus0);
  } else if (endpoint_id == 2) {
    zigbee_light->onLightChange(zigbee_light_change_callback_bus1);
  }
  
  // Initialize with current bus settings using the correct API
  bool is_on = false;
  // TODO - Change this to use the bus brightness
  uint8_t brightness_level = controlled_bus->repeller_brightness() * 254 / 100; // Convert 0-100 to 0-254
  uint8_t red = controlled_bus->repeller_red();
  uint8_t green = controlled_bus->repeller_green();
  uint8_t blue = controlled_bus->repeller_blue();
  
  // Use the comprehensive setLight method
  // TODO - Move this to after the Zigbee radio is initialized
  // zigbee_light->setLight(is_on, brightness_level, red, green, blue);
  
  Serial.printf("Zigbee device initialized for Bus %d on endpoint %d\n", 
                controlled_bus->getBusId(), endpoint_id);

}

// Main Zigbee controller functions
void zigbee_controller_setup() {
  Serial.println("Setting up Zigbee controller...");
  
  // Create Zigbee devices for each bus
  zigbee_bus0_device = new ZigbeeRepellerDevice(1, &bus0); // Endpoint 1 for Bus 0
  zigbee_bus1_device = new ZigbeeRepellerDevice(2, &bus1); // Endpoint 2 for Bus 1
  
  // Initialize devices and register callbacks
  zigbee_bus0_device->init();
  zigbee_bus1_device->init();
  

  // Add endpoints to Zigbee Core
  if (zigbee_bus0_device->getZigbeeLight()) {
    Zigbee.addEndpoint(zigbee_bus0_device->getZigbeeLight());
    Serial.println("Zigbee endpoint for Bus 0 added");
  }
  if (zigbee_bus1_device->getZigbeeLight()) {
    Zigbee.addEndpoint(zigbee_bus1_device->getZigbeeLight());
    Serial.println("Zigbee endpoint for Bus 1 added");
  }
  
  // Start Zigbee as coordinator
  // esp_zb_cfg_t zb_nwk_cfg = ZIGBEE_DEFAULT_COORDINATOR_CONFIG();
  // if (!Zigbee.begin(&zb_nwk_cfg)) {
  //   Serial.println("Zigbee failed to start!");
  //   return;
  // }

    // Start Zigbee
  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start!");
    return;
  }
  
  // Set device configuration
  Zigbee.setRebootOpenNetwork(180); // Keep network open for 3 minutes after reboot
  
  Serial.println("Zigbee controller setup completed");
  Serial.println("Waiting for devices to join network...");

}

void zigbee_controller_loop() {
  static unsigned long last_update = 0;
  unsigned long current_time = millis();
  
  // Update Zigbee attributes every 5 seconds
  if (current_time - last_update > 5000) {
    update_zigbee_attributes_from_bus(zigbee_bus0_device);
    update_zigbee_attributes_from_bus(zigbee_bus1_device);
    
    // Save active seconds for both buses
    // TODO - Figure out how I want to handle this
    // bus0.save_active_seconds();
    // bus1.save_active_seconds();
    
    // Check for automatic shutoff
    if (bus0.past_automatic_shutoff() && bus0.getState() == BUS_REPELLING) {
      Serial.println("Bus 0: Auto shutoff triggered");
      bus0.ZigbeePowerOff();
    }
    
    if (bus1.past_automatic_shutoff() && bus1.getState() == BUS_REPELLING) {
      Serial.println("Bus 1: Auto shutoff triggered");
      bus1.ZigbeePowerOff();
    }
    
    last_update = current_time;
  }
}

// Zigbee light change callback
void zigbee_light_change_callback(ZigbeeRepellerDevice* device, bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level) {
  if (!device || !device->getBus()) {
    Serial.println("ERROR: Invalid device in light change callback");
    return;
  }
  
  Bus* bus = device->getBus();
  Serial.printf("Bus %d: Light change - State: %s, RGB: (%d,%d,%d), Level: %d\n", 
                bus->getBusId(), state ? "ON" : "OFF", red, green, blue, level);
  
  if(bus->getState() == BUS_OFFLINE || bus->getState() == BUS_POWERED) {
    // If the bus is offline or powered but not warming up/repelling, we need to power it on
    if(state) {
      Serial.println("Powering bus on...");
      bus->ZigbeePowerOn();
    }
  } else {
    if(!state) {
      Serial.println("Turning bus off...");
      // If the bus is already warming up or repelling, we can just turn it off
      bus->ZigbeePowerOff();
    }
  }

  
  // Set brightness (convert from Zigbee 0-254 to internal 0-254 scale)
  bus->ZigbeeSetBrightness(level);
  
  // Set RGB color directly
  bus->ZigbeeSetRGB(red, green, blue);
}


// Static callback functions for each bus
void zigbee_light_change_callback_bus0(bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level) {
  if (zigbee_bus0_device) {
    zigbee_light_change_callback(zigbee_bus0_device, state, red, green, blue, level);
  }
}

void zigbee_light_change_callback_bus1(bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level) {
  if (zigbee_bus1_device) {
    zigbee_light_change_callback(zigbee_bus1_device, state, red, green, blue, level);
  }
}


// Custom manufacturer cluster callbacks (placeholder for future implementation)
void zigbee_custom_cluster_command_callback(uint8_t endpoint, uint16_t cluster_id, uint8_t command_id, esp_zb_zcl_command_t *command) {
  ZigbeeRepellerDevice* device = get_zigbee_device_by_endpoint(endpoint);
  if (!device || !device->getBus()) {
    Serial.printf("ERROR: No device found for endpoint %d\n", endpoint);
    return;
  }
  
  if (cluster_id == CLUSTER_ID_CUSTOM_MANUFACTURER && command_id == CMD_ID_RESET_CARTRIDGE) {
    Serial.printf("Bus %d: Reset cartridge command received\n", device->getBus()->getBusId());
    device->getBus()->ZigbeeResetCartridge();
  }
}

esp_err_t zigbee_custom_cluster_read_callback(uint8_t endpoint, uint16_t cluster_id, uint16_t attribute_id, uint8_t *data, uint16_t max_len) {
  ZigbeeRepellerDevice* device = get_zigbee_device_by_endpoint(endpoint);
  if (!device || !device->getBus()) {
    return ESP_ERR_NOT_FOUND;
  }
  
  if (cluster_id == CLUSTER_ID_CUSTOM_MANUFACTURER) {
    if (attribute_id == ATTR_ID_RUNTIME_HOURS && max_len >= sizeof(uint16_t)) {
      uint16_t runtime_hours = device->getBus()->get_cartridge_runtime_hours();
      memcpy(data, &runtime_hours, sizeof(uint16_t));
      return ESP_OK;
    } else if (attribute_id == ATTR_ID_PERCENT_LEFT && max_len >= sizeof(uint8_t)) {
      uint8_t percent_left = device->getBus()->get_cartridge_percent_left();
      memcpy(data, &percent_left, sizeof(uint8_t));
      return ESP_OK;
    }
  }
  
  return ESP_ERR_NOT_FOUND;
}

// Helper functions
ZigbeeRepellerDevice* get_zigbee_device_by_endpoint(uint8_t endpoint) {
  if (endpoint == 1 && zigbee_bus0_device) {
    return zigbee_bus0_device;
  } else if (endpoint == 2 && zigbee_bus1_device) {
    return zigbee_bus1_device;
  }
  return nullptr;
}

void update_zigbee_attributes_from_bus(ZigbeeRepellerDevice* device) {
  bool changed = false;

  if (!device || !device->getBus()) {
    return;
  }  

  if (!device->getZigbeeLight()) {
    return;
  }
  
  Bus* bus = device->getBus();
  ZigbeeColorDimmableLight* light = device->getZigbeeLight();
  
  // Update all light attributes using the comprehensive setLight method
  bool is_on = (bus->getState() != BUS_OFFLINE && bus->getState() != BUS_ERROR);
  uint8_t brightness_254 = bus->repeller_brightness() * 254 / 100; // Convert 0-100 to 0-254
  uint8_t red = bus->repeller_red();
  uint8_t green = bus->repeller_green();
  uint8_t blue = bus->repeller_blue();
  
  if(light->getLightState() != is_on) {
    changed = true;
    Serial.printf("Bus %d: Update from bus: Light state changed to %s\n", bus->getBusId(), is_on ? "ON" : "OFF");
  }
  if(light->getLightLevel() != brightness_254) {
    changed = true;
    Serial.printf("Bus %d: Update from bus: Light brightness changed from %d to %d\n", bus->getBusId(), light->getLightLevel(), brightness_254);
  }
  if(light->getLightRed() != red || light->getLightGreen() != green || light->getLightBlue() != blue) {
    changed = true;
    Serial.printf("Bus %d: Update from bus: Light color changed from (%d, %d, %d) to (%d, %d, %d)\n", bus->getBusId(), light->getLightRed(), light->getLightGreen(), light->getLightBlue(), red, green, blue);
  }

  if(changed)
    light->setLight(is_on, brightness_254, red, green, blue);

}

// #endif // ZIGBEE_MODE_ED