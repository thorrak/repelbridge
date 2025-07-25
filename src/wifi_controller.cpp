#include "wifi_controller.h"

// Global instances
WiFiRepellerDevice* wifi_bus0_device = nullptr;
WiFiRepellerDevice* wifi_bus1_device = nullptr;
WebServer* web_server = nullptr;
WiFiManager wifiManager;

// WiFiRepellerDevice implementation
WiFiRepellerDevice::WiFiRepellerDevice(uint8_t id, Bus* bus) : bus_id(id), controlled_bus(bus) {
    Serial.printf("WiFiRepellerDevice created for Bus %d\n", bus_id);
}

WiFiRepellerDevice::~WiFiRepellerDevice() {
    Serial.printf("WiFiRepellerDevice destroyed for Bus %d\n", bus_id);
}

String WiFiRepellerDevice::getBusStatusJson() {
    JsonDocument doc;
    
    doc["bus_id"] = bus_id;
    doc["state"] = controlled_bus->getStateString();
    doc["powered"] = (controlled_bus->getState() != BUS_OFFLINE);
    doc["brightness"] = controlled_bus->repeller_brightness();
    doc["color"]["red"] = controlled_bus->repeller_red();
    doc["color"]["green"] = controlled_bus->repeller_green();
    doc["color"]["blue"] = controlled_bus->repeller_blue();
    doc["repeller_count"] = controlled_bus->getRepellers().size();
    
    String output;
    serializeJson(doc, output);
    return output;
}

String WiFiRepellerDevice::getCartridgeStatusJson() {
    JsonDocument doc;
    
    doc["bus_id"] = bus_id;
    doc["runtime_hours"] = controlled_bus->get_cartridge_runtime_hours();
    doc["percent_left"] = controlled_bus->get_cartridge_percent_left();
    doc["active_seconds"] = controlled_bus->get_cartridge_active_seconds();
    doc["warn_at_hours"] = controlled_bus->get_cartridge_warn_at_seconds() / 3600;
    doc["auto_shutoff_seconds"] = controlled_bus->get_auto_shut_off_after_seconds();
    
    String output;
    serializeJson(doc, output);
    return output;
}

String WiFiRepellerDevice::getSystemStatusJson() {
    JsonDocument doc;
    
    doc["device_name"] = "Liv Repeller WiFi Controller";
    doc["wifi_connected"] = WiFi.isConnected();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime_ms"] = millis();
    
    String output;
    serializeJson(doc, output);
    return output;
}

// Main WiFi controller functions
void wifi_controller_setup() {
    Serial.println("Initializing WiFi Controller...");
    
    // Initialize WiFi devices for both buses
    wifi_bus0_device = new WiFiRepellerDevice(0, &bus0);
    wifi_bus1_device = new WiFiRepellerDevice(1, &bus1);
    
    // Initialize both buses
    bus0.init();
    bus1.init();
    
    // Setup WiFiManager
    wifiManager.setConfigPortalTimeout(300); // 5 minute timeout
    wifiManager.setConnectTimeout(30); // 30 second connect timeout
    
    // Try to connect to WiFi or start config portal
    if (!wifiManager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD)) {
        Serial.println("Failed to connect to WiFi, restarting...");
        delay(3000);
        ESP.restart();
    }
    
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Setup mDNS
    if (!MDNS.begin("liv-repeller")) {
        Serial.println("Error setting up mDNS responder!");
    } else {
        Serial.println("mDNS responder started");
        MDNS.addService(WIFI_MDNS_SERVICE, "tcp", WIFI_WEB_PORT);
    }
    
    // Initialize web server
    web_server = new WebServer(WIFI_WEB_PORT);
    setupWebServerRoutes();
    web_server->begin();
    
    Serial.println("Web server started on port 80");
    Serial.println("WiFi Controller initialization complete!");
}

void wifi_controller_loop() {
    // Handle incoming web requests
    web_server->handleClient();
    
    // Monitor WiFi connection
    if (!WiFi.isConnected()) {
        Serial.println("WiFi disconnected, attempting reconnection...");
        WiFi.reconnect();
    }
    
    // Update cartridge monitoring for active buses
    if (bus0.getState() == BUS_WARMING_UP || bus0.getState() == BUS_REPELLING) {
        if (bus0.past_automatic_shutoff()) {
            Serial.println("Bus 0 auto-shutoff triggered");
            bus0.ZigbeePowerOff();
        }
    }
    
    if (bus1.getState() == BUS_WARMING_UP || bus1.getState() == BUS_REPELLING) {
        if (bus1.past_automatic_shutoff()) {
            Serial.println("Bus 1 auto-shutoff triggered");
            bus1.ZigbeePowerOff();
        }
    }
}

// Helper function to extract bus ID from URL path
int getBusIdFromPath(const String& path) {
    // Extract bus ID from path like /api/bus/0/status
    int start = path.indexOf("/bus/") + 5;
    int end = path.indexOf("/", start);
    if (end == -1) end = path.length();
    return path.substring(start, end).toInt();
}


// REST API endpoint handlers
void handleBusStatus() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    sendJsonResponse(200, device->getBusStatusJson());
}

void handleBusPower() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    // Read form data
    if (web_server->hasArg("state")) {
        String state_str = web_server->arg("state");
        bool power_on = (state_str == "true" || state_str == "1");
        
        if (power_on) {
            device->getBus()->ZigbeePowerOn();
            Serial.printf("Bus %d powered ON via WiFi API\n", bus_id);
        } else {
            device->getBus()->ZigbeePowerOff();
            Serial.printf("Bus %d powered OFF via WiFi API\n", bus_id);
        }
        
        sendJsonResponse(200, device->getBusStatusJson());
    } else {
        sendErrorResponse(400, "Missing state parameter");
    }
}

void handleBusBrightness() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    if (web_server->hasArg("value")) {
        int brightness = web_server->arg("value").toInt();
        if (brightness >= 0 && brightness <= 254) {
            device->getBus()->ZigbeeSetBrightness(brightness);
            Serial.printf("Bus %d brightness set to %d via WiFi API\n", bus_id, brightness);
            sendJsonResponse(200, device->getBusStatusJson());
        } else {
            sendErrorResponse(400, "Brightness must be 0-254");
        }
    } else {
        sendErrorResponse(400, "Missing value parameter");
    }
}

void handleBusColor() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    if (web_server->hasArg("red") && web_server->hasArg("green") && web_server->hasArg("blue")) {
        int red = web_server->arg("red").toInt();
        int green = web_server->arg("green").toInt();
        int blue = web_server->arg("blue").toInt();
        
        if (red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 && blue <= 255) {
            device->getBus()->ZigbeeSetRGB(red, green, blue);
            Serial.printf("Bus %d color set to RGB(%d,%d,%d) via WiFi API\n", bus_id, red, green, blue);
            sendJsonResponse(200, device->getBusStatusJson());
        } else {
            sendErrorResponse(400, "RGB values must be 0-255");
        }
    } else {
        sendErrorResponse(400, "Missing red, green, or blue parameters");
    }
}

void handleBusCartridgeStatus() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    sendJsonResponse(200, device->getCartridgeStatusJson());
}

void handleBusCartridgeReset() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    device->getBus()->ZigbeeResetCartridge();
    Serial.printf("Bus %d cartridge reset via WiFi API\n", bus_id);
    sendJsonResponse(200, device->getCartridgeStatusJson());
}

void handleBusAutoShutoff() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    if (web_server->method() == HTTP_GET) {
        JsonDocument doc;
        doc["bus_id"] = bus_id;
        doc["auto_shutoff_minutes"] = device->getBus()->get_auto_shut_off_after_seconds() / 60;
        
        String output;
        serializeJson(doc, output);
        sendJsonResponse(200, output);
    } else if (web_server->method() == HTTP_POST) {
        if (web_server->hasArg("minutes")) {
            int minutes = web_server->arg("minutes").toInt();
            
            if (minutes >= 0 && minutes <= 960) {
                int seconds = minutes * 60;
                device->getBus()->ZigbeeSetAutoShutOffAfterSeconds(seconds);
                Serial.printf("Bus %d auto shutoff set to %d minutes (%d seconds) via WiFi API\n", bus_id, minutes, seconds);
                sendJsonResponse(200, device->getCartridgeStatusJson());
            } else {
                sendErrorResponse(400, "Auto shutoff must be 0-960 minutes");
            }
        } else {
            sendErrorResponse(400, "Missing minutes parameter");
        }
    } else {
        sendErrorResponse(405, "Method not allowed");
    }
}



void handleBusCartridgeWarnAt() {
    int bus_id = getBusIdFromPath(web_server->uri());
    WiFiRepellerDevice* device = getDeviceByBusId(bus_id);
    
    if (!device) {
        sendErrorResponse(404, "Bus not found");
        return;
    }
    
    if (web_server->method() == HTTP_GET) {
        JsonDocument doc;
        doc["bus_id"] = bus_id;
        doc["warn_at_hours"] = device->getBus()->get_cartridge_warn_at_seconds() / 3600;
        
        String output;
        serializeJson(doc, output);
        sendJsonResponse(200, output);
    } else if (web_server->method() == HTTP_POST) {
        if (web_server->hasArg("hours")) {
            int hours = web_server->arg("hours").toInt();
            if (hours >= 0) {
                uint32_t seconds = hours * 3600;
                device->getBus()->ZigbeeSetCartridgeWarnAtSeconds(seconds);
                Serial.printf("Bus %d cartridge warn threshold set to %d hours via WiFi API\n", bus_id, hours);
                sendJsonResponse(200, device->getCartridgeStatusJson());
            } else {
                sendErrorResponse(400, "Hours must be >= 0");
            }
        } else {
            sendErrorResponse(400, "Missing hours parameter");
        }
    }
}



void handleSystemStatus() {
    // Return combined system status
    JsonDocument doc;
    
    doc["device_name"] = "Liv Repeller WiFi Controller";
    doc["wifi_connected"] = WiFi.isConnected();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime_ms"] = millis();
    
    // Add bus statuses
    doc["bus0"]["state"] = bus0.getStateString();
    doc["bus0"]["repeller_count"] = bus0.getRepellers().size();
    doc["bus1"]["state"] = bus1.getStateString();
    doc["bus1"]["repeller_count"] = bus1.getRepellers().size();
    
    String output;
    serializeJson(doc, output);
    sendJsonResponse(200, output);
}

void handleNotFound() {
    sendErrorResponse(404, "Endpoint not found");
}

// Helper functions
WiFiRepellerDevice* getDeviceByBusId(uint8_t bus_id) {
    if (bus_id == 0) return wifi_bus0_device;
    if (bus_id == 1) return wifi_bus1_device;
    return nullptr;
}

void setupWebServerRoutes() {
    // Bus 0 control endpoints
    web_server->on("/api/bus/0/status", HTTP_GET, handleBusStatus);
    web_server->on("/api/bus/0/power", HTTP_POST, handleBusPower);
    web_server->on("/api/bus/0/brightness", HTTP_POST, handleBusBrightness);
    web_server->on("/api/bus/0/color", HTTP_POST, handleBusColor);
    web_server->on("/api/bus/0/cartridge", HTTP_GET, handleBusCartridgeStatus);
    web_server->on("/api/bus/0/cartridge/reset", HTTP_POST, handleBusCartridgeReset);
    web_server->on("/api/bus/0/auto_shutoff", HTTP_GET, handleBusAutoShutoff);
    web_server->on("/api/bus/0/auto_shutoff", HTTP_POST, handleBusAutoShutoff);
    web_server->on("/api/bus/0/warn_at", HTTP_GET, handleBusCartridgeWarnAt);
    web_server->on("/api/bus/0/warn_at", HTTP_POST, handleBusCartridgeWarnAt);
    
    // Bus 1 control endpoints
    web_server->on("/api/bus/1/status", HTTP_GET, handleBusStatus);
    web_server->on("/api/bus/1/power", HTTP_POST, handleBusPower);
    web_server->on("/api/bus/1/brightness", HTTP_POST, handleBusBrightness);
    web_server->on("/api/bus/1/color", HTTP_POST, handleBusColor);
    web_server->on("/api/bus/1/cartridge", HTTP_GET, handleBusCartridgeStatus);
    web_server->on("/api/bus/1/cartridge/reset", HTTP_POST, handleBusCartridgeReset);
    web_server->on("/api/bus/1/auto_shutoff", HTTP_GET, handleBusAutoShutoff);
    web_server->on("/api/bus/1/auto_shutoff", HTTP_POST, handleBusAutoShutoff);
    web_server->on("/api/bus/1/warn_at", HTTP_GET, handleBusCartridgeWarnAt);
    web_server->on("/api/bus/1/warn_at", HTTP_POST, handleBusCartridgeWarnAt);
    
    // System endpoints
    web_server->on("/api/system/status", HTTP_GET, handleSystemStatus);
    
    // Handle OPTIONS requests for CORS and 404s
    web_server->onNotFound([]() {
        if (web_server->method() == HTTP_OPTIONS) {
            web_server->sendHeader("Access-Control-Allow-Origin", "*");
            web_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            web_server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
            web_server->send(200, "text/plain", "");
        } else {
            handleNotFound();
        }
    });
    
    Serial.println("Web server routes configured");
}

void sendJsonResponse(int status_code, const String& json) {
    web_server->sendHeader("Access-Control-Allow-Origin", "*");
    web_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    web_server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    web_server->send(status_code, "application/json", json);
}

void sendErrorResponse(int status_code, const String& error_message) {
    JsonDocument doc;
    doc["error"] = error_message;
    doc["status"] = status_code;
    
    String output;
    serializeJson(doc, output);
    sendJsonResponse(status_code, output);
}