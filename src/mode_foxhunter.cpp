/*
 * Mode 2: OUI Spy Foxhunter
 * Single-target RSSI proximity tracker with real-time beeping.
 * Wraps the original foxhunter firmware in an anonymous namespace.
 */

// All includes from the original foxhunter (outside namespace)
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <esp_wifi.h>
#include "modes.h"
#include "display.h"

// Rename setup/loop
#define setup foxhunter_ns_setup
#define loop  foxhunter_ns_loop

namespace {
#include "raw/foxhunter.cpp"
} // anonymous namespace

#undef setup
#undef loop

void foxhunter_setup() {
    ouispy_mode_preamble("MODE 2 FOXHUNTER");
    foxhunter_ns_setup();
    ouispy_log_ap_state("MODE 2 FOXHUNTER", /*expectAP=*/true);
}
void foxhunter_loop()  { foxhunter_ns_loop(); }
void foxhunter_stop()  { /* Stage 1: end web server, stop BLE scan, silence buzzer */ }

void foxhunter_get_stats(ModeStats* out) {
    *out = ModeStats{};

    // Mode 2 serves its config/target UI from a softAP named "foxhunter".
    String apSsid = WiFi.softAPSSID();
    out->apActive = apSsid.length() > 0;
    snprintf(out->apSsid, sizeof(out->apSsid), "%s", apSsid.c_str());
    snprintf(out->apIp, sizeof(out->apIp), "%s", WiFi.softAPIP().toString().c_str());

    snprintf(out->tileLabel[0], sizeof(out->tileLabel[0]), "TARGET");
    if (targetMAC.length() > 0) {
        snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "%s", targetMAC.c_str());
    } else {
        snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "--");
    }

    snprintf(out->tileLabel[1], sizeof(out->tileLabel[1]), "RSSI");
    if (targetDetected) {
        snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "%d dBm", currentRSSI);
    } else {
        snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "--");
    }

    // Proximity label derived from the same RSSI bands calculateBeepInterval()
    // uses for beep speed, so the dashboard tile matches what you hear.
    snprintf(out->tileLabel[2], sizeof(out->tileLabel[2]), "PROXIMITY");
    const char* proximity;
    if (!targetDetected) {
        proximity = "LOST";
    } else if (currentRSSI >= -35) {
        proximity = "VERY CLOSE";
    } else if (currentRSSI >= -55) {
        proximity = "CLOSE";
    } else if (currentRSSI >= -75) {
        proximity = "FAR";
    } else {
        proximity = "VERY FAR";
    }
    snprintf(out->tileValue[2], sizeof(out->tileValue[2]), "%s", proximity);

    out->logCount = 0;
    if (targetMAC.length() == 0) {
        snprintf(out->logLines[out->logCount], sizeof(out->logLines[0]), "No target MAC configured");
        out->logCount++;
    } else if (targetDetected) {
        snprintf(out->logLines[out->logCount], sizeof(out->logLines[0]),
                 "Tracking %s", targetMAC.c_str());
        out->logCount++;
    } else {
        snprintf(out->logLines[out->logCount], sizeof(out->logLines[0]),
                 "Target %s not in range", targetMAC.c_str());
        out->logCount++;
    }
}
