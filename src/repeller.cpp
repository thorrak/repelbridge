#include "repeller.h"

// Global list to track all discovered repellers
std::list<Repeller> repellers;

// Find a repeller by address, returns nullptr if not found
Repeller* get_repeller(uint8_t address) {
  for (auto& repeller : repellers) {
    if (repeller.address == address) {
      return &repeller;
    }
  }
  return nullptr;
}

// Find a repeller by address, create it if it doesn't exist
Repeller* get_or_create_repeller(uint8_t address) {
  // First try to find existing repeller
  Repeller* existing = get_repeller(address);
  if (existing != nullptr) {
    return existing;
  }
  
  // Create new repeller and add to list
  repellers.emplace_back(address);
  return &repellers.back();
}