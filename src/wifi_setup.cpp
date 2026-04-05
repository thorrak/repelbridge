#include "wifi_setup.h"
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "getGuid.h"

static const char* TAG = "wifi_setup";

#define WIFI_MDNS_SERVICE "_repelbridge"
#define WIFI_WEB_PORT 80

// --- esp_bus event callbacks ---

static void on_wifi_got_ip(const char *event, const void *data, size_t len, void *ctx) {
    ESP_LOGI(TAG, "WiFi got IP — starting mDNS");

    ESP_ERROR_CHECK(mdns_init());
    char guid[17];
    getGuid(guid);
    char mdns_name[25];
    snprintf(mdns_name, sizeof(mdns_name), "repel-%s", guid);
    mdns_hostname_set(mdns_name);
    mdns_service_add(NULL, WIFI_MDNS_SERVICE, "_tcp", WIFI_WEB_PORT, NULL, 0);
    ESP_LOGI(TAG, "mDNS responder started as %s.local", mdns_name);
}

static void on_wifi_connected(const char *event, const void *data, size_t len, void *ctx) {
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "WiFi connected to %s (RSSI: %d dBm)", info->ssid, info->rssi);
}

static void on_wifi_disconnected(const char *event, const void *data, size_t len, void *ctx) {
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    ESP_LOGW(TAG, "WiFi disconnected from %s (reason: %d)", info->ssid, info->reason);
}

// --- Public API ---

httpd_handle_t wifi_setup_init() {
    // Initialize esp_bus (must be before wifi_cfg_init)
    esp_err_t ret = esp_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_bus: %s", esp_err_to_name(ret));
        return NULL;
    }

    // Subscribe to WiFi events BEFORE wifi_cfg_init to catch early events
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_got_ip, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_disconnected, NULL);

    // Create HTTP server — we own it, and pass it to the library
    httpd_handle_t http_server = NULL;
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 32;  // Room for our routes + library routes
    http_config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&http_server, &http_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    // Initialize esp_wifi_config
    wifi_cfg_config_t wifi_config = {
        .auto_reconnect = true,
        .max_reconnect_attempts = 5,
        .on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART,

        .provisioning_mode = WIFI_PROV_WHEN_UNPROVISIONED,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,

        .http_post_prov_mode = WIFI_HTTP_DISABLED,

        .default_ap = {
            .ssid = "RepelBridgeAP",
            .password = "repelbridge",
        },
        .enable_ap = true,

        .http = {
            .httpd = http_server,
        },

        .ble = {
            .enable = true,
            .device_name = "RepelBridge-{id}",
        },

        .improv = {
            .firmware_name = "RepelBridge",
            .firmware_version = "1.0.0",
            .device_name = "RepelBridge",
        },
    };

    ret = wifi_cfg_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Config: %s", esp_err_to_name(ret));
        return NULL;
    }

    ESP_LOGI(TAG, "WiFi Config initialized — waiting for connection...");

    ret = wifi_cfg_wait_connected(30000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected!");
    } else {
        ESP_LOGW(TAG, "WiFi not connected yet — provision via captive portal, BLE, or Improv");
    }

    return http_server;
}
