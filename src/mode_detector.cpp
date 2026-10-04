/*
 * Mode 1: OUI Spy Detector
 * WiFi & BLE surveillance device scanner with web configuration.
 * Wraps the original detector firmware in an anonymous namespace.
 */

// All includes from the original detector (outside namespace for correct linkage)
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEUtils.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <FS.h>
#include <SPIFFS.h>
#include <Adafruit_NeoPixel.h>
#include "modes.h"
#include "display.h"

// Rename setup/loop to avoid conflict with Arduino entry points
#define setup detector_ns_setup
#define loop  detector_ns_loop

// Anonymous namespace: all symbols get internal linkage (no linker conflicts)
namespace {
#include "raw/detector.cpp"
} // anonymous namespace

#undef setup
#undef loop

// Exported mode entry points (called from main.cpp)
void detector_setup() {
    ouispy_mode_preamble("MODE 1 DETECTOR");
    detector_ns_setup();
    ouispy_log_ap_state("MODE 1 DETECTOR", /*expectAP=*/true);
}
void detector_loop()  { detector_ns_loop(); }
void detector_stop() {
    // The mode's globals live in this file's anonymous namespace, so they are
    // reachable unqualified here. Stop mode-specific resources; the manager's
    // releaseRadios() then deinits NimBLE and powers WiFi down after we return.
    if (pBLEScan) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }
    detectorDNS.stop();
    server.end();
    strip.clear();
    strip.show();
}

void detector_get_stats(ModeStats* out) {
    *out = ModeStats{};

    // Mode 1 serves its config web UI from a softAP — report the real state.
    String apSsid = WiFi.softAPSSID();
    out->apActive = apSsid.length() > 0;
    snprintf(out->apSsid, sizeof(out->apSsid), "%s", apSsid.c_str());
    snprintf(out->apIp, sizeof(out->apIp), "%s", WiFi.softAPIP().toString().c_str());

    // targetFilters = configured OUI/MAC/name/etc filters; devices = every
    // device that has matched one of those filters this session.
    snprintf(out->tileLabel[0], sizeof(out->tileLabel[0]), "TARGETS");
    snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "%u", (unsigned)targetFilters.size());

    snprintf(out->tileLabel[1], sizeof(out->tileLabel[1]), "ALERTS");
    snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "%u", (unsigned)devices.size());

    snprintf(out->tileLabel[2], sizeof(out->tileLabel[2]), "LAST HIT");
    if (detectedMAC.length() > 0) {
        snprintf(out->tileValue[2], sizeof(out->tileValue[2]), "%s", detectedMAC.c_str());
    } else {
        snprintf(out->tileValue[2], sizeof(out->tileValue[2]), "--");
    }

    // Hits themselves go to the event log/radar via DisplayUI calls in the
    // scan loop; the status lines say what the mode is doing right now.
    out->logCount = 0;
    if (currentMode == CONFIG_MODE) {
        snprintf(out->logLines[out->logCount++], sizeof(out->logLines[0]),
                 "Config web UI open - join the AP");
        snprintf(out->logLines[out->logCount++], sizeof(out->logLines[0]),
                 "Scanning starts when config closes");
    } else {
        snprintf(out->logLines[out->logCount++], sizeof(out->logLines[0]),
                 "Scanning BLE for %u filter(s)", (unsigned)targetFilters.size());
        snprintf(out->logLines[out->logCount++], sizeof(out->logLines[0]),
                 "Heard %lu adverts / %d devices", (unsigned long)bleAdvertsHeard, bleNearbyCount);
    }
}
