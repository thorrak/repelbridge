# Liv Repeller Controller Emulation Project

## Project Overview
This project emulates a controller for a device called the "Liv Repeller" using reverse-engineered RS-485 communication protocols. The goal is to create an ESP32-based controller that can communicate with and control Liv Repeller devices.

## Hardware Setup
- **ESP32-C6 Development Board** (targets `esp32-c6-devkitc-1` in platformio.ini)
- **MAX3485 RS-485 Transceiver** on a breakout board
- **RS-485 Bus Connection** to monitor/control Liv Repeller devices

### Pin Connections
```
MAX3485 → ESP32-C6
- A/B terminals → RS-485 bus (differential pair)
- RO (Receive Output) → GPIO22 (Serial1 RX)
- DI (Driver Input) → GPIO19 (Serial1 TX)
- DE/RE (tied together) → GPIO21 (Direction control)
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
- `src/main.cpp` - Mode switching and main application logic
- `src/sniffer_mode.h/.cpp` - RS-485 packet sniffing functionality
- `src/tx_controller.h/.cpp` - Controller emulation and transmission
- `src/known_packets.h` - Predefined packet definitions

### Mode Selection
Change `CURRENT_MODE` in `main.cpp`:
```cpp
#define MODE_SNIFFER 0      // Passive monitoring
#define MODE_CONTROLLER 1   // Active controller emulation
```

### Key Functions
- `sniffer_setup()`, `sniffer_loop()` - Passive packet capture
- `tx_controller_init()` - Initialize controller mode
- `send_tx_*()` - Individual packet transmission functions
- `receive_packet()` - Packet reception with timeout
- `identifyPacket()` - Address-aware packet identification

## Development Workflow

### Testing Controller Emulation
1. Set `CURRENT_MODE = MODE_CONTROLLER` in `main.cpp`
2. Upload firmware to ESP32
3. Connect RS-485 bus to Liv Repeller device
4. Monitor serial output for response packets
5. Verify device responds to commands

### Monitoring Communications
1. Set `CURRENT_MODE = MODE_SNIFFER` in `main.cpp`
2. Connect ESP32 to existing controller ↔ repeller bus
3. Observe packet exchanges to decode new message types

### Adding New Packet Types
1. Capture unknown packets using sniffer mode
2. Add packet definitions to `known_packets.h`
3. Update `identifyPacket()` in both `sniffer_mode.cpp` and `tx_controller.cpp`
4. Add transmission functions to `tx_controller.h/.cpp` if needed

## Build and Development

### PlatformIO Configuration
- **Platform**: espressif32
- **Board**: esp32-c6-devkitc-1
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

### Startup Sequence
1. Multiple `tx_startup:XX` messages (device discovery?)
2. `tx_powerup` (system-wide power on)
3. `tx_warmup_1:XX` → `rx_warmup_1` (warmup sequence)
4. `tx_warmup_2:XX` → `rx_warmup_2`
5. `tx_warmup_3:XX` → `rx_warmup_3`
6. `tx_heartbeat:XX` messages (status checks)
7. `tx_color_startup:XX` (initial color setup)
8. `tx_led_brightness_startup:XX` (initial brightness)
9. `tx_startup_comp:XX` (startup complete)

### Communication Patterns
- **Request-Response**: Most TX commands get RX acknowledgments
- **Timeout Handling**: 500ms default timeout for responses
- **Error Detection**: Incomplete packets are discarded
- **Packet Separation**: 8ms gap indicates new packet

## Known Issues & Limitations

### Current Limitations
- Transmission functions use hardcoded addresses (need parameterization)
- Some RX packet patterns may need device-specific variations
- Color commands may be broadcast vs. addressed (needs verification)

### Future Enhancements
- Auto-discovery of device addresses
- Dynamic address assignment for transmission functions
- Enhanced error handling and retry logic
- Configuration file for device-specific parameters

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
```

### Address Discovery
Run sniffer mode and look for `rx_startup:XX` packets to identify device addresses.