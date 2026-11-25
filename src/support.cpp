#include "support.h"
#include "web_status.h"
#include <Arduino.h>

WiFiClient espClient;
PubSubClient MQTTclient(espClient);
int WIFI_lost = 0;
int MQTT_lost = 0;

struct rising_edge_cnt_struct{
  volatile uint32_t SCK = 0;
  volatile uint32_t MOSI = 0;
  volatile uint32_t MISO = 0;
} rising_edge_cnt;

IRAM_ATTR void handleInterrupt_SCK() {
  rising_edge_cnt.SCK = rising_edge_cnt.SCK + 1;
}

IRAM_ATTR void handleInterrupt_MOSI() {
  rising_edge_cnt.MOSI = rising_edge_cnt.MOSI + 1;
}

IRAM_ATTR void handleInterrupt_MISO() {
  rising_edge_cnt.MISO = rising_edge_cnt.MISO + 1;
}

void MeasureFrequency() {  // measure the frequency on the pins
  pinMode(SCK_PIN, INPUT);
  pinMode(MOSI_PIN, INPUT);
  pinMode(MISO_PIN, INPUT);
  Serial.println(F("Measure frequency for SCK, MOSI and MISO pin"));
  attachInterrupt(digitalPinToInterrupt(SCK_PIN), handleInterrupt_SCK, RISING);
  attachInterrupt(digitalPinToInterrupt(MOSI_PIN), handleInterrupt_MOSI, RISING);
  attachInterrupt(digitalPinToInterrupt(MISO_PIN), handleInterrupt_MISO, RISING);
  unsigned long starttimeMicros = micros();
  while (micros() - starttimeMicros < 1000000);
  detachInterrupt(SCK_PIN);
  detachInterrupt(MOSI_PIN);
  detachInterrupt(MISO_PIN);

  Serial.printf_P(PSTR("SCK frequency=%luHz (expected: >3000Hz) "), (unsigned long)rising_edge_cnt.SCK);
  if (rising_edge_cnt.SCK > 3000)
    Serial.println(F("o.k."));
  else
    Serial.println(F("out of range!"));

  Serial.printf("MOSI frequency=%luHz (expected: <SCK frequency) ", (unsigned long)rising_edge_cnt.MOSI);
  if ((rising_edge_cnt.MOSI > 30) & (rising_edge_cnt.MOSI < rising_edge_cnt.SCK))
    Serial.println(F("o.k."));
  else
    Serial.println(F("out of range!"));

  Serial.printf("MISO frequency=%luHz (expected: ~0Hz) ", (unsigned long)rising_edge_cnt.MISO);
  if (rising_edge_cnt.MISO <= 10) {
    Serial.println(F("o.k."));
  }
  else {
    // the original code stopped here forever (while(1)), which also made WiFi,
    // the settings page and OTA unreachable; keep running and just report it
    Serial.println(F("out of range! Check the wiring / level shifter of the MISO line"));
  }
}

#if HA_DISCOVERY
// publish one retained discovery config, streamed so it doesn't need a large MQTT buffer
static void haPublish(const String& component, const String& objectId, const String& json) {
  String topic = String(HA_DISCOVERY_PREFIX "/") + component + "/" + deviceId() + "/" + objectId + "/config";
  MQTTclient.beginPublish(topic.c_str(), json.length(), true);
  MQTTclient.print(json);
  MQTTclient.endPublish();
}

static void publishHomeAssistantDiscovery() {
  const String p = mqttPrefix();
  const String id = String("mhiac_") + deviceId();
  const String dev = String(",\"avty_t\":\"") + p + TOPIC_CONNECTED +
    "\",\"pl_avail\":\"" PAYLOAD_CONNECTED_TRUE "\",\"pl_not_avail\":\"" PAYLOAD_CONNECTED_FALSE "\"" +
    ",\"dev\":{\"ids\":[\"" + id + "\"],\"name\":\"" + deviceHostname() +
    "\",\"mdl\":\"MHI-AC-Ctrl\",\"mf\":\"Mitsubishi Heavy Industries\",\"sw\":\"" VERSION "\"}}";

  // climate entity: mode (incl. off), target temperature, room temperature, fan, vanes
  haPublish("climate", "ac", String("{\"name\":null,\"uniq_id\":\"") + id + "_ac\"" +
    ",\"modes\":[\"off\",\"auto\",\"cool\",\"dry\",\"fan_only\",\"heat\"]" +
    ",\"mode_cmd_t\":\"" + p + "set/" TOPIC_MODE "\"" +
    ",\"mode_cmd_tpl\":\"{{ {'off':'" PAYLOAD_MODE_OFF "','auto':'" PAYLOAD_MODE_AUTO "','cool':'" PAYLOAD_MODE_COOL
      "','dry':'" PAYLOAD_MODE_DRY "','fan_only':'" PAYLOAD_MODE_FAN "','heat':'" PAYLOAD_MODE_HEAT "'}[value] }}\"" +
    ",\"mode_stat_t\":\"" + p + TOPIC_MODE "\"" +
    ",\"mode_stat_tpl\":\"{{ {'" PAYLOAD_MODE_OFF "':'off','" PAYLOAD_MODE_AUTO "':'auto','" PAYLOAD_MODE_COOL "':'cool','"
      PAYLOAD_MODE_DRY "':'dry','" PAYLOAD_MODE_FAN "':'fan_only','" PAYLOAD_MODE_HEAT "':'heat'}.get(value, 'off') }}\"" +
    ",\"temp_cmd_t\":\"" + p + "set/" TOPIC_TSETPOINT "\",\"temp_stat_t\":\"" + p + TOPIC_TSETPOINT "\"" +
    ",\"curr_temp_t\":\"" + p + TOPIC_TROOM "\"" +
    ",\"min_temp\":18,\"max_temp\":30,\"temp_step\":0.5,\"precision\":0.5,\"temp_unit\":\"C\"" +
    ",\"fan_modes\":[\"1\",\"2\",\"3\",\"4\",\"" PAYLOAD_FAN_AUTO "\"]" +
    ",\"fan_mode_cmd_t\":\"" + p + "set/" TOPIC_FAN "\",\"fan_mode_stat_t\":\"" + p + TOPIC_FAN "\"" +
    ",\"swing_modes\":[\"1\",\"2\",\"3\",\"4\",\"" PAYLOAD_VANES_SWING "\"]" +
    ",\"swing_mode_cmd_t\":\"" + p + "set/" TOPIC_VANES "\",\"swing_mode_stat_t\":\"" + p + TOPIC_VANES "\"" + dev);

  haPublish("sensor", "outdoor", String("{\"name\":\"Outdoor temperature\",\"uniq_id\":\"") + id + "_outdoor\"" +
    ",\"stat_t\":\"" + mqttOpPrefix() + TOPIC_OUTDOOR "\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\u00b0C\",\"stat_cla\":\"measurement\"" + dev);
  haPublish("sensor", "energy", String("{\"name\":\"Energy\",\"uniq_id\":\"") + id + "_energy\"" +
    ",\"stat_t\":\"" + mqttOpPrefix() + TOPIC_KWH "\",\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\"" + dev);
  haPublish("sensor", "error", String("{\"name\":\"Error code\",\"uniq_id\":\"") + id + "_error\"" +
    ",\"stat_t\":\"" + p + TOPIC_ERRORCODE "\",\"ic\":\"mdi:alert-circle-outline\",\"ent_cat\":\"diagnostic\"" + dev);
  haPublish("sensor", "rssi", String("{\"name\":\"WiFi signal\",\"uniq_id\":\"") + id + "_rssi\"" +
    ",\"stat_t\":\"" + p + TOPIC_RSSI "\",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"ent_cat\":\"diagnostic\"" + dev);
  Serial.println(F("Home Assistant discovery published"));
}
#endif

int MQTTreconnect() {
  char strtmp[50];
  static int reconnect_trials=0;
  static unsigned long lastAttemptMillis = 0;
  static bool firstAttempt = true;
  if(!MQTTclient.connected()) {
    // a failed connect blocks for a few seconds, so don't retry on every loop
    // (keeps the settings page, OTA and WiFi handling responsive without a broker)
    if (!firstAttempt && millis() - lastAttemptMillis < MQTT_RETRY_INTERVAL_S * 1000UL)
      return MQTT_NOT_CONNECTED;
    firstAttempt = false;
    lastAttemptMillis = millis();
    Serial.printf("MQTTreconnect(): Attempting MQTT connection to %s:%u (MQTTclient.state=%i), WiFi.status()=%i ...\n", mqttConfig.server, mqttConfig.port, MQTTclient.state(), (int)WiFi.status());  // state(), see https://pubsubclient.knolleary.net/api#state
    if(reconnect_trials++>9){
      // the original code dropped WiFi here (work around for https://github.com/esp8266/Arduino/issues/7432);
      // only reset the TCP socket instead, so a missing/broken broker never takes WiFi down
      Serial.printf("MQTTreconnect(): reconnect_trials=%i, resetting MQTT socket\n", reconnect_trials);
      espClient.stop();
      reconnect_trials=0;
    }

    String willTopic = String(mqttPrefix()) + TOPIC_CONNECTED;
    if (MQTTclient.connect(deviceHostname(), mqttConfig.user[0] ? mqttConfig.user : nullptr, mqttConfig.user[0] ? mqttConfig.password : nullptr, willTopic.c_str(), 0, true, PAYLOAD_CONNECTED_FALSE)) {
      Serial.println(F(" connected"));
      Serial.printf("MQTTclient.connected=%i\n", MQTTclient.connected());
      reconnect_trials=0;
      output_P((ACStatus)type_status, PSTR(TOPIC_CONNECTED), PSTR(PAYLOAD_CONNECTED_TRUE));
      output_P((ACStatus)type_status, PSTR(TOPIC_VERSION), PSTR(VERSION));
      itoa(WiFi.RSSI(), strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_RSSI), strtmp);
      itoa(WIFI_lost, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_WIFI_LOST), strtmp);
      itoa(MQTT_lost, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_MQTT_LOST), strtmp);
      WiFi.BSSIDstr().toCharArray(strtmp, 20);
      output_P((ACStatus)type_status, PSTR(TOPIC_WIFI_BSSID), strtmp);

      utoa(rising_edge_cnt.SCK, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_FSCK), strtmp);
      utoa(rising_edge_cnt.MOSI, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_FMOSI), strtmp);
      utoa(rising_edge_cnt.MISO, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_FMISO), strtmp);
      
      MQTTclient.subscribe((String(mqttSetPrefix()) + "#").c_str());
#if HA_DISCOVERY
      publishHomeAssistantDiscovery();
#endif
      return MQTT_RECONNECTED;
    }
    else {
      Serial.print(F(" reconnect failed, reason "));
      itoa(MQTTclient.state(), strtmp, 10);
      Serial.print(strtmp);
      Serial.print(", WiFi status: ");
      Serial.println((int)WiFi.status());
      return MQTT_NOT_CONNECTED;
    }
  }
  MQTTclient.loop();
  return MQTT_CONNECTED;
}

void publish_cmd_ok() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CMD_RECEIVED), PSTR(PAYLOAD_CMD_OK));
}
void publish_cmd_unknown() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CMD_RECEIVED), PSTR(PAYLOAD_CMD_UNKNOWN));
}
void publish_cmd_invalidparameter() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CMD_RECEIVED), PSTR(PAYLOAD_CMD_INVALID_PARAMETER));
}

void output_P(const ACStatus status, PGM_P topic, PGM_P payload) {
  const int mqtt_topic_size = 100;
  char mqtt_topic[mqtt_topic_size];

  Serial.printf_P(PSTR("status=%i topic=%s payload=%s\n"), (int)status, topic, payload);

  WebStatusGroup group = WS_STATUS;
  const char* prefix = mqttPrefix();
  if ((status & 0xc0) == type_opdata) {
    prefix = mqttOpPrefix();
    group = WS_OPDATA;
  }
  else if ((status & 0xc0) == type_erropdata) {
    prefix = mqttErrOpPrefix();
    group = WS_ERROPDATA;
  }
  strlcpy(mqtt_topic, prefix, mqtt_topic_size);
  strncat_P(mqtt_topic, topic, mqtt_topic_size - strlen(mqtt_topic) - 1);
  webStatusRecord(group, String(FPSTR(topic)), String(FPSTR(payload)));
  if (MQTTclient.connected())
    MQTTclient.publish_P(mqtt_topic, payload, true);
}
#if TEMP_MEASURE_PERIOD > 0
OneWire oneWire(ONE_WIRE_BUS);       // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature.
DeviceAddress insideThermometer;     // arrays to hold device address

byte getDs18x20Temperature(int temp_hysterese) {
  static unsigned long DS1820Millis = millis();
  static int16_t tempR_old = 0xffff;

  if (millis() - DS1820Millis > TEMP_MEASURE_PERIOD * 1000) {
    int16_t tempR = sensors.getTemp(insideThermometer);
    if (tempR == DEVICE_DISCONNECTED_RAW) {
      tempR_old = tempR;
      return DS18X20_NOT_CONNECTED;
    }
    tempR += ROOM_TEMP_DS18X20_OFFSET*128;
    if (tempR > (48*128) || tempR < (-10*128)) {    // skip onrealistic values
      tempR = tempR_old;    // use previous value
    }
    int16_t tempR_diff = tempR - tempR_old; // avoid using other functions inside the brackets of abs, see https://www.arduino.cc/reference/en/language/functions/math/abs/
    if (abs(tempR_diff) > temp_hysterese) {
      tempR_old = tempR;
      char strtmp[10];
      dtostrf(sensors.rawToCelsius(tempR), 0, 2, strtmp);
      //Serial.printf_P(PSTR("new DS18x20 temperature=%s°C\n"), strtmp);
      output_P((ACStatus)type_status, PSTR(TOPIC_TDS1820), strtmp);
    }
    DS1820Millis = millis();
    sensors.requestTemperatures();
  }
  //Serial.printf_P(PSTR("temp DS18x20 tempR_old=%i %i\n"), tempR_old, (byte)(tempR_old/32 + 61));
  if(tempR_old < 0)
    return 0;
  return tempR_old/32 + 61;
}

void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16)
      Serial.print(F("0"));
    Serial.print(deviceAddress[i], HEX);
  }
}

void setup_ds18x20() {
  sensors.begin();
  Serial.printf_P(PSTR("Found %u DS18xxx family devices.\n"), (unsigned)sensors.getDS18Count());
  if (!sensors.getAddress(insideThermometer, 0))
    Serial.println(F("Unable to find address for Device 0"));
  else
  {
    Serial.print(F("Device 0 Address: 0x"));
    printAddress(insideThermometer);
    Serial.println();
  }
  sensors.setResolution(insideThermometer, 9); // set the resolution to 9 bit
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures(); // Send the command to get temperatures
}
#endif

void setupOTA() {
  ArduinoOTA.setHostname(deviceHostname());
  // OTA always requires a password: OTA_PASSWORD if set at build time, otherwise the device password
  ArduinoOTA.setPassword(strlen(OTA_PASSWORD) > 0 ? OTA_PASSWORD : devicePassword());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\nEnd"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf_P(PSTR("Progress: %u%%\n"), (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf_P(PSTR("OTA Error[%u]\n"), (unsigned)error);
    if (error == OTA_AUTH_ERROR)
      Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)
      Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR)
      Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR)
      Serial.println(F("End Failed"));
  });
  ArduinoOTA.begin();
  Serial.println(F("OTA Ready"));
}
