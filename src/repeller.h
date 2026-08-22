#ifndef REPELLER_H
#define REPELLER_H

#include <cstdint>
#include <cstdio>
#include <cstring>

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
  char serial[17];  // Two 8-character halves plus the terminating NUL
  RepellerState state;
  uint64_t turned_on_at;
  
  // Constructor - requires address, initializes serial to blank and state to inactive
  Repeller(uint8_t addr) : address(addr), state(INACTIVE), turned_on_at(0) {
    serial[0] = '\0';  // Initialize serial as empty string
  }
  
  // Method to set serial number from two parts
  void setSerial(const char* part1, const char* part2) {
    snprintf(serial, sizeof(serial), "%.8s%.8s", part1, part2);
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


#endif