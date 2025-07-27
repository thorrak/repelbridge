# RepelBridge - Thermacell Liv Repeller Controller Emulation Project

## Project Overview
RepelBridge emulates a controller for Thermacell's Liv mosquito repeller using reverse-engineered RS-485 communication protocols. The goal is to create an ESP32-based controller that can communicate with and control Thermacell Liv repeller devices through multiple interfaces including WiFi REST API, Zigbee smart home integration, and direct controller emulation.

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
- `src/bus.h/.cpp` - Bus class for controller emulation, transmission, and reception
- `src/packet.h/.cpp` - Packet class for RS-485 packet handling
- `src/repeller.h/.cpp` - Repeller device management
- `src/known_packets.h` - Predefined packet definitions
- `src/zigbee_controller.h/.cpp` - Zigbee device management and smart home integration
- `src/wifi_controller.h/.cpp` - WiFi web server and REST API for Home Assistant integration
- `src/getGuid.h/.cpp` - Device identification utilities

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
- Independent RGB color storage (red, green, blue uint8_t values)

### Mode Selection
Modes are now controlled via PlatformIO build environments and compile-time flags:
```cpp
#define MODE_SNIFFER 0          // Passive monitoring
#define MODE_CONTROLLER 1       // Active controller emulation
#define MODE_ZIGBEE_CONTROLLER 2 // Zigbee smart home integration
#define MODE_WIFI_CONTROLLER 3   // WiFi REST API with Home Assistant integration
```

Build environments:
- `esp32c6dev-wifi` - WiFi controller mode (recommended for Home Assistant)
- `esp32c6dev-zigbee` - Zigbee controller mode
- `esp32dev` - Generic development environment

### Key Classes and Functions

#### Bus Class Methods
- `bus.init()` - Initialize bus hardware and load settings from filesystem
- `bus.activate()` - Power on and activate the bus
- `bus.powerdown()` - Power down and deactivate the bus
- `bus.poll()` - Poll bus for repeller status updates (called periodically)
- `bus.transmit(Packet *packet)` - Transmit packet on specific bus
- `bus.receive_packet(Packet& packet, timeout)` - Receive packet on specific bus
- `bus.receive_and_print(expected_type, timeout)` - Receive and print packet for debugging
- `bus.send_tx_*()` - Individual packet transmission functions
- `bus.discover_repellers()` - Find devices on this bus
- `bus.get_repeller(address)` - Get repeller by address
- `bus.heartbeat_poll()` - Poll all repellers on this bus
- `bus.warm_up_all()`, `bus.shutdown_all()` - Bus-wide operations
- `bus.change_led_brightness(brightness_pct)` - Change LED brightness during operation
- `bus.change_led_color(red, green, blue)` - Change LED color during operation

#### Settings Management (LittleFS)
Each bus maintains persistent settings saved to `/bus[X]_settings.dat`:
- `bus.ZigbeeSetRGB(red, green, blue)` - Set RGB color values and save to filesystem
- `bus.ZigbeeSetBrightness(0-254)` - Set brightness and save to filesystem
- `bus.ZigbeeResetCartridge()` - Reset cartridge active time to 0
- `bus.ZigbeeSetCartridgeWarnAtSeconds(seconds)` - Set cartridge warning threshold
- `bus.ZigbeeSetAutoShutOffAfterSeconds(0-57600)` - Set auto shut-off timer
- `bus.repeller_brightness()` - Convert Zigbee brightness (0-254) to repeller format (0-100)
- `bus.repeller_red/green/blue()` - Return stored RGB color components
- `bus.save_active_seconds()` - Update cartridge usage tracking
- `bus.past_automatic_shutoff()` - Check if auto shut-off time has elapsed

#### RGB Color Management
Each bus stores RGB values directly instead of using hue-based color conversion:

```cpp
// Default RGB values (cyan-blue color)
uint8_t red = 0x03;    // Low red component
uint8_t green = 0xd5;  // High green component  
uint8_t blue = 0xff;   // Maximum blue component

// Simple accessors return stored values
uint8_t Bus::repeller_red() { return red; }
uint8_t Bus::repeller_green() { return green; }
uint8_t Bus::repeller_blue() { return blue; }

// Zigbee interface sets RGB directly
void Bus::ZigbeeSetRGB(uint8_t zb_red, uint8_t zb_green, uint8_t zb_blue);
```

Note that the Zigbee standard sends values to/from devices as hue and saturation, both of which are clamped to a value of 254 (not 255). This means that the color #0000FF must instead become #0101FF 

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
- **Dependencies**: WiFiManager, ArduinoJson, LittleFS

### Build Commands
```bash
# WiFi Controller Mode (recommended)
pio run -e esp32c6dev-wifi
pio run -e esp32c6dev-wifi -t upload

# Zigbee Controller Mode
pio run -e esp32c6dev-zigbee
pio run -e esp32c6dev-zigbee -t upload

# Generic ESP32 Development
pio run -e esp32dev
pio run -e esp32dev -t upload

# Monitor serial output
pio device monitor
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

### Persistent Settings
Each bus stores configuration in LittleFS at `/bus[X]_settings.dat`:
- **Red** (0-255): Red color component for repeller LEDs (default: 0x03)
- **Green** (0-255): Green color component for repeller LEDs (default: 0xd5)
- **Blue** (0-255): Blue color component for repeller LEDs (default: 0xff)
- **Brightness** (0-254): Brightness setting for repeller LEDs (default: 100)
- **CartridgeActiveSeconds**: Total seconds the cartridge has been active (default: 0)
- **CartridgeWarnAtSeconds**: Warning threshold for cartridge replacement (default: 349200)
- **AutoShutOffAfterSeconds** (0-57600): Auto shut-off timer in seconds (default: 18000)

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

## Zigbee Smart Home Integration

### MODE_ZIGBEE_CONTROLLER Overview
The Zigbee controller mode transforms the ESP32-C6 into a smart home hub that exposes each repeller bus as a separate Zigbee device. Each bus functions as an RGB light with additional cartridge monitoring capabilities.

### Zigbee Device Architecture
```cpp
// Each bus gets its own Zigbee endpoint
ZigbeeRepellerDevice* zigbee_bus0_device; // Endpoint 1 for Bus 0
ZigbeeRepellerDevice* zigbee_bus1_device; // Endpoint 2 for Bus 1
```

### Supported Zigbee Clusters

#### Standard Clusters (Per Bus)
| Feature           | Cluster                  | Zigbee Attribute/Command      | Bus Method Called           |
|-------------------|--------------------------|-------------------------------|-----------------------------|
| Power control     | On/Off (`0x0006`)        | `on`, `off`                   | `ZigbeePowerOn()`, `ZigbeePowerOff()` |
| Brightness        | Level Control (`0x0008`) | `MoveToLevelWithOnOff`        | `ZigbeeSetBrightness(0-254)` |
| RGB color control | Color Control (`0x0300`) | `setLight(RGB)`               | `ZigbeeSetRGB(red, green, blue)`     |

#### Custom Manufacturer Cluster (`0xFC00`)
- **Manufacturer Code**: `0x1234`
- **Attributes**:
  - `runtime_hours` (`0xF001`): Hours of cartridge usage
  - `percent_left` (`0xF002`): Estimated life remaining (0-100%)
- **Commands**:
  - `reset_cartridge` (`0x01`): Calls `ZigbeeResetCartridge()`

### Zigbee Integration Flow

#### Power Control Integration
```cpp
// Zigbee ON command sequence
bus.ZigbeePowerOn() → {
  if (BUS_OFFLINE) bus.activate()
  if (no repellers) discover_repellers(), retrieve_serial_for_all()
  if (BUS_POWERED) warm_up_all()
}

// Zigbee OFF command sequence  
bus.ZigbeePowerOff() → {
  save_active_seconds()
  shutdown_all()
}
```

#### Settings Synchronization
- **Brightness Control**: Zigbee 0-254 scale → Bus `repeller_brightness()` 0-100 scale
- **Color Control**: Zigbee RGB values → Direct storage via `ZigbeeSetRGB()` and retrieval via `repeller_red/green/blue()`
- **Persistent Storage**: All settings saved to LittleFS via `save_settings()`

### Cartridge Monitoring Features

#### Runtime Tracking
```cpp
uint16_t runtime_hours = bus.get_cartridge_runtime_hours();
// Automatically tracks active seconds when bus is BUS_WARMING_UP or BUS_REPELLING
```

#### Life Percentage Calculation
```cpp
uint8_t percent_left = bus.get_cartridge_percent_left();
// Based on cartridge_active_seconds vs cartridge_warn_at_seconds
```

#### Automatic Shutoff
```cpp
if (bus.past_automatic_shutoff()) {
  bus.ZigbeePowerOff(); // Auto-shutoff after configured time
}
```

### Configuration and Setup

#### Hardware Requirements
- **ESP32-C6** with Zigbee radio capability
- **Dual RS-485 buses** (same hardware as controller mode)
- **LittleFS filesystem** for persistent settings storage

#### Zigbee Network Configuration
```cpp
// Device acts as Zigbee Coordinator
Zigbee.startCoordinator();
Zigbee.setRebootOpenNetwork(180); // 3-minute join window
```

#### Bus Endpoint Mapping
- **Bus 0** → Zigbee Endpoint 1 → "Liv Repeller Bus 0"
- **Bus 1** → Zigbee Endpoint 2 → "Liv Repeller Bus 1"

### Smart Home Integration Examples

#### Home Assistant Configuration
```yaml
# Each bus appears as a separate light entity
light.liv_repeller_bus_0:
  platform: zigbee
  features:
    - brightness
    - color_temp
    - rgb_color

# Custom cartridge monitoring sensors
sensor.liv_repeller_bus_0_runtime_hours:
  platform: zigbee
  
sensor.liv_repeller_bus_0_cartridge_remaining:
  platform: zigbee
```

#### Automation Examples
```yaml
# Auto-shutoff when cartridge low
automation:
  - trigger:
      platform: numeric_state
      entity_id: sensor.liv_repeller_bus_0_cartridge_remaining
      below: 10
    action:
      service: light.turn_off
      entity_id: light.liv_repeller_bus_0
```

### Development and Testing

#### Testing Zigbee Mode
1. Set `CURRENT_MODE = MODE_ZIGBEE_CONTROLLER` in `main.cpp`
2. Ensure ESP32-C6 target in platformio.ini: `board = seeed_xiao_esp32c6`
3. Upload firmware and monitor serial output for Zigbee network formation
4. Use Zigbee coordinator app to discover and control devices
5. Verify both buses operate independently through Zigbee commands

#### Debugging Zigbee Integration
- Monitor serial output for Zigbee callback execution
- Verify bus state changes when Zigbee commands are received
- Check LittleFS settings persistence across power cycles
- Validate cartridge monitoring calculations

### Known Limitations
- Custom manufacturer cluster support depends on ESP32 Zigbee library capabilities
- Currently uses shared Serial1 for both buses (hardware limitation)
- Cartridge monitoring requires manual configuration of warning thresholds

## WiFi Smart Home Integration

### MODE_WIFI_CONTROLLER Overview
The WiFi controller mode creates a web server with REST API endpoints that integrate seamlessly with Home Assistant. The device creates a captive portal for initial WiFi setup and then provides mDNS discovery for automatic integration.

### WiFi Device Architecture
```cpp
// Each bus gets its own WiFi device wrapper
WiFiRepellerDevice* wifi_bus0_device; // Bus 0 control
WiFiRepellerDevice* wifi_bus1_device; // Bus 1 control
```

### Supported REST API Endpoints

#### System Information
- `GET /api/system/status` - Device status, uptime, WiFi connection info

#### Bus Control (replace `{busId}` with 0 or 1)
- `GET /api/bus/{busId}/status` - Bus state, settings, and repeller count
- `POST /api/bus/{busId}/power` - Power control (JSON: `{"power": true/false}`)
- `POST /api/bus/{busId}/brightness` - Brightness control (JSON: `{"brightness": 0-254}`)
- `POST /api/bus/{busId}/color` - RGB color control (JSON: `{"red": 0-255, "green": 0-255, "blue": 0-255}`)

#### Cartridge Management
- `GET /api/bus/{busId}/cartridge` - Runtime hours and remaining life percentage
- `POST /api/bus/{busId}/cartridge/reset` - Reset cartridge runtime tracking
- `POST /api/bus/{busId}/auto_shutoff` - Set auto-shutoff timer (JSON: `{"seconds": 0-57600}`)
- `POST /api/bus/{busId}/cartridge_warn_at` - Set warning threshold (JSON: `{"hours": 0-9999}`)

### WiFi Integration Flow

#### Initial Setup
1. Device boots and creates WiFi AP: `RepelBridge-Setup` (password: `repelbridge`)
2. User connects and configures WiFi via captive portal
3. Device connects to network and starts mDNS service `_repelbridge._tcp.local.`
4. Home Assistant discovers device automatically or user adds manually by IP

#### Power Control Integration
```cpp
// WiFi power control sequence
POST /api/bus/0/power {"power": true} → {
  bus.ZigbeePowerOn()
  if (BUS_OFFLINE) bus.activate()
  if (no repellers) discover_repellers(), retrieve_serial_for_all()
  if (BUS_POWERED) warm_up_all()
}

// WiFi power off sequence
POST /api/bus/0/power {"power": false} → {
  bus.save_active_seconds()
  bus.shutdown_all()
}
```

#### Settings Synchronization
- **Brightness Control**: Home Assistant 0-255 scale → Bus `ZigbeeSetBrightness(0-254)` → Repeller 0-100 scale
- **Color Control**: Home Assistant RGB values → Direct storage via REST API → `ZigbeeSetRGB()`
- **Persistent Storage**: All settings automatically saved to LittleFS via `save_settings()`

### Home Assistant Configuration

#### Discovery
Device appears automatically in Home Assistant integrations as "RepelBridge" when connected to the same network.

#### Manual Configuration
If automatic discovery fails, manually add integration:
1. Go to **Settings** → **Devices & Services**
2. Click **Add Integration** → **RepelBridge**
3. Enter device IP address (find via router or serial monitor)

#### Entity Types Created
For each bus (0 and 1):
- **Light Entity**: `light.repelbridge_bus_X` (RGB color and brightness)
- **Switch Entity**: `switch.repelbridge_bus_X_power` (on/off control)
- **Sensor Entities**: Runtime hours, cartridge life %, device count
- **Number Entities**: Auto-shutoff timer, cartridge warning threshold

### Development and Testing

#### Testing WiFi Mode
1. Build with `pio run -e esp32c6dev-wifi`
2. Upload firmware and connect to `RepelBridge-Setup` AP
3. Configure WiFi via captive portal
4. Test REST API endpoints: `curl http://device-ip/api/system/status`
5. Verify Home Assistant discovery and entity creation

#### Debugging WiFi Integration
- Monitor serial output for WiFi connection status and API requests
- Verify mDNS advertisement: `dns-sd -B _repelbridge._tcp`
- Test API endpoints manually before Home Assistant integration
- Check LittleFS settings persistence across power cycles

### WiFi Mode Limitations
- Requires stable WiFi connection for Home Assistant integration
- REST API timeout handling may need adjustment for slow repeller responses
- Currently uses shared Serial1 for both buses (hardware limitation)
- Device must be on same network as Home Assistant for automatic discovery