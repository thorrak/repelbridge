#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "esp_http_server.h"

// Initialize esp_wifi_config and the shared HTTP server.
// Must be called after nvs_flash_init().
// Returns the HTTPD handle for registering additional routes.
httpd_handle_t wifi_setup_init();

#endif // WIFI_SETUP_H
