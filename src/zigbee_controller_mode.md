

# Zigbee Implementation — Engineering Implementation Brief

## Overview

Implement cartridge runtime tracking and reporting in a Zigbee device using a custom manufacturer-specific cluster. The device should:

- Track cartridge runtime (in hours)
- Report estimated percentage of cartridge life remaining
- Allow the cartridge counter to be reset via Zigbee command

Target compatibility: **Home Assistant** (via **ZHA** or **Zigbee2MQTT**)

---

## Zigbee Interface Specification

### Custom Manufacturer Cluster

- **Cluster ID**: `0xFC00` (custom)
- **Manufacturer Code**: `0x1234` (arbitrary, consistent across all attributes and commands)

#### Attributes

| Attribute ID | Name             | Type     | Description                      |
|--------------|------------------|----------|----------------------------------|
| `0xF001`     | `runtime_hours`  | `uint16` | Hours of cartridge usage         |
| `0xF002`     | `percent_left`   | `uint8`  | Estimated life remaining (0–100) |

Attributes should support **read** and **report** access.

#### Command

| Command ID | Name             | Description                                  |
|------------|------------------|----------------------------------------------|
| `0x01`     | `reset_cartridge`| Resets `runtime_hours` and `percent_left`    |

This command is vendor-specific and must be processed internally to reset the counters.

---

## Firmware Requirements

- Track cumulative run time internally (e.g., via `millis()` or RTC).
- Update `runtime_hours` attribute regularly (e.g., every 15 minutes or on change).
- Derive `percent_left` based on a fixed cartridge lifespan (e.g., 100 hours = 1% per hour).
- Implement command handler for `reset_cartridge` to zero the runtime counter and reset state.

---

## Home Assistant Integration

### ZHA (Zigbee Home Automation)

- Requires a **ZHA Quirk** (custom Python class in Home Assistant).
- Define the custom cluster and attribute mappings in the quirk.

**Reference**: [ZHA Device Handlers (Quirks)](https://github.com/zigpy/zha-device-handlers)

**Example:**
```python
class CartridgeMonitoringCluster(CustomCluster):
    cluster_id = 0xFC00
    manufacturer_attributes = {
        0xF001: ("runtime_hours", t.uint16_t),
        0xF002: ("percent_left", t.uint8_t),
    }
    manufacturer_commands = {
        0x01: ("reset_cartridge", ()),
    }
```

### Zigbee2MQTT

- Define a custom device in `devices.js` (or override in `external_converters`).

**Reference**: [Z2M Custom Devices](https://www.zigbee2mqtt.io/advanced/support-new-devices/01_support_new_devices.html)

**Example:**
```js
exposes: [
  exposes.numeric('runtime_hours', ea.STATE).withUnit('h'),
  exposes.numeric('percent_left', ea.STATE).withUnit('%'),
  exposes.binary('reset_cartridge', ea.SET, true, false),
],
```

---

## Testing and Verification

1. Verify that runtime and percent attributes are reported correctly to the Zigbee coordinator.
2. Issue a command to reset the cartridge counter and confirm it resets as expected.
3. In Home Assistant, ensure the runtime and percent values appear as sensor entities.
4. Confirm the reset command works via a service call or automation.

---

## References

- Zigbee Cluster Library Spec: [ZCL spec PDF](https://zigbeealliance.org/wp-content/uploads/2019/11/docs-05-3474-21-0c-zcl.pdf)
- Home Assistant ZHA Quirks: https://github.com/zigpy/zha-device-handlers
- Zigbee2MQTT Custom Device Support: https://www.zigbee2mqtt.io/advanced/support-new-devices/01_support_new_devices.html
- Example ZCL custom attribute definition: [Tuya Cluster Example](https://github.com/zigpy/zha-device-handlers/blob/dev/zhaquirks/tuya/__init__.py)
