#pragma once
// WiFi provisioning via captive portal (WiFiManager) + persisted device/MQTT settings
//
// - no WiFi credentials saved      -> setup AP is started immediately
// - WiFi offline for SETUP_AP_AFTER_OFFLINE_MIN minutes -> setup AP is started
// - while the setup AP is up, the saved network is retried every minute (only
//   when no client is connected to the AP), the AP closes once connected again
// - the settings page holds the MQTT broker, hostname and device password,
//   stored in flash. On the home network it requires a login (user "admin",
//   device password); on the setup AP the AP password protects it.
// - the device password is also the setup AP password and the OTA password.
//   First match wins: set on the settings page, DEVICE_PASSWORD, derived from
//   DEVICE_SECRET + chip ID, stored random password, new random password.
//   It is printed in the serial log at every boot.

#include <Arduino.h>

#ifndef DEVICE_PASSWORD
#ifdef SETUP_AP_PASSWORD
#define DEVICE_PASSWORD SETUP_AP_PASSWORD          // backwards compatible name
#else
#define DEVICE_PASSWORD ""                         // "" = random password generated on first boot
#endif
#endif
#ifndef DEVICE_SECRET
#define DEVICE_SECRET ""                           // set (in secrets.ini) to derive the password from the chip ID:
#endif                                             // HMAC-SHA256(DEVICE_SECRET, "MHI-AC-Ctrl:" + chip ID), see tools/device_password.py
#ifndef SETUP_AP_AFTER_OFFLINE_MIN
#define SETUP_AP_AFTER_OFFLINE_MIN 5               // minutes without WiFi before the setup AP is started
#endif
#define WEB_LOGIN_USER "admin"                     // user name for the settings page on the home network

struct MqttConfig {
  char server[64];
  uint16_t port;
  char user[32];
  char password[64];
};

extern MqttConfig mqttConfig;                      // active MQTT settings (saved or build defaults)

void wifiConfigBegin();                            // call once in setup(), before setupOTA()
bool wifiConfigLoop();                             // call every loop(), returns true when WiFi is connected
bool wifiConfigPortalActive();                     // true while the setup AP is up

const char* deviceHostname();                      // WiFi / OTA / MQTT client name, MQTT topic root
const char* devicePassword();                      // setup AP, settings page login, OTA
const char* deviceId();                            // 6 hex digits, unique per chip
const char* deviceChipId();                        // WiFi station MAC address, 12 hex digits

// MQTT topic prefixes, derived from the hostname: "<hostname>/", "<hostname>/set/", ...
const char* mqttPrefix();
const char* mqttSetPrefix();
const char* mqttOpPrefix();
const char* mqttErrOpPrefix();
