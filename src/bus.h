#ifndef BUS_H
#define BUS_H

#include <Arduino.h>
#include <list>
#include "packet.h"
#include "repeller.h"

#define BUS_POLLING_INTERVAL_MS 15000  // Poll every second

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

  uint64_t warm_on_at;  // Timestamp when the bus was last warmed up (for tracking auto-off settings)
  uint64_t active_seconds_last_save_at;  // Timestamp when the bus was last warmed up (for tracking auto-off settings)

  uint64_t last_polled;  // Timestamp at which the bus was last polled for repeller status
  
  // Settings fields (saved to filesystem)
  uint8_t red;                         // 0-255, default 0x03
  uint8_t green;                       // 0-255, default 0xd5
  uint8_t blue;                        // 0-255, default 0xff
  uint8_t brightness;                  // 0-254, default 100
  uint32_t cartridge_active_seconds;   // default 0
  uint32_t cartridge_warn_at_seconds;  // default 349200
  uint16_t auto_shut_off_after_seconds; // 0-57600, default 18000

public:
  // Constructor - requires bus ID (0 or 1)
  Bus(uint8_t id);
  
  // Initialize the bus (call this in setup)
  void init();

  void poll();  // Poll the bus for repeller status and update internal state if past the polling interval

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
  void send_set_address(uint8_t address);
  
  // Repeller management functions
  Repeller* get_repeller(uint8_t address);
  Repeller* get_or_create_repeller(uint8_t address);
  uint8_t find_next_address();
  
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
  
  // Filesystem settings methods
  void load_settings();
  void save_settings();
  void ZigbeeSetRGB(uint8_t zb_red, uint8_t zb_green, uint8_t zb_blue);
  void ZigbeeSetBrightness(uint8_t brightness);
  void ZigbeeResetCartridge();
  void ZigbeeSetCartridgeWarnAtSeconds(uint32_t seconds);
  void ZigbeeSetAutoShutOffAfterSeconds(uint16_t seconds);

  void ZigbeePowerOn();
  void ZigbeePowerOff();
  
  // Cartridge monitoring methods
  uint16_t get_cartridge_runtime_hours();
  uint8_t get_cartridge_percent_left();
  uint32_t get_cartridge_active_seconds() const { return cartridge_active_seconds; }
  uint32_t get_cartridge_warn_at_seconds() const { return cartridge_warn_at_seconds; }
  uint16_t get_auto_shut_off_after_seconds() const { return auto_shut_off_after_seconds; }
  
  // Helper methods for converting settings
  uint8_t repeller_brightness();
  uint8_t zigbee_brightness();  // Returns the current Zigbee brightness (0-254)
  uint8_t repeller_red();
  uint8_t repeller_green();
  uint8_t repeller_blue();
  void save_active_seconds();
  bool past_automatic_shutoff();
  
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