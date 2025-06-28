# Liv Repeller Controller Emulation Project

## Project Overview
This project emulates a controller for a device called the "Liv Repeller" using reverse-engineered RS-485 communication protocols. The goal is to create an ESP32-based controller that can communicate with and control Liv Repeller devices.

## Hardware Setup
- **ESP32-C6 Development Board** (targets `esp32-c6-devkitc-1` in platformio.ini)
- **MAX3485 RS-485 Transceivers** on breakout boards (up to 2 for dual bus support)
- **RS-485 Bus Connections** to monitor/control Liv Repeller devices

### Pin Connections (Dual Bus Support)
```
Bus 0 (MAX3485) → ESP32-C6
- A/B terminals → RS-485 bus 0 (differential pair)
- RO (Receive Output) → GPIO17 (Serial1 RX)
- DI (Driver Input) → GPIO16 (Serial1 TX)
- DE/RE (tied together) → GPIO23 (Direction control)
- VCC → 3.3V or 5V
- GND → Ground

Bus 1 (MAX3485) → ESP32-C6 (Optional)
- A/B terminals → RS-485 bus 1 (differential pair)
- RO (Receive Output) → GPIO22 (Bus 1 RX)
- DI (Driver Input) → GPIO19 (Bus 1 TX)
- DE/RE (tied together) → GPIO21 (Direction control)
- Power Pin → GPIO20 (Bus 1 power control)
- VCC → 3.3V or 5V
- GND → Ground
```

### Communication Parameters
- **Baud Rate**: 19200
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Protocol**: RS-485 differential signaling
- **Packet Size**: 11 bytes (fixed)

## Protocol Details

### Device Addressing
- **Address Range**: 0x01 to 0x7E (devices)
- **Address Location**: 
  - TX packets: Byte 1 (`AA XX ...`)
  - RX startup packets: Byte 3 (`AA 80 07 XX ...`)
- **Broadcast Packets**: `tx_powerup`, `tx_powerdown` (no addressing)

### Packet Structure
All packets are 11 bytes: `AA XX YY ZZ ...` where:
- Byte 0: Always `0xAA` (sync/header)
- Byte 1: Address (TX) or packet type (RX)
- Byte 2: Command/function code
- Bytes 3-10: Payload (command-specific)

### Known Message Types

#### Fixed Packets (Address-Aware)
- `tx_startup:XX` - Device startup sequence
- `tx_heartbeat:XX` - Keep-alive message
- `tx_led_on_conf:XX` - LED configuration
- `tx_warmup_1:XX`, `tx_warmup_2:XX`, `tx_warmup_3:XX` - Warmup sequence
- `tx_warmup_complete:XX` - Warmup finished
- `tx_startup_comp:XX` - Startup complete

#### Dynamic Packets (Address-Aware)
- `tx_led_brightness:XX (brightness)` - Set LED brightness (0-100)
- `tx_led_brightness_startup:XX (brightness)` - Set startup LED brightness
- `tx_color_startup:XX (RRGGBB)` - Set startup RGB color
- `tx_color (RRGGBB)` - Set RGB color (broadcast?)

#### Response Packets
- `rx_startup:XX` - Device startup response
- `rx_led_brightness (brightness)` - LED brightness confirmation
- `rx_led_brightness_startup (brightness)` - Startup brightness confirmation
- `rx_color_startup (RRGGBB)` - Startup color confirmation
- `rx_heartbeat_running (value)` - Device running status
- `rx_warmup (XXYY)` - Warmup status
- `rx_warmup_comp (XXYY)` - Warmup complete status

#### System-Level Packets
- `tx_powerup` - System power on
- `tx_powerdown` - System power off

## Code Structure

### Core Files
- `src/main.cpp` - Mode switching and main application logic with dual bus support
- `src/sniffer_mode.h/.cpp` - RS-485 packet sniffing functionality
- `src/bus.h/.cpp` - Bus class for controller emulation, transmission, and reception (replaces tx_controller and receive)
- `src/packet.h/.cpp` - Packet class for RS-485 packet handling
- `src/repeller.h/.cpp` - Repeller device management
- `src/known_packets.h` - Predefined packet definitions

### Bus Architecture
The firmware now supports dual bus operation with independent management:

```cpp
// Global bus objects
Bus bus0(0);  // Primary bus
Bus bus1(1);  // Secondary bus (optional)
```

Each bus maintains:
- Its own list of connected repellers
- Independent state management
- Separate pin configurations
- Individual packet transmission/reception

### Mode Selection
Change `CURRENT_MODE` in `main.cpp`:
```cpp
#define MODE_SNIFFER 0      // Passive monitoring
#define MODE_CONTROLLER 1   // Active controller emulation
```

### Key Classes and Functions

#### Bus Class Methods
- `bus.init()` - Initialize bus hardware
- `bus.activate()` - Power on and activate the bus
- `bus.powerdown()` - Power down and deactivate the bus
- `bus.transmit(Packet *packet)` - Transmit packet on specific bus
- `bus.receive_packet(Packet& packet, timeout)` - Receive packet on specific bus
- `bus.receive_and_print(expected_type, timeout)` - Receive and print packet for debugging
- `bus.send_tx_*()` - Individual packet transmission functions
- `bus.discover_repellers()` - Find devices on this bus
- `bus.get_repeller(address)` - Get repeller by address
- `bus.heartbeat_poll()` - Poll all repellers on this bus
- `bus.warm_up_all()`, `bus.shutdown_all()` - Bus-wide operations

#### Global Functions
- `sniffer_setup()`, `sniffer_loop()` - Passive packet capture
- `identifyPacket()` - Address-aware packet identification

## Development Workflow

### Testing Controller Emulation
1. Set `CURRENT_MODE = MODE_CONTROLLER` in `main.cpp`
2. Upload firmware to ESP32
3. Connect RS-485 bus to Liv Repeller device(s)
4. Monitor serial output for response packets
5. Verify devices respond to commands on their respective buses

### Testing Dual Bus Setup
1. Connect repellers to both Bus 0 and Bus 1
2. Modify main.cpp to initialize and use both buses:
   ```cpp
   bus0.init();
   bus1.init();
   bus0.discover_repellers();
   bus1.discover_repellers();
   ```
3. Verify independent operation of each bus

### Monitoring Communications
1. Set `CURRENT_MODE = MODE_SNIFFER` in `main.cpp`
2. Connect ESP32 to existing controller ↔ repeller bus
3. Observe packet exchanges to decode new message types

### Adding New Packet Types
1. Capture unknown packets using sniffer mode
2. Add packet definitions to `known_packets.h`
3. Update `identifyPacket()` in both `sniffer_mode.cpp` and `packet.cpp`
4. Add transmission functions to `bus.h/.cpp` if needed

## Build and Development

### PlatformIO Configuration
- **Platform**: espressif32
- **Board**: seeed_xiao_esp32c6
- **Framework**: arduino
- **Monitor Speed**: 115200

### Build Commands
```bash
pio run                    # Build
pio run -t upload         # Upload
pio device monitor        # Serial monitor
```

### Linting/Type Checking
- Check README or ask user for specific lint/typecheck commands
- Run before committing changes

## Protocol Analysis Notes

### Startup Sequence (Per Bus)
1. `bus.discover_repellers()` - Multiple `tx_discover` messages for device discovery
2. `bus.retrieve_serial_for_all()` - Get serial numbers from discovered devices
3. `bus.warm_up_all()` - Warmup sequence:
   - `tx_powerup` (system-wide power on)
   - `tx_warmup:XX` → `rx_warmup` for each device
   - `tx_color_startup:XX` (initial color setup)
   - `tx_led_brightness_startup:XX` (initial brightness)
   - `tx_startup_comp:XX` (startup complete)
4. `bus.heartbeat_poll()` - Monitor device states
5. `bus.end_warm_up_all()` - Activate all devices when warmed up

### Communication Patterns
- **Request-Response**: Most TX commands get RX acknowledgments
- **Timeout Handling**: 500ms default timeout for responses
- **Error Detection**: Incomplete packets are discarded
- **Packet Separation**: 8ms gap indicates new packet

## Bus States and Management

### Bus States
Each bus maintains its own state:
- `BUS_OFFLINE` - Bus not initialized
- `BUS_POWERED` - Bus is powered on, but repellers are not warming up or repelling
- `BUS_WARMING_UP` - Bus is powered on, and repellers are warming up
- `BUS_REPELLING` - Bus is powered on, and repellers are warmed up and repelling
- `BUS_ERROR` - Bus configuration error

### Repeller States (Per Bus)
Each repeller on each bus tracks its own state:
- `OFFLINE` - Device not responding
- `INACTIVE` - Device discovered but not activated
- `WARMING_UP` - Device in warmup phase
- `WARMED_UP` - Device ready for activation
- `ACTIVE` - Device fully operational

## Known Issues & Limitations

### Current Limitations
- Currently uses Serial1 for both buses (hardware limitation)
- Some RX packet patterns may need device-specific variations
- Color commands may be broadcast vs. addressed (needs verification)
- Bus 1 functionality ready but needs hardware testing

### Future Enhancements
- True dual Serial port support for independent bus operation
- Enhanced error handling and retry logic per bus
- Configuration file for device-specific parameters
- Bus health monitoring and automatic recovery

## Debugging Tips

### Common Issues
- **No Response**: Check wiring, baud rate, address matches
- **Garbled Data**: Verify RS-485 A/B polarity
- **Timeout Errors**: Increase timeout or check device power
- **Wrong Address**: Use sniffer mode to verify device addresses

### Serial Output Format
```
[TIMESTAMP] DIR: HEX_DATA (PACKET_NAME) [BYTE_COUNT]
[00123456] RX: AA 05 07 00 00 00 00 00 00 00 00 (tx_startup:05) [11 bytes]
Bus 0: Discovered repeller at address 0x05
Bus 1: No response to tx_discover
```

### Address Discovery
Run sniffer mode and look for `rx_startup:XX` packets to identify device addresses. Each bus will independently discover its connected devices.

### Bus-Specific Debugging
- Monitor each bus separately for troubleshooting
- Use `bus.getState()` and `bus.getStateString()` for bus status
- Check individual repeller states with `repeller.getStateString()`
- Verify pin configurations match hardware setup for each bus