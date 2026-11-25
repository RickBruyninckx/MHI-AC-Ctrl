#pragma once

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"

#define VERSION "2.9"

#ifndef HOSTNAME
#define HOSTNAME "MHI-AC-Ctrl"                      // default hostname, can be changed on the settings page
#endif

// MQTT defaults, used until settings are saved via the WiFi setup page
#ifndef MQTT_SERVER
#define MQTT_SERVER "192.168.178.111"               // broker name or IP address of the broker
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883                              // port number used by the broker
#endif
#ifndef MQTT_USER
#define MQTT_USER ""                                // if authentication is not used, leave it empty
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""                            // if authentication is not used, leave it empty
#endif
#ifndef MQTT_RETRY_INTERVAL_S
#define MQTT_RETRY_INTERVAL_S 10                    // seconds between MQTT connect attempts while the broker is unreachable
#endif
// MQTT topics: "<hostname>/<topic>", set commands "<hostname>/set/<topic>",
// operating data "<hostname>/OpData/<topic>", "<hostname>/ErrOpData/<topic>" (see wifi_config.h)

#ifndef HA_DISCOVERY
#define HA_DISCOVERY 1                              // publish Home Assistant MQTT discovery config (climate entity + sensors)
#endif
#ifndef HA_DISCOVERY_PREFIX
#define HA_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""                             // "" = use the device password (see wifi_config.h)
#endif

#define TEMP_MEASURE_PERIOD 0                       // period in seconds for temperature measurement with the external DS18x20 temperature sensor
                                                    // enter 0 if you don't use the DS18x20 
#ifndef ONE_WIRE_BUS
#define ONE_WIRE_BUS 4                              // D2, PIN for connecting temperature sensor DS18x20 DQ pin
#endif
#define ROOM_TEMP_DS18X20_OFFSET 0.0                // Temperature offset for DS18x20 sensor, can be positive or negative (examples: 0.0, -1.0, 1.5)

//#define ROOM_TEMP_DS18X20                           // use room temperature from DS18x20

#define ROOM_TEMP_MQTT_SET_TIMEOUT  40              // time in seconds, after this time w/o receiving a valid room temperature
                                                    // via MQTT fallback to IU temperature sensor value

#ifndef POWERON_WHEN_CHANGING_MODE
#define POWERON_WHEN_CHANGING_MODE 1                // 1: setting a mode switches the AC on, Mode "Off" switches it off and is
#endif                                              // published as Mode when the AC is off (needed for Home Assistant). 0 to disable

#define TROOM_FILTER_LIMIT 0.25                     // Defines from which Troom delta value a new Troom value is pubslised. Resolution 0.25°C.
                                                    // With a smaller resolution, Troom could toggle more. So deactivate the filter use 0.
//#define ENHANCED_RESOLUTION true                    // when using Tsetpoint with x.5 degrees, airco will use (x+1).0 setpoint
                                                    // uncomment this to compensatie (offset) Troom for this.
                                                    // this will simulate .x degrees resolution
#ifndef CONTINUE_WITHOUT_MQTT
#define CONTINUE_WITHOUT_MQTT 1                     // 1: communication with AC continues when MQTT or WiFi connection is disconnected. 0 to disable
#endif
                                                    // When Troom is supplied from external, it will fallback to AC internal Troom temperature sensor
                                                    // When ROOM_TEMP_DS18X20 is used, it will use room temperature from DS18x20
//#define USE_EXTENDED_FRAME_SIZE true                // uncomment if you want to use de extended frame size (33) which is used by the WF-RAC module
                                                    // Then it will be possible to get and set the 3D auto and vanes left/right



// *** The configuration ends here ***

#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif        // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <PubSubClient.h>       // https://github.com/hmueller01/pubsubclient3
#include <ArduinoOTA.h>         // https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA

#if TEMP_MEASURE_PERIOD > 0
#include <OneWire.h>            // https://www.pjrc.com/teensy/td_libs_OneWire.html
#include <DallasTemperature.h>  // https://github.com/milesburton/Arduino-Temperature-Control-Library
#endif

#ifdef ROOM_TEMP_DS18X20
#if (TEMP_MEASURE_PERIOD == 0)
#error "You have to use a value>0 for TEMP_MEASURE_PERIOD when you want to use DS18x20 as an external temperature sensor"
#endif
#endif

extern PubSubClient MQTTclient;
extern int WIFI_lost;
extern unsigned long lastAcFrameMillis;                       // millis() of the last valid frame from the AC
extern bool acFrameSeen;                                      // at least one valid frame received

#include "wifi_config.h"

void MeasureFrequency();                                      // measures the frequency of the SPI pins
int MQTTreconnect();                                          // (re)connect to MQTT broker
void publish_cmd_ok();                                        // last MQTT cmd was o.k.
void publish_cmd_unknown();                                   // last MQTT cmd was unknown
void publish_cmd_invalidparameter();                          // a paramter of the last MQTT was wrong
void output_P(ACStatus status, PGM_P topic, PGM_P payload);   // publish via MQTT

void setupOTA();                                              // initialize and start OTA
void setup_ds18x20();                                         // setup the temperature measurement
byte getDs18x20Temperature(int temp_hysterese);               // read the temperature from the DS18x20 sensor

#define MQTT_NOT_CONNECTED 2
#define MQTT_RECONNECTED 1
#define MQTT_CONNECT_OK 0

#define DS18X20_NOT_CONNECTED 1