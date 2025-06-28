#ifndef BUS_H
#define BUS_H

#include <Arduino.h>
#include <list>
#include "packet.h"
#include "repeller.h"

// Bus state enumeration
enum BusState {
  BUS_OFFLINE,
  BUS_POWERED,  // Bus is powered on, but not warming up/repelling
  BUS_WARMING_UP,
  BUS_REPELLING,
  BUS_ERROR
};

// Bus class to manage RS-485 bus and its connected repellers
class Bus {
private:
  uint8_t bus_id;
  std::list<Repeller> repellers;
  BusState bus_state;
  
  // Pin definitions based on bus ID
  int tx_pin;
  int rx_pin;
  int dir_pin;
  int pow_pin;

public:
  // Constructor - requires bus ID (0 or 1)
  Bus(uint8_t id);
  
  // Initialize the bus (call this in setup)
  void init();

  void activate();  // Activate the bus (Power on the bus if unpowered and set as Serial1)
  void powerdown();  // Power down the bus (turn off power pin if available)
  
  // Transmit packet on this bus
  void transmit(Packet *packet);
  
  // Receive packet on this bus
  bool receive_packet(Packet& packet, uint16_t timeout_ms = 1000);
  bool receive_and_print(const char* expected_type, uint16_t timeout_ms = 500);
  
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
  void send_tx_discover();
  
  // Repeller management functions
  Repeller* get_repeller(uint8_t address);
  Repeller* get_or_create_repeller(uint8_t address);
  
  // Helper functions for individual repeller operations
  void retrieve_serial(Repeller* repeller);
  void send_tx_warmup(Repeller* repeller);
  void send_startup_led_params(Repeller* repeller);
  void send_led_on_to_repeller(Repeller *repeller);
  void send_activate_at_end_of_warmup(Repeller *repeller);

  // Full functional transmissions
  // The typical flow is:
  // 1. Bus physically powers on (activate() is called) (can be done separately or as part of the transmit() in discover_repellers())
  // 2. discover_repellers() is called to find all repellers
  // 3. retrieve_serial_for_all() is called to get serial numbers for all repellers
  // 4. warm_up_all() is called to warm up all repellers
  // 5. end_warm_up_all() is called to activate all repellers after warmup
  // 6. Repellers operate for as long as needed. Optionally, change LED brightness or color during operation
  // 7. shutdown_all() is called to power down all repellers and the bus
  void discover_repellers();
  void retrieve_serial_for_all();
  void warm_up_all();
  void end_warm_up_all();
  bool heartbeat_poll();  // Returns true if all repellers are active, false otherwise (including during warmup)
  void change_led_brightness(uint8_t brightness_pct);
  void change_led_color(uint8_t red, uint8_t green, uint8_t blue);
  void shutdown_all();
  
  // Getters
  uint8_t getBusId() const { return bus_id; }
  BusState getState() const { return bus_state; }
  const std::list<Repeller>& getRepellers() const { return repellers; }
  
  // Method to get state as string for debugging
  const char* getStateString() const {
    switch(bus_state) {
      case BUS_OFFLINE: return "BUS_OFFLINE";
      case BUS_POWERED: return "BUS_POWERED";
      case BUS_WARMING_UP: return "BUS_WARMING_UP";
      case BUS_REPELLING: return "BUS_REPELLING";
      case BUS_ERROR: return "BUS_ERROR";
      default: return "BUS_UNKNOWN";
    }
  }
};

#endif