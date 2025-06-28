#ifndef TX_CONTROLLER_H
#define TX_CONTROLLER_H

#include <Arduino.h>
#include "packet.h"

// Initialize the transmitter (call this in setup)
void tx_controller_init(uint8_t bus);

// Individual packet transmission functions
void send_tx_powerup();
void send_tx_powerdown();
void send_tx_heartbeat(uint8_t address);
void send_tx_led_on_conf(uint8_t address);
void send_tx_ser_no_1(uint8_t address);
void send_tx_ser_no_2(uint8_t address);
void send_tx_warmup(uint8_t address);
void send_tx_warmup_complete(uint8_t address);
void send_tx_color(uint8_t red, uint8_t green, uint8_t blue);
void send_tx_color_confirm(uint8_t green, uint8_t blue);
void send_tx_color_startup(uint8_t address, uint8_t red, uint8_t green, uint8_t blue);
void send_tx_startup_comp(uint8_t address);
void send_tx_led_brightness(uint8_t address, uint8_t brightness);
void send_tx_led_brightness_startup(uint8_t address, uint8_t brightness);

// Full functional transmissions
void discover_repellers();
void retrieve_serial_for_all();
void warm_up_all();
void end_warm_up_all();
bool heartbeat_poll();  // Returns true if all repellers are active, false otherwise (including during warmup)
void change_led_brightness(uint8_t brightness_pct);
void change_led_color(uint8_t red, uint8_t green, uint8_t blue);
void shutdown_all();




#endif