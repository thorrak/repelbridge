#include "bus.h"
#include "known_packets.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_vfs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cinttypes>
#include <cstdio>
#include <sys/stat.h>

static const char* TAG = "bus";

// UART configuration
#define RS485_UART_NUM UART_NUM_1
#define RS485_BAUD_RATE 19200
#define RS485_BUF_SIZE 256

// How often the cartridge runtime counter is flushed to the filesystem while a
// bus is running. It used to be written only on a graceful power-off, so any
// reset or power loss discarded the entire running session.
#define CARTRIDGE_SAVE_INTERVAL_US (300ULL * 1000000ULL)

static uint32_t millis_now() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// Both buses share RS485_UART_NUM, and the HTTP handlers (httpd task) drive the
// same Bus objects as the periodic poll in the main task. Every entry point that
// touches the UART or mutates bus state serialises on this recursive mutex, so a
// long transaction (discovery, warm-up) cannot have the UART pulled out from
// under it by the other task.
static SemaphoreHandle_t bus_lock() {
  static SemaphoreHandle_t lock = xSemaphoreCreateRecursiveMutex();
  return lock;
}

class BusLock {
public:
  BusLock() { xSemaphoreTakeRecursive(bus_lock(), portMAX_DELAY); }
  ~BusLock() { xSemaphoreGiveRecursive(bus_lock()); }
  BusLock(const BusLock&) = delete;
  BusLock& operator=(const BusLock&) = delete;
};

// Settings live behind their own short-held mutex so that reading or changing a
// stored value never queues behind a multi-second RS-485 transaction holding the
// bus lock. Lock order is always bus_lock() -> settings_lock(), never the
// reverse.
static SemaphoreHandle_t settings_lock() {
  static SemaphoreHandle_t lock = xSemaphoreCreateRecursiveMutex();
  return lock;
}

class SettingsLock {
public:
  SettingsLock() { xSemaphoreTakeRecursive(settings_lock(), portMAX_DELAY); }
  ~SettingsLock() { xSemaphoreGiveRecursive(settings_lock()); }
  SettingsLock(const SettingsLock&) = delete;
  SettingsLock& operator=(const SettingsLock&) = delete;
};

#define NO_ACTIVE_BUS 0xFF

static uint8_t active_bus_id = NO_ACTIVE_BUS;  // Which bus currently owns RS485_UART_NUM
static bool uart_installed = false;            // Whether the UART driver is currently installed


// Constructor - initialize bus with ID and set pin assignments
Bus::Bus(uint8_t id) : bus_id(id), bus_state(BUS_OFFLINE), 
                       warm_on_at(0), active_seconds_last_save_at(0),
                       last_polled(0),
                       power_request(BUS_POWER_REQ_NONE), repeller_total(0),
                       red(0x03), green(0xd5), blue(0xff), brightness(100), cartridge_active_seconds(0),
                       cartridge_warn_at_seconds(349200), auto_shut_off_after_seconds(18000) {
  // Set pin assignments based on bus ID
  if (bus_id == 0) {
    tx_pin = BUS_0_TX_PIN;
    rx_pin = BUS_0_RX_PIN;
    dir_pin = BUS_0_DIR_PIN;
    pow_pin = BUS_0_POW_PIN;
  } else if (bus_id == 1) {
#ifdef BUS_1_TX_PIN
    tx_pin = BUS_1_TX_PIN;
    rx_pin = BUS_1_RX_PIN;
    dir_pin = BUS_1_DIR_PIN;
    pow_pin = BUS_1_POW_PIN;
#else
    // Bus 1 not defined, set to invalid
    tx_pin = -1;
    rx_pin = -1;
    dir_pin = -1;
    pow_pin = -1;
#endif
  } else {
    // Invalid bus ID
    tx_pin = -1;
    rx_pin = -1;
    dir_pin = -1;
    pow_pin = -1;
  }
}

// Initialize the bus (set the initial state for the pins)
void Bus::init() {
  BusLock lock;

  if (dir_pin == -1) {
    ESP_LOGE(TAG, "Bus %d: Invalid pin configuration", bus_id);
    bus_state = BUS_ERROR;
    return;
  }

  bus_state = BUS_OFFLINE;

  gpio_set_direction((gpio_num_t)dir_pin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)dir_pin, 0);  // Start in receive mode

  if(pow_pin != -1) {
    ESP_LOGI(TAG, "Bus %d: output pow pin low", bus_id);
    gpio_set_direction((gpio_num_t)pow_pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pow_pin, 0);  // Start the bus off powered off
  }

  // Load settings from filesystem
  load_settings();
}

// Give this bus ownership of the shared RS-485 UART. Does not touch bus power.
// Tracks driver installation separately from ownership: powerdown() releases
// ownership, and the previous code then skipped the delete and re-installed on
// top of a live driver, which fails with ESP_ERR_INVALID_STATE.
bool Bus::configure_uart() {
  if (tx_pin == -1 || rx_pin == -1 || dir_pin == -1) {
    ESP_LOGE(TAG, "Bus %d: Invalid pin configuration", bus_id);
    bus_state = BUS_ERROR;
    return false;
  }

  if (active_bus_id == bus_id && uart_installed) {
    return true;  // Already ours
  }

  if (uart_installed) {
    uart_driver_delete(RS485_UART_NUM);
    uart_installed = false;
    active_bus_id = NO_ACTIVE_BUS;
  }

  uart_config_t uart_config = {
    .baud_rate = RS485_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_param_config(RS485_UART_NUM, &uart_config);
  if (err == ESP_OK) {
    err = uart_set_pin(RS485_UART_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  if (err == ESP_OK) {
    err = uart_driver_install(RS485_UART_NUM, RS485_BUF_SIZE, 0, 0, NULL, 0);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Bus %d: UART setup failed (%s)", bus_id, esp_err_to_name(err));
    bus_state = BUS_ERROR;
    return false;
  }

  uart_installed = true;
  active_bus_id = bus_id;
  uart_flush_input(RS485_UART_NUM);  // Clear any existing data

  ESP_LOGI(TAG, "Bus %d: took ownership of the RS-485 UART", bus_id);
  return true;
}

// Activate the bus (Power on the bus (if unpowered) and configure UART)
void Bus::activate() {
  BusLock lock;

  if(bus_state == BUS_ERROR) {
    // Deliberately not calling powerdown() here - it resets bus_state to
    // BUS_OFFLINE, which made a permanently misconfigured bus look recoverable
    // and let the next activate() drive GPIO -1.
    ESP_LOGE(TAG, "Bus %d: Cannot activate bus in error state", bus_id);
    return;
  }

  if(dir_pin == -1) {
    ESP_LOGE(TAG, "Bus %d: Invalid pin configuration", bus_id);
    bus_state = BUS_ERROR;
    return;
  }

  gpio_set_level((gpio_num_t)dir_pin, 0);  // Start in receive mode

  if(bus_state == BUS_OFFLINE) {
    if(pow_pin != -1) {
      gpio_set_level((gpio_num_t)pow_pin, 1);  // Power on the bus
      vTaskDelay(pdMS_TO_TICKS(1000)); // Allow 1s for bus to power up
    }
    bus_state = BUS_POWERED;
  }

  configure_uart();
}

// Power down (deactivate) the bus
// NOTE - Does not send the powerdown command to the repellers first, so if there is no power pin, the repellers will keep running.
void Bus::powerdown() {
  BusLock lock;

  if(bus_state != BUS_OFFLINE) {
    if(pow_pin != -1) {
      gpio_set_level((gpio_num_t)pow_pin, 0);  // Power off the bus
      ESP_LOGI(TAG, "Bus %d: powerdown: pin set low", bus_id);
    } else {
      ESP_LOGI(TAG, "Bus %d: powerdown: no power pin", bus_id);
    }
    if(bus_state != BUS_ERROR) {
      bus_state = BUS_OFFLINE;  // A configuration error is not cleared by powering down
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: powerdown: bus already offline", bus_id);
  }

  warm_on_at = 0;  // The auto shut-off window restarts at the next warm-up

  if(active_bus_id == bus_id) {
    if(uart_installed) {
      uart_driver_delete(RS485_UART_NUM);
      uart_installed = false;
    }
    active_bus_id = NO_ACTIVE_BUS;
  }
}

// Transmit packet on this bus
void Bus::transmit(Packet *packet) {
  BusLock lock;

  if (!packet) {
    ESP_LOGE(TAG, "Bus %d: Cannot transmit null packet", bus_id);
    return;
  }

  // Transmitting must not power the bus up as a side effect. This used to call
  // activate(), so a colour or brightness request against an idle bus silently
  // switched the rail on and left it energised with nothing running on it.
  // Callers that want the bus live call activate()/powerOn() explicitly.
  if (bus_state == BUS_OFFLINE || bus_state == BUS_ERROR) {
    ESP_LOGW(TAG, "Bus %d: Not transmitting, bus is %s", bus_id, getStateString());
    return;
  }

  if (!configure_uart()) {
    return;
  }

  // Set RS-485 transceiver to transmit mode for this bus
  gpio_set_level((gpio_num_t)dir_pin, 1);
  esp_rom_delay_us(20);  // Give DE time to enable

  // Send the packet
  uart_write_bytes(RS485_UART_NUM, packet->data, sizeof(packet->data));
  uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(100));  // Wait until transmission complete

  // Back to receive mode
  esp_rom_delay_us(20);  // Give time for transmission to complete
  gpio_set_level((gpio_num_t)dir_pin, 0);
  vTaskDelay(pdMS_TO_TICKS(100));  // Allow some time before next operation
}

// Helper function to receive and print a packet with timeout
bool Bus::receive_and_print(const char* expected_type, uint16_t timeout_ms) {
  Packet received_packet;

  if (receive_packet(received_packet, timeout_ms)) {
    received_packet.print();
    return true;
  } else {
    ESP_LOGW(TAG, "Bus %d TIMEOUT: Expected %s but no response received", bus_id, expected_type);
    return false;
  }
}

// Packet-based receive function
bool Bus::receive_packet(Packet& packet, uint16_t timeout_ms) {
  BusLock lock;

  // Per-call state. These were function statics, which meant a partial frame
  // left behind by one bus could be spliced onto the next bus's packet - and
  // any fragment older than PACKET_TIMEOUT_MS is discarded anyway, so there was
  // nothing to carry across calls.
  uint8_t rx_buffer[11];
  size_t buffer_index = 0;
  uint32_t last_byte_time = 0;
  bool packet_in_progress = false;

  const uint32_t PACKET_TIMEOUT_MS = 8;  // Same as sniffer
  uint32_t start_time = millis_now();
  uint32_t current_time;

  if (bus_state == BUS_OFFLINE || bus_state == BUS_ERROR) {
    return false;
  }

  // Ensure this bus owns the UART and we're in receive mode
  if (!configure_uart()) {
    return false;
  }
  gpio_set_level((gpio_num_t)dir_pin, 0);

  while (true) {
    current_time = millis_now();

    // Check for overall timeout
    if (timeout_ms > 0 && (current_time - start_time) >= timeout_ms) {
      return false;
    }

    // Check for packet timeout (gap between bytes)
    if (packet_in_progress && (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
      if (buffer_index == 11) {
        // Complete 11-byte packet received
        packet.setData(rx_buffer);
        buffer_index = 0;
        packet_in_progress = false;
        return true;
      } else {
        // Incomplete packet, reset
        buffer_index = 0;
        packet_in_progress = false;
      }
    }

    // Check how many bytes are available
    size_t available = 0;
    uart_get_buffered_data_len(RS485_UART_NUM, &available);

    // Read available data
    while (available > 0) {
      uint8_t byte_received;
      int len = uart_read_bytes(RS485_UART_NUM, &byte_received, 1, 0);
      if (len <= 0) break;

      current_time = millis_now();

      // If we haven't received data for a while, this might be a new packet
      if (!packet_in_progress || (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
        if (packet_in_progress && buffer_index == 11) {
          // Previous complete packet available
          packet.setData(rx_buffer);
          buffer_index = 0;
          packet_in_progress = false;
          return true;
        }
        buffer_index = 0;
        packet_in_progress = true;
      }

      // Check if this byte is 0xAA (sync byte) and we're not at the start of a packet
      if (byte_received == 0xAA && buffer_index > 0) {
        // We found a sync byte but we already have data in the buffer
        // This indicates extra bytes before the real packet
        char hex_buf[34];
        for (size_t i = 0; i < buffer_index; i++) {
          snprintf(&hex_buf[i * 3], 4, "%02X ", rx_buffer[i]);
        }
        ESP_LOGD(TAG, "Bus %d: Found 0xAA at position %d, discarding %d bytes: %s", bus_id, buffer_index, buffer_index, hex_buf);

        // Reset buffer and start fresh with this 0xAA byte
        buffer_index = 0;
        packet_in_progress = true;
      }

      // Add byte to buffer
      if (buffer_index < 11) {
        rx_buffer[buffer_index++] = byte_received;
      } else {
        // Buffer overflow, reset
        buffer_index = 0;
        packet_in_progress = false;
      }

      last_byte_time = current_time;

      // Check if we have a complete packet
      if (buffer_index == 11) {
        packet.setData(rx_buffer);
        buffer_index = 0;
        packet_in_progress = false;
        return true;
      }

      // Re-check available
      uart_get_buffered_data_len(RS485_UART_NUM, &available);
    }

    // Non-blocking mode
    if (timeout_ms == 0) {
      return false;
    }
    
    // Small delay to prevent overwhelming the processor. Kept well under
    // PACKET_TIMEOUT_MS so short timeouts (discovery uses 100ms) still get a
    // useful number of polling passes.
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Fixed packet transmission functions
void Bus::send_tx_discover() {
  Packet packet;
  packet.setAsTxDiscover();
  transmit(&packet);
}

void Bus::send_tx_powerup() {
  Packet packet;
  packet.setAsTxPowerup();
  transmit(&packet);
}

void Bus::send_tx_powerdown() {
  Packet packet;
  packet.setAsTxPowerdown();
  transmit(&packet);
}

void Bus::send_tx_heartbeat(uint8_t address) {
  Packet packet;
  packet.setAsTxHeartbeat(address);
  transmit(&packet);
}

void Bus::send_tx_led_on_conf(uint8_t address) {
  Packet packet;
  packet.setAsTxLEDOnConf(address);
  transmit(&packet);
}

void Bus::send_tx_ser_no_1(uint8_t address) {
  Packet packet;
  packet.setAsTxSerNo1(address);
  transmit(&packet);
}

void Bus::send_tx_ser_no_2(uint8_t address) {
  Packet packet;
  packet.setAsTxSerNo2(address);
  transmit(&packet);
}

void Bus::send_tx_warmup(uint8_t address) {
  Packet packet;
  packet.setAsTxWarmup(address);
  transmit(&packet);
}

void Bus::send_tx_warmup_complete(uint8_t address) {
  Packet packet;
  packet.setAsTxWarmupComp(address);
  transmit(&packet);
}

void Bus::send_tx_led_brightness(uint8_t address, uint8_t brightness) {
  Packet packet;
  packet.setAsTxLED(address, brightness);
  transmit(&packet);
}

void Bus::send_tx_led_brightness_startup(uint8_t address, uint8_t brightness) {
  Packet packet;
  packet.setAsTxLEDStartup(address, brightness);
  transmit(&packet);
}

void Bus::send_tx_color(uint8_t red, uint8_t green, uint8_t blue) {
  // Note that this one broadcasts the color to all devices
  Packet packet;
  packet.setAsTxColor(red, green, blue);
  transmit(&packet);
}

void Bus::send_tx_color_confirm(uint8_t green, uint8_t blue) {
  // Send the color confirmation packet: AA 8E 03 08 YY ZZ 00 00 00 00 00
  // No idea why the green and blue values are in YY and ZZ, (and the red is missing) but that's how it is
  Packet packet;
  packet.setAsTxColorConfirm(green, blue);
  transmit(&packet);
}

void Bus::send_tx_color_startup(uint8_t address, uint8_t red, uint8_t green, uint8_t blue) {
  Packet packet;
  packet.setAsTxColorStartup(address, red, green, blue);
  transmit(&packet);
}

void Bus::send_tx_startup_comp(uint8_t address) {
  Packet packet;
  packet.setAsTxStartupComp(address);
  transmit(&packet);
}

void Bus::send_set_address(uint8_t address) {
  Packet packet;
  packet.setAsSetAddress(address);
  transmit(&packet);
}

// Repeller management functions
Repeller* Bus::get_repeller(uint8_t address) {
  for (auto& repeller : repellers) {
    if (repeller.address == address) {
      return &repeller;
    }
  }
  return nullptr;
}

Repeller* Bus::get_or_create_repeller(uint8_t address) {
  // First try to find existing repeller
  Repeller* existing = get_repeller(address);
  if (existing != nullptr) {
    return existing;
  }
  
  // Create new repeller and add to list
  repellers.emplace_back(address);
  repeller_total.store(repellers.size(), std::memory_order_relaxed);
  return &repellers.back();
}

uint8_t Bus::find_next_address() {
  uint8_t next_address = 0x01;  // Start looking for available address from 0x01
  for(next_address = 0x01; next_address <= 0x1F; next_address++) {
    if (get_repeller(next_address) == nullptr) {
      // Found an available address
      return next_address;
    }
  }
  return 0x20; // Technically, this should be an error.
}


// Discover all repellers on the bus by sending broadcast tx_startup commands
void Bus::discover_repellers() {
  BusLock lock;
  ESP_LOGI(TAG, "Bus %d: Discovering repellers on the bus...", bus_id);

  // Anything already known starts as OFFLINE and is only brought back by an
  // actual response. Previously a device that had been unplugged was carried
  // forward as if it were still present and got warmed up along with the rest.
  for (auto& repeller : repellers) {
    repeller.state = OFFLINE;
  }

  Packet received_packet;
  int consecutive_no_response = 0;
  int total_discovered = 0;
  
  while (consecutive_no_response < 3) {
    // Send broadcast tx_startup
    ESP_LOGI(TAG, "Bus %d: Sending tx_discover broadcast...", bus_id);
    send_tx_discover();
    
    // Wait for response with 100ms timeout
    if (receive_packet(received_packet, 100)) {
      if(received_packet.identifyPacket() == RX_STARTUP) {
        uint8_t device_address = received_packet.getAddress();
        ESP_LOGI(TAG, "Bus %d: Discovered repeller at address 0x%02X", bus_id, device_address);
        
        // Create or get the repeller
        Repeller* repeller = get_or_create_repeller(device_address);
        repeller->state = INACTIVE;
        
        total_discovered++;
        consecutive_no_response = 0;  // Reset counter
        
      } else if(received_packet.identifyPacket() == RX_STARTUP_00) {
        // Special case for RX_STARTUP_00, which indicates the repeller is not set up yet
        ESP_LOGI(TAG, "Bus %d: Received RX_STARTUP_00, indicating no address set yet", bus_id);


        // Find an address that isn't already taken
        uint8_t available_address = find_next_address();

        if(available_address == 0x20) {
          ESP_LOGI(TAG, "Bus %d: No available addresses found for new repeller", bus_id);
          consecutive_no_response++;
          continue;  // Skip to next iteration
        }

        // Once we've found an available address, we can set it on the repeller
        ESP_LOGI(TAG, "Bus %d: Setting repeller address to 0x%02X", bus_id, available_address);
        send_set_address(available_address);

        // I THINK there is a response here that I could read, which I THINK is an incomplete packet 
        if (receive_packet(received_packet, 500)) {
          ESP_LOGI(TAG, "Bus %d: Received set response packet", bus_id);
          received_packet.print();
        }

        // The repeller should theoretically respond to the next tx_discover - but let's add it to the list now
        // so we don't try to create a duplicate.
        // Create a new repeller with the available address
        Repeller* repeller = get_or_create_repeller(available_address);
        repeller->state = INACTIVE;
        
        total_discovered++;
        consecutive_no_response = 0;  // Reset counter
      } else {
        // Received packet but not rx_startup, print it for debugging
        received_packet.print();
        consecutive_no_response++;
      }
    } else {
      // No response received
      ESP_LOGI(TAG, "Bus %d: No response to tx_discover", bus_id);
      consecutive_no_response++;
    }
  }
  
  ESP_LOGI(TAG, "Bus %d: Repeller discovery complete. Found %d devices.", bus_id, total_discovered);
  
  // Print discovered repellers
  if (total_discovered > 0) {
    ESP_LOGI(TAG, "Bus %d: Discovered repellers:", bus_id);
    for (const auto& repeller : repellers) {
      ESP_LOGI(TAG, "  Address: 0x%02X, State: %s", repeller.address, repeller.getStateString());
    }
  }
}

void Bus::retrieve_serial(Repeller* repeller) {
  if (!repeller) {
    ESP_LOGI(TAG, "Bus %d: Invalid repeller pointer", bus_id);
    return;
  }
  
  // Send tx_ser_no_1 and wait for response
  send_tx_ser_no_1(repeller->address);
  Packet response_packet;
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_SER_NO_1) {
      // Extract serial number part 1. The payload is NUL padded, so stop at the
      // first non-printable byte rather than substituting a placeholder.
      char serial_part1[9];
      int len1 = 0;
      while (len1 < 8 && response_packet.data[len1 + 3] >= 32 && response_packet.data[len1 + 3] <= 126) {
        serial_part1[len1] = (char)response_packet.data[len1 + 3];
        len1++;
      }
      serial_part1[len1] = '\0'; // Null-terminate the string
      
      // Send tx_ser_no_2 and wait for response
      send_tx_ser_no_2(repeller->address);
      
      if (receive_packet(response_packet, 1000)) {
        if (response_packet.identifyPacket() == RX_SER_NO_2) {
          // Extract serial number part 2 (same NUL padded layout as part 1)
          char serial_part2[9];
          int len2 = 0;
          while (len2 < 8 && response_packet.data[len2 + 3] >= 32 && response_packet.data[len2 + 3] <= 126) {
            serial_part2[len2] = (char)response_packet.data[len2 + 3];
            len2++;
          }
          serial_part2[len2] = '\0'; // Null-terminate the string
          
          // Combine both parts into the repeller's serial
          repeller->setSerial(serial_part1, serial_part2);
          ESP_LOGI(TAG, "Bus %d: Retrieved serial number: %s", bus_id, repeller->serial);
        } else {
          ESP_LOGI(TAG, "Bus %d: Failed to retrieve serial number part 2", bus_id);
        }
      } else {
        ESP_LOGI(TAG, "Bus %d: No response for tx_ser_no_2", bus_id);
      }
    } else {
      ESP_LOGI(TAG, "Bus %d: Failed to retrieve serial number part 1", bus_id);
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: No response for tx_ser_no_1", bus_id);
  }
}

void Bus::send_tx_warmup(Repeller* repeller) {
  if (!repeller) {
    ESP_LOGI(TAG, "Bus %d: Invalid repeller pointer", bus_id);
    return;
  }
  
  // Send tx_warmup and wait for response
  send_tx_warmup(repeller->address);
  Packet response_packet;
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_WARMUP) {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X warming up", bus_id, repeller->address);
    } else {
      // TODO - Print the packet here
      ESP_LOGI(TAG, "Bus %d: Invalid response packet received", bus_id);
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X failed to respond to TX_WARMUP", bus_id, repeller->address);
  }
}

void Bus::send_startup_led_params(Repeller* repeller) {
  if (!repeller) {
    ESP_LOGI(TAG, "Bus %d: Invalid repeller pointer", bus_id);
    return;
  }
  
  ESP_LOGI(TAG, "Bus %d: Setting startup LED parameters for repeller 0x%02X...", bus_id, repeller->address);
  
  // There are three parts to this:
  // 1. Send tx_color_startup with red, green, blue values and then look for rx_color_startup
  ESP_LOGI(TAG, "Bus %d: Setting startup color for repeller 0x%02X...", bus_id, repeller->address);
  // Use the configured color from settings
  send_tx_color_startup(repeller->address, repeller_red(), repeller_green(), repeller_blue());
  
  Packet response_packet;
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_COLOR_STARTUP) {
      // Note - The response contains the color values as well (I think??), but the colors do not necessarily match what we sent
      // For now, I'm just ignoring them.
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X confirmed startup color", bus_id, repeller->address);
    } else {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected color response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: No color startup response from repeller 0x%02X", bus_id, repeller->address);
  }
  
  // 2. Send tx_led_brightness_startup with brightness value and then look for rx_led_brightness_startup
  ESP_LOGI(TAG, "Bus %d: Setting startup brightness for repeller 0x%02X...", bus_id, repeller->address);
  // Use the configured brightness from settings
  send_tx_led_brightness_startup(repeller->address, repeller_brightness());
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_LED_BRIGHTNESS_STARTUP) {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X confirmed startup brightness", bus_id, repeller->address);
    } else {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected brightness response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: No LED startup response from repeller 0x%02X", bus_id, repeller->address);
  }
  
  // 3. Send tx_startup_comp and then look for rx_startup_comp
  ESP_LOGI(TAG, "Bus %d: Sending startup complete to repeller 0x%02X...", bus_id, repeller->address);
  send_tx_startup_comp(repeller->address);
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_STARTUP_COMP) {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X confirmed startup complete", bus_id, repeller->address);
    } else {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected startup complete response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: No startup complete response from repeller 0x%02X", bus_id, repeller->address);
  }
  
  ESP_LOGI(TAG, "Bus %d: LED parameter setup complete for repeller 0x%02X", bus_id, repeller->address);
}

void Bus::send_led_on_to_repeller(Repeller *repeller) {
  Packet response_packet;
  // 2. send_tx_led_on_conf and look for RX_LED_ON_CONF
  ESP_LOGI(TAG, "Bus %d: Sending LED on confirmation to repeller 0x%02X...", bus_id, repeller->address);
  send_tx_led_on_conf(repeller->address);
  
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_LED_ON_CONF) {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X confirmed LED activation", bus_id, repeller->address);
    } else {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected LED on response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: No LED on confirmation response from repeller 0x%02X", bus_id, repeller->address);
  }
}

void Bus::send_activate_at_end_of_warmup(Repeller *repeller) {
  if (!repeller) {
    ESP_LOGI(TAG, "Bus %d: Invalid repeller pointer", bus_id);
    return;
  }

  ESP_LOGI(TAG, "Bus %d: Activating repeller 0x%02X at end of warmup...", bus_id, repeller->address);

  // This function is called after every repeller has been warmed up to actually activate the repeller
  // Not sure if this does anything other than turn on the LEDs, but that's enough!

  // 1. send_tx_warmup_complete and look for RX_WARMUP_COMPLETE
  ESP_LOGI(TAG, "Bus %d: Sending warmup complete to repeller 0x%02X...", bus_id, repeller->address);
  send_tx_warmup_complete(repeller->address);
  
  Packet response_packet;
  if (receive_packet(response_packet, 1000)) {
    if (response_packet.identifyPacket() == RX_WARMUP_COMPLETE) {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X confirmed warmup complete", bus_id, repeller->address);
    } else {
      ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected warmup complete response: ", bus_id, repeller->address);
      response_packet.print();
    }
  } else {
    ESP_LOGI(TAG, "Bus %d: No warmup complete response from repeller 0x%02X", bus_id, repeller->address);
  }
  
  // 2. send_tx_led_on_conf and look for RX_LED_ON_CONF
  send_led_on_to_repeller(repeller);
  
  ESP_LOGI(TAG, "Bus %d: Activation complete for repeller 0x%02X", bus_id, repeller->address);
}

void Bus::retrieve_serial_for_all() {
  BusLock lock;
  ESP_LOGI(TAG, "Bus %d: Retrieving serial numbers for all discovered repellers...", bus_id);
  
  for (auto& repeller : repellers) {
    if (repeller.state == OFFLINE) {
      continue;  // Did not answer discovery
    }
    if(strlen(repeller.serial) > 0) {
      // We only need to retrieve serial once
      ESP_LOGI(TAG, "Bus %d: Repeller at address 0x%02X already has serial: %s", bus_id, repeller.address, repeller.serial);
      continue;  // Skip if serial already retrieved
    } else {
      ESP_LOGI(TAG, "Bus %d: Retrieving serial for repeller at address 0x%02X...", bus_id, repeller.address);
      retrieve_serial(&repeller);
    }
  }
  
  ESP_LOGI(TAG, "Bus %d: Serial retrieval complete.", bus_id);
}

void Bus::warm_up_all() {
  BusLock lock;

  ESP_LOGI(TAG, "Bus %d: Warming up all repellers...", bus_id);

  size_t present = 0;
  for (const auto& repeller : repellers) {
    if (repeller.state != OFFLINE) present++;
  }

  if (present == 0) {
    // Without this the bus parked in BUS_WARMING_UP with nothing on it and sat
    // there until the auto shut-off fired.
    ESP_LOGW(TAG, "Bus %d: No repellers responded - nothing to warm up", bus_id);
    return;
  }

  send_tx_powerup();  // This is the command that actually powers up the repellers

  warm_on_at = esp_timer_get_time();  // Record the time when the repellers were turned on
  active_seconds_last_save_at = warm_on_at;  // Initialize the last save time to the warm on time
  bus_state = BUS_WARMING_UP;
  
  for (auto& repeller : repellers) {
    if (repeller.state == OFFLINE) continue;
    repeller.state = WARMING_UP;
    repeller.turned_on_at = esp_timer_get_time();  // Record the time when the repeller was turned on
    ESP_LOGI(TAG, "Bus %d: Sending warmup instruction to repeller at address 0x%02X...", bus_id, repeller.address);
    send_tx_warmup(&repeller);  // Not sure entirely what this does
  }
  
  vTaskDelay(pdMS_TO_TICKS(4000));  // Wait for 4 seconds to allow warmup to start

  for (auto& repeller : repellers) {
    if (repeller.state == OFFLINE) continue;
    ESP_LOGI(TAG, "Bus %d: Sending LED parameters to repeller at address 0x%02X...", bus_id, repeller.address);
    send_startup_led_params(&repeller);
  }

  ESP_LOGI(TAG, "Bus %d: Sent warmup & initial LED instructions to all repellers.", bus_id);
}

void Bus::end_warm_up_all() {
  BusLock lock;

  ESP_LOGI(TAG, "Bus %d: Activating all repellers (ending warm up)...", bus_id);

  send_tx_powerup();  // This is the command that actually powers up the repellers
  
  for (auto& repeller : repellers) {
    if (repeller.state == OFFLINE) continue;
    repeller.state = ACTIVE;
    ESP_LOGI(TAG, "Bus %d: Activating (ending warm up) repeller at address 0x%02X...", bus_id, repeller.address);
    send_activate_at_end_of_warmup(&repeller);
  }

  bus_state = BUS_REPELLING;
  ESP_LOGI(TAG, "Bus %d: Activated all repellers.", bus_id);
}

bool Bus::heartbeat_poll() {
  // Send the heartbeat command (`send_tx_heartbeat`) to each repeller, and read the response. Update the status of each repeller based on the response.
  // If the response is RX_WARMUP, the state is WARMING_UP.
  // If the response is RX_WARMUP_COMP, the state is WARMED_UP
  // If the response is RX_ACTIVE, the state is ACTIVE
  // If no response is received, the state is OFFLINE. We'll need to add handling for this later. 

  BusLock lock;

  ESP_LOGI(TAG, "Bus %d: Starting heartbeat poll...", bus_id);

  if (repellers.empty()) {
    // "Nothing warming up" is not the same as "everything is running"
    ESP_LOGW(TAG, "Bus %d: Heartbeat poll with no known repellers", bus_id);
    return false;
  }

  for (auto& repeller : repellers) {
    ESP_LOGI(TAG, "Bus %d: Sending heartbeat to repeller 0x%02X...", bus_id, repeller.address);
    
    // Send heartbeat command
    send_tx_heartbeat(repeller.address);
    
    // Wait for response
    Packet response_packet;
    if (receive_packet(response_packet, 1000)) {
      PacketType packet_type = response_packet.identifyPacket();

      // For now, output the packet itself to the console
      ESP_LOGI(TAG, "Bus %d: Received response from repeller 0x%02X: ", bus_id, repeller.address);
      response_packet.print();
      
      switch (packet_type) {
        case RX_WARMUP:
          repeller.state = WARMING_UP;
          ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X is warming up", bus_id, repeller.address);
          break;
          
        case RX_WARMUP_COMP:
          repeller.state = WARMED_UP;
          ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X is warmed up", bus_id, repeller.address);
          break;
          
        case RX_HEARTBEAT_RUNNING:  // Assuming RX_HEARTBEAT_RUNNING means ACTIVE
          repeller.state = ACTIVE;
          ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X is active", bus_id, repeller.address);
          break;
          
        default:
          ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected response: ", bus_id, repeller.address);
          response_packet.print();
          break;
      }
    } else {
      // No response received - state remains as is for now
      ESP_LOGI(TAG, "Bus %d: No response from repeller 0x%02X", bus_id, repeller.address);
    }
  }

  // Once we have finished the heartbeat poll, loop over each repeller in the list. If any repeller is in the WARMING_UP state, set any_warming_up to true.
  // If any repeller is in the WARMED_UP state, set any_warmed_up to true.
  // Once this is done, we'll need to handle actually setting the controllers to active when any_warmed_up is true and any_warming_up is false. We'll do this later.
  bool any_warming_up = false;
  bool any_warmed_up = false;

  for (const auto& repeller : repellers) {
    if (repeller.state == WARMING_UP) {
      any_warming_up = true;
    } else if (repeller.state == WARMED_UP) {
      any_warmed_up = true;
    }
  }

  if(!any_warming_up && !any_warmed_up) {
    return true;
  } else if(!any_warming_up && any_warmed_up) {
    // All repellers are warmed up and none are warming up, so we can activate them
    ESP_LOGI(TAG, "Bus %d: All repellers warmed up, activating...", bus_id);
    end_warm_up_all();
  } else{
    ESP_LOGI(TAG, "Bus %d: Heartbeat poll complete. Warming up: %s, Warmed up: %s", bus_id,
                  any_warming_up ? "true" : "false", 
                  any_warmed_up ? "true" : "false");
  }

  return false; 
}

// Called every main-loop pass. This is the only place long RS-485 transactions
// are started, which keeps them off the httpd task.
void Bus::poll() {
  // Power on/off requested by another task. Runs before the lock is taken for
  // the periodic work below because it acquires the lock itself.
  service_power_request();

  BusLock lock;

  if (bus_state != BUS_WARMING_UP && bus_state != BUS_REPELLING) {
    return;  // Nothing to poll on an idle bus
  }

  uint32_t current_time = millis_now();

  if (current_time - last_polled > BUS_POLLING_INTERVAL_MS) {
    ESP_LOGI(TAG, "Bus %d: Sending periodic heartbeat...", bus_id);
    heartbeat_poll();  // Poll the heartbeat status of all repellers on the bus
    last_polled = current_time;
  }

  // Flush the cartridge runtime counter so an unexpected reset does not discard
  // the whole running session.
  if ((esp_timer_get_time() - active_seconds_last_save_at) >= CARTRIDGE_SAVE_INTERVAL_US) {
    save_active_seconds();
  }

  if (past_automatic_shutoff()) {
    ESP_LOGI(TAG, "Bus %d: auto shut-off reached", bus_id);
    powerOff();
  }
}


void Bus::change_led_brightness(uint8_t brightness_pct) {
  BusLock lock;

  // This function broadcasts the LED brightness change to all devices
  ESP_LOGI(TAG, "Bus %d: Changing LED brightness to %d%% for all devices...", bus_id, brightness_pct);

  // 1. send_tx_led_brightness for each repeller with the specified brightness, and look for RX_LED_BRIGHTNESS
  for (auto& repeller : repellers) {
    if (repeller.state == ACTIVE) {
      ESP_LOGI(TAG, "Bus %d: Setting brightness to %d%% for repeller 0x%02X...", bus_id, brightness_pct, repeller.address);
      send_tx_led_brightness(repeller.address, brightness_pct);
      
      Packet response_packet;
      if (receive_packet(response_packet, 1000)) {
        if (response_packet.identifyPacket() == RX_LED_BRIGHTNESS) {
          ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X confirmed brightness change", bus_id, repeller.address);
          send_led_on_to_repeller(&repeller);  // Trigger the repeller actually using the new brightness
        } else {
          ESP_LOGI(TAG, "Bus %d: Repeller 0x%02X sent unexpected brightness response: ", bus_id, repeller.address);
          response_packet.print();
        }
      } else {
        ESP_LOGI(TAG, "Bus %d: No brightness response from repeller 0x%02X", bus_id, repeller.address);
      }
    } else {
      ESP_LOGI(TAG, "Bus %d: Skipping repeller 0x%02X (not active, state: %s)", bus_id, repeller.address, repeller.getStateString());
    }
  }
  
  ESP_LOGI(TAG, "Bus %d: LED brightness change complete.", bus_id);
}

void Bus::change_led_color(uint8_t red, uint8_t green, uint8_t blue) {
  BusLock lock;

  // This function broadcasts the LED color change to all devices
  ESP_LOGI(TAG, "Bus %d: Changing LED color to R:%d G:%d B:%d for all devices...", bus_id, red, green, blue);

  // 1. Send the broadcast color command
  send_tx_color(red, green, blue);
  ESP_LOGI(TAG, "Bus %d: Sent broadcast color change command", bus_id);
  
  // 2. Send the color confirmation command (AA 8E 03 08 YY ZZ ...)
  send_tx_color_confirm(green, blue);
  ESP_LOGI(TAG, "Bus %d: Sent color confirmation command", bus_id);
  
  ESP_LOGI(TAG, "Bus %d: Color change complete.", bus_id);
}

void Bus::shutdown_all() {
  BusLock lock;

  ESP_LOGI(TAG, "Bus %d: Shutting down all repellers...", bus_id);
  
  // 1. Send send_tx_powerdown
  send_tx_powerdown();
  ESP_LOGI(TAG, "Bus %d: Sent powerdown command to all repellers", bus_id);
  
  // 2. Loop through all repellers and set their state to OFFLINE
  for (auto& repeller : repellers) {
    repeller.state = OFFLINE;
    ESP_LOGI(TAG, "Bus %d: Set repeller 0x%02X to OFFLINE state", bus_id, repeller.address);
  }

  // 3. Power down the bus itself
  powerdown();
  
  ESP_LOGI(TAG, "Bus %d: All repellers shut down.", bus_id);
}

// Load settings from filesystem
void Bus::load_settings() {
  SettingsLock lock;

  char filename[32];
  snprintf(filename, sizeof(filename), "/littlefs/bus%d_settings.dat", bus_id);

  struct stat st;
  if (stat(filename, &st) != 0) {
    ESP_LOGI(TAG, "Bus %d: Settings file not found, using defaults", bus_id);
    return; // Use default values set in constructor
  }

  FILE* file = fopen(filename, "rb");
  if (!file) {
    ESP_LOGI(TAG, "Bus %d: Failed to open settings file, using defaults", bus_id);
    return;
  }

  // Read settings in order. Read into a scratch copy first: a short or corrupt
  // file used to leave the live settings as a mix of defaults and garbage.
  struct {
    uint8_t red, green, blue, brightness;
    uint32_t cartridge_active_seconds;
    uint32_t cartridge_warn_at_seconds;
    uint16_t auto_shut_off_after_seconds;
  } loaded;

  size_t ok = 0;
  ok += fread(&loaded.red, sizeof(uint8_t), 1, file);
  ok += fread(&loaded.green, sizeof(uint8_t), 1, file);
  ok += fread(&loaded.blue, sizeof(uint8_t), 1, file);
  ok += fread(&loaded.brightness, sizeof(uint8_t), 1, file);
  ok += fread(&loaded.cartridge_active_seconds, sizeof(uint32_t), 1, file);
  ok += fread(&loaded.cartridge_warn_at_seconds, sizeof(uint32_t), 1, file);
  ok += fread(&loaded.auto_shut_off_after_seconds, sizeof(uint16_t), 1, file);
  fclose(file);

  if (ok != 7) {
    ESP_LOGW(TAG, "Bus %d: Settings file is short or unreadable, using defaults", bus_id);
    return;
  }

  red = loaded.red;
  green = loaded.green;
  blue = loaded.blue;
  brightness = loaded.brightness;
  cartridge_active_seconds = loaded.cartridge_active_seconds;
  cartridge_warn_at_seconds = loaded.cartridge_warn_at_seconds;
  auto_shut_off_after_seconds = loaded.auto_shut_off_after_seconds;
  if (auto_shut_off_after_seconds > 57600) auto_shut_off_after_seconds = 18000; // Validate range

  ESP_LOGI(TAG, "Bus %d: Settings loaded from filesystem", bus_id);
}

// Save settings to filesystem
void Bus::save_settings() {
  SettingsLock lock;

  char filename[32];
  snprintf(filename, sizeof(filename), "/littlefs/bus%d_settings.dat", bus_id);

  FILE* file = fopen(filename, "wb");
  if (!file) {
    ESP_LOGE(TAG, "Bus %d: Failed to open settings file for writing", bus_id);
    return;
  }

  // Write settings in order
  fwrite(&red, sizeof(uint8_t), 1, file);
  fwrite(&green, sizeof(uint8_t), 1, file);
  fwrite(&blue, sizeof(uint8_t), 1, file);
  fwrite(&brightness, sizeof(uint8_t), 1, file);
  fwrite(&cartridge_active_seconds, sizeof(uint32_t), 1, file);
  fwrite(&cartridge_warn_at_seconds, sizeof(uint32_t), 1, file);
  fwrite(&auto_shut_off_after_seconds, sizeof(uint16_t), 1, file);

  fclose(file);
  ESP_LOGI(TAG, "Bus %d: Settings saved to filesystem", bus_id);
}

// Settings interface methods
void Bus::setRGB(uint8_t new_red, uint8_t new_green, uint8_t new_blue) {
  SettingsLock lock;

  if(new_red != red || new_green != green || new_blue != blue) {
    red = new_red;
    green = new_green;
    blue = new_blue;
    save_settings();
    ESP_LOGI(TAG, "Bus %d: RGB set to (%d, %d, %d)", bus_id, red, green, blue);
  } else {
    ESP_LOGI(TAG, "Bus %d: RGB already set to (%d, %d, %d), no changes made", bus_id, red, green, blue);
  }
}

void Bus::setBrightness(uint8_t new_brightness) {
  SettingsLock lock;

  if(brightness != new_brightness) {
    brightness = new_brightness;
    save_settings();
    ESP_LOGI(TAG, "Bus %d: Brightness set to %d", bus_id, brightness);
  } else {
    ESP_LOGI(TAG, "Bus %d: Brightness already set to %d, no changes made", bus_id, brightness);
  }
}

void Bus::resetCartridge() {
  SettingsLock lock;

  cartridge_active_seconds = 0;
  save_settings();
  ESP_LOGI(TAG, "Bus %d: Cartridge reset, active seconds set to 0", bus_id);
}

void Bus::setCartridgeWarnAtSeconds(uint32_t seconds) {
  SettingsLock lock;

  if(cartridge_warn_at_seconds != seconds) {
    cartridge_warn_at_seconds = seconds;
    save_settings();
    ESP_LOGI(TAG, "Bus %d: Cartridge warn time set to %" PRIu32 " seconds", bus_id, seconds);
  } else {
    ESP_LOGI(TAG, "Bus %d: Cartridge warn time already set to %" PRIu32 " seconds, no changes made", bus_id, cartridge_warn_at_seconds);
  }
}

void Bus::setAutoShutOffAfterSeconds(uint16_t seconds) {
  SettingsLock lock;

  if (seconds > 57600) {
    ESP_LOGI(TAG, "Bus %d: Invalid auto shut-off value %d, must be 0-57600", bus_id, seconds);
    return;
  }

  if(auto_shut_off_after_seconds != seconds) {
    auto_shut_off_after_seconds = seconds;
    save_settings();
    ESP_LOGI(TAG, "Bus %d: Auto shut-off set to %d seconds", bus_id, seconds);
  } else {
    ESP_LOGI(TAG, "Bus %d: Auto shut-off already set to %d seconds, no changes made", bus_id, auto_shut_off_after_seconds);
  }
}

// Helper conversion methods
uint8_t Bus::repeller_brightness() {
  SettingsLock lock;

  // Convert brightness (0-255) to repeller brightness (0-100)
  return (uint8_t)round((brightness * 100.0) / 255.0);
}

uint8_t Bus::repeller_red() {
  return red;
}

uint8_t Bus::repeller_green() {
  return green;
}

uint8_t Bus::repeller_blue() {
  return blue;
}

void Bus::save_active_seconds() {
  SettingsLock lock;

  if (bus_state == BUS_WARMING_UP || bus_state == BUS_REPELLING) {
    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_microseconds = current_time - active_seconds_last_save_at;
    uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
    
    cartridge_active_seconds += elapsed_seconds;
    active_seconds_last_save_at = current_time;
    save_settings();
    
    ESP_LOGI(TAG, "Bus %d: Active seconds updated: %" PRIu32 " total", bus_id, cartridge_active_seconds);
  }
}

bool Bus::past_automatic_shutoff() {
  if (auto_shut_off_after_seconds == 0) {
    return false; // Auto shut-off disabled
  }
  
  if (warm_on_at == 0) {
    return false; // Not warmed up yet
  }
  
  uint64_t current_time = esp_timer_get_time();
  uint64_t elapsed_microseconds = current_time - warm_on_at;
  uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
  
  return elapsed_seconds >= auto_shut_off_after_seconds;
}

void Bus::requestPower(bool on) {
  // Deliberately lock-free: this runs on the httpd task and must return at once.
  // The main loop picks the request up in poll() within a few milliseconds.
  power_request.store(on ? BUS_POWER_REQ_ON : BUS_POWER_REQ_OFF, std::memory_order_relaxed);
  ESP_LOGI(TAG, "Bus %d: queued power %s request", bus_id, on ? "on" : "off");
}

void Bus::service_power_request() {
  uint8_t req = power_request.load(std::memory_order_relaxed);
  if (req == BUS_POWER_REQ_NONE) {
    return;
  }

  if (req == BUS_POWER_REQ_ON) {
    powerOn();
  } else {
    powerOff();
  }

  // Only clear the request if it is still the one we just serviced. A reversing
  // request that arrived while we were working stays queued for the next pass.
  uint8_t expected = req;
  power_request.compare_exchange_strong(expected, (uint8_t)BUS_POWER_REQ_NONE,
                                        std::memory_order_relaxed);
}

void Bus::powerOn() {
  BusLock lock;

  ESP_LOGI(TAG, "Bus %d: Power on command received", bus_id);
  
  // Activate the bus if it's offline
  if (bus_state == BUS_OFFLINE) {
    activate();
  }
  
  // If bus is just powered, then discover repellers, retrieve serial numbers, and warm up
  if (bus_state == BUS_POWERED) {
    discover_repellers();
    retrieve_serial_for_all();
    warm_up_all();

    if (bus_state == BUS_POWERED) {
      // warm_up_all() found nothing to start - don't leave the rail energised
      ESP_LOGW(TAG, "Bus %d: Power on found no repellers, powering back down", bus_id);
      powerdown();
      return;
    }
  }
  
  ESP_LOGI(TAG, "Bus %d: Power on sequence initiated", bus_id);
}

void Bus::powerOff() {
  BusLock lock;

  ESP_LOGI(TAG, "Bus %d: Power off command received", bus_id);
  
  // Save any remaining active seconds before shutdown
  save_active_seconds();
  
  // Shutdown all repellers and the bus
  shutdown_all();
  
  ESP_LOGI(TAG, "Bus %d: Power off completed", bus_id);
}

// Cartridge monitoring methods
uint16_t Bus::get_cartridge_runtime_hours() {
  SettingsLock lock;

  // Update active seconds if currently running
  if (bus_state == BUS_WARMING_UP || bus_state == BUS_REPELLING) {
    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_microseconds = current_time - active_seconds_last_save_at;
    uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
    uint32_t total_seconds = cartridge_active_seconds + elapsed_seconds;
    return total_seconds / 3600; // Convert to hours
  }
  
  return cartridge_active_seconds / 3600; // Convert to hours
}

uint8_t Bus::get_cartridge_percent_left() {
  SettingsLock lock;

  uint32_t total_active_seconds = cartridge_active_seconds;
  
  // Add current session time if running
  if (bus_state == BUS_WARMING_UP || bus_state == BUS_REPELLING) {
    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_microseconds = current_time - active_seconds_last_save_at;
    uint32_t elapsed_seconds = elapsed_microseconds / 1000000; // Convert to seconds
    total_active_seconds += elapsed_seconds;
  }
  
  // Calculate percentage remaining
  if (cartridge_warn_at_seconds == 0) {
    return 100; // No limit set, always 100%
  }
  
  if (total_active_seconds >= cartridge_warn_at_seconds) {
    return 0; // Cartridge expired
  }
  
  uint32_t remaining_seconds = cartridge_warn_at_seconds - total_active_seconds;
  uint8_t percent_left = (remaining_seconds * 100) / cartridge_warn_at_seconds;
  
  return percent_left;
}
