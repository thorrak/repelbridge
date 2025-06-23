#include "packet.h"

// Packet reception function
// Returns true if a packet was received, false if timeout or no packet
// packet: Packet object to store received data
// timeout_ms: maximum time to wait for a complete packet (0 = non-blocking)
bool receive_packet(Packet& packet, uint16_t timeout_ms = 1000);

static bool receive_and_print(const char* expected_type, uint16_t timeout_ms = 500);
