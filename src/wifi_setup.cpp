#include "wifi_setup.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi_config.h"
#include "getGuid.h"

static const char* TAG = "wifi_setup";

#define WIFI_MDNS_SERVICE "_repelbridge"
#define WIFI_WEB_PORT 80

// --- esp_event callbacks ---
//
// esp_wifi_config 0.2.0 publishes on the default event loop under
// WIFI_CFG_EVENT, so these run on the system event task and share it with
// IDF's own WIFI_EVENT / IP_EVENT handlers. Keep them short.

static void on_wifi_got_ip(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    const esp_netif_ip_info_t *ip = (const esp_netif_ip_info_t *)data;
    ESP_LOGI(TAG, "WiFi got IP " IPSTR, IP2STR(&ip->ip));
}

// mDNS must be initialised BEFORE the STA connects. mdns_priv_netif_init()
// only registers IP_EVENT/WIFI_EVENT handlers; the STA interface is enabled by
// post_enable_pcb() when IP_EVENT_STA_GOT_IP arrives, and already-connected
// interfaces are never enumerated. Starting mDNS from a got-IP callback is
// therefore too late: the service PTR is advertised but no address record is
// ever announced, so the hostname does not resolve (it only starts working
// after the next reconnect delivers another got-IP).
static void start_mdns() {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    char guid[17];
    getGuid(guid);
    char mdns_name[25];
    snprintf(mdns_name, sizeof(mdns_name), "repel-%s", guid);
    mdns_hostname_set(mdns_name);
    mdns_service_add(NULL, WIFI_MDNS_SERVICE, "_tcp", WIFI_WEB_PORT, NULL, 0);
    ESP_LOGI(TAG, "mDNS responder registered as %s.local", mdns_name);
}

static void on_wifi_connected(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "WiFi connected to %s (RSSI: %d dBm)", info->ssid, info->rssi);
}

static void on_wifi_disconnected(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    ESP_LOGW(TAG, "WiFi disconnected from %s (reason: %d)", info->ssid, info->reason);
}

// --- Public API ---

httpd_handle_t wifi_setup_init() {
    // Bring up the TCP/IP stack before httpd_start() below opens its listening
    // socket. wifi_cfg_init() calls esp_netif_init() too, but it runs after we
    // start the server because it needs the handle — so without this, lwIP's
    // tcpip thread does not exist yet and the first socket() call aborts with
    // "Invalid mbox". Re-initialising is harmless.
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(ret));
        return NULL;
    }

    // wifi_cfg_init() creates the default loop itself; creating it here first
    // is what lets the handlers below catch the events it emits during init.
    // A second create returns ESP_ERR_INVALID_STATE and is harmless.
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(ret));
        return NULL;
    }

    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP, on_wifi_got_ip, NULL);
    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_CONNECTED, on_wifi_connected, NULL);
    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_DISCONNECTED, on_wifi_disconnected, NULL);

    // Create HTTP server — we own it, and pass it to the library
    httpd_handle_t http_server = NULL;
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 48;  // Room for our routes + library routes + static UI fallback
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    http_config.lru_purge_enable = true;

    if (httpd_start(&http_server, &http_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    start_mdns();

    // Initialize esp_wifi_config (0.2.0). Provisioning is the SoftAP captive
    // portal (enable_ap below). Improv, the library Web UI and Network
    // Provisioning over BLE are all off — see sdkconfig.defaults for why BLE
    // and the SoftAP cannot both run. RepelBridge serves its own control UI
    // from LittleFS.
    //
    // WIFI_CFG_DEFAULTS is mandatory as of 0.2.0: wifi_cfg_init() no longer
    // patches unset fields, so a field this struct does not mention is taken
    // literally (retry_interval_ms == 0 is rejected outright, auto_reconnect
    // == false silently never reconnects). The overrides are assignments
    // rather than further designated initialisers because C++ requires
    // designators in declaration order, and the macro already sets the last
    // three members.
    wifi_cfg_config_t wifi_config = { WIFI_CFG_DEFAULTS };

    wifi_config.max_reconnect_attempts = 5;   // default 0 = retry forever
    wifi_config.provisioning_mode = WIFI_PROV_WHEN_UNPROVISIONED;
    wifi_config.stop_provisioning_on_connect = true;
    wifi_config.provisioning_teardown_delay_ms = 5000;
    wifi_config.http_post_prov_mode = WIFI_HTTP_DISABLED;
    wifi_config.enable_ap = true;

    // Sub-struct members are set individually, not as a nested initialiser: a
    // designated initialiser replaces the whole sub-struct and would blank
    // what WIFI_CFG_DEFAULTS just put there.
    snprintf(wifi_config.default_ap.ssid, sizeof(wifi_config.default_ap.ssid), "RepelBridgeAP");
    snprintf(wifi_config.default_ap.password, sizeof(wifi_config.default_ap.password), "repelbridge");

    wifi_config.http.httpd = http_server;

    // Only consulted when CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING is set,
    // which it is not; kept so re-enabling BLE is a one-line Kconfig change.
    wifi_config.prov_ble.device_name = "RepelBridge-{id}";
    wifi_config.prov_ble.firmware_version = "1.0.0";

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
        ESP_LOGW(TAG, "WiFi not connected yet — provision via captive portal or BLE");
    }

    return http_server;
}
