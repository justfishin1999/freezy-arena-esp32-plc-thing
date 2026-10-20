# Wiring: ESP32 → Horner HE-XT102CH (Type 2 I/O)

Power **OFF** both units while wiring. Never attach PLC 24V positive to ESP32.

| ESP32 terminal | Horner J1 terminal | Purpose |
|---|---|---|
| GND | 0V | Common low-voltage reference |
| GPIO26 (P26) | A1 | Heartbeat toggles 0/3.3V once per second while FMS state valid |
| GPIO27 (P27) | A2 | 0V idle / 3.3V match running |

- The Horner remains powered using its proper 24V DC supply. ESP32 remains powered over USB from the regulated 5V converter.
- Both Horner physical analog jumper positions **AND** Cscape Analog In configuration must be **0–10 V VOLTAGE**, not current mode. The voltage-input impedance is high; current-mode input is NOT suitable for GPIO direct drive. Verify the specific board revision and labeling before connection.
- If the power supplies have incompatible common grounding, unexpected voltages, or are non-isolated, STOP and investigate. Normal isolated low-voltage supplies can share 0V.
- Do not assume A1/A2 0V terminal is the same as 24V +, and do not attach GND to a 24V+ input.
- Never attach relay coils or lamp wires to the ESP32; the Horner controls 12V stack light on its isolated dry-contact outputs (R1 and R2), as already tested with PLC.
- PLC software must be V2 Freezy source: analog A1/A2, R1/R2, 3-second watchdog.
- No extra level shifter or relay board is necessary for this *voltage-input* connection.

## First test

1. Leave 12V lamp disconnected, but PLC powered and running the V2 logic.
2. Disconnect A1/A2 initially and confirm ESP32 GPIO26 and GPIO27 are approximately 0V relative to ESP32 GND on boot.
3. Configure ESP32 Wi-Fi + Arena host using local web interface.
4. With ESP32 online, GPIO26 should alternate between approximately 0V and 3.3V **once per second**; GPIO27 should read 0V when idle and 3.3V during a match.
5. Power both devices off, wire shared 0V, A1 and A2, then power on. On PLC diagnostics: A1 oscillates near zero/approximately 10.5k counts; A2 reflects 0 or approx 10.5k counts (assuming Cscape scaled 0..32000 for 0..10V).
6. Wait two heartbeat transitions for PLC to qualify the link. With an idle FMS expect R1, with a match expect R2. Disconnect FMS, expect both off after ESP32 detects the loss plus PLC watchdog timeout (about 3s after last qualifying edge).
7. Use ESP32 web manual tests only when no real Arena match is running; they auto-expire after 15s. Separately, PLC's *own* HMI manual tests remain independent.