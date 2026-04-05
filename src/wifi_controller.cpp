#include "wifi_controller.h"
#include "wifi_setup.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "wifi_ctrl";

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
    doc["brightness"] = controlled_bus->get_brightness();
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
        device->getBus()->powerOn();
        ESP_LOGI(TAG, "Bus %d powered ON via WiFi API", bus_id);
    } else {
        device->getBus()->powerOff();
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
    if (brightness < 0 || brightness > 255)
        return send_error_response(req, 400, "Brightness must be 0-255");

    device->getBus()->setBrightness((uint8_t)brightness);
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

    device->getBus()->setRGB(red, green, blue);
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

    device->getBus()->resetCartridge();
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
    device->getBus()->setAutoShutOffAfterSeconds(seconds);
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
    device->getBus()->setCartridgeWarnAtSeconds(seconds);
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

    // Include WiFi connection status from esp_wifi_config
    wifi_status_t wifi_status;
    if (wifi_cfg_get_status(&wifi_status) == ESP_OK) {
        doc["wifi"]["state"] = wifi_status.state == WIFI_STATE_CONNECTED ? "connected" : "disconnected";
        doc["wifi"]["ssid"] = wifi_status.ssid;
        doc["wifi"]["ip"] = wifi_status.ip;
        doc["wifi"]["rssi"] = wifi_status.rssi;
        doc["wifi"]["quality"] = wifi_status.quality;
    }

    serializeJson(doc, buf, sizeof(buf));
    return send_json_response(req, 200, buf);
}

// --- Register RepelBridge routes on the shared HTTP server ---

static void register_http_routes(httpd_handle_t server) {
    #define REGISTER_URI(path, method_val, handler_fn) do { \
        httpd_uri_t uri = { .uri = path, .method = method_val, .handler = handler_fn, .user_ctx = NULL }; \
        httpd_register_uri_handler(server, &uri); \
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

    ESP_LOGI(TAG, "RepelBridge HTTP routes registered");
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

    // Initialize WiFi provisioning and get the shared HTTP server handle
    httpd_handle_t server = wifi_setup_init();
    if (server == NULL) {
        ESP_LOGE(TAG, "WiFi setup failed");
        return;
    }

    // Register our RepelBridge API routes on the shared server
    register_http_routes(server);

    ESP_LOGI(TAG, "WiFi Controller initialization complete!");
}

void wifi_controller_loop() {
    // Update cartridge monitoring for active buses
    if (bus0.getState() == BUS_WARMING_UP || bus0.getState() == BUS_REPELLING) {
        bus0.poll();
        if (bus0.past_automatic_shutoff()) {
            ESP_LOGI(TAG, "Bus 0 auto-shutoff triggered");
            bus0.powerOff();
        }
    }

    if (bus1.getState() == BUS_WARMING_UP || bus1.getState() == BUS_REPELLING) {
        bus1.poll();
        if (bus1.past_automatic_shutoff()) {
            ESP_LOGI(TAG, "Bus 1 auto-shutoff triggered");
            bus1.powerOff();
        }
    }
}
