/*
 * OUI SPY - Board abstraction
 *
 * Every mode's raw/*.cpp hardcodes BUZZER_PIN/LED_PIN for the Seeed XIAO
 * ESP32-S3. To run the same firmware on an ESP32 "Cheap Yellow Display"
 * (CYD) board, each mode's two #define lines are swapped for the board-aware
 * values here (OUISPY_BUZZER_PIN / OUISPY_LED_PIN) instead of touching every
 * digitalWrite/tone/ledc call site.
 *
 * Board is selected at build time via a -D flag in platformio.ini:
 *   -DOUISPY_BOARD_CYD_2432S028        (2.8" resistive CYD, single micro-USB,
 *                                       ILI9341 + XPT2046)
 *   -DOUISPY_BOARD_CYD_2432S028_2USB   (2.8" resistive CYD, micro-USB + USB-C,
 *                                       ST7789 + XPT2046, drawn via TFT_eSPI)
 * No flag defined => Seeed XIAO ESP32-S3 (the original/default target).
 *
 * CYD boards have no buzzer and no single-color status LED broken out the
 * same way the XIAO does; OUISPY_BUZZER_PIN/OUISPY_LED_PIN are pointed at
 * safe, otherwise-unused GPIOs on that board so the existing pinMode/
 * digitalWrite/ledc/tone call sites keep working unmodified (driving an
 * unconnected pin is harmless) rather than requiring every call site to be
 * guarded. The CYD's onboard RGB LED (active-low, GPIO4/16/17) and the
 * display itself are handled separately by display.cpp, not through these
 * macros.
 */
#ifndef OUISPY_BOARDS_H
#define OUISPY_BOARDS_H

#if defined(OUISPY_BOARD_CYD_2432S028) || defined(OUISPY_BOARD_CYD_2432S028_2USB) || \
    defined(OUISPY_BOARD_CYD_2432S024) || defined(OUISPY_BOARD_CYD_3248S035)

#define OUISPY_HAS_TFT      1
#define OUISPY_HAS_BUZZER   0
#define OUISPY_HAS_RGB_LED  1

// GPIO26 drives the CYD's onboard piezo speaker (via amp transistor) on
// most board revisions, so tone()/ledc beeps are actually audible there
// instead of being a no-op. GPIO4 is the red channel of the onboard RGB
// LED (active-low, same polarity the existing "inverted logic" LED code
// already assumes), so the single-color LED_PIN blink is real too.
// GPIO22 is unused by the panel/touch/SD/RGB LED and is where the
// Adafruit_NeoPixel single-pixel effects (detector mode only) land; on
// this board nothing is actually wired there, so that specific effect is
// just a harmless no-op rather than a real light.
#define OUISPY_BUZZER_PIN    26
#define OUISPY_LED_PIN        4
#define OUISPY_NEOPIXEL_PIN  22

// CYD onboard RGB LED (common CYD boards: active-low on these three pins).
#define OUISPY_RGB_LED_R_PIN 4
#define OUISPY_RGB_LED_G_PIN 16
#define OUISPY_RGB_LED_B_PIN 17

// Unlike a bare WROOM-32 devkit, CYD boards do NOT reliably expose GPIO0 as
// a debounced user button in normal operation (confirmed on hardware: with
// OUISPY_BOOT_BUTTON_PIN=0, every mode immediately self-triggered the
// hold-to-return-to-menu logic as if the button were stuck held). -1 means
// "no physical button here" - main.cpp skips all BOOT_BUTTON_PIN handling
// when this is negative, leaving the touchscreen as the only way back to
// the menu on this board.
#define OUISPY_BOOT_BUTTON_PIN -1

// A classic ESP32 (CYD) has ~320KB of internal DRAM total, shared with the
// WiFi/BT stacks and every other mode's statics (all 6 modes link into one
// binary even though only one runs at a time) - nowhere near the XIAO
// ESP32-S3's 512KB SRAM + PSRAM. detector's and flock-you's persisted
// session tables are the two largest static arrays in the whole firmware,
// so they're capped smaller here; this only limits how many *distinct*
// devices/detections are remembered at once, not detection accuracy.
#define OUISPY_MAX_BLE_DETECTIONS 64
#define OUISPY_MAX_FY_DETECTIONS  64

#if defined(OUISPY_BOARD_CYD_3248S035)
  // 3.5" ILI9488 480x320 variant.
  #define OUISPY_TFT_WIDTH   480
  #define OUISPY_TFT_HEIGHT  320
  #define OUISPY_TFT_PANEL_ILI9488 1
#elif defined(OUISPY_BOARD_CYD_2432S028_2USB)
  // 2.8" dual-USB (micro-USB + USB-C) "CYD2USB" revision: ST7789, NOT the
  // ILI9341 the single-micro-USB 2432S028 uses. LovyanGFX could not render
  // correct colours on it under any colour order / depth / init sequence;
  // TFT_eSPI does (panel setup lives in platformio.ini's TFT_eSPI -D flags:
  // ST7789, BGR, inversion off). Portrait, USB ports at the bottom.
  #define OUISPY_TFT_WIDTH   240
  #define OUISPY_TFT_HEIGHT  320
  #define OUISPY_TFT_USE_TFT_ESPI 1
  #define OUISPY_TFT_ROTATION     0
  // XPT2046 raw readings at the screen edges in that orientation, derived
  // from corner presses captured on this board (raw X = 0xD1 conversion,
  // raw Y = 0x91 conversion; raw X runs right->left, raw Y top->bottom).
  #define OUISPY_TOUCH_RAWX_AT_LEFT    3800
  #define OUISPY_TOUCH_RAWX_AT_RIGHT   240
  #define OUISPY_TOUCH_RAWY_AT_TOP     200
  #define OUISPY_TOUCH_RAWY_AT_BOTTOM  3700
#else
  // 2.8" ILI9341 320x240 (single-USB resistive 2432S028 or capacitive
  // 2432S024). Unverified on hardware.
  #define OUISPY_TFT_WIDTH   320
  #define OUISPY_TFT_HEIGHT  240
  #define OUISPY_TFT_PANEL_ILI9341 1
#endif

#ifndef OUISPY_TFT_USE_TFT_ESPI
  #define OUISPY_TFT_USE_TFT_ESPI 0
#endif
#ifndef OUISPY_TFT_ROTATION
  #define OUISPY_TFT_ROTATION 7       // unverified default
#endif
#ifndef OUISPY_TFT_RGB_ORDER
  #define OUISPY_TFT_RGB_ORDER 0
#endif
#ifndef OUISPY_TOUCH_X_MIN
  #define OUISPY_TOUCH_X_MIN  200
  #define OUISPY_TOUCH_X_MAX  3900
  #define OUISPY_TOUCH_Y_MIN  200
  #define OUISPY_TOUCH_Y_MAX  3900
  #define OUISPY_TOUCH_OFFSET_ROTATION 0
#endif

#if defined(OUISPY_BOARD_CYD_2432S024)
  #define OUISPY_TOUCH_CAPACITIVE 1
#else
  #define OUISPY_TOUCH_RESISTIVE 1
#endif

// Shared CYD pinout (LovyanGFX config in display.cpp reads these).
#define OUISPY_TFT_SCLK   14
#define OUISPY_TFT_MOSI   13
#define OUISPY_TFT_MISO   12
#define OUISPY_TFT_DC      2
#define OUISPY_TFT_CS     15
#define OUISPY_TFT_RST    -1
#define OUISPY_TFT_BL     21

#if defined(OUISPY_TOUCH_RESISTIVE)
#define OUISPY_TOUCH_CS   33
#define OUISPY_TOUCH_IRQ  36
#define OUISPY_TOUCH_CLK  25
#define OUISPY_TOUCH_MOSI 32
#define OUISPY_TOUCH_MISO 39
#else
// Capacitive (CST820/GT911) variant shares the TFT's SPI/I2C bus pins.
#define OUISPY_TOUCH_SDA  33
#define OUISPY_TOUCH_SCL  32
#define OUISPY_TOUCH_INT  21
#define OUISPY_TOUCH_RST  25
#endif

#else // ---- default: Seeed XIAO ESP32-S3 ----

#define OUISPY_HAS_TFT        0
#define OUISPY_HAS_BUZZER     1
#define OUISPY_HAS_RGB_LED    0
#define OUISPY_BUZZER_PIN     3
#define OUISPY_LED_PIN        21
#define OUISPY_NEOPIXEL_PIN   4   // GPIO4 (D3), separate from LED_PIN on the XIAO
#define OUISPY_BOOT_BUTTON_PIN 0

// Original sizes - the XIAO ESP32-S3 has plenty of RAM for these.
#define OUISPY_MAX_BLE_DETECTIONS 256
#define OUISPY_MAX_FY_DETECTIONS  200

#endif

#endif // OUISPY_BOARDS_H
