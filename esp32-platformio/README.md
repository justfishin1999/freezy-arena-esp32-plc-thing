# ESP32 / Horner Gateway v1.6 — Stock Freezy Arena

This firmware connects to **unmodified Freezy Arena**. No FMS plugin, extension, or code patch is required.

## Field data streams

- **Mode 1: Match Running** uses `ws://<host>:<port>/api/arena/websocket`. This low-volume endpoint provides `matchTime` and FMS keepalive messages.
- **Modes 2 and 3: Robot Connectivity / Field Readiness** use `ws://<host>:<port>/displays/field_monitor/websocket?displayId=esp32-fieldlight-<device>&nickname=ESP32-Field-Light`. This existing, non-administrative display endpoint publishes `arenaStatus`, including each assigned robot's link and bypass state and `CanStartMatch`.

In Modes 2/3, the ESP32 immediately skips unrelated large Field Monitor updates (`realtimeScore`, display configuration etc.). It assembles fragmented `arenaStatus` messages, applies an ArduinoJson field filter, and caches the filter instead of rebuilding it for every update. The FMS may show `ESP32-Field-Light` as a display client.

The `/api/status` diagnostics include websocket path, accepted readiness frames, ignored messages, fragments, parse errors and peak parse time. Any malformed or stale readiness data is **not treated as green**. If the FMS connection is lost entirely, the PLC heartbeat stops and both lamps turn off after the watchdog timeout.

## Installation

1. Open this directory as a PlatformIO project.
2. Run `pio test -e native` for native C++ tests (or compile `tests/test_logic.cpp` using g++), then `pio run -e esp32dev`.
3. Flash over USB (`pio run -t upload`) or install the resulting `firmware.bin` through the current device's management dashboard. **Do not erase NVS** if preserving settings.
4. Select a mode under **Settings → Field Monitoring Mode**; save and reboot.
5. Confirm `arenaStatus` increments in Diagnostics on Mode 2/3. Mode 1 has no `arenaStatus` and does not need it.

Horner wiring is unchanged: GPIO26 → A1 heartbeat, GPIO27 → A2 green request, GND → Horner 0V (after checking common power), 0–10 V voltage-mode analog inputs. The PLC and stack-light wiring should stay as already configured.

## Validation caveat

Native mapping and websocket-envelope tests are included. Full PlatformIO ESP32 compilation and hardware integration must still be verified on your build machine.