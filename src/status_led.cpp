#include "status_led.h"

#if STATUS_LED

static LedState ledState = LED_CONNECTING;
static unsigned long stateSinceMillis = 0;

#if defined(ESP32) && defined(RGB_BUILTIN)
// ---------- RGB LED ----------
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN RGB_BUILTIN
#endif
// The lolin_s3_mini board definition claims RGB order, but the WS2812 on the
// tested board expects the standard GRB order (red showed up as green).
// Override with -DSTATUS_LED_COLOR_ORDER=LED_COLOR_ORDER_RGB if colors are swapped.
#ifndef STATUS_LED_COLOR_ORDER
#define STATUS_LED_COLOR_ORDER LED_COLOR_ORDER_GRB
#endif

struct Rgb { uint8_t r, g, b; };
// kept low on purpose: these LEDs are very bright
static const Rgb COLORS[] = {
  /* LED_CONNECTING */ {40, 25, 0},
  /* LED_SETUP_AP   */ {0, 0, 48},
  /* LED_NO_MQTT    */ {48, 12, 0},
  /* LED_NO_AC      */ {48, 0, 0},
  /* LED_OK         */ {0, 6, 0},
};
static int lastWritten = -1;   // index into COLORS, or -2 for "off"

static void writeLed(int idx) {
  if (idx == lastWritten) return;
  lastWritten = idx;
  if (idx < 0) rgbLedWriteOrdered(STATUS_LED_PIN, STATUS_LED_COLOR_ORDER, 0, 0, 0);
  else rgbLedWriteOrdered(STATUS_LED_PIN, STATUS_LED_COLOR_ORDER, COLORS[idx].r, COLORS[idx].g, COLORS[idx].b);
}

void statusLedBegin() { writeLed(LED_CONNECTING); }

void statusLedLoop() {
  bool on = true;
  if (ledState == LED_SETUP_AP)
    on = ((millis() - stateSinceMillis) % 1000) < 500;   // 1 Hz blink
  writeLed(on ? (int)ledState : -2);
}

#else
// ---------- single LED (D1 mini: GPIO2, active low) ----------
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN LED_BUILTIN
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 1
#endif

static int lastLevel = -1;

static void writeLed(bool on) {
  int level = (on != (bool)STATUS_LED_ACTIVE_LOW) ? HIGH : LOW;
  if (level == lastLevel) return;
  lastLevel = level;
  digitalWrite(STATUS_LED_PIN, level);
}

void statusLedBegin() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  writeLed(false);
}

void statusLedLoop() {
  unsigned long t = millis() - stateSinceMillis;
  bool on = false;
  switch (ledState) {
    case LED_CONNECTING: on = (t % 250) < 125; break;                    // fast blink
    case LED_SETUP_AP:   on = (t % 1000) < 500; break;                   // slow blink
    case LED_NO_MQTT:    { unsigned long p = t % 2000;                   // double blink
                           on = p < 100 || (p >= 250 && p < 350); } break;
    case LED_NO_AC:      on = true; break;                               // steady
    case LED_OK:         on = (t % 3000) < 30; break;                    // short flash
  }
  writeLed(on);
}
#endif

void statusLedSet(LedState state) {
  if (state == ledState) return;
  ledState = state;
  stateSinceMillis = millis();
}

#else   // STATUS_LED disabled
void statusLedBegin() {}
void statusLedSet(LedState) {}
void statusLedLoop() {}
#endif
