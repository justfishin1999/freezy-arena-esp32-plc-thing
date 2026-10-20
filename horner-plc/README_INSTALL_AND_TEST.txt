FREEZY ARENA - HORNER HE-XT102CH TOUCHSCREEN + SAFE RELAY TESTS (V2)
======================================================================
TARGET: Horner XT102, XL OCS Series, XLT, Model HEXT102, XL I/O Type 2
USER-VERIFIED CONTROLLER FIRMWARE: 12.86, CPLD 0.9.
SCREEN: XLT monochrome 160 (width) x 128 (height).

WHAT'S INCLUDED
===============
01_MainLoop_ST_V2.txt                   Replaces existing STBlock1 program
02_GlobalVariables_IEC_V2.txt         Replaces existing GLOBAL IEC vars
03_SCREEN_LAYOUTS_AND_REGISTERS.txt   Pixel-perfect placement, object wiring
screen_backgrounds/*.bmp               Four exact-size optional background art
screen_previews/*.png                  4x enlarged illustration of each screen
README_INSTALL_AND_TEST.txt           This file

IMPORTANT LIMITATION
====================
This ZIP is NOT a compiled Cscape .CSP or .PGM program. Its BMPs are STATIC
background assets. Cscape must construct real graphics screens, text tables,
screen jumps, and pushbutton controls, then compile and export your firmware-
compatible .PGM. Do not copy the BMPs onto the PLC's microSD card expecting
working touchscreen navigation; that is not how Cscape graphic objects work.

FIRST: MAKE CONTROLLER SHOW SOMETHING NOW
=========================================
If the PLC is complaining because 'First Screen is Empty' or 'No Graphics',
open your original saved project in Cscape and create a real Screen 1 with
static text 'FREEZY ARENA', run Error Check and export targeting firmware
12.86. You do not need the changed PLC logic just to clear a blank screen.

INSTALL THE COMPLETE V2
=======================
1. Save an untouched copy of your existing WORKING Cscape .CSP project and
   the SD card .PGM before making changes.
2. In Cscape, Project Navigator > Control > Logic Modules > Main Loop Modules
   > STBlock1: REPLACE its entire content with 01_MainLoop_ST_V2.txt.
   This is not append-only; it includes the original automatic FMS logic.
3. Tools > Program Variables > GLOBAL > Edit Variables as Text > IEC Format:
   REPLACE the whole VAR ... END_VAR group with the block from
   02_GlobalVariables_IEC_V2.txt. Verify mappings in variables grid:
     %AI1 HeartbeatRaw, %AI2 RunningRaw;
     %Q1 RedRelay, %Q2 GreenRelay;
     %T101 TestRedPressed; %T102 TestGreenPressed;
     %R200 FmsStatusCode; %R201 MatchStatusCode;
     %R202 OutputStatusCode; %R203 TestStateCode; %R204 EdgeCountDisplay.
   %T101/%T102 are NON-RETENTIVE PLC memory bits driven by HMI momentary buttons.
4. Hardware configuration must remain HEXT102 / XL I/O Type 2;
   analog channels 1 and 2 0..10V and physical JP2 jumpers in voltage mode.
5. Under Project Navigator > Graphics > Main (or Screens > View/Edit Screens
   in classic Cscape), open the XLT Graphics Editor. Create 4 screens with
   numbers 1=Menu 2=Status 3=Test 4=Diagnostics. If Screen 1 already exists,
   edit it. Ensure FIRST/STARTUP SCREEN points to screen 1.
6. OPTIONAL: Put each 160x128 monochrome BMP background on its screen via
   Fixed Objects > Picture/Graphic. Set to full size (0,0,160x128). Keep the
   bitmap in the back (Send to Back). Static pictures by themselves do NOT
   provide touch controls or live register data. If bitmap import fails on
   the old firmware, use Static Text objects and Rectangles instead.
7. Add the HMI objects according to 03_SCREEN_LAYOUTS_AND_REGISTERS.txt:
   - Text Table objects bound read-only to %R200..%R203.
   - Numeric Data objects bound read-only to %AI1, %AI2, %R204.
   - Screen Jump objects to navigate to screens 1..4.
   - TWO Button/Switch objects on Screen 3; Momentary, source Touch,
     write to %T101 and %T102. DO NOT use Toggle or ON actions.
   To make picture-drawn buttons clickable, set Screen Jump objects to
   'invisible but touchable' if your firmware honors this feature,
   placing them IN FRONT of the bitmap; or use visible labeled buttons.
8. Run Cscape Error Check for LOGIC and SCREENS. It should no longer warn
   'Screen set as first screen is empty'. A blank FIRST screen can cause
   complaints even if later screens exist.
9. File > Export to Removable Media / Export PGM and explicitly select
   firmware export version 12.86, as you did for the successful install.
10. With lamp wiring disconnected, copy PGM to the microSD, load it on
    the Horner using the Removable Media menu, put into RUN, check HMI.
11. Verify buttons hold only while pressed and turn off after 5 sec;
    restore existing outputs ONLY once safe tests succeed.

OPERATING / FAILSAFE LOGIC
==========================
- P26 / analog A1 toggling ~every 1s = ESP32 FMS connection heartbeat.
- P27 / analog A2 high = FMS running (match states 1,2,3,4).
- Two heartbeat transitions required before LIVE; timeout after 3s.
- Running high blocks local manual testing regardless of heartbeat health.
- Manual TEST RED/TEST GREEN work offline too, only while pressed,
  but will stop automatically at 5 seconds and require button release.
- BOTH TEST BUTTONS pressed => both relays OFF (when not match running).
- Test timeout cannot keep relays on and does not re-arm until released.
- With no heartbeat, no test button pressed: both relays OFF.
- Both output coils cannot be ON simultaneously through this logic.
- No 24V must reach ESP32 pins; shared 0V must be verified safely.
- Relay common voltage should match the LIGHT's own voltage, NOT PLC supply
  unless the LIGHT supports that same voltage.
- XLT relay lamp indication is NOT a safety-rated interlock or E-stop.

BEFORE YOU TEST
===============
- Confirm analog 0-10V channels are physically set to VOLTAGE mode.
- Confirm ESP32 code has been updated to heartbeat-on-P26 and run-on-P27.
  Previous ESP32 firmware driving red/green relays will NOT behave correctly.
- All graphics are monochrome, appropriate for older XLT units.
- Use read-only Text Table/Numeric Data fields so the screen cannot
  accidentally edit output-status or analog-input registers.
- Test with the 12V tower light unplugged and relay contacts unloaded.
- Validate PLC COM/NO contact operations with a multimeter before connecting
  the light. No two outputs energized together.

REALISTIC EXPECTATIONS
======================
This source is constructed to match the existing Cscape IEC TON100mS syntax
that your original application compiled successfully with, but V2 itself
has not been compiled against your installed Cscape 12.x compiler or tested
on a physical HE-XT102CH. If any error occurs, capture exact error line.
The screen BMP artwork has correct dimensions but may require native Cscape
font/graphics conversion on old firmware.

OFFICIAL CSCAPE GRAPHICS DOCUMENTATION
======================================
https://cscapehelp.hornerautomation.com/Content/User%20Interface/UI-ScreenDev_LAND.htm
https://cscapehelp.hornerautomation.com/Content/User%20Interface/UI-GO-Butt_Switch.htm
https://cscapehelp.hornerautomation.com/Content/User%20Interface/UI-GO-ScreenJump.htm
https://cscapehelp.hornerautomation.com/Content/User%20Interface/UI-GO-TextTable.htm