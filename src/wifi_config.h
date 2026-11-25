#pragma once
// WiFi provisioning via captive portal (WiFiManager) + persisted MQTT settings
//
// - no WiFi credentials saved      -> setup AP is started immediately
// - WiFi offline for SETUP_AP_AFTER_OFFLINE_MIN minutes -> setup AP is started
// - while the setup AP is up, the saved network is retried every minute (only
//   when no client is connected to the AP), the AP closes once connected again
// - the setup page also holds the MQTT broker settings, stored in flash

#include <Arduino.h>

#ifndef SETUP_AP_SSID
#define SETUP_AP_SSID HOSTNAME "-Setup"           // name of the setup access point
#endif
#ifndef SETUP_AP_PASSWORD
#define SETUP_AP_PASSWORD "mhi-ac-setup"           // min. 8 chars, override in secrets.ini; "" = open AP
#endif
#ifndef SETUP_AP_AFTER_OFFLINE_MIN
#define SETUP_AP_AFTER_OFFLINE_MIN 5               // minutes without WiFi before the setup AP is started
#endif

struct MqttConfig {
  char server[64];
  uint16_t port;
  char user[32];
  char password[64];
};

extern MqttConfig mqttConfig;                      // active MQTT settings (saved or build defaults)

void wifiConfigBegin();                            // call once in setup()
bool wifiConfigLoop();                             // call every loop(), returns true when WiFi is connected
bool wifiConfigPortalActive();                     // true while the setup AP is up
