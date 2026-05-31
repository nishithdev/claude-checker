// ============================================================
//  TFT_eSPI User_Setup.h for ESP32 Cheap Yellow Display
//  Board: ESP32-2432S028R (CYD)  |  Display: ILI9341 2.8"
//
//  INSTALL: Copy this file to:
//    Arduino/libraries/TFT_eSPI/User_Setup.h
//    (overwrite the existing one)
// ============================================================

// ── Driver ───────────────────────────────────────────────────
#define ILI9341_DRIVER

// ── Resolution ───────────────────────────────────────────────
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ── SPI Pins (CYD display uses HSPI / SPI2) ──────────────────
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15   // Chip select
#define TFT_DC    2   // Data/Command
#define TFT_RST  -1   // RST tied to EN pin; -1 = use ESP reset

// ── Backlight ────────────────────────────────────────────────
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// ── Touch (XPT2046 on separate SPI bus — defined for reference)
#define TOUCH_CS 33

// ── Fonts to load ─────────────────────────────────────────────
#define LOAD_GLCD    // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2   // Font 2. Small 16 pixel high font
#define LOAD_FONT4   // Font 4. Medium 26 pixel high font
#define LOAD_FONT6   // Font 6. Large 48 pixel high font (numbers only)
#define LOAD_FONT7   // Font 7. 7-segment 48 pixel high font (numbers only)
#define LOAD_FONT8   // Font 8. Large 75 pixel high font (numbers only)
#define LOAD_GFXFF   // FreeFonts — many small/medium fonts available
#define SMOOTH_FONT

// ── SPI Frequency ─────────────────────────────────────────────
#define SPI_FREQUENCY        55000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000
