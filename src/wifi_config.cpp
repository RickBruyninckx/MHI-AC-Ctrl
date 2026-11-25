#include "support.h"
#include "wifi_config.h"
#include "web_theme.h"
#include "web_status.h"
#include <WiFiManager.h>   // https://github.com/tzapu/WiFiManager
#include <EEPROM.h>

// ---------- persisted settings ----------

#define CFG_MAGIC_V1 0x4D514331UL     // "MQC1": MqttConfig only
#define CFG_MAGIC_V2 0x4D514332UL     // "MQC2": MqttConfig + hostname + device password
#define CFG_EEPROM_SIZE 256

struct StoredConfig {
  uint32_t magic;
  MqttConfig mqtt;                    // same offset as in V1, so V1 data can be migrated
  char hostname[32];
  char password[32];
};
static_assert(sizeof(StoredConfig) <= CFG_EEPROM_SIZE, "EEPROM area too small");

MqttConfig mqttConfig;
static char hostname[32];
static char password[32];
static char devId[7];
static char prefix[40], setPrefix[44], opPrefix[48], errOpPrefix[52];

const char* deviceHostname() { return hostname; }
const char* devicePassword() { return password; }
const char* deviceId() { return devId; }
const char* mqttPrefix() { return prefix; }
const char* mqttSetPrefix() { return setPrefix; }
const char* mqttOpPrefix() { return opPrefix; }
const char* mqttErrOpPrefix() { return errOpPrefix; }

static bool validHostname(const char* h) {
  size_t n = strlen(h);
  if (n == 0 || n >= sizeof(hostname)) return false;
  for (size_t i = 0; i < n; i++)
    if (!isalnum((unsigned char)h[i]) && h[i] != '-') return false;
  return h[0] != '-' && h[n - 1] != '-';
}

static void randomPassword(char* out, size_t len) {
  static const char chars[] = "abcdefghjkmnpqrstuvwxyz23456789";   // no 0/O, 1/l/i
  for (size_t i = 0; i < len - 1; i++) {
#if defined(ESP32)
    uint32_t r = esp_random();
#else
    uint32_t r = ESP.random();
#endif
    out[i] = chars[r % (sizeof(chars) - 1)];
  }
  out[len - 1] = 0;
}

static void buildPrefixes() {
  snprintf(prefix, sizeof(prefix), "%s/", hostname);
  snprintf(setPrefix, sizeof(setPrefix), "%sset/", prefix);
  snprintf(opPrefix, sizeof(opPrefix), "%sOpData/", prefix);
  snprintf(errOpPrefix, sizeof(errOpPrefix), "%sErrOpData/", prefix);
}

static void saveConfig() {
  StoredConfig stored;
  memset(&stored, 0, sizeof(stored));
  stored.magic = CFG_MAGIC_V2;
  stored.mqtt = mqttConfig;
  strlcpy(stored.hostname, hostname, sizeof(stored.hostname));
  strlcpy(stored.password, password, sizeof(stored.password));
  EEPROM.put(0, stored);
  EEPROM.commit();
  Serial.printf_P(PSTR("Settings saved: hostname=%s MQTT %s:%u user=%s\n"), hostname, mqttConfig.server, mqttConfig.port, mqttConfig.user);
}

static void loadConfig() {
#if defined(ESP32)
  snprintf(devId, sizeof(devId), "%06lx", (unsigned long)((ESP.getEfuseMac() >> 24) & 0xFFFFFF));
#else
  snprintf(devId, sizeof(devId), "%06lx", (unsigned long)(ESP.getChipId() & 0xFFFFFF));
#endif

  StoredConfig stored;
  EEPROM.begin(CFG_EEPROM_SIZE);
  EEPROM.get(0, stored);
  bool dirty = false;

  if (stored.magic == CFG_MAGIC_V1 || stored.magic == CFG_MAGIC_V2) {
    mqttConfig = stored.mqtt;
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
    dirty = true;
  }

  if (stored.magic == CFG_MAGIC_V2) {
    stored.hostname[sizeof(stored.hostname) - 1] = 0;
    stored.password[sizeof(stored.password) - 1] = 0;
  }
  if (stored.magic == CFG_MAGIC_V2 && validHostname(stored.hostname)) {
    strlcpy(hostname, stored.hostname, sizeof(hostname));
  } else {
    strlcpy(hostname, HOSTNAME, sizeof(hostname));
    dirty = true;
  }
  if (stored.magic == CFG_MAGIC_V2 && strlen(stored.password) >= 8) {
    strlcpy(password, stored.password, sizeof(password));
  } else if (strlen(DEVICE_PASSWORD) >= 8) {
    strlcpy(password, DEVICE_PASSWORD, sizeof(password));
    dirty = true;
  } else {
    randomPassword(password, 11);
    Serial.println(F("Generated a new random device password"));
    dirty = true;
  }
  if (dirty) saveConfig();

  buildPrefixes();
  Serial.printf_P(PSTR("Hostname: %s, device id: %s, MQTT topics: %s...\n"), hostname, devId, prefix);
  // printed on purpose: the serial port needs physical access, and this is how
  // the user learns a generated password
  Serial.printf_P(PSTR("Device password (setup AP, settings login '" WEB_LOGIN_USER "', OTA): %s\n"), password);
}

// ---------- WiFiManager ----------

static WiFiManager wm;
static char portStr[6];
static WiFiManagerParameter paramMqttHeader("<h3>MQTT broker</h3>");
static WiFiManagerParameter paramDeviceHeader("<br/><h3>Device</h3>");
static WiFiManagerParameter *paramServer, *paramPort, *paramUser, *paramPassword;
static WiFiManagerParameter *paramHostname, *paramDevicePassword;
static char setupApSsid[40];

static unsigned long offlineSinceMillis = 0;      // 0 = currently connected
static unsigned long lastRetryMillis = 0;
static unsigned long restartAtMillis = 0;         // 0 = no restart pending
static bool wasConnected = false;

// called when the user presses "Save" on the setup page (WiFi and/or settings)
static void onSaveParams() {
  strlcpy(mqttConfig.server, paramServer->getValue(), sizeof(mqttConfig.server));
  long port = atol(paramPort->getValue());
  mqttConfig.port = (port > 0 && port <= 65535) ? port : 1883;
  strlcpy(mqttConfig.user, paramUser->getValue(), sizeof(mqttConfig.user));
  // empty password fields mean "keep the current one" (passwords are never sent to the browser)
  if (strlen(paramPassword->getValue()) > 0)
    strlcpy(mqttConfig.password, paramPassword->getValue(), sizeof(mqttConfig.password));

  bool restart = false;
  const char* newHost = paramHostname->getValue();
  if (strcmp(newHost, hostname) != 0) {
    if (validHostname(newHost)) {
      strlcpy(hostname, newHost, sizeof(hostname));
      restart = true;
    } else {
      Serial.printf_P(PSTR("Invalid hostname \"%s\" ignored (letters, digits, '-', max 31)\n"), newHost);
    }
  }
  const char* newPw = paramDevicePassword->getValue();
  if (strlen(newPw) > 0) {
    if (strlen(newPw) >= 8 && strlen(newPw) < sizeof(password)) {
      strlcpy(password, newPw, sizeof(password));
      restart = true;
    } else {
      Serial.println(F("Device password ignored: 8..31 characters required"));
    }
  }
  saveConfig();

  if (restart) {
    Serial.println(F("Hostname or device password changed, restarting in 3s"));
    restartAtMillis = millis() + 3000;               // let the browser receive the response first
  } else {
    MQTTclient.disconnect();                         // reconnect with the new settings
    MQTTclient.setServer(mqttConfig.server, mqttConfig.port);
  }
}

// First handler on the web server (registered before WiFiManager's own routes):
// - blocks WiFiManager's firmware upload page
// - requires a login for requests on the home network (not on the setup AP,
//   which is protected by its own password)
//
// The login is checked in handle(), NOT in canHandle(): the web server selects
// the handler before it has parsed the request headers, so in canHandle() the
// Authorization header would still be the one of the previous request.
// On the home network the gate therefore takes every request and, once the
// login is valid, passes it on to the next matching handler itself.
#if defined(ESP32)
using WmServer = WebServer;
#else
using WmServer = ESP8266WebServer;
#endif

class AccessGate : public RequestHandler {
 public:
  explicit AccessGate(WmServer* s) : server(s) {}

  bool canHandle(HTTPMethod method, const String& uri) override {
    (void)method;
    return isBlocked(uri) || !onSetupAp();
  }
  bool canUpload(const String& uri) override { return isBlocked(uri); }   // swallow upload data
#if defined(ESP32)
  // ESP32 core 3.x only calls these overloads; their defaults return false
  // instead of falling back to the ones above
  bool canHandle(WebServer& srv, HTTPMethod method, const String& uri) override {
    (void)srv;
    return canHandle(method, uri);
  }
  bool canUpload(WebServer& srv, const String& uri) override {
    (void)srv;
    return canUpload(uri);
  }
#endif
  bool handle(WmServer& srv, HTTPMethod method, const String& uri) override {
    if (isBlocked(uri)) {
      srv.send(404, "text/plain", "Firmware upload via the web page is disabled, use OTA");
      return true;
    }
    if (!srv.authenticate(WEB_LOGIN_USER, password)) {
      srv.requestAuthentication(BASIC_AUTH, deviceHostname());
      return true;
    }
    for (RequestHandler* h = next(); h; h = h->next()) {
#if defined(ESP32)
      if (h->canHandle(srv, method, uri)) return h->handle(srv, method, uri);
#else
      if (h->canHandle(method, uri)) return h->handle(srv, method, uri);
#endif
    }
    srv.send(404, "text/plain", "Not found");
    return true;
  }

 private:
  WmServer* server;
  static bool isBlocked(const String& uri) { return uri == "/update" || uri == "/u"; }
  bool onSetupAp() {
    return (WiFi.getMode() & WIFI_AP) && server->client().localIP() == WiFi.softAPIP();
  }
};

static void onWebServerSetup() {
  // the server object is recreated for every portal start, add our handlers again
  wm.server->addHandler(new AccessGate(wm.server.get()));
  wm.server->on("/status", []() { wm.server->send(200, "text/html", webStatusPage()); });
}

static void startPortal(const __FlashStringHelper* reason) {
  Serial.printf_P(PSTR("Starting setup AP \"%s\" (%s)\n"), setupApSsid, (const char*)reason);
  wm.startConfigPortal(setupApSsid, password);     // non-blocking, served by wm.process()
  Serial.printf_P(PSTR("Setup page: http://%s/\n"), WiFi.softAPIP().toString().c_str());
  lastRetryMillis = millis();
}

void wifiConfigBegin() {
  loadConfig();
  snprintf(setupApSsid, sizeof(setupApSsid), "%s-Setup", hostname);

  snprintf(portStr, sizeof(portStr), "%u", mqttConfig.port);
  paramServer   = new WiFiManagerParameter("mqtt_server", "Broker (host or IP)", mqttConfig.server, sizeof(mqttConfig.server) - 1);
  paramPort     = new WiFiManagerParameter("mqtt_port", "Port", portStr, 5, "type='number' min='1' max='65535'");
  paramUser     = new WiFiManagerParameter("mqtt_user", "User (optional)", mqttConfig.user, sizeof(mqttConfig.user) - 1);
  paramPassword = new WiFiManagerParameter("mqtt_pass", "Password (empty = keep current)", "", sizeof(mqttConfig.password) - 1, "type='password'");
  paramHostname = new WiFiManagerParameter("hostname", "Hostname (also MQTT topic root, restarts the device)", hostname, sizeof(hostname) - 1, "pattern='[A-Za-z0-9-]+'");
  paramDevicePassword = new WiFiManagerParameter("dev_pass", "New device password (setup AP, login, OTA; empty = keep)", "", sizeof(password) - 1, "type='password' minlength='8'");
  wm.addParameter(&paramMqttHeader);
  wm.addParameter(paramServer);
  wm.addParameter(paramPort);
  wm.addParameter(paramUser);
  wm.addParameter(paramPassword);
  wm.addParameter(&paramDeviceHeader);
  wm.addParameter(paramHostname);
  wm.addParameter(paramDevicePassword);

  wm.setTitle(hostname);
  wm.setDarkMode(true);                            // also inverts the WiFi signal icons
  wm.setCustomHeadElement(WEB_THEME);
  wm.setHostname(hostname);
  wm.setConfigPortalBlocking(false);
  wm.setBreakAfterConfig(true);                    // run the save callback even if the WiFi connect fails
  wm.setSaveParamsCallback(onSaveParams);
  wm.setSaveConfigCallback(onSaveParams);
  wm.setWebServerCallback(onWebServerSetup);
  wm.setShowInfoUpdate(false);                     // no firmware upload button (route blocked as well)
  wm.setConnectTimeout(20);
  wm.setCustomMenuHTML("<form action='/status' method='get'><button>AC status</button></form><br/>\n");
  static std::vector<const char*> menu = {"custom", "wifi", "param", "info", "sep", "restart"};
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

  if (restartAtMillis && (long)(millis() - restartAtMillis) >= 0) {
    Serial.println(F("Restarting to apply the new settings"));
    delay(100);
    ESP.restart();
  }

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
      // settings page also reachable on the home network (login required)
      wm.startWebPortal();
      Serial.printf_P(PSTR("Settings page: http://%s/ (user '" WEB_LOGIN_USER "', device password)\n"), WiFi.localIP().toString().c_str());
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
      // don't rely on the SDK auto-reconnect alone: it gives up after an
      // explicit WiFi.disconnect() and some disconnect reasons
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
