/*
 * OUI SPY - On-device TFT dashboard (CYD boards only)
 *
 * Compiled to nothing (every call a no-op) unless OUISPY_HAS_TFT is set by
 * boards.h, i.e. unless building for a CYD environment. On the XIAO build
 * this header still exists so main.cpp can call it unconditionally.
 *
 * Two screens:
 *   - Boot menu: touch grid of the 6 modes, mirrors the web selector at
 *     http://192.168.4.1. Tapping a tile does the same NVS-write + reboot
 *     the web selector's /select endpoint does.
 *   - Mode dashboard: shown while a mode is running. Polls that mode's
 *     <id>_get_stats() a few times a second. If the mode fills the
 *     structured tile fields, renders them as stat tiles (mirroring its
 *     web dashboard, where it has one); if it only fills log lines,
 *     renders a plain scrolling log instead.
 */
#ifndef OUISPY_DISPLAY_H
#define OUISPY_DISPLAY_H

#include "boards.h"
#include "mode_iface.h"

namespace DisplayUI {

// Call once from setup(), after Serial is up. No-op on non-TFT boards.
void begin();

// Draws the boot/mode-select menu and blocks returning control to the
// caller's loop() by handling its own touch polling internally each tick();
// call tick() from loop() while currentMode == 0 (selector). Returns the
// 1-based mode number the user tapped, or 0 if nothing tapped yet.
// No-op (always returns 0) on non-TFT boards.
int tickBootMenu();

// Switches the display into "mode dashboard" view for mode index (1-based,
// matching main.cpp's currentMode) and draws the static chrome (title, AP
// info placeholder). Call once right after a mode's _setup() returns.
void beginModeDashboard(int modeNumber, const char* modeName, const char* modeDesc);

// Call every loop() while a mode (1-6) is active. Internally rate-limited;
// polls the given mode's get_stats() and redraws only the parts that
// changed. No-op on non-TFT boards or if get_stats is nullptr (falls back
// to a static "no live stats" message drawn once by beginModeDashboard).
void tickModeDashboard(mode_stats_fn getStats);

// Appends a timestamped line to the dashboard's scrolling event log.
// printf-style. Call from the main loop (not BLE/WiFi callbacks - they run
// on other tasks and must not touch the SPI display). No-op without a TFT.
void logEvent(const char* fmt, ...);

// Same as logEvent, highlighted, and flashes the whole screen (colour
// inversion) for ~1s - the visual stand-in for the buzzer on boards with no
// speaker fitted. Main-loop only, same as logEvent.
void notifyDetection(const char* fmt, ...);

// Places/updates a blip on the dashboard's proximity radar. `id` is any
// stable key (MAC address); radius comes from `rssi` (dBm, stronger = closer
// to the centre) and the bearing is a fixed hash of `id` - a single receiver
// has no direction information. Blips fade after 20s and drop after 60s.
// Main-loop only, same as logEvent.
void radarPing(const char* id, int rssi, bool alert);

// Diagnostic screen: fills the ENTIRE canvas edge-to-edge with 4 colored
// quadrants (top-left/top-right/bottom-left/bottom-right), each one a touch
// button that flashes white and prints its name + the raw touch coordinate
// to Serial when pressed. Used to visually confirm there is no clipping/
// offset at the panel's true edges and that touch position maps to the
// right on-screen quadrant. Never returns (loops forever) - only call it
// from a dedicated calibration build, not the normal firmware. No-op on
// non-TFT boards.
void runCalibrationLoop();

} // namespace DisplayUI

#endif // OUISPY_DISPLAY_H
