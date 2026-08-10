// Seeed reTerminal Sticky pin map.
// Sources: V01 schematic (2026-06-05), vendor peripheral demo pin_config.h,
// and the FreeInk SDK BoardConfig (CrossPoint Reader), which triple-sourced them.
#pragma once

// --- Power latch -------------------------------------------------------------
// Must be driven HIGH first thing in setup() or the board powers off the
// moment USB is unplugged (battery operation).
constexpr int PIN_PWR_HOLD = 45;
constexpr int PIN_PWR_LOCK = 46;

// --- E-paper (SSD1677, 800x480, SPI) ----------------------------------------
constexpr int PIN_EPD_SCK  = 13;
constexpr int PIN_EPD_MOSI = 14;
constexpr int PIN_EPD_MISO = 12;  // shared bus (SD card); unused by the panel
constexpr int PIN_EPD_CS   = 15;
constexpr int PIN_EPD_DC   = 16;
constexpr int PIN_EPD_RST  = 17;
constexpr int PIN_EPD_BUSY = 18;  // active HIGH while refreshing
constexpr int PIN_EPD_PWR_EN = 47;  // panel rail, active HIGH

// --- Touch (GT911, own I2C bus) ---------------------------------------------
constexpr int PIN_TP_SDA = 3;
constexpr int PIN_TP_SCL = 2;
constexpr int PIN_TP_INT = 21;
constexpr int PIN_TP_RST = 41;
constexpr int PIN_TP_EN  = 42;  // touch rail, active HIGH
constexpr uint8_t GT911_ADDR      = 0x5D;
constexpr uint8_t GT911_ADDR_ALT  = 0x14;

// --- Buttons (active low, 10K pull-ups) -------------------------------------
constexpr int PIN_BTN_UP   = 5;
constexpr int PIN_BTN_DOWN = 6;
constexpr int PIN_BTN_OK   = 4;   // shared with power button

// --- microSD (shares the panel's SPI bus) -------------------------------------
// Only the chip select is its own; clock, MOSI and MISO are the display's.
// Sharing means every transaction has to leave CS high for the other device,
// and it means a card that misbehaves can take the panel down with it -- which
// is why nothing has used this yet and why the service screen tests it before
// any app is allowed to depend on it.
constexpr int PIN_SD_CS = 8;
// The slot's power is gated by a load switch: HIGH powers the card, LOW cuts
// it (V01 schematic via the FreeInk SDK BoardConfig, SD_PWR_EN, active high).
// This is why the first probe found no card: everything on the bus was right
// and the card had no volts. Held LOW except while the card is actually in
// use, which also means an inserted card cannot lean on the shared bus.
constexpr int PIN_SD_PWR = 10;

// --- Buzzer ------------------------------------------------------------------
constexpr int PIN_BUZZER = 48;    // LEDC-driven

// --- Sensor I2C bus (Wire1) ---------------------------------------------------
// BQ27220 fuel gauge (0x55), PCF8563 RTC (0x51), SHT40 (0x44), LSM6DS3TR-C
// (0x6A) all share this bus. GPIO0 is a strapping pin; the bus starts after
// boot, which is safe.
constexpr int PIN_SENS_SDA = 1;
constexpr int PIN_SENS_SCL = 0;
constexpr int PIN_CHARGE_STATE = 40;  // from the BQ25616 charger

// --- Display geometry --------------------------------------------------------
// The panel itself is always 800x480 landscape; PANEL_* describe the hardware.
constexpr int PANEL_W = 800;
constexpr int PANEL_H = 480;
constexpr int EPD_WB = PANEL_W / 8;                            // bytes per row
constexpr uint32_t EPD_BUF_SIZE = (uint32_t)EPD_WB * PANEL_H;  // 48000

// TOYBOX_PORTRAIT turns the whole UI a quarter turn: the device is meant to hang
// on a fridge like a sheet of paper, so every screen is laid out for 480x800 and
// the driver maps those coordinates onto the landscape panel. Undefine it to get
// the original landscape build back.
#define TOYBOX_PORTRAIT 1

#ifdef TOYBOX_PORTRAIT
constexpr int EPD_W = PANEL_H;  // 480 logical width
constexpr int EPD_H = PANEL_W;  // 800 logical height
#else
constexpr int EPD_W = PANEL_W;
constexpr int EPD_H = PANEL_H;
#endif
