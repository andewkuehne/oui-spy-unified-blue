/*
 * OUI SPY - On-device TFT dashboard implementation. See display.h.
 *
 * Everything in this file compiles to nothing on the XIAO build (no TFT).
 * On a CYD build it drives the panel via TFT_eSPI (dual-USB ST7789 board)
 * or LovyanGFX (other CYD boards) using the pin map from boards.h.
 */
#include "display.h"

#if OUISPY_HAS_TFT

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include <stdarg.h>

#if OUISPY_TFT_USE_TFT_ESPI
// ---------------------------------------------------------------------------
// TFT_eSPI backend (dual-USB CYD, ST7789). Panel setup comes from the
// TFT_eSPI -D flags in platformio.ini. LovyanGFX could not drive this panel:
// every colour order/depth/init variant scrambled the colours, while
// TFT_eSPI rendered them correctly first try.
// ---------------------------------------------------------------------------
#include <SPI.h>
#include <TFT_eSPI.h>
#include <utility>

namespace {

TFT_eSPI gfx;

constexpr uint8_t kDatumTL = TL_DATUM;
constexpr uint8_t kDatumTC = TC_DATUM;
constexpr uint8_t kDatumML = ML_DATUM;
constexpr uint8_t kDatumMC = MC_DATUM;

// XPT2046 resistive touch on its own pins (VSPI; the panel owns HSPI).
// Read the same way LovyanGFX's Touch_XPT2046 does (0xD1 -> raw X,
// 0x91 -> raw Y, IRQ low while pressed) so the raw ranges measured on this
// board carry over unchanged.
SPIClass gTouchSpi(VSPI);

uint16_t xptRead(uint8_t cmd) {
    gTouchSpi.transfer(cmd);
    return gTouchSpi.transfer16(0) >> 3;
}

int32_t median3(int32_t a, int32_t b, int32_t c) {
    if (a > b) std::swap(a, b);
    if (b > c) std::swap(b, c);
    if (a > b) std::swap(a, b);
    return b;
}

void touchBegin() {
    pinMode(OUISPY_TOUCH_CS, OUTPUT);
    digitalWrite(OUISPY_TOUCH_CS, HIGH);
    pinMode(OUISPY_TOUCH_IRQ, INPUT);
    gTouchSpi.begin(OUISPY_TOUCH_CLK, OUISPY_TOUCH_MISO, OUISPY_TOUCH_MOSI, OUISPY_TOUCH_CS);
}

bool readTouchRaw(int32_t* rx, int32_t* ry) {
    if (digitalRead(OUISPY_TOUCH_IRQ) == HIGH) return false;
    int32_t xs[3], ys[3];
    gTouchSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(OUISPY_TOUCH_CS, LOW);
    xptRead(0xD1); // first conversion after wake is noisy - discard
    for (int i = 0; i < 3; i++) {
        xs[i] = xptRead(0xD1);
        ys[i] = xptRead(0x91);
    }
    xptRead(0x80); // power down, IRQ re-enabled
    digitalWrite(OUISPY_TOUCH_CS, HIGH);
    gTouchSpi.endTransaction();
    *rx = median3(xs[0], xs[1], xs[2]);
    *ry = median3(ys[0], ys[1], ys[2]);
    // Same validity window LovyanGFX uses; outside it the reading is the
    // ADC rail, i.e. the stylus lifted mid-sample.
    return *rx > 128 && *rx <= 3968 && *ry > 128 && *ry <= 3968;
}

int32_t mapClamp(int32_t v, int32_t inA, int32_t inB, int32_t outMax) {
    int32_t o = (int32_t)((int64_t)(v - inA) * outMax / (inB - inA));
    return o < 0 ? 0 : (o > outMax ? outMax : o);
}

bool readTouch(int32_t* x, int32_t* y, int32_t* rawX = nullptr, int32_t* rawY = nullptr) {
    int32_t rx, ry;
    if (!readTouchRaw(&rx, &ry)) return false;
    if (rawX) *rawX = rx;
    if (rawY) *rawY = ry;
    *x = mapClamp(rx, OUISPY_TOUCH_RAWX_AT_LEFT, OUISPY_TOUCH_RAWX_AT_RIGHT, gfx.width() - 1);
    *y = mapClamp(ry, OUISPY_TOUCH_RAWY_AT_TOP, OUISPY_TOUCH_RAWY_AT_BOTTOM, gfx.height() - 1);
    return true;
}

void panelBegin() {
    gfx.init(); // TFT_eSPI sends SWRESET itself when TFT_RST is -1
    gfx.setRotation(OUISPY_TFT_ROTATION);
    touchBegin();
}

#else
// ---------------------------------------------------------------------------
// LovyanGFX backend (other CYD boards; pins from boards.h).
// ---------------------------------------------------------------------------
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace {

class LGFX : public lgfx::LGFX_Device {
#if defined(OUISPY_TFT_PANEL_ILI9488)
    lgfx::Panel_ILI9488 _panel_instance;
#else
    lgfx::Panel_ILI9341 _panel_instance;
#endif
    lgfx::Bus_SPI _bus_instance;
#if defined(OUISPY_TOUCH_RESISTIVE)
    lgfx::Touch_XPT2046 _touch_instance;
#else
    lgfx::Touch_CST816S _touch_instance;
#endif

public:
    LGFX() {
        auto bus_cfg = _bus_instance.config();
        bus_cfg.spi_host = VSPI_HOST;
        bus_cfg.spi_mode = 0;
        bus_cfg.freq_write = 20000000;
        bus_cfg.freq_read  = 8000000;
        bus_cfg.spi_3wire  = false;
        bus_cfg.use_lock   = true;
        bus_cfg.dma_channel = SPI_DMA_CH_AUTO;
        bus_cfg.pin_sclk = OUISPY_TFT_SCLK;
        bus_cfg.pin_mosi = OUISPY_TFT_MOSI;
        bus_cfg.pin_miso = OUISPY_TFT_MISO;
        bus_cfg.pin_dc   = OUISPY_TFT_DC;
        _bus_instance.config(bus_cfg);

        _panel_instance.setBus(&_bus_instance);
        auto panel_cfg = _panel_instance.config();
        panel_cfg.pin_cs   = OUISPY_TFT_CS;
        panel_cfg.pin_rst  = OUISPY_TFT_RST;
        panel_cfg.pin_busy = -1;
        // NATIVE (portrait) memory size, before setRotation().
        panel_cfg.panel_width  = OUISPY_TFT_HEIGHT;
        panel_cfg.panel_height = OUISPY_TFT_WIDTH;
        panel_cfg.memory_width  = panel_cfg.panel_width;
        panel_cfg.memory_height = panel_cfg.panel_height;
        panel_cfg.invert = false;
        panel_cfg.rgb_order = OUISPY_TFT_RGB_ORDER;
        _panel_instance.config(panel_cfg);
        setPanel(&_panel_instance);

        auto touch_cfg = _touch_instance.config();
        // x/y_min/max are RAW controller readings (XPT2046: 12-bit ADC,
        // ~200-3900 edge to edge), NOT screen pixels.
#if defined(OUISPY_TOUCH_RESISTIVE)
        touch_cfg.x_min = OUISPY_TOUCH_X_MIN;
        touch_cfg.x_max = OUISPY_TOUCH_X_MAX;
        touch_cfg.y_min = OUISPY_TOUCH_Y_MIN;
        touch_cfg.y_max = OUISPY_TOUCH_Y_MAX;
#else
        touch_cfg.x_min = 0;
        touch_cfg.x_max = OUISPY_TFT_WIDTH - 1;
        touch_cfg.y_min = 0;
        touch_cfg.y_max = OUISPY_TFT_HEIGHT - 1;
#endif
        touch_cfg.offset_rotation = OUISPY_TOUCH_OFFSET_ROTATION;
#if defined(OUISPY_TOUCH_RESISTIVE)
        touch_cfg.spi_host = HSPI_HOST;
        touch_cfg.freq = 1000000;
        touch_cfg.pin_sclk = OUISPY_TOUCH_CLK;
        touch_cfg.pin_mosi = OUISPY_TOUCH_MOSI;
        touch_cfg.pin_miso = OUISPY_TOUCH_MISO;
        touch_cfg.pin_cs   = OUISPY_TOUCH_CS;
        touch_cfg.pin_int  = OUISPY_TOUCH_IRQ;
#else
        touch_cfg.i2c_port = 0;
        touch_cfg.pin_sda  = OUISPY_TOUCH_SDA;
        touch_cfg.pin_scl  = OUISPY_TOUCH_SCL;
        touch_cfg.pin_int  = OUISPY_TOUCH_INT;
        touch_cfg.pin_rst  = OUISPY_TOUCH_RST;
        touch_cfg.i2c_addr = 0x15;
        touch_cfg.freq = 400000;
#endif
        _touch_instance.config(touch_cfg);
        _panel_instance.setTouch(&_touch_instance);
    }
};

LGFX gfx;

constexpr auto kDatumTL = top_left;
constexpr auto kDatumTC = top_center;
constexpr auto kDatumML = middle_left;
constexpr auto kDatumMC = middle_center;

bool readTouch(int32_t* x, int32_t* y, int32_t* rawX = nullptr, int32_t* rawY = nullptr) {
    if (!gfx.getTouch(x, y)) return false;
    if (rawX && rawY) gfx.getTouchRaw(rawX, rawY);
    return true;
}

void panelBegin() {
    gfx.init();
    // CYD has no panel reset line (RST=-1), and without one LovyanGFX's
    // init never sends a software reset - so the controller keeps whatever
    // register state the previous firmware left behind. Force a SWRESET and
    // run the init sequence again on top of known defaults.
    gfx.startWrite();
    gfx.writeCommand(0x01);
    gfx.endWrite();
    delay(150);
    gfx.init();
    gfx.setRotation(OUISPY_TFT_ROTATION);
}

#endif // OUISPY_TFT_USE_TFT_ESPI

// ---------------------------------------------------------------------------
// Mode metadata for the boot menu. Mirrors main.cpp's SELECTOR_HTML `info`
// table and mode_manager.cpp's registry (kept in sync by hand, same as the
// existing duplication between those two).
// ---------------------------------------------------------------------------
struct MenuEntry { const char* name; const char* desc; };
constexpr MenuEntry kMenu[6] = {
    { "DETECTOR",       "BLE alert on target devices" },
    { "FOXHUNTER",      "RSSI proximity tracker" },
    { "FLOCK-YOU WIFI", "Promiscuous 2.4GHz sniffer" },
    { "PCAP",           "Passive WiFi capture" },
    { "SKY SPY",        "Drone Remote ID monitor" },
    { "BLE SNIFF",      "Passive BLE adverts capture" },
};

// Runtime canvas size, read from the driver AFTER setRotation() in begin()
// rather than assumed at compile time: LovyanGFX's mirrored rotation family
// (4-7) does not necessarily swap width/height the same way the plain
// family (0-3) does for every panel driver, and getting this wrong silently
// clips everything past the true edge instead of erroring - exactly what
// produced the missing menu button and the stray partial tile seen on
// hardware. gfx.width()/height() are the only authoritative source.
int gScreenW = OUISPY_TFT_WIDTH;  // placeholder until begin() corrects it
int gScreenH = OUISPY_TFT_HEIGHT;
int gRowH = OUISPY_TFT_HEIGHT / 6;

// Tap-to-return MENU tile shown in the title bar of every mode dashboard
// (top-right corner, inside the 0-40px title band).
constexpr int kMenuBtnW = 56;
constexpr int kMenuBtnH = 24;
int gMenuBtnX = 0; // computed in begin() from gScreenW
constexpr int kMenuBtnY = 8;

bool gBegun = false;
int gLastDashModeNumber = -1;
unsigned long gLastDashDraw = 0;

void drawMenuRow(int i, bool pressed) {
    int y = i * gRowH;
    gfx.fillRect(0, y, gScreenW, gRowH, pressed ? TFT_DARKGREEN : TFT_BLACK);
    gfx.drawRect(0, y, gScreenW, gRowH, TFT_GREEN);
    gfx.setTextColor(pressed ? TFT_WHITE : TFT_GREEN, pressed ? TFT_DARKGREEN : TFT_BLACK);
    gfx.setTextDatum(kDatumML);
    gfx.setTextSize(1);
    gfx.drawString(kMenu[i].name, 10, y + gRowH / 3);
    gfx.setTextSize(1);
    gfx.setTextColor(TFT_DARKGREY, pressed ? TFT_DARKGREEN : TFT_BLACK);
    gfx.drawString(kMenu[i].desc, 10, y + (gRowH * 2) / 3);
}

void drawFullMenu() {
    gfx.fillScreen(TFT_BLACK);
    gfx.setTextFont(1);
    for (int i = 0; i < 6; i++) drawMenuRow(i, false);
}

// Writes the chosen mode to NVS and reboots, exactly like main.cpp's
// web /select endpoint.
void selectModeAndReboot(int modeNumber) {
    Preferences resetPrefs;
    resetPrefs.begin("ouispy-rst", false);
    resetPrefs.putBool("flag", false);
    resetPrefs.end();

    Preferences prefs;
    prefs.begin("unified-mode", false);
    prefs.putInt("mode", modeNumber);
    prefs.end();

    gfx.fillScreen(TFT_BLACK);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.setTextDatum(kDatumMC);
    gfx.setTextSize(2);
    gfx.drawString(kMenu[modeNumber - 1].name, gScreenW / 2, gScreenH / 2 - 10);
    gfx.setTextSize(1);
    gfx.drawString("REBOOTING...", gScreenW / 2, gScreenH / 2 + 14);

    delay(400);
    ESP.restart();
}

} // namespace

namespace DisplayUI {

void begin() {
    if (gBegun) return;
#if defined(OUISPY_TFT_BL) && (OUISPY_TFT_BL >= 0)
    pinMode(OUISPY_TFT_BL, OUTPUT);
    digitalWrite(OUISPY_TFT_BL, HIGH); // backlight on
#endif
    // Rotation is the per-board value from boards.h, verified on hardware.
    panelBegin();

    // Read the REAL post-rotation canvas size from the driver rather than
    // assuming it (see note above gScreenW's decl).
    gScreenW = gfx.width();
    gScreenH = gfx.height();
    gRowH = gScreenH / 6;
    gMenuBtnX = gScreenW - kMenuBtnW - 4;
    Serial.printf("[DISPLAY] canvas after rotation(%d): %dx%d\n",
                  OUISPY_TFT_ROTATION, gScreenW, gScreenH);

    gfx.fillScreen(TFT_BLACK);
    gBegun = true;
}

int tickBootMenu() {
    if (!gBegun) return 0;

    static bool drawnOnce = false;
    static int pressedRow = -1;
    if (!drawnOnce) {
        drawFullMenu();
        drawnOnce = true;
    }

    int32_t tx, ty;
    bool touched = readTouch(&tx, &ty);

    if (touched) {
        int row = ty / gRowH;
        if (row < 0) row = 0;
        if (row > 5) row = 5;
        if (row != pressedRow) {
            if (pressedRow >= 0) drawMenuRow(pressedRow, false);
            drawMenuRow(row, true);
            pressedRow = row;
        }
    } else if (pressedRow >= 0) {
        // Released: a clean press-then-release on the same row selects it.
        int chosen = pressedRow + 1;
        drawMenuRow(pressedRow, false);
        pressedRow = -1;
        selectModeAndReboot(chosen); // reboots; never returns
        return chosen;
    }
    return 0;
}

namespace {
// ---- Dashboard layout (works for portrait 240x320 and landscape) ----------
constexpr int kTitleH     = 40;
constexpr int kApY        = 44;
constexpr int kTilesTop   = 58;
constexpr int kTileRowH   = 22;
constexpr int kStatusRows = 3;   // live status lines from the mode's stats
constexpr int kLineH      = 12;
constexpr int kEventLineH = 11;

int statusTop()  { return kTilesTop + 3 * kTileRowH + 4; }
int lowerHdrY()  { return statusTop() + kStatusRows * kLineH + 2; }
int lowerTop()   { return lowerHdrY() + 13; }
int eventRowsFit() {
    int rows = (gScreenH - 2 - lowerTop()) / kEventLineH;
    return rows < 1 ? 1 : rows;
}

// The lower half shows either the event log or the proximity radar; tapping
// it switches between them.
enum LowerView { VIEW_EVENTS, VIEW_RADAR };
LowerView gView = VIEW_EVENTS;
bool gLowerPressed = false;

// ---- Event log: a ring of timestamped lines shown newest-at-bottom. Fed by
// DisplayUI::logEvent/notifyDetection (explicit mode events) and by changes
// to the mode's status lines.
constexpr int kMaxEvents = 32;
constexpr int kEventLen  = 44;
struct Event { char text[kEventLen]; bool alert; };
Event gEvents[kMaxEvents];
int gEventHead = 0;   // next write slot
int gEventCount = 0;
bool gEventsDirty = true;

// Status-line -> event auto-logging, rate limited per slot so a line with a
// live counter in it (pkt/s etc.) adds at most one history entry per 5s.
constexpr unsigned long kAutoEventMinMs = 5000;
char gAutoLast[ModeStats::kMaxLogLines][ModeStats::kLineLen] = {{0}};
unsigned long gAutoLastMs[ModeStats::kMaxLogLines] = {0};

// ---- Proximity radar. One receiver gives signal strength, not direction:
// radius = RSSI (strong = near the centre), bearing = a fixed hash of the
// device id so each device keeps its own spot. Fed by DisplayUI::radarPing.
constexpr int kMaxBlips = 16;
constexpr unsigned long kBlipFadeMs = 20000;  // drawn dim after this
constexpr unsigned long kBlipDropMs = 60000;  // removed after this
constexpr int kRssiNear = -30;
constexpr int kRssiFar  = -100;
struct Blip {
    uint32_t id;
    int rssi;
    unsigned long seen;
    bool alert;
    int px, py;      // where it was last drawn (-1 = not drawn)
};
Blip gBlips[kMaxBlips];
int gBlipCount = 0;
int gSweepDeg = 0;
int gSweepX = -1, gSweepY = -1;  // last drawn sweep endpoint
bool gRadarFull = true;          // static rings need a full repaint

// Last-drawn values, so each region only repaints when it changed.
char gLastTileVal[3][16] = {{0}};
char gLastStatus[kStatusRows][ModeStats::kLineLen] = {{0}};
char gLastAp[48] = {0};

// Detection flash: whole-panel colour inversion for ~1s.
unsigned long gFlashUntil = 0;
bool gFlashOn = false;

// Tap-to-return MENU button in the title bar.
bool gMenuPressed = false;

void appendEvent(bool alert, const char* text) {
    unsigned long s = millis() / 1000;
    Event& e = gEvents[gEventHead];
    snprintf(e.text, sizeof(e.text), "%02lu:%02lu %s", s / 60, s % 60, text);
    e.alert = alert;
    gEventHead = (gEventHead + 1) % kMaxEvents;
    if (gEventCount < kMaxEvents) gEventCount++;
    gEventsDirty = true;
}

uint32_t hashId(const char* key) {
    uint32_t h = 2166136261u; // FNV-1a
    for (; *key; ++key) {
        h ^= (uint8_t)toupper((unsigned char)*key);
        h *= 16777619u;
    }
    return h;
}

bool pointInMenuBtn(int32_t x, int32_t y) {
    return x >= gMenuBtnX && x < gMenuBtnX + kMenuBtnW &&
           y >= kMenuBtnY && y < kMenuBtnY + kMenuBtnH;
}

void drawMenuBtn(bool pressed) {
    uint16_t bg = pressed ? TFT_MAROON : TFT_RED;
    gfx.fillRect(gMenuBtnX, kMenuBtnY, kMenuBtnW, kMenuBtnH, bg);
    gfx.setTextDatum(kDatumMC);
    gfx.setTextColor(TFT_WHITE, bg);
    gfx.setTextSize(1);
    gfx.drawString("MENU", gMenuBtnX + kMenuBtnW / 2, kMenuBtnY + kMenuBtnH / 2);
}

// Writes mode 0 (selector) to NVS and reboots - same effect as a BOOT-button
// hold or the web selector's /menu endpoint.
void returnToMenuAndReboot() {
    Preferences prefs;
    prefs.begin("unified-mode", false);
    prefs.putInt("mode", 0);
    prefs.end();
    gfx.invertDisplay(false);
    gfx.fillScreen(TFT_BLACK);
    gfx.setTextDatum(kDatumMC);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.setTextSize(2);
    gfx.drawString("MENU", gScreenW / 2, gScreenH / 2);
    delay(300);
    ESP.restart();
}

void drawTileRow(int i, const char* label, const char* value) {
    const int y = kTilesTop + i * kTileRowH;
    gfx.fillRect(0, y, gScreenW, kTileRowH, TFT_BLACK);
    gfx.drawFastHLine(4, y + kTileRowH - 1, gScreenW - 8, TFT_DARKGREEN);
    gfx.setTextDatum(kDatumML);
    gfx.setTextSize(1);
    gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
    gfx.drawString(label, 6, y + kTileRowH / 2);
    // Value right-aligned; size 2 unless it would collide with the label
    // (MAC addresses and the like drop to size 1).
    const int room = gScreenW - 12 - gfx.textWidth(label) - 10;
    gfx.setTextSize(2);
    if (gfx.textWidth(value) > room) gfx.setTextSize(1);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.drawString(value, gScreenW - 6 - gfx.textWidth(value), y + kTileRowH / 2);
}

void drawLowerHeader() {
    const int hy = lowerHdrY();
    gfx.fillRect(0, hy, gScreenW, 13, TFT_BLACK);
    gfx.drawFastHLine(0, hy, gScreenW, TFT_DARKGREEN);
    gfx.setTextSize(1);
    gfx.setTextDatum(kDatumTL);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.drawString(gView == VIEW_RADAR ? "RADAR" : "EVENTS", 6, hy + 3);
    gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
    const char* hint = gView == VIEW_RADAR ? "near=strong | tap: events" : "tap: radar";
    gfx.drawString(hint, gScreenW - 4 - gfx.textWidth(hint), hy + 3);
}

void drawEvents() {
    const int top = lowerTop();
    const int rows = eventRowsFit();
    gfx.fillRect(0, top, gScreenW, gScreenH - top, TFT_BLACK);
    gfx.setTextDatum(kDatumTL);
    gfx.setTextSize(1);
    const int shown = gEventCount < rows ? gEventCount : rows;
    // Oldest of the visible window first, newest on the bottom row.
    for (int k = 0; k < shown; k++) {
        int idx = (gEventHead - shown + k + kMaxEvents) % kMaxEvents;
        const Event& e = gEvents[idx];
        gfx.setTextColor(e.alert ? TFT_YELLOW : TFT_LIGHTGREY, TFT_BLACK);
        gfx.drawString(e.text, 4, top + k * kEventLineH);
    }
    gEventsDirty = false;
}

// Radar geometry: largest circle that fits the lower area.
void radarGeom(int* cx, int* cy, int* R) {
    const int top = lowerTop();
    const int h = gScreenH - 2 - top;
    int d = h < gScreenW - 8 ? h : gScreenW - 8;
    *R = d / 2 - 1;
    *cx = gScreenW / 2;
    *cy = top + h / 2;
}

int rssiToRadius(int rssi, int R) {
    if (rssi > kRssiNear) rssi = kRssiNear;
    if (rssi < kRssiFar) rssi = kRssiFar;
    int r = (kRssiNear - rssi) * R / (kRssiNear - kRssiFar);
    return r < 4 ? 4 : r;
}

void drawRadarRings(int cx, int cy, int R) {
    gfx.drawCircle(cx, cy, R, TFT_DARKGREEN);
    gfx.drawCircle(cx, cy, rssiToRadius(-50, R), TFT_DARKGREEN);
    gfx.drawCircle(cx, cy, rssiToRadius(-70, R), TFT_DARKGREEN);
    gfx.drawFastHLine(cx - R, cy, 2 * R, TFT_DARKGREEN);
    gfx.drawFastVLine(cx, cy - R, 2 * R, TFT_DARKGREEN);
    gfx.fillCircle(cx, cy, 2, TFT_GREEN);
}

void drawRadar(unsigned long now) {
    int cx, cy, R;
    radarGeom(&cx, &cy, &R);

    if (gRadarFull) {
        gfx.fillRect(0, lowerTop(), gScreenW, gScreenH - lowerTop(), TFT_BLACK);
        gfx.setTextSize(1);
        gfx.setTextDatum(kDatumTL);
        gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
        gfx.drawString("-50", cx + rssiToRadius(-50, R) + 2, cy + 2);
        gfx.drawString("-70", cx + rssiToRadius(-70, R) + 2, cy + 2);
        for (int i = 0; i < gBlipCount; i++) gBlips[i].px = -1;
        gSweepX = -1;
        gRadarFull = false;
    }

    // Erase the previous sweep line and any blip that moved / is going away.
    if (gSweepX >= 0) gfx.drawLine(cx, cy, gSweepX, gSweepY, TFT_BLACK);
    for (int i = 0; i < gBlipCount; i++) {
        Blip& b = gBlips[i];
        if (b.px < 0) continue;
        gfx.fillCircle(b.px, b.py, 4, TFT_BLACK);
        b.px = -1;
    }
    // Drop blips not seen for a while.
    for (int i = 0; i < gBlipCount;) {
        if (now - gBlips[i].seen > kBlipDropMs) gBlips[i] = gBlips[--gBlipCount];
        else i++;
    }

    drawRadarRings(cx, cy, R);

    // Sweep (cosmetic) - one step per dashboard refresh.
    gSweepDeg = (gSweepDeg + 24) % 360;
    const float sa = gSweepDeg * (float)DEG_TO_RAD;
    gSweepX = cx + (int)(R * cosf(sa));
    gSweepY = cy + (int)(R * sinf(sa));
    gfx.drawLine(cx, cy, gSweepX, gSweepY, TFT_GREEN);

    int active = 0;
    for (int i = 0; i < gBlipCount; i++) {
        Blip& b = gBlips[i];
        const unsigned long age = now - b.seen;
        const float a = (b.id % 360) * (float)DEG_TO_RAD;
        const int r = rssiToRadius(b.rssi, R);
        b.px = cx + (int)(r * cosf(a));
        b.py = cy + (int)(r * sinf(a));
        uint16_t c;
        if (age > kBlipFadeMs) c = TFT_DARKGREY;
        else if (b.alert) c = TFT_RED;
        else c = TFT_GREEN;
        if (age <= kBlipFadeMs) active++;
        gfx.fillCircle(b.px, b.py, age < 3000 ? 4 : 3, c);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%d in range ", active);
    gfx.setTextSize(1);
    gfx.setTextDatum(kDatumTL);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.drawString(buf, 4, gScreenH - 11);
}

void redrawLower() {
    drawLowerHeader();
    if (gView == VIEW_RADAR) {
        gRadarFull = true;
        drawRadar(millis());
    } else {
        drawEvents();
    }
}
} // namespace

void beginModeDashboard(int modeNumber, const char* modeName, const char* modeDesc) {
    if (!gBegun) return;
    gLastDashModeNumber = modeNumber;
    gLastDashDraw = 0;
    memset(gLastTileVal, 0, sizeof(gLastTileVal));
    memset(gLastStatus, 0, sizeof(gLastStatus));
    memset(gLastAp, 0, sizeof(gLastAp));
    // Detector and foxhunter feed the radar; everything else opens on the
    // event log (tap the lower half to switch either way).
    gView = (modeNumber == 1 || modeNumber == 2) ? VIEW_RADAR : VIEW_EVENTS;

    gfx.fillScreen(TFT_BLACK);
    gfx.setTextFont(1);
    gfx.setTextDatum(kDatumTL);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    // Size-2 title unless it would run under the MENU button (long names
    // like FLOCK-YOU WIFI on a 240px-wide portrait screen).
    const char* title = modeName ? modeName : "MODE";
    gfx.setTextSize(2);
    if (6 + gfx.textWidth(title) > gMenuBtnX - 4) gfx.setTextSize(1);
    gfx.drawString(title, 6, 4);
    gfx.setTextSize(1);
    gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
    gfx.drawString(modeDesc ? modeDesc : "", 6, 26);
    gfx.drawFastHLine(0, kTitleH, gScreenW, TFT_GREEN);

    // No physical button on this board, so this tile is the way back to the
    // boot menu while a mode is running (tap: press and release on it).
    drawMenuBtn(false);

    appendEvent(false, "mode started");
    redrawLower();
}

void logEvent(const char* fmt, ...) {
    if (!gBegun) return;
    char buf[kEventLen];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    appendEvent(false, buf);
}

void notifyDetection(const char* fmt, ...) {
    if (!gBegun) return;
    char buf[kEventLen];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    appendEvent(true, buf);
    gFlashUntil = millis() + 1000;
}

void radarPing(const char* id, int rssi, bool alert) {
    if (!gBegun || !id || !id[0]) return;
    const uint32_t h = hashId(id);
    const unsigned long now = millis();
    for (int i = 0; i < gBlipCount; i++) {
        if (gBlips[i].id == h) {
            gBlips[i].rssi = rssi;
            gBlips[i].seen = now;
            gBlips[i].alert = alert;
            return;
        }
    }
    int slot = gBlipCount;
    if (gBlipCount < kMaxBlips) {
        gBlipCount++;
    } else {
        // Full: replace the stalest blip (erase it first if it's on screen).
        slot = 0;
        for (int i = 1; i < kMaxBlips; i++)
            if (gBlips[i].seen < gBlips[slot].seen) slot = i;
        if (gView == VIEW_RADAR && gBlips[slot].px >= 0)
            gfx.fillCircle(gBlips[slot].px, gBlips[slot].py, 4, TFT_BLACK);
    }
    gBlips[slot] = Blip{h, rssi, now, alert, -1, -1};
}

void tickModeDashboard(mode_stats_fn getStats) {
    if (!gBegun || gLastDashModeNumber < 0) return;
    const unsigned long now = millis();

    // Detection flash - checked every call so it lasts ~1s, not ~1 refresh.
    if (gFlashUntil) {
        if (!gFlashOn && now < gFlashUntil) {
            gfx.invertDisplay(true);
            gFlashOn = true;
        } else if (now >= gFlashUntil) {
            gfx.invertDisplay(false);
            gFlashOn = false;
            gFlashUntil = 0;
        }
    }

    // Touch: MENU button (press + release on it returns to the boot menu;
    // sliding off cancels) and the lower half (tap toggles radar/events).
    {
        int32_t tx, ty;
        const bool touched = readTouch(&tx, &ty);
        if (touched) {
            const bool inside = pointInMenuBtn(tx, ty);
            if (inside != gMenuPressed) {
                gMenuPressed = inside;
                drawMenuBtn(inside);
            }
            gLowerPressed = ty >= lowerHdrY();
        } else {
            if (gMenuPressed) returnToMenuAndReboot(); // never returns
            if (gLowerPressed) {
                gLowerPressed = false;
                gView = gView == VIEW_RADAR ? VIEW_EVENTS : VIEW_RADAR;
                redrawLower();
            }
        }
    }

    if (now - gLastDashDraw < 400) return; // ~2.5 Hz refresh is plenty
    gLastDashDraw = now;

    if (getStats) {
        ModeStats s;
        getStats(&s);

        // AP line.
        char ap[48];
        if (s.apActive) snprintf(ap, sizeof(ap), "AP %s  %s", s.apSsid, s.apIp);
        else snprintf(ap, sizeof(ap), "no AP (passive capture)");
        if (strcmp(ap, gLastAp) != 0) {
            snprintf(gLastAp, sizeof(gLastAp), "%s", ap);
            gfx.fillRect(0, kApY, gScreenW, 12, TFT_BLACK);
            gfx.setTextDatum(kDatumTL);
            gfx.setTextSize(1);
            gfx.setTextColor(s.apActive ? TFT_CYAN : TFT_DARKGREY, TFT_BLACK);
            gfx.drawString(ap, 6, kApY + 2);
        }

        // Stat tiles as full-width rows: label left, value right.
        for (int i = 0; i < 3; i++) {
            const char* value = s.tileLabel[i][0] ? s.tileValue[i] : "";
            if (gLastTileVal[i][0] != 0 && strcmp(gLastTileVal[i], value[0] ? value : " ") == 0) continue;
            snprintf(gLastTileVal[i], sizeof(gLastTileVal[i]), "%s", value[0] ? value : " ");
            if (s.tileLabel[i][0]) drawTileRow(i, s.tileLabel[i], value);
            else gfx.fillRect(0, kTilesTop + i * kTileRowH, gScreenW, kTileRowH, TFT_BLACK);
        }

        // Live status lines (first kStatusRows of the mode's log lines).
        for (int i = 0; i < kStatusRows; i++) {
            const char* text = (i < s.logCount) ? s.logLines[i] : "";
            if (strcmp(gLastStatus[i], text) == 0) continue;
            snprintf(gLastStatus[i], sizeof(gLastStatus[i]), "%s", text);
            const int y = statusTop() + i * kLineH;
            gfx.fillRect(0, y, gScreenW, kLineH, TFT_BLACK);
            gfx.setTextDatum(kDatumTL);
            gfx.setTextSize(1);
            gfx.setTextColor(TFT_GREEN, TFT_BLACK);
            gfx.drawString(text, 6, y + 2);
        }

        // Any status line that changed becomes a history entry in the event
        // log (rate limited per line, see kAutoEventMinMs).
        for (int i = 0; i < s.logCount && i < ModeStats::kMaxLogLines; i++) {
            if (s.logLines[i][0] == 0 || strcmp(gAutoLast[i], s.logLines[i]) == 0) continue;
            if (gAutoLastMs[i] && now - gAutoLastMs[i] < kAutoEventMinMs) continue;
            snprintf(gAutoLast[i], sizeof(gAutoLast[i]), "%s", s.logLines[i]);
            gAutoLastMs[i] = now;
            appendEvent(false, s.logLines[i]);
        }
    }

    if (gView == VIEW_RADAR) drawRadar(now);
    else if (gEventsDirty) drawEvents();
}

#ifdef OUISPY_CALIB_ROTATION_SWEEP
namespace {
// Draws one orientation-sweep frame: a 1px white border around the whole
// logical canvas, a colored labelled block in each corner, and the current
// rotation/invert/canvas size in the middle. On the correct setting the
// border is fully visible on all 4 sides, the text reads normally (not
// mirrored) with the board held the way it will be used, and the corner
// colors match their labels (RED top-left, GREEN top-right, BLUE bottom-left,
// YELLOW bottom-right).
void drawSweepFrame(int rot, bool inv) {
    const int w = gfx.width();
    const int h = gfx.height();
    const int b = 48;
    gfx.fillScreen(TFT_BLACK);
    gfx.drawRect(0, 0, w, h, TFT_WHITE);
    gfx.fillRect(1, 1, b, b, TFT_RED);
    gfx.fillRect(w - 1 - b, 1, b, b, TFT_GREEN);
    gfx.fillRect(1, h - 1 - b, b, b, TFT_BLUE);
    gfx.fillRect(w - 1 - b, h - 1 - b, b, b, TFT_YELLOW);
    gfx.setTextSize(1);
    gfx.setTextDatum(kDatumMC);
    gfx.setTextColor(TFT_BLACK, TFT_RED);    gfx.drawString("RED",  1 + b / 2, 1 + b / 2);
    gfx.setTextColor(TFT_BLACK, TFT_GREEN);  gfx.drawString("GRN",  w - 1 - b / 2, 1 + b / 2);
    gfx.setTextColor(TFT_WHITE, TFT_BLUE);   gfx.drawString("BLU",  1 + b / 2, h - 1 - b / 2);
    gfx.setTextColor(TFT_BLACK, TFT_YELLOW); gfx.drawString("YEL",  w - 1 - b / 2, h - 1 - b / 2);

    char buf[32];
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.setTextSize(3);
    snprintf(buf, sizeof(buf), "ROT %d", rot);
    gfx.drawString(buf, w / 2, h / 2 - 30);
    gfx.setTextSize(2);
    snprintf(buf, sizeof(buf), "INV %d  %dx%d", inv ? 1 : 0, w, h);
    gfx.drawString(buf, w / 2, h / 2 + 4);
    gfx.setTextSize(1);
    gfx.drawString("^ UP ^", w / 2, 12);
}

// Clears the panel's entire GRAM. With RST=-1 the controller is never
// hardware-reset, so anything outside the region the current config
// addresses keeps whatever an earlier firmware drew there (the solid blue
// band at the bottom of the bad calibration photo). Filling in both a
// portrait and a landscape rotation covers the whole memory regardless of
// which axis the panel really treats as native.
void clearWholeGram() {
    for (int r = 0; r < 2; r++) {
        gfx.setRotation(r);
        gfx.fillScreen(TFT_BLACK);
    }
}
} // namespace
#endif

void runCalibrationLoop() {
    if (!gBegun) return;

    // --- 2. Orientation / inversion sweep -----------------------------------
    // Cycles all 8 LovyanGFX rotations x invert off/on, 3s each (~48s total),
    // then falls through to the touch quadrant test at the normal rotation.
    // Photograph (or note) the ROT/INV frame that looks correct. Only needed
    // when bringing up a new panel - build with -DOUISPY_CALIB_ROTATION_SWEEP.
#ifdef OUISPY_CALIB_ROTATION_SWEEP
    const int origRot = gfx.getRotation();
    clearWholeGram();
    for (int rot = 0; rot < 8; rot++) {
        for (int inv = 0; inv < 2; inv++) {
            gfx.setRotation(rot);
            gfx.invertDisplay(inv != 0);
            Serial.printf("[CALIB] sweep ROT %d INV %d canvas %dx%d\n",
                          rot, inv, (int)gfx.width(), (int)gfx.height());
            drawSweepFrame(rot, inv != 0);
            delay(3000);
            clearWholeGram();
        }
    }
    gfx.invertDisplay(false);
    gfx.setRotation(origRot);
    gScreenW = gfx.width();
    gScreenH = gfx.height();
#endif

    // --- 3. Touch quadrant test ---------------------------------------------
    struct Quad { const char* name; uint32_t color; int x, y, w, h; };
    const int halfW = gScreenW / 2;
    const int halfH = gScreenH / 2;
    Quad quads[4] = {
        { "TOP-LEFT",     TFT_RED,    0,     0,     halfW, halfH },
        { "TOP-RIGHT",    TFT_GREEN,  halfW, 0,     gScreenW - halfW, halfH },
        { "BOTTOM-LEFT",  TFT_BLUE,   0,     halfH, halfW, gScreenH - halfH },
        { "BOTTOM-RIGHT", TFT_YELLOW, halfW, halfH, gScreenW - halfW, gScreenH - halfH },
    };

    auto drawQuad = [&](int i, bool flashed) {
        const Quad& q = quads[i];
        gfx.fillRect(q.x, q.y, q.w, q.h, flashed ? TFT_WHITE : q.color);
        gfx.setTextDatum(kDatumMC);
        gfx.setTextColor(flashed ? TFT_BLACK : TFT_BLACK, flashed ? TFT_WHITE : q.color);
        gfx.setTextSize(1);
        gfx.drawString(q.name, q.x + q.w / 2, q.y + q.h / 2 - 10);
        char buf[24];
        snprintf(buf, sizeof(buf), "%d,%d %dx%d", q.x, q.y, q.w, q.h);
        gfx.drawString(buf, q.x + q.w / 2, q.y + q.h / 2 + 10);
    };

    gfx.fillScreen(TFT_BLACK);
    Serial.printf("[CALIB] canvas %dx%d - quadrants: TL(0,0) TR(%d,0) BL(0,%d) BR(%d,%d)\n",
                  gScreenW, gScreenH, halfW, halfH, halfW, halfH);
    for (int i = 0; i < 4; i++) drawQuad(i, false);

    int flashedIdx = -1;
    unsigned long flashUntil = 0;

    while (true) {
        int32_t tx, ty;
        int32_t rx = -1, ry = -1;
        if (readTouch(&tx, &ty, &rx, &ry)) {
            int idx = (ty < halfH ? 0 : 2) + (tx < halfW ? 0 : 1);
            Serial.printf("[CALIB] touch raw=(%ld,%ld) x=%ld y=%ld -> quadrant %s\n",
                          (long)rx, (long)ry, (long)tx, (long)ty, quads[idx].name);
            if (idx != flashedIdx) {
                if (flashedIdx >= 0) drawQuad(flashedIdx, false);
                drawQuad(idx, true);
                flashedIdx = idx;
            }
            flashUntil = millis() + 150;
        } else if (flashedIdx >= 0 && millis() > flashUntil) {
            drawQuad(flashedIdx, false);
            flashedIdx = -1;
        }
        delay(10);
    }
}

} // namespace DisplayUI

#else // !OUISPY_HAS_TFT - no-op stubs so main.cpp can call these unconditionally

namespace DisplayUI {
void begin() {}
int tickBootMenu() { return 0; }
void beginModeDashboard(int, const char*, const char*) {}
void tickModeDashboard(mode_stats_fn) {}
void logEvent(const char*, ...) {}
void notifyDetection(const char*, ...) {}
void radarPing(const char*, int, bool) {}
void runCalibrationLoop() {}
} // namespace DisplayUI

#endif // OUISPY_HAS_TFT
