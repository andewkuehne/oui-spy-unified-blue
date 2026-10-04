/*
 * Mode 4: PCAP — Passive WiFi Packet Capture
 *
 * Wraps the merged colonelpanichacks/ouispy-pcap firmware in an anonymous
 * namespace so its symbols (config/capture/pcap_stream/session_pcap/
 * text_summary/web_dashboard) don't collide with any other mode's code.
 *
 * Fills the mode-4 slot vacated by the retired Flock-You BLE mode.
 */

// All includes the merged file needs live OUTSIDE the anonymous namespace so
// they get external linkage. Re-inclusions inside the namespace are no-ops
// thanks to header guards.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <driver/ledc.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
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
#define setup pcap_ns_setup
#define loop  pcap_ns_loop

namespace {
#include "raw/pcap.cpp"
} // anonymous namespace

#undef setup
#undef loop

void pcap_setup() {
    ouispy_mode_preamble("MODE 4 PCAP");
    pcap_ns_setup();
    ouispy_log_ap_state("MODE 4 PCAP", /*expectAP=*/true);
}
void pcap_loop()  { pcap_ns_loop(); }
void pcap_stop() {
    // main.cpp reboots back to the selector on BOOT hold, so this only runs
    // in the (currently unused) hot-swap path via ModeManager. Just kill the
    // radio; the async web server + writer tasks unwind cleanly on restart.
    esp_wifi_set_promiscuous(false);
}

// Mirrors the same counters the web_dashboard /ws "status" JSON reports
// (capture::total_packets/current_channel + session_pcap::size) so the
// on-device TFT dashboard matches what the browser shows.
void pcap_get_stats(ModeStats* out) {
    *out = ModeStats{};

    String ssid = WiFi.softAPSSID();
    if (ssid.length() > 0) {
        out->apActive = true;
        snprintf(out->apSsid, sizeof(out->apSsid), "%s", ssid.c_str());
        snprintf(out->apIp, sizeof(out->apIp), "%s", WiFi.softAPIP().toString().c_str());
    }

    snprintf(out->tileLabel[0], sizeof(out->tileLabel[0]), "PACKETS");
    snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "%u", (unsigned)capture::total_packets());

    snprintf(out->tileLabel[1], sizeof(out->tileLabel[1]), "CHANNEL");
    snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "%u", (unsigned)capture::current_channel());

    snprintf(out->tileLabel[2], sizeof(out->tileLabel[2]), "PCAP");
    snprintf(out->tileValue[2], sizeof(out->tileValue[2]), "%u KB",
             (unsigned)(session_pcap::size() / 1024));

    snprintf(out->logLines[0], sizeof(out->logLines[0]), "pcap: %s, %u pkt/s",
             session_pcap::state_name(), (unsigned)capture::packets_per_sec());
    out->logCount = 1;
}
