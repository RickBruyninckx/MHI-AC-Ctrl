#include "support.h"
#include "wifi_config.h"
#include <WiFiManager.h>   // https://github.com/tzapu/WiFiManager
#include <EEPROM.h>

// ---------- persisted MQTT settings ----------

#define MQTT_CFG_MAGIC 0x4D514331UL   // "MQC1", bump when MqttConfig layout changes
#define MQTT_CFG_EEPROM_SIZE 256

struct StoredMqttConfig {
  uint32_t magic;
  MqttConfig cfg;
};
static_assert(sizeof(StoredMqttConfig) <= MQTT_CFG_EEPROM_SIZE, "EEPROM area too small");

MqttConfig mqttConfig;

static void loadMqttConfig() {
  StoredMqttConfig stored;
  EEPROM.begin(MQTT_CFG_EEPROM_SIZE);
  EEPROM.get(0, stored);
  if (stored.magic == MQTT_CFG_MAGIC) {
    mqttConfig = stored.cfg;
    mqttConfig.server[sizeof(mqttConfig.server) - 1] = 0;   // guard against garbage
    mqttConfig.user[sizeof(mqttConfig.user) - 1] = 0;
    mqttConfig.password[sizeof(mqttConfig.password) - 1] = 0;
    Serial.printf_P(PSTR("MQTT settings loaded from flash: %s:%u\n"), mqttConfig.server, mqttConfig.port);
  } else {
    strlcpy(mqttConfig.server, MQTT_SERVER, sizeof(mqttConfig.server));
    mqttConfig.port = MQTT_PORT;
    strlcpy(mqttConfig.user, MQTT_USER, sizeof(mqttConfig.user));
    strlcpy(mqttConfig.password, MQTT_PASSWORD, sizeof(mqttConfig.password));
    Serial.printf_P(PSTR("No saved MQTT settings, using build defaults: %s:%u\n"), mqttConfig.server, mqttConfig.port);
  }
}

static void saveMqttConfig() {
  StoredMqttConfig stored;
  stored.magic = MQTT_CFG_MAGIC;
  stored.cfg = mqttConfig;
  EEPROM.put(0, stored);
  EEPROM.commit();
  Serial.printf_P(PSTR("MQTT settings saved: %s:%u user=%s\n"), mqttConfig.server, mqttConfig.port, mqttConfig.user);
}

// ---------- WiFiManager ----------

static WiFiManager wm;
static char portStr[6];
static WiFiManagerParameter paramHeader("<hr><h3>MQTT broker</h3>");
static WiFiManagerParameter *paramServer;
static WiFiManagerParameter *paramPort;
static WiFiManagerParameter *paramUser;
static WiFiManagerParameter *paramPassword;

static unsigned long offlineSinceMillis = 0;      // 0 = currently connected
static unsigned long lastRetryMillis = 0;
static bool wasConnected = false;

// called when the user presses "Save" on the setup page (WiFi and/or MQTT settings)
static void onSaveParams() {
  strlcpy(mqttConfig.server, paramServer->getValue(), sizeof(mqttConfig.server));
  long port = atol(paramPort->getValue());
  mqttConfig.port = (port > 0 && port <= 65535) ? port : 1883;
  strlcpy(mqttConfig.user, paramUser->getValue(), sizeof(mqttConfig.user));
  // empty password field means "keep the current password" (we never send it to the browser)
  if (strlen(paramPassword->getValue()) > 0)
    strlcpy(mqttConfig.password, paramPassword->getValue(), sizeof(mqttConfig.password));
  saveMqttConfig();

  MQTTclient.disconnect();                         // reconnect with the new settings
  MQTTclient.setServer(mqttConfig.server, mqttConfig.port);
}

static void startPortal(const __FlashStringHelper* reason) {
  Serial.printf_P(PSTR("Starting setup AP \"%s\" (%s)\n"), SETUP_AP_SSID, (const char*)reason);
  const char* pw = strlen(SETUP_AP_PASSWORD) > 0 ? SETUP_AP_PASSWORD : nullptr;
  wm.startConfigPortal(SETUP_AP_SSID, pw);         // non-blocking, served by wm.process()
  Serial.printf_P(PSTR("Setup page: http://%s/\n"), WiFi.softAPIP().toString().c_str());
  lastRetryMillis = millis();
}

void wifiConfigBegin() {
  loadMqttConfig();

  snprintf(portStr, sizeof(portStr), "%u", mqttConfig.port);
  paramServer   = new WiFiManagerParameter("mqtt_server", "Broker (host or IP)", mqttConfig.server, sizeof(mqttConfig.server) - 1);
  paramPort     = new WiFiManagerParameter("mqtt_port", "Port", portStr, 5, "type='number' min='1' max='65535'");
  paramUser     = new WiFiManagerParameter("mqtt_user", "User (optional)", mqttConfig.user, sizeof(mqttConfig.user) - 1);
  paramPassword = new WiFiManagerParameter("mqtt_pass", "Password (empty = keep current)", "", sizeof(mqttConfig.password) - 1, "type='password'");
  wm.addParameter(&paramHeader);
  wm.addParameter(paramServer);
  wm.addParameter(paramPort);
  wm.addParameter(paramUser);
  wm.addParameter(paramPassword);

  wm.setTitle(HOSTNAME);
  wm.setHostname(HOSTNAME);
  wm.setConfigPortalBlocking(false);
  wm.setBreakAfterConfig(true);                    // run the save callback even if the WiFi connect fails
  wm.setSaveParamsCallback(onSaveParams);
  wm.setSaveConfigCallback(onSaveParams);
  wm.setConnectTimeout(20);
  static std::vector<const char*> menu = {"wifi", "param", "info", "sep", "restart"};
  wm.setMenu(menu);

  WiFi.persistent(true);                           // keep credentials across reboots
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (!wm.getWiFiIsSaved()) {
    startPortal(F("no WiFi credentials saved"));
  } else {
    Serial.printf_P(PSTR("Connecting to saved WiFi \"%s\"\n"), wm.getWiFiSSID(true).c_str());
    WiFi.begin();                                  // use saved credentials
  }
  offlineSinceMillis = millis();
}

bool wifiConfigPortalActive() {
  return wm.getConfigPortalActive();
}

bool wifiConfigLoop() {
  wm.process();
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected) {
    if (!wasConnected) {
      Serial.printf_P(PSTR("WiFi connected to \"%s\", IP %s (%ddBm)\n"),
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
      wasConnected = true;
    }
    offlineSinceMillis = 0;
    if (wm.getConfigPortalActive()) {
      Serial.println(F("WiFi back online, closing setup AP"));
      wm.stopConfigPortal();
      WiFi.mode(WIFI_STA);
    }
    if (!wm.getWebPortalActive()) {
      // settings page also reachable on the home network
      wm.startWebPortal();
      Serial.printf_P(PSTR("Settings page: http://%s/\n"), WiFi.localIP().toString().c_str());
    }
    return true;
  }

  // not connected
  if (wasConnected) {
    Serial.println(F("WiFi connection lost"));
    WIFI_lost++;
    wasConnected = false;
    lastRetryMillis = millis();
  }
  if (offlineSinceMillis == 0)
    offlineSinceMillis = millis();

  if (!wm.getConfigPortalActive()) {
    if (millis() - offlineSinceMillis >= SETUP_AP_AFTER_OFFLINE_MIN * 60UL * 1000UL) {
      if (wm.getWebPortalActive())
        wm.stopWebPortal();                        // setup AP brings its own web server
      startPortal(F("WiFi offline for too long"));
    } else if (wm.getWiFiIsSaved() && millis() - lastRetryMillis >= 30UL * 1000UL) {
      // don't rely on the SDK auto-reconnect: it is disabled after an explicit
      // WiFi.disconnect() (e.g. by the MQTT reconnect workaround)
      Serial.println(F("WiFi offline, retrying saved network"));
      WiFi.begin();
      lastRetryMillis = millis();
    }
  } else if (wm.getWiFiIsSaved() && WiFi.softAPgetStationNum() == 0 &&
             millis() - lastRetryMillis >= 60UL * 1000UL) {
    // retry the saved network, but not while someone is using the setup page
    // (connecting makes the radio hop channels and would drop the phone from the AP)
    Serial.println(F("Setup AP active, retrying saved WiFi"));
    WiFi.begin();
    lastRetryMillis = millis();
  }
  return false;
}
