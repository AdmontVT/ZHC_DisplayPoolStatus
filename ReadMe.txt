================================================================================
ZHC Fireplace Room Display — Particle Photon 2
================================================================================

PROJECT OVERVIEW
----------------
This project drives a Waveshare 2-inch ST7789V SPI LCD display from a Particle
Photon 2. It displays pool temperature, inside room temperature, outside
temperature, light level, heater state, and related data received from other
Particle devices via cloud events.

This project was ported from an original Particle Photon 1 (STM32) codebase.
The display-only phase was completed first, with temperature sensing (DS18B20)
to be re-integrated in a future phase.

--------------------------------------------------------------------------------
HARDWARE
--------------------------------------------------------------------------------

Microcontroller:  Particle Photon 2 (RTL872x architecture, Device OS 6.4.0+)
Display:          Waveshare 2inch LCD Module, ST7789V controller, 240x320 SPI

--------------------------------------------------------------------------------
WIRING — Photon 2 to Waveshare 2inch LCD
--------------------------------------------------------------------------------

  Display Pin  |  Photon 2 Pin  |  Notes
  -------------|----------------|-----------------------------------------------
  VCC          |  3V3           |  3.3V only — Photon 2 is NOT 5V tolerant
  GND          |  GND           |
  DIN          |  S0            |  Hardware SPI MOSI (primary SPI bus)
  CLK          |  S2            |  Hardware SPI SCK  (primary SPI bus)
  CS           |  D2            |  Chip select (GPIO)
  DC           |  D5            |  Data/Command select (GPIO)
  RST          |  D6            |  Reset (GPIO)
  BL           |  D7            |  Backlight enable (GPIO)

IMPORTANT NOTES ON WIRING:
  - DIN must connect to S0 (labeled S0 on the Photon 2 board silkscreen).
    S0 is the primary hardware SPI MOSI pin. Do NOT use D4.
  - CLK must connect to S2 (labeled S2 on the Photon 2 board silkscreen).
    S2 is the primary hardware SPI SCK pin. Do NOT use D3.
  - D3 and D4 are SPI1 (secondary SPI bus) pins, not primary SPI.
  - No MISO wire is needed — the display is write-only.
  - VCC must be 3.3V. The Photon 2 logic is 3.3V only.

--------------------------------------------------------------------------------
PIN DEFINITIONS IN CODE
--------------------------------------------------------------------------------

  #define TFT_CS   D2
  #define TFT_DC   D5
  #define TFT_RST  D6
  #define TFT_BL   D7

  Hardware SPI pins (S0/S2) are used automatically by the SPI object
  and do not need to be defined in code.

--------------------------------------------------------------------------------
SOFTWARE LIBRARIES
--------------------------------------------------------------------------------

  - Arduino_ST7789.cpp / .h   — Modified ST7789 display driver (see notes below)
  - Adafruit_mfGFX.cpp / .h   — Multi-font GFX library (Particle port)
  - fonts.h / fonts.cpp       — Custom font definitions (COMICS_8, GLCDFONT, etc.)
  - ArduinoJson               — JSON parsing for cloud event data
  - OneWire                   — DS18B20 temperature sensor (stubbed, future use)

Particle libraries (add via Web IDE or CLI):
  particle library add ArduinoJson
  particle library add OneWire

--------------------------------------------------------------------------------
CRITICAL FIXES TO Arduino_ST7789.cpp FOR PHOTON 2
--------------------------------------------------------------------------------

The original Arduino_ST7789 library was written for the Photon 1 (STM32). 
Several changes were required to make it work on the Photon 2 (RTL872x):

1. REPLACE pinSetFast/pinResetFast WITH digitalWriteFast
   The original library used pinSetFast() and pinResetFast() for fast GPIO
   toggling of the DC and CS lines. These are STM32-specific bare-metal
   functions that do not work on the Photon 2's Realtek architecture.
   Replace all four pin control functions as follows:

   BEFORE (broken on Photon 2):
     inline void Arduino_ST7789::CS_HIGH(void) { pinSetFast(_cs); }
     inline void Arduino_ST7789::CS_LOW(void)  { pinResetFast(_cs); }
     inline void Arduino_ST7789::DC_HIGH(void) { pinSetFast(_dc); }
     inline void Arduino_ST7789::DC_LOW(void)  { pinResetFast(_dc); }

   AFTER (correct for Photon 2):
     inline void Arduino_ST7789::CS_HIGH(void) { if (_cs >= 0) digitalWriteFast(_cs, HIGH); }
     inline void Arduino_ST7789::CS_LOW(void)  { if (_cs >= 0) digitalWriteFast(_cs, LOW); }
     inline void Arduino_ST7789::DC_HIGH(void) { digitalWriteFast(_dc, HIGH); }
     inline void Arduino_ST7789::DC_LOW(void)  { digitalWriteFast(_dc, LOW); }

2. WAVESHARE ST7789V INITIALIZATION SEQUENCE
   The generic ST7789 init sequence in the original library is insufficient
   for the Waveshare ST7789V controller. A full Waveshare-specific init
   sequence is required, including PORCTRL, GCTRL, VCOMS, VDVVRHEN, VRHS,
   VDVS, FRCTRL2, and PWCTRL1 commands. See Arduino_ST7789.cpp for the
   complete generic_st7789[] array.

3. SPI SPEED
   Reduced from 30MHz to 4MHz for Photon 2 compatibility:
   mySPISettings = SPISettings(4000000, MSBFIRST, SPI_MODE0);

4. FILLRECT BUG FIX
   Original code had a logical AND bug:
     uint8_t lo = color && 0xff;   // WRONG — logical AND
   Fixed to:
     uint8_t lo = color & 0xFF;    // CORRECT — bitwise AND

5. CS PIN GUARD
   Changed (if _cs) to (if _cs >= 0) to correctly handle pin number 0.

--------------------------------------------------------------------------------
CLOUD EVENTS
--------------------------------------------------------------------------------

This device subscribes to two Particle cloud events:

  hook-response/weather_openmeteo
    Receives current outdoor temperature in Fahrenheit as a plain float string.
    Published by a Particle webhook configured against the Open-Meteo API.

  poolData
    Receives a JSON payload from the pool controller device containing:
      {
        "poolTemp": 82.5,
        "systemTemp": 78.0,
        "startupTemp": 65.0,
        "heaterControlState": 1,
        "dailyHeaterOnMinutes": 45,
        "lastTempF": 68.2,
        "logLight": 1200.0
      }

    heaterControlState values:
      0 = HEATER_UNKNOWN
      1 = HEATER_AUTO
      2 = HEATER_ON
      3 = HEATER_OFF

--------------------------------------------------------------------------------
FUTURE WORK
--------------------------------------------------------------------------------

  - Re-integrate DS18B20 temperature sensor (OneWire on D10)
  - Re-integrate light sensor (analog read on A0)
  - Re-integrate LUX-based heater control logic
  - Re-integrate EEPROM settings persistence
  - Re-integrate Particle cloud functions for remote configuration

--------------------------------------------------------------------------------
PORTING NOTES — PHOTON 1 TO PHOTON 2
--------------------------------------------------------------------------------

  Function          Photon 1 Pin    Photon 2 Pin
  ----------------  --------------  --------------
  SPI MOSI (DIN)    A5              S0
  SPI SCK  (CLK)    A3              S2
  TFT CS            A2              D2
  TFT DC            D2              D5
  TFT RST           D3              D6
  TFT BL            D0              D7
  DS18B20 data      D4              D10 (future)
  Light sensor      A0              A0  (future)

  The Photon 2 does not have A2, A3, or A5 as SPI pins.
  Primary SPI lives on the S0/S2 pins instead.
  The Photon 2 is NOT 5V tolerant on any GPIO.
  pinSetFast/pinResetFast are STM32-only and must be replaced with
  digitalWriteFast on the Photon 2.

================================================================================
