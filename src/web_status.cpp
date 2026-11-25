#include "support.h"
#include "web_status.h"
#include "web_theme.h"
#include <map>

static std::map<String, String> values[3];        // per WebStatusGroup: topic -> latest value

void webStatusRecord(WebStatusGroup group, const String& topic, const String& value) {
  values[group][topic] = value;
}

static void row(String& html, const String& k, const String& v) {
  html += F("<tr><td>");
  html += k;
  html += F("</td><td>");
  html += v;
  html += F("</td></tr>");
}

static void table(String& html, const __FlashStringHelper* title, WebStatusGroup group) {
  html += F("<h3>");
  html += title;
  html += F("</h3><table>");
  if (values[group].empty())
    row(html, F("&ndash;"), F("no data yet"));
  for (const auto& kv : values[group])
    row(html, kv.first, kv.second);
  html += F("</table>");
}

static String uptime() {
  unsigned long s = millis() / 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "%lud %02luh %02lum %02lus", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
  return buf;
}

String webStatusPage() {
  String html;
  html.reserve(4096);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta http-equiv='refresh' content='5'><title>");
  html += deviceHostname();
  html += F(" - AC status</title>");
  html += WEB_THEME;
  html += F("</head><body class='invert'><div class='wrap'><h1>");
  html += deviceHostname();
  html += F("</h1><h3>AC status &middot; refreshes every 5 s</h3>");

  bool acOk = acFrameSeen && (millis() - lastAcFrameMillis < 5000);
  html += F("<div class='msg ");
  html += acOk ? F("S'><strong>AC connected</strong>") : F("D'><strong>No data from the AC</strong>");
  html += F("</div>");

  table(html, F("Air conditioner"), WS_STATUS);
  table(html, F("Operating data"), WS_OPDATA);
  if (!values[WS_ERROPDATA].empty())
    table(html, F("Operating data at last error"), WS_ERROPDATA);

  html += F("<h3>Device</h3><table>");
  row(html, F("Firmware"), F("MHI-AC-Ctrl " VERSION));
  row(html, F("Uptime"), uptime());
  row(html, F("WiFi"), WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)");
  row(html, F("IP address"), WiFi.localIP().toString());
  row(html, F("MQTT broker"), String(mqttConfig.server) + ":" + String(mqttConfig.port) +
                              (MQTTclient.connected() ? F(" &ndash; connected") : F(" &ndash; not connected")));
  row(html, F("MQTT topics"), String(mqttPrefix()) + "...");
  row(html, F("Free heap"), String(ESP.getFreeHeap()) + " bytes");
  html += F("</table><br/><form action='/' method='get'><button>Back</button></form></div></body></html>");
  return html;
}
