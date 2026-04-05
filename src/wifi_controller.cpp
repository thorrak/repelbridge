#include "wifi_controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char* TAG = "wifi_ctrl";

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// HTTP server handle
static httpd_handle_t http_server = NULL;

// Global instances
WiFiRepellerDevice* wifi_bus0_device = nullptr;
WiFiRepellerDevice* wifi_bus1_device = nullptr;

static uint32_t millis_now() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// WiFiRepellerDevice implementation
WiFiRepellerDevice::WiFiRepellerDevice(uint8_t id, Bus* bus) : bus_id(id), controlled_bus(bus) {
    ESP_LOGI(TAG, "WiFiRepellerDevice created for Bus %d", bus_id);
}

WiFiRepellerDevice::~WiFiRepellerDevice() {
    ESP_LOGI(TAG, "WiFiRepellerDevice destroyed for Bus %d", bus_id);
}

int WiFiRepellerDevice::writeBusStatusJson(char* buf, size_t buf_size) {
    JsonDocument doc;

    doc["bus_id"] = bus_id;
    doc["state"] = controlled_bus->getStateString();
    doc["powered"] = (controlled_bus->getState() != BUS_OFFLINE);
    doc["brightness"] = controlled_bus->zigbee_brightness() + 1; // Convert 0-254 to 1-255 for HTTP API
    doc["color"]["red"] = controlled_bus->repeller_red();
    doc["color"]["green"] = controlled_bus->repeller_green();
    doc["color"]["blue"] = controlled_bus->repeller_blue();
    doc["repeller_count"] = controlled_bus->getRepellers().size();

    return serializeJson(doc, buf, buf_size);
}

int WiFiRepellerDevice::writeCartridgeStatusJson(char* buf, size_t buf_size) {
    JsonDocument doc;

    doc["bus_id"] = bus_id;
    doc["runtime_hours"] = controlled_bus->get_cartridge_runtime_hours();
    doc["percent_left"] = controlled_bus->get_cartridge_percent_left();
    doc["active_seconds"] = controlled_bus->get_cartridge_active_seconds();
    doc["warn_at_hours"] = controlled_bus->get_cartridge_warn_at_seconds() / 3600;
    doc["auto_shutoff_seconds"] = controlled_bus->get_auto_shut_off_after_seconds();

    return serializeJson(doc, buf, buf_size);
}

// Helper
WiFiRepellerDevice* getDeviceByBusId(uint8_t bus_id) {
    if (bus_id == 0) return wifi_bus0_device;
    if (bus_id == 1) return wifi_bus1_device;
    return nullptr;
}

// --- HTTP response helpers ---

static esp_err_t send_json_response(httpd_req_t *req, int status, const char* json) {
    httpd_resp_set_status(req, status == 200 ? "200 OK" :
                               status == 400 ? "400 Bad Request" :
                               status == 404 ? "404 Not Found" :
                               status == 405 ? "405 Method Not Allowed" : "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t send_error_response(httpd_req_t *req, int status, const char* error_message) {
    char buf[128];
    JsonDocument doc;
    doc["error"] = error_message;
    doc["status"] = status;
    serializeJson(doc, buf, sizeof(buf));
    return send_json_response(req, status, buf);
}

// Extract bus ID from URI like /api/bus/0/status -> 0
static int extract_bus_id(const char* uri) {
    const char* p = strstr(uri, "/bus/");
    if (!p) return -1;
    p += 5; // skip "/bus/"
    return (*p >= '0' && *p <= '1') ? (*p - '0') : -1;
}

// Read POST body into buffer
static int read_post_body(httpd_req_t *req, char* buf, size_t buf_size) {
    int content_len = req->content_len;
    if (content_len <= 0 || (size_t)content_len >= buf_size) return -1;
    int received = httpd_req_recv(req, buf, content_len);
    if (received <= 0) return -1;
    buf[received] = '\0';
    return received;
}

// --- Endpoint handlers ---

static esp_err_t handle_bus_status(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char buf[256];
    device->writeBusStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_power(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char body[64];
    if (read_post_body(req, body, sizeof(body)) < 0)
        return send_error_response(req, 400, "Missing body");

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return send_error_response(req, 400, "Invalid JSON");

    if (doc["state"].isNull())
        return send_error_response(req, 400, "Missing state parameter");

    bool power_on = doc["state"].as<bool>();
    if (power_on) {
        device->getBus()->ZigbeePowerOn();
        ESP_LOGI(TAG, "Bus %d powered ON via WiFi API", bus_id);
    } else {
        device->getBus()->ZigbeePowerOff();
        ESP_LOGI(TAG, "Bus %d powered OFF via WiFi API", bus_id);
    }

    char buf[256];
    device->writeBusStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_brightness(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char body[64];
    if (read_post_body(req, body, sizeof(body)) < 0)
        return send_error_response(req, 400, "Missing body");

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return send_error_response(req, 400, "Invalid JSON");

    if (doc["value"].isNull())
        return send_error_response(req, 400, "Missing value parameter");

    int brightness = doc["value"].as<int>();
    if (brightness < 1 || brightness > 255)
        return send_error_response(req, 400, "Brightness must be 1-255");

    uint8_t zigbee_brightness = brightness - 1;
    device->getBus()->ZigbeeSetBrightness(zigbee_brightness);
    device->getBus()->change_led_brightness(device->getBus()->repeller_brightness());
    ESP_LOGI(TAG, "Bus %d brightness set to %d via WiFi API", bus_id, brightness);

    char buf[256];
    device->writeBusStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_color(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char body[128];
    if (read_post_body(req, body, sizeof(body)) < 0)
        return send_error_response(req, 400, "Missing body");

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return send_error_response(req, 400, "Invalid JSON");

    if (doc["red"].isNull() || doc["green"].isNull() || doc["blue"].isNull())
        return send_error_response(req, 400, "Missing red, green, or blue parameters");

    int red = doc["red"].as<int>();
    int green = doc["green"].as<int>();
    int blue = doc["blue"].as<int>();

    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255)
        return send_error_response(req, 400, "RGB values must be 0-255");

    device->getBus()->ZigbeeSetRGB(red, green, blue);
    device->getBus()->change_led_color(red, green, blue);
    ESP_LOGI(TAG, "Bus %d color set to RGB(%d,%d,%d) via WiFi API", bus_id, red, green, blue);

    char buf[256];
    device->writeBusStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_cartridge_status(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char buf[256];
    device->writeCartridgeStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_cartridge_reset(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    device->getBus()->ZigbeeResetCartridge();
    ESP_LOGI(TAG, "Bus %d cartridge reset via WiFi API", bus_id);

    char buf[256];
    device->writeCartridgeStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_auto_shutoff_get(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char buf[128];
    JsonDocument doc;
    doc["bus_id"] = bus_id;
    doc["auto_shutoff_minutes"] = device->getBus()->get_auto_shut_off_after_seconds() / 60;
    serializeJson(doc, buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_auto_shutoff_post(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char body[64];
    if (read_post_body(req, body, sizeof(body)) < 0)
        return send_error_response(req, 400, "Missing body");

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return send_error_response(req, 400, "Invalid JSON");

    if (doc["minutes"].isNull())
        return send_error_response(req, 400, "Missing minutes parameter");

    int minutes = doc["minutes"].as<int>();
    if (minutes < 0 || minutes > 960)
        return send_error_response(req, 400, "Auto shutoff must be 0-960 minutes");

    int seconds = minutes * 60;
    device->getBus()->ZigbeeSetAutoShutOffAfterSeconds(seconds);
    ESP_LOGI(TAG, "Bus %d auto shutoff set to %d minutes via WiFi API", bus_id, minutes);

    char buf[256];
    device->writeCartridgeStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_warn_at_get(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char buf[128];
    JsonDocument doc;
    doc["bus_id"] = bus_id;
    doc["warn_at_hours"] = device->getBus()->get_cartridge_warn_at_seconds() / 3600;
    serializeJson(doc, buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_bus_warn_at_post(httpd_req_t *req) {
    int bus_id = extract_bus_id(req->uri);
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    if (!device) return send_error_response(req, 404, "Bus not found");

    char body[64];
    if (read_post_body(req, body, sizeof(body)) < 0)
        return send_error_response(req, 400, "Missing body");

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return send_error_response(req, 400, "Invalid JSON");

    if (doc["hours"].isNull())
        return send_error_response(req, 400, "Missing hours parameter");

    int hours = doc["hours"].as<int>();
    if (hours < 0)
        return send_error_response(req, 400, "Hours must be >= 0");

    uint32_t seconds = hours * 3600;
    device->getBus()->ZigbeeSetCartridgeWarnAtSeconds(seconds);
    ESP_LOGI(TAG, "Bus %d cartridge warn threshold set to %d hours via WiFi API", bus_id, hours);

    char buf[256];
    device->writeCartridgeStatusJson(buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

static esp_err_t handle_system_status(httpd_req_t *req) {
    char buf[512];
    JsonDocument doc;

    char guid[17];
    getGuid(guid);

    doc["device_name"] = "RepelBridge WiFi Controller";
    doc["guid"] = guid;
    doc["free_heap"] = esp_get_free_heap_size();
    doc["uptime_ms"] = millis_now();

    doc["bus0"]["state"] = bus0.getStateString();
    doc["bus0"]["repeller_count"] = bus0.getRepellers().size();
    doc["bus1"]["state"] = bus1.getStateString();
    doc["bus1"]["repeller_count"] = bus1.getRepellers().size();

    serializeJson(doc, buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

// --- WiFi event handler ---

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// --- HTTP server setup ---

static void setup_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;  // We register many routes
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&http_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    // Macro to reduce boilerplate for route registration
    #define REGISTER_URI(path, method_val, handler_fn) do { \
        httpd_uri_t uri = { .uri = path, .method = method_val, .handler = handler_fn, .user_ctx = NULL }; \
        httpd_register_uri_handler(http_server, &uri); \
    } while(0)

    // Bus 0 endpoints
    REGISTER_URI("/api/bus/0/status",          HTTP_GET,  handle_bus_status);
    REGISTER_URI("/api/bus/0/power",           HTTP_POST, handle_bus_power);
    REGISTER_URI("/api/bus/0/brightness",      HTTP_POST, handle_bus_brightness);
    REGISTER_URI("/api/bus/0/color",           HTTP_POST, handle_bus_color);
    REGISTER_URI("/api/bus/0/cartridge",       HTTP_GET,  handle_bus_cartridge_status);
    REGISTER_URI("/api/bus/0/cartridge/reset", HTTP_POST, handle_bus_cartridge_reset);
    REGISTER_URI("/api/bus/0/auto_shutoff",    HTTP_GET,  handle_bus_auto_shutoff_get);
    REGISTER_URI("/api/bus/0/auto_shutoff",    HTTP_POST, handle_bus_auto_shutoff_post);
    REGISTER_URI("/api/bus/0/warn_at",         HTTP_GET,  handle_bus_warn_at_get);
    REGISTER_URI("/api/bus/0/warn_at",         HTTP_POST, handle_bus_warn_at_post);

    // Bus 1 endpoints
    REGISTER_URI("/api/bus/1/status",          HTTP_GET,  handle_bus_status);
    REGISTER_URI("/api/bus/1/power",           HTTP_POST, handle_bus_power);
    REGISTER_URI("/api/bus/1/brightness",      HTTP_POST, handle_bus_brightness);
    REGISTER_URI("/api/bus/1/color",           HTTP_POST, handle_bus_color);
    REGISTER_URI("/api/bus/1/cartridge",       HTTP_GET,  handle_bus_cartridge_status);
    REGISTER_URI("/api/bus/1/cartridge/reset", HTTP_POST, handle_bus_cartridge_reset);
    REGISTER_URI("/api/bus/1/auto_shutoff",    HTTP_GET,  handle_bus_auto_shutoff_get);
    REGISTER_URI("/api/bus/1/auto_shutoff",    HTTP_POST, handle_bus_auto_shutoff_post);
    REGISTER_URI("/api/bus/1/warn_at",         HTTP_GET,  handle_bus_warn_at_get);
    REGISTER_URI("/api/bus/1/warn_at",         HTTP_POST, handle_bus_warn_at_post);

    // System endpoint
    REGISTER_URI("/api/system/status",         HTTP_GET,  handle_system_status);

    #undef REGISTER_URI

    ESP_LOGI(TAG, "HTTP server started with all routes registered");
}

// --- Main WiFi controller functions ---

void wifi_controller_setup() {
    ESP_LOGI(TAG, "Initializing WiFi Controller...");

    // Initialize WiFi devices for both buses
    wifi_bus0_device = new WiFiRepellerDevice(0, &bus0);
    wifi_bus1_device = new WiFiRepellerDevice(1, &bus1);

    // Initialize both buses
    bus0.init();
    bus1.init();

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, &instance_got_ip));

    // Configure WiFi
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_STA_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_STA_PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s ...", WIFI_STA_SSID);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi Connected!");
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi, restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    // Setup mDNS
    ESP_ERROR_CHECK(mdns_init());
    char guid[17];
    getGuid(guid);
    char mdns_name[25];
    snprintf(mdns_name, sizeof(mdns_name), "repel-%s", guid);
    mdns_hostname_set(mdns_name);
    mdns_service_add(NULL, WIFI_MDNS_SERVICE, "_tcp", WIFI_WEB_PORT, NULL, 0);
    ESP_LOGI(TAG, "mDNS responder started as %s.local", mdns_name);

    // Start HTTP server
    setup_http_server();

    ESP_LOGI(TAG, "WiFi Controller initialization complete!");
}

void wifi_controller_loop() {
    // Update cartridge monitoring for active buses
    if (bus0.getState() == BUS_WARMING_UP || bus0.getState() == BUS_REPELLING) {
        bus0.poll();
        if (bus0.past_automatic_shutoff()) {
            ESP_LOGI(TAG, "Bus 0 auto-shutoff triggered");
            bus0.ZigbeePowerOff();
        }
    }

    if (bus1.getState() == BUS_WARMING_UP || bus1.getState() == BUS_REPELLING) {
        bus1.poll();
        if (bus1.past_automatic_shutoff()) {
            ESP_LOGI(TAG, "Bus 1 auto-shutoff triggered");
            bus1.ZigbeePowerOff();
        }
    }
}
