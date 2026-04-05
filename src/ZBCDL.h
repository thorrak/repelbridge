// Standalone ZigbeeColorDimmableLight - no Arduino ZigbeeEP dependency
// Originally based on Arduino-ESP32 ZigbeeColorDimmableLight
// Pending merge of: https://github.com/espressif/arduino-esp32/pull/11528

#pragma once

#include "sdkconfig.h"
#if CONFIG_ZB_ENABLED

#include <cstdint>
#include <cstring>
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "ColorFormat.h"

#define ZIGBEE_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG()                                     \
  {                                                                                      \
    .basic_cfg =                                                                         \
      {                                                                                  \
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,                       \
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,                     \
      },                                                                                 \
    .identify_cfg =                                                                      \
      {                                                                                  \
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,                \
      },                                                                                 \
    .groups_cfg =                                                                        \
      {                                                                                  \
        .groups_name_support_id = ESP_ZB_ZCL_GROUPS_NAME_SUPPORT_DEFAULT_VALUE,          \
      },                                                                                 \
    .scenes_cfg =                                                                        \
      {                                                                                  \
        .scenes_count = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,                     \
        .current_scene = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,                  \
        .current_group = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,                  \
        .scene_valid = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,                      \
        .name_support = ESP_ZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE,                    \
      },                                                                                 \
    .on_off_cfg =                                                                        \
      {                                                                                  \
        .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,                                \
      },                                                                                 \
    .level_cfg =                                                                         \
      {                                                                                  \
        .current_level = ESP_ZB_ZCL_LEVEL_CONTROL_CURRENT_LEVEL_DEFAULT_VALUE,           \
      },                                                                                 \
    .color_cfg = {                                                                       \
      .current_x = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_X_DEF_VALUE,                         \
      .current_y = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_Y_DEF_VALUE,                         \
      .color_mode = ESP_ZB_ZCL_COLOR_CONTROL_COLOR_MODE_DEFAULT_VALUE,                   \
      .options = ESP_ZB_ZCL_COLOR_CONTROL_OPTIONS_DEFAULT_VALUE,                         \
      .enhanced_color_mode = ESP_ZB_ZCL_COLOR_CONTROL_ENHANCED_COLOR_MODE_DEFAULT_VALUE, \
      .color_capabilities = 0x0009,                                                      \
    },                                                                                   \
  }

class ZigbeeColorDimmableLight {
public:
  ZigbeeColorDimmableLight(uint8_t endpoint);
  ~ZigbeeColorDimmableLight() {}

  // Endpoint config accessors (used by zigbee_controller for registration)
  uint8_t getEndpoint() const { return _endpoint; }
  esp_zb_endpoint_config_t getEpConfig() const { return _ep_config; }
  esp_zb_cluster_list_t* getClusterList() const { return _cluster_list; }

  // Configuration
  bool setManufacturerAndModel(const char *name, const char *model);

  void onLightChange(void (*callback)(bool, uint8_t, uint8_t, uint8_t, uint8_t)) {
    _on_light_change = callback;
  }

  // Called by the action handler when an attribute changes on this endpoint
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message);

  bool setLightState(bool state);
  bool setLightLevel(uint8_t level);
  bool setLightColor(uint8_t red, uint8_t green, uint8_t blue);
  bool setLight(bool state, uint8_t level, uint8_t red, uint8_t green, uint8_t blue);

  bool getLightState() { return _current_state; }
  uint8_t getLightLevel() { return _current_level; }
  uint8_t getLightRed() { return _current_color.r; }
  uint8_t getLightGreen() { return _current_color.g; }
  uint8_t getLightBlue() { return _current_color.b; }

private:
  uint8_t _endpoint;
  esp_zb_endpoint_config_t _ep_config;
  esp_zb_cluster_list_t* _cluster_list;

  uint16_t getCurrentColorX();
  uint16_t getCurrentColorY();
  uint8_t getCurrentColorHue();
  uint8_t getCurrentColorSaturation();

  void lightChanged();
  void (*_on_light_change)(bool, uint8_t, uint8_t, uint8_t, uint8_t);

  bool _current_state;
  uint8_t _current_level;
  espRgbColor_t _current_color;
};

#endif  // CONFIG_ZB_ENABLED
