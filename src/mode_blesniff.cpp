/*
 * Mode 6: BLE SNIFF - Passive BLE Advertising Capture
 *
 * Wraps the merged colonelpanichacks/ouispy-blesniff firmware in an anonymous
 * namespace so its symbols (config/scan/nordic_pcap/pcap_stream/session_pcap/
 * text_summary/web_dashboard) don't collide with any other mode's code.
 *
 * Slot: right after Sky Spy (mode 6). LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR
 * pcap over USB-CDC + text summary, dashboard at ouispy-blesniff /
 * sniffuntothem, 2 MB in-PSRAM session pcap.
 */

// All includes the merged file needs live OUTSIDE the anonymous namespace so
// they get external linkage. Re-inclusions inside the namespace are no-ops
// thanks to header guards.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <driver/ledc.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "modes.h"

// Rename setup/loop so they don't collide with the Arduino entry points in
// main.cpp or the other modes' wrapped setup/loop.
#define setup blesniff_ns_setup
#define loop  blesniff_ns_loop

namespace {
#include "raw/blesniff.cpp"
} // anonymous namespace

#undef setup
#undef loop

void blesniff_setup() {
    ouispy_mode_preamble("MODE 6 BLESNIFF");
    blesniff_ns_setup();
    ouispy_log_ap_state("MODE 6 BLESNIFF", /*expectAP=*/true);
}
void blesniff_loop()  { blesniff_ns_loop(); }
void blesniff_stop() {
    // main.cpp reboots back to the selector on BOOT hold, so this only runs
    // in the (currently unused) hot-swap path via ModeManager. Best-effort
    // teardown; the async web server + writer tasks unwind cleanly on restart.
    if (NimBLEDevice::getInitialized()) {
        NimBLEScan* s = NimBLEDevice::getScan();
        if (s) s->stop();
    }
}

// Mirrors the same counters the web_dashboard /ws "status" JSON reports
// (scan::total_adverts/adverts_per_sec + session_pcap::size) so the
// on-device TFT dashboard matches what the browser shows.
void blesniff_get_stats(ModeStats* out) {
    *out = ModeStats{};

    String ssid = WiFi.softAPSSID();
    if (ssid.length() > 0) {
        out->apActive = true;
        snprintf(out->apSsid, sizeof(out->apSsid), "%s", ssid.c_str());
        snprintf(out->apIp, sizeof(out->apIp), "%s", WiFi.softAPIP().toString().c_str());
    }

    snprintf(out->tileLabel[0], sizeof(out->tileLabel[0]), "ADVERTS");
    snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "%u", (unsigned)scan::total_adverts());

    snprintf(out->tileLabel[1], sizeof(out->tileLabel[1]), "RATE");
    snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "%u/s", (unsigned)scan::adverts_per_sec());

    snprintf(out->tileLabel[2], sizeof(out->tileLabel[2]), "PCAP");
    snprintf(out->tileValue[2], sizeof(out->tileValue[2]), "%u KB",
             (unsigned)(session_pcap::size() / 1024));

    snprintf(out->logLines[0], sizeof(out->logLines[0]), "blesniff: %s",
             session_pcap::state_name());
    out->logCount = 1;
}
