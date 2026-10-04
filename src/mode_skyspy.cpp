/*
 * Mode 5: Sky Spy - Open Drone ID Detector
 * Monitors WiFi and BLE for FAA Remote ID broadcasts from drones.
 * Ported from classic ESP32 BLE to NimBLE via compatibility macros.
 * Wraps the original Sky-Spy firmware in an anonymous namespace.
 */

// All includes (outside namespace) - NimBLE instead of classic BLE
#include <Arduino.h>
#include <HardwareSerial.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "opendroneid.h"
#include "odid_wifi.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include "modes.h"

// Rename setup/loop
#define setup skyspy_ns_setup
#define loop  skyspy_ns_loop

namespace {
#include "raw/skyspy.cpp"
} // anonymous namespace

#undef setup
#undef loop

void skyspy_setup() {
    // Mode 5 has NO AP (WIFI_STA + promiscuous for Remote-ID capture), but
    // still touches the radio, so we run the same preamble.
    ouispy_mode_preamble("MODE 5 SKY SPY");
    skyspy_ns_setup();
    ouispy_log_ap_state("MODE 5 SKY SPY", /*expectAP=*/false);
}
void skyspy_loop()  { skyspy_ns_loop(); }
void skyspy_stop()  { /* Stage 1: stop BLE scan + WiFi action-frame capture */ }

// Mode 5 has no web dashboard (serial/BLE Remote-ID monitor only), so this
// mirrors the same uavs[] tracking table the serial JSON output is built
// from: how many drones are currently in range, and the most recently seen
// one's id + RSSI.
void skyspy_get_stats(ModeStats* out) {
    *out = ModeStats{};

    unsigned long now = millis();
    int droneCount = 0;
    int latestIdx = -1;
    unsigned long latestSeen = 0;
    for (int i = 0; i < MAX_UAVS; i++) {
        if (uavs[i].mac[0] == 0) continue;
        if ((now - uavs[i].last_seen) < 7000UL) {
            droneCount++;
            if (latestIdx < 0 || uavs[i].last_seen > latestSeen) {
                latestIdx = i;
                latestSeen = uavs[i].last_seen;
            }
        }
    }

    snprintf(out->tileLabel[0], sizeof(out->tileLabel[0]), "DRONES");
    snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "%d", droneCount);

    if (latestIdx >= 0) {
        const id_data& uav = uavs[latestIdx];

        snprintf(out->tileLabel[1], sizeof(out->tileLabel[1]), "RSSI");
        snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "%d dBm", uav.rssi);

        char idbuf[24];
        if (uav.uav_id[0]) {
            snprintf(idbuf, sizeof(idbuf), "%s", uav.uav_id);
        } else {
            snprintf(idbuf, sizeof(idbuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                     uav.mac[0], uav.mac[1], uav.mac[2],
                     uav.mac[3], uav.mac[4], uav.mac[5]);
        }
        snprintf(out->logLines[0], sizeof(out->logLines[0]), "last: %s", idbuf);
    } else {
        snprintf(out->logLines[0], sizeof(out->logLines[0]), "scanning for Remote ID...");
    }
    out->logCount = 1;
}
