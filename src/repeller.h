#ifndef REPELLER_H
#define REPELLER_H

#include <Arduino.h>
#include <list>

// Repeller state enumeration
enum RepellerState {
  OFFLINE,
  INACTIVE,
  WARMING_UP,
  WARMED_UP,
  ACTIVE
};

// Repeller class to manage individual repeller devices
class Repeller {
public:
  uint8_t address;
  char serial[16];
  RepellerState state;
  
  // Constructor - requires address, initializes serial to blank and state to inactive
  Repeller(uint8_t addr) : address(addr), state(INACTIVE) {
    serial[0] = '\0';  // Initialize serial as empty string
  }
  
  // Method to set serial number from two parts
  void setSerial(const char* part1, const char* part2) {
    snprintf(serial, sizeof(serial), "%s%s", part1, part2);
  }
  
  // Method to get state as string for debugging
  const char* getStateString() const {
    switch(state) {
      case OFFLINE: return "OFFLINE";
      case INACTIVE: return "INACTIVE";
      case WARMING_UP: return "WARMING_UP";
      case WARMED_UP: return "WARMED_UP";
      case ACTIVE: return "ACTIVE";
      default: return "UNKNOWN";
    }
  }
};

// Global list to track all discovered repellers
extern std::list<Repeller> repellers;

// Function declarations
Repeller* get_repeller(uint8_t address);
Repeller* get_or_create_repeller(uint8_t address);

#endif