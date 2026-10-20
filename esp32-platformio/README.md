# Freezy Arena ESP32 -> Horner PLC Gateway v1.5

## IMPORTANT: Two-part update

The Freezy Arena FMS **must receive the compact status notifier patch** in the accompanying `fms-patch/` directory before modes 2/3 report readiness. Without it, Mode 1 continues to work, and modes 2/3 show RED when the match is idle and readiness is unknown. This is a read-only additive change; it does not modify match-start permissions or PLC outputs.

The ESP32 now uses **only** `ws://<arena>:8080/api/arena/websocket` for all modes, which avoids the high-volume Field Monitor `arenaStatus` stream that was causing WebSocket backpressure warnings. The new `fieldLightStatus` payload contains `CanStartMatch`, `RobotsAssigned`, `RobotsSatisfied`, and `RobotsReady`; `matchTime` stays unchanged. The FMS patch sends this payload every ~500 ms.

**Wiring and Horner Cscape code unchanged:** GPIO26->A1 0-10V input, GPIO27->A2 0-10V input, ESP32 GND->Horner 0V only after verifying common reference and proper physical voltage-mode JP2 jumpers. USB 5V remains the ESP32 supply. No PLC changes are required.

### Installation order
1. On a Freezy Arena development branch, apply `fms-patch/apply_compact_status.py` to the Freezy Arena repository root. Review its modifications before committing and restarting FMS.
2. Build and test the Freezy Arena server using your normal repository Go test and build commands, then deploy it at an appropriate maintenance window. Preserve a server backup and rollback procedure.
3. Open `esp32-platformio/` with PlatformIO, Clean/Build/Upload. Firmware preserves your NVS settings.
4. In monitoring mode 2/3, Diagnostics should report `FMS subscription: /api/arena/websocket` and the first `fieldLightStatus` message. Readiness frames should advance every ~0.5 sec.
5. Verify Mode 1, Mode 2 (all required robots linked/bypassed), Mode 3 (start permitted or match running), and network-loss shutdown. Keep the PLC's watchdog unchanged.

### Caveats
- We could not run a full ESP32 toolchain build or full FMS integration test in this environment.
- Freezy Arena currently evaluates `CanStartMatch` using its existing start conditions, including all configured stations. This patch does not change those semantics. Mode 2 ignores unassigned stations but does not treat zero assigned teams as ready.
- When FMS itself is disconnected, the GPIO heartbeat stops; the PLC should turn both lights off after its three-second watchdog.
- The FMS patch adds a small, non-sensitive read-only field status update to a public endpoint, so review applicability to your local event network.