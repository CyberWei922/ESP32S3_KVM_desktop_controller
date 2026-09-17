# MD024 vendor-reference display test

This is an isolated A/B diagnostic firmware for the existing ESP32-S3 wiring.
It intentionally contains no LVGL, Wi-Fi, networking, relays, buttons, or DMA.

The display transport follows the vendor ESP32 sample's GPIO software-SPI method,
while the controller sequence and geometry are corrected for the actual
MD024-QVGA-01-V01 / ILI9341V 320x240 landscape module.

## What you should see

1. Eight full-height color bands are drawn and then remain static.
2. GPIO15 outputs 20 kHz PWM and changes once per second between:
   - 30% duty (about 1.0 V average equivalent)
   - 61% duty (about 2.0 V average equivalent)
   - 100% duty (3.3 V)

Interpretation:

- Brightness changes in three clear steps: BLK behaves like a PWM-capable active-high input.
- Only 3.3 V is on and the lower levels are off/unstable: BLK behaves like a logic enable.
- Little or no difference: BLK is not providing direct analog brightness control.

This is PWM, not a true analog 1 V or 2 V output: the pin rapidly switches between
0 V and 3.3 V. Because the LCD specification and observed module behavior conflict,
place a 1 kOhm series resistor between GPIO15 and BLK before flashing this build.
