#ifdef MODE_ZIGBEE_CONTROLLER
#include "zigbee_controller.h"

#include "esp_zigbee_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "zigbee_ctrl";

static uint32_t millis_now() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// Global Zigbee device instances
ZigbeeRepellerDevice* zigbee_bus0_device = nullptr;
ZigbeeRepellerDevice* zigbee_bus1_device = nullptr;

// Zigbee startup synchronization
static SemaphoreHandle_t s_zigbee_started_sem = nullptr;
static bool s_zigbee_started = false;

// ---- Zigbee action handler (attribute set dispatch) ----

static esp_err_t zb_attribute_set_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
  if (!message || message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Invalid or failed attribute set message");
    return ESP_FAIL;
  }

  ESP_LOGD(TAG, "Attribute set: endpoint(%d), cluster(0x%x), attribute(0x%x)",
           message->info.dst_endpoint, message->info.cluster, message->attribute.id);

  // Dispatch to the right ZBCDL instance by endpoint
  ZigbeeRepellerDevice* device = get_zigbee_device_by_endpoint(message->info.dst_endpoint);
  if (device && device->getZigbeeLight()) {
    device->getZigbeeLight()->zbAttributeSet(message);
  }

  return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
  switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
      return zb_attribute_set_handler((esp_zb_zcl_set_attr_value_message_t *)message);
    default:
      ESP_LOGW(TAG, "Unhandled Zigbee action callback: 0x%x", callback_id);
      break;
  }
  return ESP_OK;
}

// ---- Zigbee signal handler (required global C function) ----

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask) {
  ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

extern "C" void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
  uint32_t *p_sg_p = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;

  switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
      ESP_LOGI(TAG, "Zigbee stack initialized");
      esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
      break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
      if (err_status == ESP_OK) {
        ESP_LOGI(TAG, "Device started up in %sfactory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non ");
        if (esp_zb_bdb_is_factory_new()) {
          ESP_LOGI(TAG, "Start network steering");
          esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
          ESP_LOGI(TAG, "Device rebooted");
          if (ZIGBEE_REBOOT_OPEN_NETWORK_TIME > 0) {
            ESP_LOGI(TAG, "Opening network for joining for %d seconds", ZIGBEE_REBOOT_OPEN_NETWORK_TIME);
            esp_zb_bdb_open_network(ZIGBEE_REBOOT_OPEN_NETWORK_TIME);
          }
        }
        s_zigbee_started = true;
        if (s_zigbee_started_sem) {
          xSemaphoreGive(s_zigbee_started_sem);
        }
      } else {
        ESP_LOGW(TAG, "Commissioning failed (status: %s), retrying...", esp_err_to_name(err_status));
        esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_INITIALIZATION, 500);
      }
      break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
      if (err_status == ESP_OK) {
        ESP_LOGI(TAG, "Joined network (PAN ID: 0x%04hx, Channel: %d)",
                 esp_zb_get_pan_id(), esp_zb_get_current_channel());
      } else {
        ESP_LOGW(TAG, "Network steering failed (status: %s), retrying...", esp_err_to_name(err_status));
        esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
      }
      s_zigbee_started = true;
      if (s_zigbee_started_sem) {
        xSemaphoreGive(s_zigbee_started_sem);
      }
      break;

    default:
      ESP_LOGD(TAG, "ZDO signal: 0x%x, status: %s", sig_type, esp_err_to_name(err_status));
      break;
  }
}

// ---- Zigbee task ----

static void esp_zb_task(void *pvParameters) {
  ESP_ERROR_CHECK(esp_zb_start(false));
  esp_zb_stack_main_loop();
}

// ---- ZigbeeRepellerDevice implementation ----

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
    ESP_LOGE(TAG, "ERROR: Bus not assigned for endpoint %d", endpoint_id);
    return;
  }

  zigbee_light = new ZigbeeColorDimmableLight(endpoint_id);
  zigbee_light->setManufacturerAndModel(ZIGBEE_DEVICE_NAME, ZIGBEE_DEVICE_MODEL);

  if (endpoint_id == 1) {
    zigbee_light->onLightChange(zigbee_light_change_callback_bus0);
  } else if (endpoint_id == 2) {
    zigbee_light->onLightChange(zigbee_light_change_callback_bus1);
  }

  ESP_LOGI(TAG, "Zigbee device initialized for Bus %d on endpoint %d",
            controlled_bus->getBusId(), endpoint_id);
}

// ---- Main Zigbee controller functions ----

void zigbee_controller_setup() {
  ESP_LOGI(TAG, "Setting up Zigbee controller...");

  zigbee_bus0_device = new ZigbeeRepellerDevice(1, &bus0);
  zigbee_bus1_device = new ZigbeeRepellerDevice(2, &bus1);

  zigbee_bus0_device->init();
  zigbee_bus1_device->init();

  s_zigbee_started_sem = xSemaphoreCreateBinary();

  // Platform configuration
  esp_zb_platform_config_t platform_config = {};
  platform_config.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
  platform_config.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;
  ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));

  // Initialize Zigbee stack as End Device
  esp_zb_cfg_t zb_cfg = {};
  zb_cfg.esp_zb_role = ESP_ZB_DEVICE_TYPE_ED;
  zb_cfg.install_code_policy = false;
  zb_cfg.nwk_cfg.zed_cfg.ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN;
  zb_cfg.nwk_cfg.zed_cfg.keep_alive = 3000;
  esp_zb_init(&zb_cfg);

  // Build endpoint list and register both endpoints
  esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

  if (zigbee_bus0_device->getZigbeeLight()) {
    esp_zb_ep_list_add_ep(ep_list,
      zigbee_bus0_device->getZigbeeLight()->getClusterList(),
      zigbee_bus0_device->getZigbeeLight()->getEpConfig());
    ESP_LOGI(TAG, "Zigbee endpoint for Bus 0 added");
  }
  if (zigbee_bus1_device->getZigbeeLight()) {
    esp_zb_ep_list_add_ep(ep_list,
      zigbee_bus1_device->getZigbeeLight()->getClusterList(),
      zigbee_bus1_device->getZigbeeLight()->getEpConfig());
    ESP_LOGI(TAG, "Zigbee endpoint for Bus 1 added");
  }

  ESP_ERROR_CHECK(esp_zb_device_register(ep_list));
  esp_zb_core_action_handler_register(zb_action_handler);
  ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK));

  // Start Zigbee task
  xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL);

  if (xSemaphoreTake(s_zigbee_started_sem, pdMS_TO_TICKS(30000)) != pdTRUE) {
    ESP_LOGW(TAG, "Zigbee startup timed out, continuing anyway");
  }

  ESP_LOGI(TAG, "Zigbee controller setup completed, initializing bus values");
  update_zigbee_attributes_from_bus(zigbee_bus0_device);
  update_zigbee_attributes_from_bus(zigbee_bus1_device);
  ESP_LOGI(TAG, "Bus values initialized. Zigbee endpoints ready.");
  ESP_LOGI(TAG, "Waiting for devices to join network...");
}

void zigbee_controller_loop() {
  static uint32_t last_update = 0;
  uint32_t current_time = millis_now();

  if (current_time - last_update > 5000) {
    update_zigbee_attributes_from_bus(zigbee_bus0_device);
    update_zigbee_attributes_from_bus(zigbee_bus1_device);

    if (bus0.past_automatic_shutoff() && bus0.getState() == BUS_REPELLING) {
      ESP_LOGI(TAG, "Bus 0: Auto shutoff triggered");
      bus0.ZigbeePowerOff();
    }

    if (bus1.past_automatic_shutoff() && bus1.getState() == BUS_REPELLING) {
      ESP_LOGI(TAG, "Bus 1: Auto shutoff triggered");
      bus1.ZigbeePowerOff();
    }

    last_update = current_time;
  }
}

// ---- Light change callbacks ----

void zigbee_light_change_callback(ZigbeeRepellerDevice* device, bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level) {
  if (!device || !device->getBus()) {
    ESP_LOGE(TAG, "ERROR: Invalid device in light change callback");
    return;
  }

  Bus* bus = device->getBus();
  ESP_LOGI(TAG, "Bus %d: Light change - State: %s, RGB: (%d,%d,%d), Level: %d",
            bus->getBusId(), state ? "ON" : "OFF", red, green, blue, level);

  if(bus->getState() == BUS_OFFLINE || bus->getState() == BUS_POWERED) {
    if(state) {
      ESP_LOGI(TAG, "Powering bus on...");
      bus->ZigbeePowerOn();
    }
  } else {
    if(!state) {
      ESP_LOGI(TAG, "Turning bus off...");
      bus->ZigbeePowerOff();
    }
  }

  bus->ZigbeeSetBrightness(level);
  bus->ZigbeeSetRGB(red, green, blue);
}

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

// ---- Custom manufacturer cluster callbacks ----

void zigbee_custom_cluster_command_callback(uint8_t endpoint, uint16_t cluster_id, uint8_t command_id, esp_zb_zcl_command_t *command) {
  ZigbeeRepellerDevice* device = get_zigbee_device_by_endpoint(endpoint);
  if (!device || !device->getBus()) {
    ESP_LOGE(TAG, "ERROR: No device found for endpoint %d", endpoint);
    return;
  }

  if (cluster_id == CLUSTER_ID_CUSTOM_MANUFACTURER && command_id == CMD_ID_RESET_CARTRIDGE) {
    ESP_LOGI(TAG, "Bus %d: Reset cartridge command received", device->getBus()->getBusId());
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

// ---- Helper functions ----

ZigbeeRepellerDevice* get_zigbee_device_by_endpoint(uint8_t endpoint) {
  if (endpoint == 1 && zigbee_bus0_device) return zigbee_bus0_device;
  if (endpoint == 2 && zigbee_bus1_device) return zigbee_bus1_device;
  return nullptr;
}

void update_zigbee_attributes_from_bus(ZigbeeRepellerDevice* device) {
  if (!device || !device->getBus() || !device->getZigbeeLight()) return;

  bool changed = false;
  Bus* bus = device->getBus();
  ZigbeeColorDimmableLight* light = device->getZigbeeLight();

  bool is_on = (bus->getState() != BUS_OFFLINE && bus->getState() != BUS_ERROR);
  uint8_t brightness_254 = bus->repeller_brightness() * 254 / 100;
  uint8_t red = bus->repeller_red();
  uint8_t green = bus->repeller_green();
  uint8_t blue = bus->repeller_blue();

  if(light->getLightState() != is_on) {
    changed = true;
    ESP_LOGI(TAG, "Bus %d: Light state changed to %s", bus->getBusId(), is_on ? "ON" : "OFF");
  }
  if(light->getLightLevel() != brightness_254) {
    changed = true;
    ESP_LOGI(TAG, "Bus %d: Light brightness changed from %d to %d", bus->getBusId(), light->getLightLevel(), brightness_254);
  }
  if(light->getLightRed() != red || light->getLightGreen() != green || light->getLightBlue() != blue) {
    changed = true;
    ESP_LOGI(TAG, "Bus %d: Light color changed from (%d,%d,%d) to (%d,%d,%d)", bus->getBusId(),
             light->getLightRed(), light->getLightGreen(), light->getLightBlue(), red, green, blue);
  }

  if(changed)
    light->setLight(is_on, brightness_254, red, green, blue);
}

#endif // MODE_ZIGBEE_CONTROLLER
