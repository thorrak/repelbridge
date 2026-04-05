#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sniffer_mode.h"
#include "bus.h"

#ifdef MODE_ZIGBEE_CONTROLLER
#include "zigbee_controller.h"
#endif

#ifdef MODE_WIFI_CONTROLLER
#include "wifi_controller.h"
#endif

static const char* TAG = "main";

static uint32_t millis_now() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// Global bus objects
Bus bus0(0);
Bus bus1(1);

void setup() {
  vTaskDelay(pdMS_TO_TICKS(5000));
  ESP_LOGI(TAG, "Initializing...");

#ifndef ARDUINO
  // Initialize NVS flash (required for WiFi and other subsystems)
  // Arduino framework handles this automatically
  esp_err_t nvs_ret = nvs_flash_init();
  if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_ret);
#endif

  // Initialize LittleFS filesystem via VFS
  esp_vfs_littlefs_conf_t conf = {
    .base_path = "/littlefs",
    .partition_label = "spiffs",  // Uses the partition labeled "spiffs" in the partition table
    .format_if_mount_failed = true,
    .dont_mount = false,
  };

  esp_err_t lfs_ret = esp_vfs_littlefs_register(&conf);
  if (lfs_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(lfs_ret));
  } else {
    ESP_LOGI(TAG, "LittleFS initialized successfully");
  }

#ifdef MODE_SNIFFER
  ESP_LOGI(TAG, "Starting in SNIFFER mode...");
  sniffer_setup();
#endif

#ifdef MODE_CONTROLLER
  ESP_LOGI(TAG, "Starting in CONTROLLER mode...");
  ESP_LOGI(TAG, "Initializing controller emulation...");

  vTaskDelay(pdMS_TO_TICKS(1000));

  // Initialize bus 0
  bus0.init();
  bus0.activate();  // Activate bus 0

  ESP_LOGI(TAG, "Running full startup sequence...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  bus0.discover_repellers();
  ESP_LOGI(TAG, "Sent set as address...");
  vTaskDelay(pdMS_TO_TICKS(10000));

  bus0.discover_repellers();
  bus0.retrieve_serial_for_all();
  bus0.warm_up_all();

  ESP_LOGI(TAG, "Full startup sequence completed!");
#endif

#ifdef MODE_ZIGBEE_CONTROLLER
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ZIGBEE_MODE_ED)
#error "Zigbee mode requires ESP32-C6 target"
#endif
  ESP_LOGI(TAG, "Starting in ZIGBEE_CONTROLLER mode...");
  ESP_LOGI(TAG, "Initializing Zigbee controller...");

  vTaskDelay(pdMS_TO_TICKS(1000));

  // Initialize both buses for Zigbee control
  bus0.init();
  bus1.init();

  // Initialize Zigbee controller
  zigbee_controller_setup();

  ESP_LOGI(TAG, "Zigbee controller initialization completed!");
#endif

#ifdef MODE_WIFI_CONTROLLER
  ESP_LOGI(TAG, "Starting in WIFI_CONTROLLER mode...");
  ESP_LOGI(TAG, "Initializing WiFi controller...");

  vTaskDelay(pdMS_TO_TICKS(1000));

  // Initialize WiFi controller
  wifi_controller_setup();

  ESP_LOGI(TAG, "WiFi controller initialization completed!");
#endif
}

static bool ran_once = false;
void loop() {
#ifdef MODE_SNIFFER
  sniffer_loop();
#endif

#ifdef MODE_CONTROLLER
    static uint32_t last_heartbeat = 0;
    uint32_t current_time = millis_now();

    bool heartbeat = false;
    if(!ran_once) {
      if (current_time - last_heartbeat > 15000) {
        ESP_LOGI(TAG, "Sending periodic heartbeat...");
        heartbeat = bus0.heartbeat_poll();
        last_heartbeat = current_time;
      }

      if(heartbeat) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        bus0.change_led_color(0xA0, 0x00, 0x00);
        vTaskDelay(pdMS_TO_TICKS(10000));
        bus0.change_led_brightness(10);
        vTaskDelay(pdMS_TO_TICKS(10000));

        ESP_LOGI(TAG, "Shutting down all repellers...");
        bus0.shutdown_all();
        ran_once = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
#endif

#ifdef MODE_ZIGBEE_CONTROLLER
    zigbee_controller_loop();
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

#ifdef MODE_WIFI_CONTROLLER
    wifi_controller_loop();
    vTaskDelay(pdMS_TO_TICKS(1));
#endif
}

// ESP-IDF native entry point (used when building without Arduino framework)
#ifndef ARDUINO
extern "C" void app_main(void) {
  setup();

  // Create main loop task
  xTaskCreatePinnedToCore(
    [](void*) {
      for(;;) {
        loop();
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    },
    "mainLoop", 8192, nullptr, 1, nullptr, 1
  );
}
#endif
