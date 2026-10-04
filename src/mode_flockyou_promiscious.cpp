/*
 * Mode 3: Flock-You — Promiscuous WiFi Edition
 *
 * Passive 2.4 GHz promiscuous-mode detector for Flock Safety surveillance
 * infrastructure. Wraps the standalone firmware from the `promiscious`
 * branch of colonelpanichacks/flock-you in an anonymous namespace.
 *
 * Detection methods (no AP, no BLE transmit — both radios passive):
 *   - addr2 OUI match  (transmitter-side, union of @NitekryDPaul community
 *                       list and firmware-dump OUIs)
 *   - addr1 OUI match  (receiver-side, @NitekryDPaul's sleeper-catch)
 *   - wildcard probe   (probe req + zero-length SSID + known OUI, the
 *                       DeFlockJoplin high-precision signature; tier 4 when
 *                       the community IE fingerprint also matches)
 *   - BLE advert match (firmware-derived: Penguin/FS-battery names, XUNTONG
 *                       0x09C8 mfg data, Flock/Raven GATT service UUIDs)
 *
 * Outputs:
 *   - Live Flask-compatible JSON over USB-CDC (one line per detection)
 *   - SPIFFS-persisted session with CRC envelope; the host can pull it
 *     back via the CMD:* protocol (CMD:DUMP_PREV / CMD:DUMP_LIVE etc.)
 */

// All includes from the original firmware must be OUTSIDE the namespace
// so they get external linkage. Re-inclusions inside the namespace are
// no-ops thanks to header guards.
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <ctype.h>
#include <string.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>   // BLE side of the Flock-You union signature set
#include "modes.h"

// Rename setup/loop so they don't collide with the unified main.cpp's
// Arduino entry points (and the other modes' wrapped setup/loop).
#define setup flockyou_promiscious_ns_setup
#define loop  flockyou_promiscious_ns_loop

namespace {
#include "raw/flockyou_promiscious.cpp"
} // anonymous namespace

#undef setup
#undef loop

void flockyou_promiscious_setup() {
    // Mode 3 has NO AP (promiscuous only), but still touches the radio.
    // The preamble matters so a prior mode's leftover softAP state can't
    // reappear on this boot.
    ouispy_mode_preamble("MODE 3 FLOCK-YOU");
    flockyou_promiscious_ns_setup();
    ouispy_log_ap_state("MODE 3 FLOCK-YOU", /*expectAP=*/false);
}
void flockyou_promiscious_loop()  { flockyou_promiscious_ns_loop(); }
void flockyou_promiscious_stop()  { /* Stage 1: disable promiscuous cb, flush SPIFFS session */ }

void flockyou_promiscious_get_stats(ModeStats* out) {
    *out = ModeStats{};

    // Mode 3 is promiscuous-only — no AP ever comes up, so leave
    // apActive/apSsid/apIp at their zeroed defaults.

    snprintf(out->tileLabel[0], sizeof(out->tileLabel[0]), "DETECTS");
    snprintf(out->tileValue[0], sizeof(out->tileValue[0]), "%d", fyDetCount);

    snprintf(out->tileLabel[1], sizeof(out->tileLabel[1]), "TIER");
    snprintf(out->tileValue[1], sizeof(out->tileValue[1]), "%u", (unsigned)fyLastTargetTier);

    snprintf(out->tileLabel[2], sizeof(out->tileLabel[2]), "CHANNEL");
    snprintf(out->tileValue[2], sizeof(out->tileValue[2]), "%u", (unsigned)currentChannel);

    out->logCount = 0;
    if (fyDetCount == 0) {
        snprintf(out->logLines[out->logCount], sizeof(out->logLines[0]),
                 "scanning ch %u - no hits yet", (unsigned)currentChannel);
        out->logCount++;
    } else {
        // Most recent entries are appended at the end of fyDet[]; show the
        // last few as a mini detection log.
        int shown = (fyDetCount < 3) ? fyDetCount : 3;
        for (int i = 0; i < shown && out->logCount < ModeStats::kMaxLogLines; i++) {
            const FYDetection& d = fyDet[fyDetCount - shown + i];
            snprintf(out->logLines[out->logCount], sizeof(out->logLines[0]),
                     "%s %s rssi=%d ch=%u", d.mac, tierLabel(d.tier), d.rssi, (unsigned)d.channel);
            out->logCount++;
        }
    }
    if (out->logCount < ModeStats::kMaxLogLines) {
        snprintf(out->logLines[out->logCount], sizeof(out->logLines[0]), "no AP - promiscuous only");
        out->logCount++;
    }
}
