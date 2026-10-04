#ifndef MODES_H
#define MODES_H

#include "mode_iface.h"

// Shared preamble used by every mode's exported setup() to reset the radio
// state (WiFi.persistent(false) + WIFI_OFF + esp_wifi_restore). Prevents the
// Arduino WiFi wrapper from writing per-mode SSIDs back to ESP32-native NVS,
// which is the root cause of "wrong SSID after switching modes" bugs.
void ouispy_mode_preamble(const char* modeName);

// Log the actual softAP state after a mode's setup runs. If expectAP is true
// and no AP came up, starts a fallback "oui-spy-recovery" AP so the board is
// still reachable. Returns true if an AP is live at the end of the call.
bool ouispy_log_ap_state(const char* modeName, bool expectAP);

// Mode 1: OUI Spy Detector
void detector_setup();
void detector_loop();
void detector_stop();
void detector_get_stats(ModeStats* out);

// Mode 2: Foxhunter
void foxhunter_setup();
void foxhunter_loop();
void foxhunter_stop();
void foxhunter_get_stats(ModeStats* out);

// Mode 3: Flock-You — Promiscuous WiFi Edition
void flockyou_promiscious_setup();
void flockyou_promiscious_loop();
void flockyou_promiscious_stop();
void flockyou_promiscious_get_stats(ModeStats* out);

// Mode 4: PCAP — Passive WiFi Packet Capture
void pcap_setup();
void pcap_loop();
void pcap_stop();
void pcap_get_stats(ModeStats* out);

// Mode 5: Sky Spy
void skyspy_setup();
void skyspy_loop();
void skyspy_stop();
void skyspy_get_stats(ModeStats* out);

// Mode 6: BLE Sniff — Passive BLE advertising capture
void blesniff_setup();
void blesniff_loop();
void blesniff_stop();
void blesniff_get_stats(ModeStats* out);

#endif // MODES_H
