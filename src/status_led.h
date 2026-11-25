#pragma once
// Status LED
//
// LOLIN S3 mini (RGB LED on GPIO47):        D1 mini (blue LED on GPIO2):
//   yellow          connecting to WiFi        fast blink
//   blue, blinking  setup AP is open          slow blink (1 Hz)
//   orange          WiFi OK, no MQTT broker   double blink every 2 s
//   red             no data from the AC       steady on
//   dim green       everything OK             short flash every 3 s
//
// Disable with -DSTATUS_LED=0, override the pin with -DSTATUS_LED_PIN=<gpio>.

#include <Arduino.h>

#ifndef STATUS_LED
#define STATUS_LED 1
#endif

enum LedState { LED_CONNECTING, LED_SETUP_AP, LED_NO_MQTT, LED_NO_AC, LED_OK };

void statusLedBegin();
void statusLedSet(LedState state);   // cheap to call every loop, only acts on changes
void statusLedLoop();                // drives blinking, call every loop
