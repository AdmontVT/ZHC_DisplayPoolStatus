/***************************************************
  Library for the ST7789 IPS SPI display.
  Written by Ananev Ilya.
  Modified for 240x320 screen only.
  Fixed for Particle Photon 2:
    - pinSetFast/pinResetFast replaced with digitalWriteFast
      (pinSetFast/pinResetFast are STM32-specific and do not
       work on the Photon 2's Realtek architecture)
    - SPI speed reduced from 30MHz to 4MHz
    - fillRect bitwise AND bug fixed (&& -> &)
    - CS guard changed from (if _cs) to (if _cs >= 0)
    - Reset delay extended for reliable init
    - Waveshare ST7789V specific init sequence
****************************************************/

#include "Arduino_ST7789.h"
#include <limits.h>
#include <SPI.h>

// Waveshare ST7789V specific initialization sequence
static const uint8_t PROGMEM generic_st7789[] = {
    18,                                              // 18 commands
    ST7789_SWRESET, ST_CMD_DELAY,   150,             // 1: Software reset, 150ms
    ST7789_SLPOUT,  ST_CMD_DELAY,    10,             // 2: Out of sleep, 10ms
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,          // 3: PORCTRL porch setting
    0xB7, 1, 0x35,                                   // 4: GCTRL gate control
    0xBB, 1, 0x19,                                   // 5: VCOMS VCOM setting
    0xC0, 1, 0x2C,                                   // 6: LCMCTRL
    0xC2, 1, 0x01,                                   // 7: VDVVRHEN enable
    0xC3, 1, 0x12,                                   // 8: VRHS VRH set
    0xC4, 1, 0x20,                                   // 9: VDVS VDV set
    0xC6, 1, 0x0F,                                   // 10: FRCTRL2 frame rate 60Hz
    0xD0, 2, 0xA4, 0xA1,                             // 11: PWCTRL1 power control
    ST7789_COLMOD, 1+ST_CMD_DELAY, 0x55, 10,         // 12: Color mode RGB565, 10ms
    ST7789_MADCTL, 1, 0x00,                          // 13: Memory access control
    ST7789_CASET,  4, 0x00, 0x00, 0x00, 0xEF,       // 14: Column address 0-239
    ST7789_RASET,  4, 0x00, 0x00, 0x01, 0x3F,       // 15: Row address 0-319
    ST7789_INVON,  ST_CMD_DELAY,    10,              // 16: Inversion on, 10ms
    ST7789_NORON,  ST_CMD_DELAY,    10,              // 17: Normal display on, 10ms
    ST7789_DISPON, ST_CMD_DELAY,   100,              // 18: Display on, 100ms
};

inline uint16_t swapcolor(uint16_t x) {
    return (x << 11) | (x & 0x07E0) | (x >> 11);
}

#if defined(SPI_HAS_TRANSACTION)
static SPISettings mySPISettings;
#elif defined(__AVR__) || defined(CORE_TEENSY)
static uint8_t SPCRbackup;
static uint8_t mySPCR;
#endif

#if defined(SPI_HAS_TRANSACTION)
#define SPI_BEGIN_TRANSACTION() if (_hwSPI) SPI.beginTransaction(mySPISettings)
#define SPI_END_TRANSACTION()   if (_hwSPI) SPI.endTransaction()
#else
#define SPI_BEGIN_TRANSACTION()
#define SPI_END_TRANSACTION()
#endif

// Constructor for software SPI
Arduino_ST7789::Arduino_ST7789(int8_t dc, int8_t rst, int8_t sid, int8_t sclk, int8_t cs)
    : Adafruit_GFX(240, 320) {
    _cs   = cs;
    _dc   = dc;
    _sid  = sid;
    _sclk = sclk;
    _rst  = rst;
    _hwSPI = false;
}

// Constructor for hardware SPI
Arduino_ST7789::Arduino_ST7789(int8_t dc, int8_t rst, int8_t cs)
    : Adafruit_GFX(240, 320) {
    _cs    = cs;
    _dc    = dc;
    _rst   = rst;
    _hwSPI = true;
    _sid   = _sclk = -1;
}

inline void Arduino_ST7789::spiwrite(uint8_t c) {
    SPI.transfer(c);
}

void Arduino_ST7789::writecommand(uint8_t c) {
    SPI_BEGIN_TRANSACTION();
    DC_LOW();
    CS_LOW();
    spiwrite(c);
    CS_HIGH();
    SPI_END_TRANSACTION();
}

void Arduino_ST7789::writedata(uint8_t c) {
    SPI_BEGIN_TRANSACTION();
    DC_HIGH();
    CS_LOW();
    spiwrite(c);
    CS_HIGH();
    SPI_END_TRANSACTION();
}

void Arduino_ST7789::displayInit(const uint8_t *addr) {
    uint8_t numCommands, numArgs;
    uint16_t ms;

    numCommands = pgm_read_byte(addr++);
    while (numCommands--) {
        writecommand(pgm_read_byte(addr++));
        numArgs = pgm_read_byte(addr++);
        ms = numArgs & ST_CMD_DELAY;
        numArgs &= ~ST_CMD_DELAY;
        while (numArgs--) {
            writedata(pgm_read_byte(addr++));
        }
        if (ms) {
            ms = pgm_read_byte(addr++);
            if (ms == 255) ms = 500;
            delay(ms);
        }
    }
}

void Arduino_ST7789::commonInit(const uint8_t *cmdList) {
    _ystart = _xstart = 0;
    _colstart = 0;
    _rowstart = 0;

    pinMode(_dc, OUTPUT);
    if (_cs >= 0) {
        pinMode(_cs, OUTPUT);
        digitalWriteFast(_cs, HIGH); // deselect
    }

    if (_hwSPI) {
        SPI.begin();
#if defined(SPI_HAS_TRANSACTION)
        mySPISettings = SPISettings(4000000, MSBFIRST, SPI_MODE0);
#elif defined(__AVR__) || defined(CORE_TEENSY)
        SPCRbackup = SPCR;
        SPI.begin();
        SPI.setClockDivider(SPI_CLOCK_DIV4);
        SPI.setDataMode(SPI_MODE0);
        mySPCR = SPCR;
        SPCR = SPCRbackup;
#elif defined(__SAM3X8E__)
        SPI.begin();
        SPI.setClockDivider(21);
        SPI.setDataMode(SPI_MODE0);
#endif
    } else {
        pinMode(_sclk, OUTPUT);
        pinMode(_sid,  OUTPUT);
        digitalWriteFast(_sclk, LOW);
        digitalWriteFast(_sid,  LOW);
    }

    // Hardware reset
    if (_rst >= 0) {
        pinMode(_rst, OUTPUT);
        digitalWriteFast(_rst, HIGH);
        delay(50);
        digitalWriteFast(_rst, LOW);
        delay(50);
        digitalWriteFast(_rst, HIGH);
        delay(150);
    }

    if (cmdList) {
        displayInit(cmdList);
    }
}

void Arduino_ST7789::setRotation(uint8_t m) {
    uint8_t madctl = 0;
    rotation = m & 3;
    switch (rotation) {
        case 0:
            madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_RGB;
            _xstart = _colstart;
            _ystart = _rowstart;
            _width  = 240;
            _height = 320;
            break;
        case 1:
            madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB;
            _xstart = _rowstart;
            _ystart = _colstart;
            _width  = 320;
            _height = 240;
            break;
        case 2:
            madctl = ST7789_MADCTL_RGB;
            _xstart = _colstart;
            _ystart = _rowstart;
            _width  = 240;
            _height = 320;
            break;
        case 3:
            madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV | ST7789_MADCTL_RGB;
            _xstart = _rowstart;
            _ystart = _colstart;
            _width  = 320;
            _height = 240;
            break;
    }
    writecommand(ST7789_MADCTL);
    writedata(madctl);
}

void Arduino_ST7789::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint16_t x_start = x0 + _xstart, x_end = x1 + _xstart;
    uint16_t y_start = y0 + _ystart, y_end = y1 + _ystart;

    writecommand(ST7789_CASET);
    writedata(x_start >> 8);
    writedata(x_start & 0xFF);
    writedata(x_end >> 8);
    writedata(x_end & 0xFF);

    writecommand(ST7789_RASET);
    writedata(y_start >> 8);
    writedata(y_start & 0xFF);
    writedata(y_end >> 8);
    writedata(y_end & 0xFF);

    writecommand(ST7789_RAMWR);
}

void Arduino_ST7789::pushColor(uint16_t color) {
    SPI_BEGIN_TRANSACTION();
    DC_HIGH();
    CS_LOW();
    spiwrite(color >> 8);
    spiwrite(color);
    CS_HIGH();
    SPI_END_TRANSACTION();
}

void Arduino_ST7789::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height)) return;
    setAddrWindow(x, y, x + 1, y + 1);
    SPI_BEGIN_TRANSACTION();
    DC_HIGH();
    CS_LOW();
    spiwrite(color >> 8);
    spiwrite(color);
    CS_HIGH();
    SPI_END_TRANSACTION();
}

void Arduino_ST7789::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if ((x >= _width) || (y >= _height)) return;
    if ((y + h - 1) >= _height) h = _height - y;
    setAddrWindow(x, y, x, y + h - 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    SPI_BEGIN_TRANSACTION();
    DC_HIGH();
    CS_LOW();
    while (h--) {
        spiwrite(hi);
        spiwrite(lo);
    }
    CS_HIGH();
    SPI_END_TRANSACTION();
}

void Arduino_ST7789::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if ((x >= _width) || (y >= _height)) return;
    if ((x + w - 1) >= _width) w = _width - x;
    setAddrWindow(x, y, x + w - 1, y);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    SPI_BEGIN_TRANSACTION();
    DC_HIGH();
    CS_LOW();
    while (w--) {
        spiwrite(hi);
        spiwrite(lo);
    }
    CS_HIGH();
    SPI_END_TRANSACTION();
}

void Arduino_ST7789::fillScreen(uint16_t color) {
    fillRect(0, 0, _width, _height, color);
}

void Arduino_ST7789::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if ((x >= _width) || (y >= _height)) return;
    if ((x + w - 1) >= _width)  w = _width  - x;
    if ((y + h - 1) >= _height) h = _height - y;
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    SPI_BEGIN_TRANSACTION();
    DC_HIGH();
    CS_LOW();
    for (y = h; y > 0; y--) {
        for (x = w; x > 0; x--) {
            spiwrite(hi);
            spiwrite(lo);
        }
    }
    CS_HIGH();
    SPI_END_TRANSACTION();
}

uint16_t Arduino_ST7789::Color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Arduino_ST7789::invertDisplay(boolean i) {
    writecommand(i ? ST7789_INVON : ST7789_INVOFF);
}

// FIX: replaced pinSetFast/pinResetFast with digitalWriteFast
// pinSetFast/pinResetFast are STM32-specific and fail silently
// on the Photon 2's Realtek architecture
inline void Arduino_ST7789::CS_HIGH(void) {
    if (_cs >= 0) digitalWriteFast(_cs, HIGH);
}

inline void Arduino_ST7789::CS_LOW(void) {
    if (_cs >= 0) digitalWriteFast(_cs, LOW);
}

inline void Arduino_ST7789::DC_HIGH(void) {
    digitalWriteFast(_dc, HIGH);
}

inline void Arduino_ST7789::DC_LOW(void) {
    digitalWriteFast(_dc, LOW);
}

void Arduino_ST7789::init(uint16_t width, uint16_t height) {
    commonInit(NULL);
    _colstart = 0;
    _rowstart = 0;
    _width  = width;
    _height = height;
    displayInit(generic_st7789);
    setRotation(0);
}

