#ifndef TX_CONTROLLER_H
#define TX_CONTROLLER_H

#include <Arduino.h>
#include "packet.h"

// Initialize the transmitter (call this in setup)
void tx_controller_init();

// Fixed packet transmission functions
void send_tx_startup();
void send_tx_startup_comp();
void send_tx_powerup();
void send_tx_powerdown();
void send_tx_heartbeat();
void send_tx_led_on_conf();
void send_tx_ser_no_1(uint8_t address);
void send_tx_ser_no_2(uint8_t address);
void send_tx_warmup(uint8_t address);
void send_tx_warmup_complete();


// Full functional transmissions
void full_startup();
void full_poweron();
void discover_repellers();

void retrieve_serial_for_all();
void warm_up_all();


// Dynamic packet transmission functions
void send_tx_led(uint8_t brightness);
void send_tx_led_startup(uint8_t brightness);
void send_tx_color(uint8_t red, uint8_t green, uint8_t blue);
void send_tx_color_startup(uint8_t red, uint8_t green, uint8_t blue);


#endif