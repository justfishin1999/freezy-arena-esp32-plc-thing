# Freezy Arena — ESP32 + Horner PLC Field Light

An ESP32 Wi-Fi gateway that monitors **Freezy Arena** and drives the analog inputs of a **Horner HE-XT102CH** PLC for a two-color field-status stack light. The Horner performs relay switching and the heartbeat watchdog; the ESP32 never directly powers the stack light.

## Components

- **`esp32-platformio/`** — PlatformIO firmware, local management dashboard, monitoring logic and tests.
- **`fms-patch/`** — a review-before-applying Python patch for the Freezy Arena server; adds a compact read-only `fieldLightStatus` notification without streaming the full field monitor payload. Apply this to a **development branch** of Freezy Arena and validate before deploying.
- **`horner-plc/`** — Cscape Structured Text source, variable declarations, screen configuration notes, and HMI build/testing instructions. These are *source/instructions*, not a compiled `.PGM` or editable `.CSP` project.

## Monitoring modes

1. **Match running** — red when not running, green when running.
2. **Robot connectivity** — green when every assigned station is linked or bypassed, red otherwise.
3. **Field readiness** — green when Freezy Arena permits starting the match or a match is running, red otherwise.

The stack light's own internal flasher creates the flashing effect. On ESP32/FMS communication loss, the heartbeat stops and the PLC watchdog turns both outputs off. This is a **field indicator**, not a safety-rated control or E-stop.

## Wiring overview

| ESP32 | Horner | Function |
| --- | --- | --- |
| GPIO26 | A1 (0–10 V) | Heartbeat |
| GPIO27 | A2 (0–10 V) | Green/running request |
| GND | 0 V | Shared DC reference |

**Important:** Check the **physical JP2 jumpers** on the Horner for voltage mode, confirm compatible DC grounding before bonding supplies, and never connect the PLC's 24 V positive to an ESP32 GPIO. See [`esp32-platformio/docs/ESP32_TO_HORNER_WIRING.md`](esp32-platformio/docs/ESP32_TO_HORNER_WIRING.md).

## Build / deploy

1. Review and apply [`fms-patch/README.md`](fms-patch/README.md) to the appropriate Freezy Arena source tree. Validate Go tests and the new WebSocket feed before deployment. Modes 2/3 require the FMS patch to report actual readiness.
2. Open **`esp32-platformio/`** as a PlatformIO project, then **Clean → Build → Upload**. The pre-build script generates `include/WebUI.h` from `web/index.html` automatically.
3. Preserve your working PLC program; change it only if necessary, following the files in **`horner-plc/`**. For an older XT102 firmware, Cscape's export compatibility **must match the actual PLC firmware** (previous test hardware used firmware 12.86).
4. Test each monitoring mode and communications loss with the stack light disconnected before connecting the physical lamp.

## Status and security

This repository contains the latest project sources from the **v1.5 compact-status package**, plus the PLC/HMI source files. The ESP32 app has not been fully compiled and hardware-tested here; validate in your environment before an event. The firmware generates management/AP credentials on first boot and stores device-specific settings in NVS; **do not commit device exports, Wi-Fi credentials, or secrets**.

This is a separate personal project repository; no changes have been made to the main Freezy Arena repository by uploading these files.