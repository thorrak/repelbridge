# RepelBridge - Cloud-free Mosquito Repelling

An ESP32-based controller that manages Thermacell Liv Repeller devices over WiFi without using the cloud. 

**NOTE** - This is an independent, third party project, and is not affiliated with Thermacell in any way. Thermacell and Thermacell LIV are trademarks of Thermacell Repellents, Inc. 

## Features

### Cloud Free
- Works entirely locally
- Integrates with Home Assistant over WiFi

### Dual Bus Support
- Independent control of two sets of up to five repellers
- Create "zones" of repellers that are controlled independently of one another

### Device Management
- Automatic device discovery and addressing (including for brand new devices)
- RGB LED color control and brightness adjustment
- Cartridge usage tracking and monitoring (WiFi only, see "limitations" below)
- Configurable auto-shutoff timers & cartridge warn timers

### Multi-Mode Operation
- **WiFi Mode**: Web-based REST API control for Home Assistant integration
- **Sniffer Mode**: Passive monitoring of RS-485 communications (for development)

### 100% Free & Open Source
- Open source firmware
- Open source hardware
- Open source Home Assistant integration
- All 100% free, and licensed under the (Apache 2.0 license)[LICENSE]


## Limitations

The communication protocol was reverse engineered from the official controller, and is not complete. Known to be missing is the built in "end of cartridge life" blinking, but other features not identified 



## Hardware

### Required Components
- **ESP32-S3 Microcontroller** - [ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html) recommended
- **Assembled RepelBridge PCB** - Design files available in this repo, including complete Bill of Materials. See [build guide](BUILDING.md) for details.
- **1x or 2x M12 4-pin A-coded Female Jacks** - [These](https://www.amazon.com/Waterproof-Connector-Bulkhead-Straight-Receptacle/dp/B0BVZDQYH5) or similar
- **Power Cable** - Standard [AC power cable](https://www.amazon.com/dp/B07C9D6CXY) of the appropriate length
- **Enclosure** - The one I used is [this one](https://www.amazon.com/dp/B0D97GQRGX), see [build guide](BUILDING.md) for more info

For complete build instructions, see the [build guide](BUILDING.md). 

For my build, material cost excluding shipping/taxes was appx. $97.14 per controller (each of which can manage two banks of repellers)


### Pin Connections (Xiao ESP32-S3)

#### Bus 0 (Primary)
- MAX3485 A/B → RS-485 bus 0 differential pair
- RO (Receive) → GPIO44 (UART RX)
- DI (Driver) → GPIO43 (UART TX)
- DE/RE (tied) → GPIO6 (Direction control)
- Power control → GPIO9

#### Bus 1 (Secondary)
- MAX3485 A/B → RS-485 bus 1 differential pair
- RO (Receive) → GPIO5
- DI (Driver) → GPIO7
- DE/RE (tied) → GPIO4 (Direction control)
- Power control → GPIO8

Pin assignments are defined as `BUS_X_*_PIN` build flags in [platformio.ini](platformio.ini) and can be overridden there.

### Communication Specs
- **Baud Rate**: 19,200
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Protocol**: RS-485 differential signaling
- **Packet Size**: 11 bytes fixed






## Building

### Prerequisites
- [PlatformIO](https://platformio.org/) installed
- ESP-IDF toolchain (fetched automatically by PlatformIO)
- Git for cloning dependencies

### Build Environment

The project ships a single PlatformIO environment targeting the Seeed XIAO ESP32-S3 with the ESP-IDF framework:

```bash
pio run -e esp32-s3-wifi
```

### Dependencies
Automatically managed by PlatformIO:
- `thorrak/esp_wifi_config` for WiFi provisioning (captive portal / Network Provisioning over BLE)
- `bblanchon/ArduinoJson` for REST API responses
- LittleFS (via ESP-IDF) for persistent settings storage

## Flashing

### Initial Flash
1. Connect the XIAO ESP32-S3 to your computer via USB-C
2. Build and upload firmware:
   ```bash
   pio run -e esp32-s3-wifi -t upload
   ```
3. Monitor serial output:
   ```bash
   pio device monitor -e esp32-s3-wifi
   ```

### WiFi Setup (First Boot)
1. Device creates WiFi access point: `RepelBridgeAP`
2. Connect to AP using password: `repelbridge`
3. Configure your WiFi network via captive portal (BLE provisioning is also supported)
4. Device will restart and connect to your network

### Mode Selection
Operational mode is selected at compile time via build flags in [platformio.ini](platformio.ini). The shipped `esp32-s3-wifi` environment defines `MODE_WIFI_CONTROLLER`. Alternate modes (`MODE_SNIFFER`, `MODE_CONTROLLER`) are still supported in the source and can be enabled by swapping the build flag.

## Home Assistant Integration

### Automatic Discovery
- Device advertises via mDNS as `_repelbridge._tcp.local.`
- Appears in Home Assistant as discovered integration
- Alternatively, manually add using device IP address

### Entities Created
For each bus (0 and 1):

**Light Entities**
- `light.repelbridge_bus_0` - RGB color and brightness control
- `light.repelbridge_bus_1` - RGB color and brightness control

**Switch Entities**
- `switch.repelbridge_bus_0_power` - Power on/off control
- `switch.repelbridge_bus_1_power` - Power on/off control

**Sensor Entities**
- `sensor.repelbridge_bus_0_runtime_hours` - Cartridge usage hours
- `sensor.repelbridge_bus_0_cartridge_life` - Remaining cartridge life %
- `sensor.repelbridge_bus_0_device_count` - Connected repeller count

**Configuration Entities**
- `number.repelbridge_bus_0_auto_shutoff` - Auto shutoff timer (seconds)
- `number.repelbridge_bus_0_cartridge_warning` - Cartridge warning threshold (hours)

### Services
- `repelbridge.reset_cartridge` - Reset cartridge runtime tracking

## Automation Examples

### Low Cartridge Warning

```yaml
automation:
  - alias: "RepelBridge Low Cartridge Warning"
    trigger:
      platform: numeric_state
      entity_id: sensor.repelbridge_bus_0_cartridge_life
      below: 10
    action:
      service: notify.mobile_app_your_phone
      data:
        message: "RepelBridge Bus 0 cartridge is running low ({{ states('sensor.repelbridge_bus_0_cartridge_life') }}% remaining)"
```

### Daily Schedule

```yaml
automation:
  - alias: "RepelBridge Daily Schedule"
    trigger:
      platform: time
      at: "20:00:00"
    action:
      - service: light.turn_on
        target:
          entity_id: light.repelbridge_bus_0
        data:
          rgb_color: [0, 213, 255]  # Cyan-blue
          brightness: 200
  
  - alias: "RepelBridge Turn Off"
    trigger:
      platform: time
      at: "06:00:00"
    action:
      service: light.turn_off
      target:
        entity_id: light.repelbridge_bus_0
```

### REST API Endpoints

Direct API access available at `http://device-ip/`:

**System Information**
- `GET /api/system/status` - Device status, uptime, WiFi info

**Bus Control** (replace `{0,1}` with bus number)
- `GET /api/bus/{0,1}/status` - Bus state and current settings
- `POST /api/bus/{0,1}/power` - Power control (JSON: `{"state": true/false}`)
- `POST /api/bus/{0,1}/brightness` - Brightness (JSON: `{"value": 0-255}`)
- `POST /api/bus/{0,1}/color` - RGB color (JSON: `{"red": 0-255, "green": 0-255, "blue": 0-255}`)

**Cartridge Management**
- `GET /api/bus/{0,1}/cartridge` - Usage stats and remaining life
- `POST /api/bus/{0,1}/cartridge/reset` - Reset runtime counter
- `GET /api/bus/{0,1}/auto_shutoff` - Get current auto-shutoff timer in minutes
- `POST /api/bus/{0,1}/auto_shutoff` - Set auto-shutoff timer (JSON: `{"minutes": 0-960}`)
- `GET /api/bus/{0,1}/warn_at` - Get current cartridge warning threshold in hours
- `POST /api/bus/{0,1}/warn_at` - Set warning threshold (JSON: `{"hours": 0-9999}`)

## Troubleshooting

### Device Not Discovered
- Ensure device is on the same network
- Check device is in WiFi mode (`MODE_WIFI_CONTROLLER`)
- Verify mDNS service `_repelbridge._tcp.local.` is broadcast

### Connection Issues
- Verify IP address is correct
- Check device web interface is accessible at `http://device-ip/api/system/status`
- Ensure firewall allows HTTP traffic on port 80

### Entity Updates
- Default update interval is 30 seconds
- Entities update automatically after control commands
- Check Home Assistant logs for API communication errors

## Support

For issues and feature requests, please use the GitHub repository issue tracker.

## License

This integration is provided under the Apache 2.0 license. 