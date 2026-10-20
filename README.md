# Freezy Arena — ESP32 + Horner PLC Field Light

An ESP32 Wi-Fi gateway that monitors **unmodified Freezy Arena** and drives the analog inputs of a **Horner HE-XT102CH** PLC for a two-color field-status stack light. The Horner switches the relays and implements the heartbeat watchdog; the ESP32 never powers the stack light directly.

**No Freezy Arena patches or server-side configuration changes are required.** The gateway uses the FMS’s existing WebSocket endpoints.

## Components

- **`esp32-platformio/`** — PlatformIO ESP32 firmware, management dashboard, monitoring logic, tests and wiring instructions.
- **`horner-plc/`** — Cscape Structured Text source and HMI documentation (not a compiled PGM or CSP project).

## Monitoring modes

1. **Match running** — uses `/api/arena/websocket`; red while idle, green while a match is running.
2. **Robot connectivity** — uses the existing `/displays/field_monitor/websocket` stream; green when each **assigned** robot is linked or its station is bypassed, with no E-stop; red otherwise. Unassigned stations are ignored.
3. **Field readiness** — uses the same existing Field Monitor stream; green when `CanStartMatch` is true, or a match is running; red otherwise.

The ESP32 processes only the useful FMS `arenaStatus` and `matchTime` messages and discards unrelated high-volume display messages without deserializing them. It supports fragmented WebSocket frames. The Field Monitor WebSocket registers a named ESP32 display in the FMS display registry. This is a read-only connection.

The stack light's internal flasher creates the flashing effect. When the FMS WebSocket disconnects, or its heartbeat is stale, both GPIO requests go LOW and the Horner PLC watchdog turns both lamps off. While a healthy FMS connection is established but readiness details have not arrived, Modes 2/3 show RED rather than suggesting the field is ready. This is **not** a safety-rated control, E-stop, or protective device.

## Wiring

| ESP32 | Horner | Purpose |
|---|---|---|
| GPIO26 | A1 (0–10 V mode) | Heartbeat (toggles every 1 sec) |
| GPIO27 | A2 (0–10 V mode) | Green / running request (HIGH = green) |
| GND | 0 V | Shared signal reference |

Verify the Horner's physical JP2 voltage-mode jumpers before connecting, establish compatible power references, and **never** connect PLC 24 V positive to ESP32 GPIO. See [`esp32-platformio/docs/ESP32_TO_HORNER_WIRING.md`](esp32-platformio/docs/ESP32_TO_HORNER_WIRING.md).

## Build and deploy

1. Open **`esp32-platformio/`** in VS Code/PlatformIO and run Clean → Build → Upload. `tools/embed_web.py` generates `include/WebUI.h` during build.
2. Use the ESP32 management UI to set Wi-Fi, FMS hostname/port and the monitoring mode. The mode is saved in NVS and settings persist between firmware updates.
3. Check Diagnostics for the active FMS WebSocket path, `arenaStatus` frame count, fragmented message count, parse errors and heartbeat output.
4. Verify each mode in a controlled field test and verify the heartbeat stops on FMS disconnect.

No Freezy Arena changes and no Horner PLC changes are needed. The ESP32 project has native logic tests; a complete ESP32 toolchain build and physical FMS/PLC integration test should be completed before event use.

Do not commit device exports, Wi-Fi credentials or management passwords.