#ifndef TX_CONTROLLER_H
#define TX_CONTROLLER_H

#include <Arduino.h>

// Initialize the transmitter (call this in setup)
void tx_controller_init(uint8_t direction_pin);

// Fixed packet transmission functions
void send_tx_startup();
void send_tx_startup_comp();
void send_tx_powerup();
void send_tx_powerdown();
void send_tx_heartbeat();
void send_tx_led_on_conf();
void send_tx_warmup_1();
void send_tx_warmup_2();
void send_tx_warmup_3();
void send_tx_warmup_complete();


// Full functional transmissions
void full_startup();
void full_poweron();


// Dynamic packet transmission functions
void send_tx_led(uint8_t brightness);
void send_tx_led_startup(uint8_t brightness);
void send_tx_color(uint8_t red, uint8_t green, uint8_t blue);
void send_tx_color_startup(uint8_t red, uint8_t green, uint8_t blue);

// Packet reception function
// Returns true if a packet was received, false if timeout or no packet
// packet_buffer must be at least 11 bytes
// timeout_ms: maximum time to wait for a complete packet (0 = non-blocking)
bool receive_packet(uint8_t* packet_buffer, uint16_t timeout_ms = 1000);

#endif