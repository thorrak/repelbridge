// Standalone ZigbeeColorDimmableLight - no Arduino ZigbeeEP dependency
// Originally based on Arduino-ESP32 ZigbeeColorDimmableLight
// Pending merge of: https://github.com/espressif/arduino-esp32/pull/11528

#include "ZBCDL.h"
#if CONFIG_ZB_ENABLED

#include "esp_log.h"

static const char* TAG = "zbcdl";

ZigbeeColorDimmableLight::ZigbeeColorDimmableLight(uint8_t endpoint) : _endpoint(endpoint), _on_light_change(nullptr) {
  esp_zb_color_dimmable_light_cfg_t light_cfg = ZIGBEE_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();
  _cluster_list = esp_zb_color_dimmable_light_clusters_create(&light_cfg);

  // Add support for hue and saturation
  uint8_t hue = 0;
  uint8_t saturation = 0;

  esp_zb_attribute_list_t *color_cluster = esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_color_control_cluster_add_attr(color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID, &hue);
  esp_zb_color_control_cluster_add_attr(color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID, &saturation);

  _ep_config = {
    .endpoint = _endpoint,
    .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
    .app_device_id = ESP_ZB_HA_COLOR_DIMMABLE_LIGHT_DEVICE_ID,
    .app_device_version = 0
  };

  // Set default values
  _current_state = false;
  _current_level = 255;
  _current_color = {255, 255, 255};
}

bool ZigbeeColorDimmableLight::setManufacturerAndModel(const char *name, const char *model) {
  size_t name_length = strlen(name);
  size_t model_length = strlen(model);
  if (name_length > 32 || model_length > 32) {
    ESP_LOGE(TAG, "Manufacturer or model name is too long");
    return false;
  }

  // ZCL strings are length-prefixed
  char *zb_name = new char[name_length + 2];
  char *zb_model = new char[model_length + 2];
  zb_name[0] = static_cast<char>(name_length);
  zb_model[0] = static_cast<char>(model_length);
  memcpy(zb_name + 1, name, name_length);
  memcpy(zb_model + 1, model, model_length);
  zb_name[name_length + 1] = '\0';
  zb_model[model_length + 1] = '\0';

  esp_zb_attribute_list_t *basic_cluster = esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_err_t ret_name = esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)zb_name);
  if (ret_name != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set manufacturer: 0x%x: %s", ret_name, esp_err_to_name(ret_name));
  }
  esp_err_t ret_model = esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)zb_model);
  if (ret_model != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set model: 0x%x: %s", ret_model, esp_err_to_name(ret_model));
  }
  delete[] zb_name;
  delete[] zb_model;
  return ret_name == ESP_OK && ret_model == ESP_OK;
}

uint16_t ZigbeeColorDimmableLight::getCurrentColorX() {
  return (*(uint16_t *)esp_zb_zcl_get_attribute(
             _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID
  )
             ->data_p);
}

uint16_t ZigbeeColorDimmableLight::getCurrentColorY() {
  return (*(uint16_t *)esp_zb_zcl_get_attribute(
             _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID
  )
             ->data_p);
}

uint8_t ZigbeeColorDimmableLight::getCurrentColorHue() {
  return (*(uint8_t *)esp_zb_zcl_get_attribute(
             _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID
  )
             ->data_p);
}

uint8_t ZigbeeColorDimmableLight::getCurrentColorSaturation() {
  return (*(uint8_t *)esp_zb_zcl_get_attribute(
             _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID
  )
             ->data_p);
}

// Attribute set handler - called from the Zigbee action handler
void ZigbeeColorDimmableLight::zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) {
  if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
      if (_current_state != *(bool *)message->attribute.data.value) {
        _current_state = *(bool *)message->attribute.data.value;
        lightChanged();
      }
      return;
    } else {
      ESP_LOGW(TAG, "Received message ignored. Attribute ID: %d not supported for On/Off Light", message->attribute.id);
    }
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      if (_current_level != *(uint8_t *)message->attribute.data.value) {
        _current_level = *(uint8_t *)message->attribute.data.value;
        lightChanged();
      }
      return;
    } else {
      ESP_LOGW(TAG, "Received message ignored. Attribute ID: %d not supported for Level Control", message->attribute.id);
    }
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL) {
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
      uint16_t light_color_x = (*(uint16_t *)message->attribute.data.value);
      uint16_t light_color_y = getCurrentColorY();
      _current_color = espXYToRgbColor(255, light_color_x, light_color_y);
      lightChanged();
      return;
    } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
      uint16_t light_color_x = getCurrentColorX();
      uint16_t light_color_y = (*(uint16_t *)message->attribute.data.value);
      _current_color = espXYToRgbColor(255, light_color_x, light_color_y);
      lightChanged();
      return;
    } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      uint8_t light_color_hue = (*(uint8_t *)message->attribute.data.value);
      _current_color = espHsvToRgbColor(light_color_hue, getCurrentColorSaturation(), 255);
      lightChanged();
      return;
    } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      uint8_t light_color_saturation = (*(uint8_t *)message->attribute.data.value);
      _current_color = espHsvToRgbColor(getCurrentColorHue(), light_color_saturation, 255);
      lightChanged();
      return;
    } else {
      ESP_LOGW(TAG, "Received message ignored. Attribute ID: %d not supported for Color Control", message->attribute.id);
    }
  } else {
    ESP_LOGW(TAG, "Received message ignored. Cluster ID: %d not supported for Color dimmable Light", message->info.cluster);
  }
}

void ZigbeeColorDimmableLight::lightChanged() {
  if (_on_light_change) {
    _on_light_change(_current_state, _current_color.r, _current_color.g, _current_color.b, _current_level);
  }
}

bool ZigbeeColorDimmableLight::setLight(bool state, uint8_t level, uint8_t red, uint8_t green, uint8_t blue) {
  esp_zb_zcl_status_t ret = ESP_ZB_ZCL_STATUS_SUCCESS;
  _current_state = state;
  _current_level = level;
  _current_color = {red, green, blue};
  lightChanged();

  espXyColor_t xy_color = espRgbColorToXYColor(_current_color);
  espHsvColor_t hsv_color = espRgbColorToHsvColor(_current_color);
  uint8_t hue = (uint8_t)hsv_color.h;
  hue = (hue > 254) ? 254 : hue;
  uint8_t saturation = (hsv_color.s > 254) ? 254 : (uint8_t)hsv_color.s;

  ESP_LOGD(TAG, "Updating light state: %d, level: %d, color: %d, %d, %d", state, level, red, green, blue);

  esp_zb_lock_acquire(portMAX_DELAY);

  // Set on/off state
  ret = esp_zb_zcl_set_attribute_val(
    _endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &_current_state, false
  );
  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set light state: 0x%x", ret);
    goto unlock_and_return;
  }

  // Set level
  ret = esp_zb_zcl_set_attribute_val(
    _endpoint, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &_current_level, false
  );
  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set light level: 0x%x", ret);
    goto unlock_and_return;
  }

  // Set x color
  ret = esp_zb_zcl_set_attribute_val(
    _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID, &xy_color.x, false
  );
  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set light x color: 0x%x", ret);
    goto unlock_and_return;
  }

  // Set y color
  ret = esp_zb_zcl_set_attribute_val(
    _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID, &xy_color.y, false
  );
  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set light y color: 0x%x", ret);
    goto unlock_and_return;
  }

  // Set hue
  ret = esp_zb_zcl_set_attribute_val(
    _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID, &hue, false
  );
  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set light hue: 0x%x", ret);
    goto unlock_and_return;
  }

  // Set saturation
  ret = esp_zb_zcl_set_attribute_val(
    _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID, &saturation, false
  );
  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set light saturation: 0x%x", ret);
    goto unlock_and_return;
  }

unlock_and_return:
  esp_zb_lock_release();
  return ret == ESP_ZB_ZCL_STATUS_SUCCESS;
}

bool ZigbeeColorDimmableLight::setLightState(bool state) {
  return setLight(state, _current_level, _current_color.r, _current_color.g, _current_color.b);
}

bool ZigbeeColorDimmableLight::setLightLevel(uint8_t level) {
  return setLight(_current_state, level, _current_color.r, _current_color.g, _current_color.b);
}

bool ZigbeeColorDimmableLight::setLightColor(uint8_t red, uint8_t green, uint8_t blue) {
  return setLight(_current_state, _current_level, red, green, blue);
}

#endif  // CONFIG_ZB_ENABLED
