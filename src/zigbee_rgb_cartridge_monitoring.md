Up until now, our “main” controller program has been a placeholder for testing. We’re now going to create a third mode: `MODE_ZIGBEE_CONTROLLER`

In this mode, we’re going to use the ESP32-C6’s Zigbee radio to control each bus.

One thing to note - even though we are controlling repellers, because of the functionality that the repellers provide, they function very similarly to how an RGB light would. 


# Zigbee Repeller + Cartridge Monitoring Feature — Engineering Implementation Brief

## Overview

This device is a Zigbee-controlled repeller (with RGB lighting) that supports:

- Standard color and brightness controls:
  - On/Off switching
  - Brightness
  - Hue
- Custom monitoring of a replaceable cartridge:
  - Track runtime in hours
  - Report remaining life in percent
  - Provide a reset command for cartridge replacement

---

## Zigbee Interface Specification

We should expose one of each of these clusters for each bus.

### Standard Clusters

| Feature           | Cluster                  | Attribute / Command                   | Notes                |
|-------------------|--------------------------|---------------------------------------|----------------------|
| Power control     | On/Off (`0x0006`)        | `on`, `off`                           | Controls bus state.  |
| Brightness        | Level Control (`0x0008`) | `MoveToLevelWithOnOff`                | Level: 0–254         |
| Hue control       | Color Control (`0x0300`) | `MoveToHue`, `MoveToHueAndSaturation` | Hue: 0–254           |

The Zigbee Brightness and Hue control are sent to us on a 0-254 scale, and when changed should be saved using `ZigbeeSetBrightness(uint8_t brightness)` and `ZigbeeSetHue(uint16_t hue)` for the appropriate bus.

When the power control is set to `on`, `ZigbeePowerOn()` should be called. When the power control is set to `off`, `ZigbeePowerOff` should be called.

---

### Custom Manufacturer Cluster

- **Cluster ID**: `0xFC00` (custom)
- **Manufacturer Code**: `0x1234`

#### Attributes

| Attribute ID | Name             | Type     | Description                      |
|--------------|------------------|----------|----------------------------------|
| `0xF001`     | `runtime_hours`  | `uint16` | Hours of cartridge usage         |
| `0xF002`     | `percent_left`   | `uint8`  | Estimated life remaining (0–100) |

#### Command

| Command ID | Name             | Description                                  |
|------------|------------------|----------------------------------------------|
| `0x01`     | `reset_cartridge`| Resets `runtime_hours` and `percent_left`    |

When the `reset_cartridge` command is triggered, `ZigbeeResetCartridge()` should be called. 