# Freezy Arena · ESP32 Direct Dual Relay

**This is the `direct-dual-relay-field-monitor` branch.** The Horner PLC version
remains intact on `main`. The standalone ESP32 monitors an **unmodified** Freezy Arena server.

## Relay behavior

| Condition | Relay 1 (GPIO26) — Robot fault | Relay 2 (GPIO27) — FMS health |
| --- | --- | --- |
| FMS connected; every assigned robot linked or station bypassed | OFF | ON |
| FMS connected; a required robot disconnects mid-match (or E-stop) | ON | ON |
| FMS disconnected or `arenaStatus` older than 4 seconds | ON | FLASH: 1 s ON / 1 s OFF |

These are **independent** outputs: both may be ON simultaneously. Neither
depends on whether a match is actually running. Freezy Arena publishes robot
`RobotLinked`, `Bypass`, `EStop`, and assigned team information through its
existing `/displays/field_monitor/websocket` endpoint, which our ESP32 subscribes
to in read-only mode. No server modifications are needed.

Unassigned stations are ignored. A bypassed assigned station counts as ready
unless E-stopped. With zero assigned teams the indicator conservatively reports
a robot-not-ready fault.

## Hardware setup

Use **two 5 V, high-level-trigger relay modules**, with **H** selected on both
modules' jumpers:

| ESP32/power | Relay board terminal | Role |
| --- | --- | --- |
| GPIO26 / P26 | Relay 1 `IN` | Robot fault |
| GPIO27 / P27 | Relay 2 `IN` | FMS health |
| Regulated 5 V | Both `DC+` | Relay coil power |
| Real ESP32 GND | Both `DC−` | Shared low-voltage reference |

Use the isolated `COM` and `NO` contacts on each relay to switch your
12 V stack light circuits. The GPIO output is 3.3 V and controls the relay
input only; it does **not** provide coil power. The two modules used during
prototyping worked reliably in HIGH-trigger mode. Do **not** use the ESP32
flash-memory `CMD` pin as ground.

## Build and test

1. Open the `esp32-platformio/` folder in VS Code/PlatformIO.
2. Run **Clean → Build → Upload** for `esp32dev`, or upload the compiled
   `.pio/build/esp32dev/firmware.bin` through the ESP32 dashboard.
3. Configure Wi-Fi and Freezy Arena IP/port in the dashboard.
4. Use the tests **Relay 1**, **Relay 2**, **Both ON**, **FMS outage pattern**,
   **Both OFF**, and **Return AUTO**. Tests last no more than 15 seconds and
   are disabled during a running FMS match.
5. Verify `arenaStatus` counter advances under Diagnostics, and test a
   disconnected robot plus unplugging FMS network access.

Existing Wi-Fi credentials and management login remain in ESP32 NVS. Pin
preferences stay configurable (defaults GPIO26/27). This is a status accessory,
**not a safety-rated device**. During ESP32 power loss or reset, the controller
cannot energize or blink relays. Frequent mechanical-relay blinking shortens
contact/coil life; for long outages consider the tower's own LED flasher or an
appropriately rated solid-state output instead.

No changes have been made to the original `Freezy-Arena/freezy-arena` FMS repository.
\nFirmware source: `src/main.cpp`. Test logic: `tests/test_logic.cpp`.\n