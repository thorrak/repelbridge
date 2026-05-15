#include "sniffer_mode.h"
#include "packet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "sniffer";


// MAX3485 pin connections come from the BUS_0_* build flags in platformio.ini:
//   RO (Receive Output)  -> BUS_0_RX_PIN  (UART RX)
//   DI (Driver Input)    -> BUS_0_TX_PIN  (UART TX)
//   DE/RE (tied)         -> BUS_0_DIR_PIN (direction control)
// Sniffer mode always holds the DIR pin low (receive only).


// UART configuration for RS-485
#define RS485_UART_NUM UART_NUM_1
#define RS485_BAUD_RATE 19200
#define RS485_BUF_SIZE 256

// Buffer for incoming data
static const size_t BUFFER_SIZE = 256;
static uint8_t rx_buffer[BUFFER_SIZE];
static size_t buffer_index = 0;

// Timing for packet detection
static uint32_t last_byte_time = 0;
static const uint32_t PACKET_TIMEOUT_MS = 8;  // 8ms gap indicates new packet
static bool packet_in_progress = false;

static uint32_t millis_now() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void processPacket() {
  if(buffer_index > 0) {
    // Only process 11-byte packets
    if(buffer_index == 11) {
      Packet packet(rx_buffer);
      packet.print();
    } else {
      // Print partial packet for debugging
      char hex_buf[BUFFER_SIZE * 3 + 1];
      for(size_t i = 0; i < buffer_index; i++) {
        snprintf(&hex_buf[i * 3], 4, "%02X ", rx_buffer[i]);
      }
      ESP_LOGI(TAG, "[%08lu] RX: %s(PARTIAL) [%d bytes]", millis_now(), hex_buf, buffer_index);
    }
    buffer_index = 0;
  }
  packet_in_progress = false;
}

void sniffer_setup() {
  ESP_LOGI(TAG, "RS-485 Sniffer Starting...");
  ESP_LOGI(TAG, "Monitoring communications between controller and Repeller...");
  ESP_LOGI(TAG, "Format: [TIMESTAMP] DIR: HEX_DATA (PACKET_NAME)");
  ESP_LOGI(TAG, "DIR: RX=Received, TX=Transmitted");
  ESP_LOGI(TAG, "----------------------------------------");

  // Set DE/RE control pin as output and keep in receive mode
  gpio_set_direction((gpio_num_t)BUS_0_DIR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)BUS_0_DIR_PIN, 0);  // Always in receive mode for sniffer

  // Initialize UART for RS-485 communication
  uart_config_t uart_config = {
    .baud_rate = RS485_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };
  uart_param_config(RS485_UART_NUM, &uart_config);
  uart_set_pin(RS485_UART_NUM, BUS_0_TX_PIN, BUS_0_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(RS485_UART_NUM, RS485_BUF_SIZE, 0, 0, NULL, 0);

  // Clear any existing data
  uart_flush_input(RS485_UART_NUM);

  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "Ready to capture RS-485 data...");
}

void sniffer_loop() {
  uint32_t current_time = millis_now();

  // Check for packet timeout
  if(packet_in_progress && (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
    processPacket();
  }

  // Check how many bytes are available
  size_t available = 0;
  uart_get_buffered_data_len(RS485_UART_NUM, &available);

  // Read available data
  while(available > 0) {
    uint8_t byte_received;
    int len = uart_read_bytes(RS485_UART_NUM, &byte_received, 1, 0);
    if (len <= 0) break;

    current_time = millis_now();

    // If we haven't received data for a while, this might be a new packet
    if(!packet_in_progress || (current_time - last_byte_time) > PACKET_TIMEOUT_MS) {
      if(packet_in_progress) {
        processPacket();  // Process previous packet first
      }
      buffer_index = 0;
      packet_in_progress = true;
    }

    // Add byte to buffer if there's space
    if(buffer_index < BUFFER_SIZE - 1) {
      rx_buffer[buffer_index++] = byte_received;
    } else {
      // Buffer overflow - process what we have
      processPacket();
      rx_buffer[0] = byte_received;
      buffer_index = 1;
      packet_in_progress = true;
    }

    last_byte_time = current_time;

    // Re-check available
    uart_get_buffered_data_len(RS485_UART_NUM, &available);
  }

  // Small delay to prevent overwhelming the processor
  vTaskDelay(pdMS_TO_TICKS(1));
}
