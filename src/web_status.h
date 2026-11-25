#pragma once
// "AC status" web page: shows the latest value of every status / operating
// data topic (recorded in output_P(), so it works with and without MQTT)
// plus some device information. Served at /status by the settings web server.

#include <Arduino.h>

enum WebStatusGroup { WS_STATUS, WS_OPDATA, WS_ERROPDATA };

void webStatusRecord(WebStatusGroup group, const String& topic, const String& value);
String webStatusPage();
