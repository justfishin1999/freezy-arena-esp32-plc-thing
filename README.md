# Freezy Arena ESP32 — E-stop / A-stop Indicator

**Branch:** `estop-astop-dual-relay` (based on the standalone direct dual-relay
firmware, not the PLC firmware). The previous `direct-dual-relay-field-monitor`
and PLC `main` branches are untouched.

The ESP32 subscribes read-only to stock Freezy Arena's existing
`/displays/field_monitor/websocket` and reads the published `EStop` and
`AStop` flags for all six alliance stations.

| Live FMS condition | Relay 1 / GPIO26 / RED | Relay 2 / GPIO27 / GREEN |
| --- | --- | --- |
| No team E-stopped or A-stopped | OFF | OFF |
| Any assigned team E-stopped | ON | OFF |
| Any assigned team A-stopped | OFF | ON |
| Both types active, on same or different teams | ON | ON |
| FMS status unavailable or older than 4 s | OFF | OFF (status UNKNOWN) |

**"Any team" means any currently assigned team (R1–R3, B1–B3).** Empty
stations are ignored. Normal robot disconnections or bypasses no longer
trigger the outputs. E-stop and A-stop status comes from the FMS; their
latching/reset behavior belongs to Freezy Arena and is not reproduced by the
ESP32. No match-running requirement: the relays reflect the published stops
whenever status is fresh.

**Unknown FMS status cannot safely be represented as "no stops".** The dashboard
explicitly displays UNKNOWN and suppresses both relay outputs. This accessory
is NOT a safety interlock, emergency-stop system, protective device or
replacement for the actual FMS/field stop hardware. If the ESP32 loses power
its relay outputs also turn OFF.

## Wiring (same as existing direct-relay project)

- 5 V to both relay-module `DC+` terminals.
- Actual ESP32 `GND` to both module `DC−` terminals. **Do not use CMD.**
- ESP32 `GPIO26 / P26` to **RED relay (R1) IN**.
- ESP32 `GPIO27 / P27` to **GREEN relay (R2) IN**.
- Set both modules' H/L trigger jumpers to **H** (HIGH to energize).
- Lamp's separate 12 V circuits connect through each relay's COM/NO contacts.

The ESP32 GPIO only sends a 3.3 V logic signal; it does not power relay coils.

## Build and verify

1. Open `esp32-platformio/` in VS Code with PlatformIO.
2. Select `esp32dev`, build and upload, or use the ESP32 OTA web dashboard
   with the built `.pio/build/esp32dev/firmware.bin`.
3. Stored Wi-Fi credentials, server address, management login and configurable
   GPIOs persist from the previous direct-relay build.
4. The dashboard labels both stop indicators and lists the stopped station IDs.
   Manual tests: **RED E-stop**, **GREEN A-stop**, **Both ON**, **Both OFF**,
   **Return AUTO**, with 15-second timeout and disabled during active matches.
5. Check each stop using normal authorized FMS test procedures, and separately
   confirm missing FMS status changes the dashboard to UNKNOWN with both
   automatic relay outputs OFF.

The PLC project, Freezy Arena server, and previous direct-relay branch are
not modified.
