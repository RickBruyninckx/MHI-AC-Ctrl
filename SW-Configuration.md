# Introduction
The following descriptions address:
1. Basic settings for your configuration
2. Advanced settings for other frameworks (e.g. Tasmota, Home Assistant, Homie)
3. Integration examples

# Basic settings
WiFi and the MQTT broker are configured at runtime on the device's settings page (see [WiFi](#wifi-setup-access-point)).
All other settings are `#define`s. Most of them can be overridden without editing the sources by adding `-D` build flags in `secrets.ini` (copy [secrets.ini.example](secrets.ini.example)), e.g. `'-DMQTT_SERVER="192.168.1.10"'`.
The defaults are located in:
- [support.h](src/support.h) for general settings related to MQTT, OTA and the external temperature sensor DS18x20
- [wifi_config.h](src/wifi_config.h) for the WiFi setup access point
- [status_led.h](src/status_led.h) for the status LED
- [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h) for input / output settings (i.e. topic / payload text)
- [MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h) for settings related to the behaviour of MHI-AC-Ctrl (e.g. selection of operating data)

## WiFi (setup access point)
WiFi credentials are not compiled into the firmware. They are entered on a setup page served by the device itself ([wifi_config.h](src/wifi_config.h), based on [WiFiManager](https://github.com/tzapu/WiFiManager)):

1. When no WiFi credentials are saved, the device opens the access point **`<hostname>-Setup`**, protected by the device password (see below).
2. Connect to it with a phone or laptop. The setup page opens automatically (otherwise browse to http://192.168.4.1/).
3. Choose **Configure WiFi**, pick your network, enter the password and, optionally, the MQTT broker settings, then press **Save**.
4. On the home network the same pages are served at `http://<device IP>/` and require a login: user **`admin`**, password = device password.

### Device password
One password protects the setup access point, the settings login and OTA updates.
The first matching source is used:
1. a password set on the settings page (**Setup** -> *New device password*, the device restarts),
2. `DEVICE_PASSWORD` (min. 8 characters, the same for every board),
3. derived from `DEVICE_SECRET` and the chip ID (recommended),
4. a random password generated on first boot and stored in flash.

The password and its source are printed in the serial log at every boot (`Device password (...)`).

**Derived password**: `HMAC-SHA256(key = DEVICE_SECRET, message = "MHI-AC-Ctrl:" + chip ID)`, mapped to 10 characters. The chip ID is the WiFi MAC address of the board as 12 hex digits (serial log, AC status page, router device list). Every board gets its own password, it survives a flash erase, and it cannot be calculated from the MAC address (which the setup access point broadcasts) without the secret. Compute it on a PC:
```
python tools/device_password.py 90:DA:72:74:52:90           # reads DEVICE_SECRET from secrets.ini
python tools/device_password.py 90DA72745290 --secret "..."
```
Keep a backup of `DEVICE_SECRET`; `secrets.ini` is not stored in git.
- `OTA_PASSWORD` can still be set to use a different password for OTA only.

Firmware upload via the web page (`/update`) is disabled; use OTA (`d1_mini_ota` environment with `upload_flags = --auth=<device password>`) or USB.

### Hostname
The hostname is the WiFi / OTA / MQTT client name and the root of all MQTT topics. Change it on the settings page (**Setup** -> *Hostname*, letters, digits and `-`); the device restarts. A unique hostname is required when multiple ACs are used, e.g. `Living-Room-AC`. `HOSTNAME` sets the default.

### AC status page
**AC status** in the menu (`/status`) shows the latest value of every status and operating data topic, the AC connection state and device information. It refreshes every 5 s and works without an MQTT broker.

The credentials are stored in flash and survive reboots and firmware updates.
When the WiFi connection is lost for `SETUP_AP_AFTER_OFFLINE_MIN` minutes (default 5), the setup access point is opened again.
While it is open, the saved network is retried every minute (only when nobody is connected to the setup access point) and the access point closes as soon as the connection is back.

Build-time options (e.g. in `secrets.ini`, see [secrets.ini.example](secrets.ini.example)):
```cpp
#define HOSTNAME "MHI-AC-Ctrl"                  // default hostname; setup AP name is "<hostname>-Setup"
#define DEVICE_PASSWORD ""                      // "" = random password on first boot, otherwise min. 8 characters
#define SETUP_AP_AFTER_OFFLINE_MIN 5            // minutes offline before the setup AP is opened
```

## MQTT ([support.h](src/support.h))
The program uses the MQTT client library [PubSubClient3](https://github.com/hmueller01/pubsubclient3) from Holger Müller (hmueller01), originally written by Nick O'Leary (knolleary).
If you are not familiar with MQTT you find on the Internet endless numbers of descriptions and tutorials. My favorites are [here](https://www.hivemq.com/blog/how-to-get-started-with-mqtt/) and [here](https://www.heise.de/developer/artikel/Kommunikation-ueber-MQTT-3238975.html).
I recommend [MQTT Explorer](http://mqtt-explorer.com/) a great all-round MQTT client that provides a structured topic overview for the first steps.

### MQTT General Settings (broker, port, account data)
The broker settings are entered on the WiFi setup page (see above) and stored in flash.
The values below are only used as defaults until settings have been saved there:

```cpp
#define MQTT_SERVER "MQTT broker name"  // broker name or IP address of the broker
#define MQTT_PORT 1883                  // port number used by the broker
```

If you want to use MQTT authentication enter user name and password:
```cpp
#define MQTT_USER ""          // if authentication is not used, leave it empty
#define MQTT_PASSWORD ""      // if authentication is not used, leave it empty
```
Note: TLS/SSL is not supported

While the broker is unreachable, a connection attempt is made every `MQTT_RETRY_INTERVAL_S` seconds (default 10). WiFi stays connected; after 10 failed attempts only the MQTT socket is reset.
```cpp
#define MQTT_RETRY_INTERVAL_S 10   // seconds between MQTT connect attempts while the broker is unreachable
```

The following sections show the configuration for the MQTT paths.

### MQTT status
The topic level for status information from the AC consists of the hostname and the function name separated by a slash, e.g. `MHI-AC-Ctrl/Power`.
For writes the topic level consists of the hostname, `set` and the function name, each separated by a slash, e.g. `MHI-AC-Ctrl/set/Power`.
Operating data is published below `<hostname>/OpData/`, operating data of the last error below `<hostname>/ErrOpData/`.
The hostname is set on the settings page (see [Hostname](#hostname)).
Please pay attention to the case sensitivity.

The following status data is available (prefix is not listed).
They are only published when there is a change of the message. The retained flag is `true`.
When writing data, the retain flag shall be `false`!

topic|r/w|value|comment
-----|---|-----|------
Power|r/w|"On", "Off"|
Mode|r/w|"Auto", "Dry", "Cool", "Fan", "Heat" and "Off"|"Off" is only supported when option [POWERON_WHEN_CHANGING_MODE](#behaviour-when-changing-ac-mode-supporth) is enabled (default).
Tsetpoint|r/w|18 ... 30|Target room temperature (float) in °C, resolution is 0.5°C
Fan|r/w|1,2,3,4,"Auto"|Fan level
Vanes|r/w|1,2,3,4,"Swing","?"|Vanes up/down position <sup>1</sup>
Troom|r/w|0 ... 35|Room temperature (float) in °C, resolution is 0.25°C <sup>2</sup>
Tds1820|r|-40 .. 85|Temperature (float) by the additional DS18x20 sensor in °C, resolution is 0.5°C <sup>3</sup>
Errorcode|r|0 .. 255|error code (unsigned int)
ErrOpData|w||triggers the reading of last error operating data
VanesLR|r/w|1,2,3,4,5,6,7,"Swing"|Vanes left/right position <sup>4</sup>
3Dauto|r/w|"On", "Off"|3D auto only works for mode Auto, Cool and heat <sup>4</sup>

<sup>1</sup> When the last command was received via the infrared remote control then the Vanes status is unknown and the `?` is published.
<sup>2</sup> Please compare with section [Room temperature](#room-temperature) for writing.
<sup>3</sup> Only available when a DS18x20 is connected, please see the description in [Hardware.md](Hardware.md) and in section [External Temperature Sensor Settings](#external-temperature-sensor-settings-supporth).
<sup>4</sup> Only available if USE_EXTENDED_FRAME_SIZE is enabled in [support.h](src/support.h).

Additionally, the following program status topics are available:

topic    |r/w| value |comment
---------|---|---|---
cmd_received|r|"o.k.", "unknown command" or "invalid parameter"|feedback for last set command
connected|r  |0, 1|MQTT connection status to broker
fMISO    |r  |unsigned integer|frequency of the MISO pin in Hz during boot
fMOSI    |r  |unsigned integer|frequency of the MOSI pin in Hz during boot
fSCK     |r  |unsigned integer|frequency of the SCK pin in Hz during boot
reset|w|"reset"|resets the ESP8266 / ESP32
RSSI     |r  |integer         |WiFI RSSI / signal Strength in dBm after MQTT (re-)connect
Version  |r  |string          |Version number of MHI-AC-Ctrl
WIFI_LOST|r  |integer         |number of lost WiFi connections since last reset
MQTT_LOST|r  |integer         |number of lost MQTT connections since last reset

Note: The topic and the payload text of the status data is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).

### MQTT operating data
MHI-AC-Ctrl can provide operating data of the indoor and outdoor unit. This data is not needed for daily use, but might be interesting in specific use cases. Operating data is only published when there is a change of the content. The retained flag is `true`.
The path to the operating data topic can be adapted.

```cpp
#define MQTT_OP_PREFIX "OpData/"    // prefix for publishing operating data
```

Without changes of the path, subscribe to `MHI-AC-Ctrl/OpData/#` for receiving all operating data. Please see section [Operating data](#operating-data-mhi-ac-ctrl-coreh) to find all supported operating data.

Note: The topic and the payload text is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).

### MQTT last error operating data
When an error in the AC occurs, some operating data of this error are stored in the AC and can be read out.
The path to the operating data topic is defined in
```cpp
#define MQTT_OP_PREFIX "ErrOpData/"    // prefix for publishing operating data from last error
```
The readout of last error operating data is triggered by publishing `ErrOpData` to topic ErrOpData. Not all of the operating data from section [Operating data](#operating-data-mhi-ac-ctrl-coreh) might be available as last error operating data.

Note: The topic and the payload text is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).

### MQTT operating data PROTECTION-NO topic
This is the Protection state number of the compressor (Compressor protection status).
The meaning of this numeric value is:

Value |meaning
------|-----
0  |Normal
1  |Discharge pipe temperature protection control
2  |Discharge pipe temperature anomaly
3  |Current safe control of inverter primary current
4  |High pressure protection control
5  |High pressure anomaly
6  |Low pressure protection control
7  |Low pressure anomaly
8  |Anti-frost prevention control
9  |Current cut
10 |Power transistor protection control
11 |Power transistor anomaly (Overheat)
12 |Compression ratio control
13 |-
14 |Condensation prevention control
15 |Current safe control of inverter secondary current
16 |Stop by compressor rotor lock
17 |Stop by compressor startup failure

## OTA Settings ([support.h](src/support.h))
OTA (Over the Air) update is the process of loading the firmware to ESP module using Wi-Fi connection rather than a serial port.
The OTA hostname is the hostname. OTA always requires a password: the device password, unless `OTA_PASSWORD` is set.
```cpp
#define OTA_PASSWORD ""           // "" = use the device password
```
With PlatformIO pass it via `upload_flags = --auth=<password>` in the `d1_mini_ota` environment (see [secrets.ini.example](secrets.ini.example)).

## External Temperature Sensor Settings ([support.h](src/support.h))
When an external temperature sensor is connected, you can configure the pin where DQ of the the DS18x20 is connected, default is Pin 4 (D2)
and how often the sensor should be read. To use reading of the external sensor you must adapt the `TEMP_MEASURE_PERIOD`.

```cpp
#define TEMP_MEASURE_PERIOD 0   // period in seconds for temperature measurement with the external DS18x20 temperature sensor
                                // set to e.g. 30 to read the sensor every 30 seconds.
#define ONE_WIRE_BUS 4          // D2, PIN for connecting temperature sensor DS18x20 DQ pin (overridable per board in platformio.ini)
#define ROOM_TEMP_DS18X20_OFFSET 0.0   // Temperature offset for DS18x20 sensor, can be positive or negative (examples: 0.0, -1.0, 1.5)
```
Note: the according libraries [OneWire](https://www.pjrc.com/teensy/td_libs_OneWire.html) and [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) are only used if `TEMP_MEASURE_PERIOD > 0`.

If the DS18x20 should replace the room temperature sensor of the AC, you have to configure it as described in the next clause.

## Room temperature
Usage of the room temperature sensor inside the AC is the default, but instead you can use the DS18x20 sensor on the MHI-AC-Ctrl board or the received temperature via the MQTT topic `Troom`. Setting `Troom` via MQTT works per default. But you should adapt `ROOM_TEMP_MQTT_TIMEOUT`. For using DS18x20 as `Troom` you have to use `ROOM_TEMP_DS18X20`.
```cpp
//#define ROOM_TEMP_DS18X20           // use room temperature from DS18x20

#define ROOM_TEMP_MQTT_TIMEOUT  40    // time in seconds, after this time w/o receiving a valid room temperature
                                      // via MQTT fallback to IU temperature sensor value
#define TROOM_FILTER_LIMIT 0.25       // Defines from which Troom delta value a new Troom value is published. Resolution 0.25°C.
                                      // With a smaller resolution, Troom could toggle more. To deactivate the filter use 0.

```
`ROOM_TEMP_MQTT_TIMEOUT` must be greater than the period of room temperature update via MQTT. E.g. when the room temperature update via MQTT is done every minute, then `ROOM_TEMP_MQTT_TIMEOUT` could be 2 minutes.
If the timeout occurs, and the system falls back to IU temperature, it will return to using the MQTT room temperature if the MQTT messages resume.

## Enhance resolution of `Tsetpoint` ([support.h](src/support.h))
The AC is only accepting a setpoint in x.0 degrees. If you send x.5 degrees, the AC will convert this to (x+1).0 degrees. So using .5 degrees as a setpoint will increase the setpoint on the AC not with .5 degrees but with 1 degree. This behaviour can be changed with:
```cpp
#define ENHANCED_RESOLUTION true                    // when using Tsetpoint with x.5 degrees, airco will use (x+1).0 setpoint
                                                    // uncomment this to compensate (offset) Troom for this.
                                                    // this will simulate .x degrees resolution
```
If you now send x.5 degrees as setpoint, still the setpoint on the AC will be (x+1). But when sending the received `Troom` (from MQTT or the external temperature sensor) to the AC, `Troom` with an offset of .5 degrees will be send to the AC. This way the AC will increase the temperature in the room with .5 degrees instead of 1 degree.

**This behaviour won't work if you are using the internal temperature sensor of the AC!**

The MQTT topic `Troom` will show (like before) the `Troom` received by the AC (including the offset).

For example: when setpoint is 20.5. When `Troom` 19.5 is received (from MQTT or DS18x20), `Troom` sent to the AC will be 20.0. Topic `Troom` will also show 20.0.

## Behaviour when changing AC mode ([support.h](src/support.h))
Per default (in this fork) the AC is switched on once you change the AC mode, switched off if you publish `Off` to `Mode`, and `Mode` reports `Off` while the AC is off. This behaviour is needed for [Home Assistant](https://www.home-assistant.io/). `set/Power` keeps working as well.
To keep the power state unchanged when the mode is changed (original behaviour), disable it:
```cpp
#define POWERON_WHEN_CHANGING_MODE 0
```

## Home Assistant discovery ([support.h](src/support.h))
On every MQTT (re)connect the device publishes retained [MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery) configs below `homeassistant/`: a climate entity (modes off/auto/cool/dry/fan_only/heat, target temperature 18-30 °C in 0.5 steps, room temperature, fan 1-4/Auto, vanes 1-4/Swing) and sensors for outdoor temperature, energy (kWh), error code and WiFi signal. The device is available while the `connected` topic is `1`.
```cpp
#define HA_DISCOVERY 1                    // 0 to disable
#define HA_DISCOVERY_PREFIX "homeassistant"
```

## Using extended frame size for enabling 3D auto and vanes L/R ([support.h](src/support.h))
Per default the 3D auto and vanes left/right is not supported.
But when you uncomment the following line, the frame size is extended to 33 bytes (like the WF-RAC module). This will make it possible to use 3D auto and vanes L/R.
```cpp
//#define USE_EXTENDED_FRAME_SIZE true                // uncomment if you want to use de extended frame size (33) which is used by the WF-RAC module
                                                    // Then it will be possible to get and set the 3D auto and vanes left/right
```

## Passive Mode

A [known limitation](../Troubleshooting.md#known-limitations) is
that the timer of the IR remote control does no longer work
as soon as a device is connected to the SPI port.

There's a workaround for this limitation: toggle the AC from _active mode_ to _passive mode_.
In _active mode_, the default behavior of MHI-AC-Ctrl,
it is possible to both monitor and control the AC while the RC timer is not working.
In _passive mode_, the AC is monitored but cannot be controlled, and the RC timer is working.

It is possible to switch between active and passive mode via MQTT topic `set/PassiveMode`:
publish `On` to enable and `Off` to disable passive mode.
Both commands send an acknowledge value of `o.k.` to topic `cmd_received` on success.

Enabling passive mode does not have immediate effect but involves a delay of ~2 minutes.
Setting `PassiveMode` to `On` while the AC is running causes that after the delay,
the AC reports a value of `1` on topic `Errorcode` and turns off.
Now the AC is in passive mode: it cannot be controlled via MHI-AC-Ctrl anymore.
The `Errorcode` will automatically be reset to `0` as soon as the AC it turned on next time.
The RC timer is working now, i.e. pressing _SLEEP_ enlightens the indoor unit's orange LED.

Publishing `Off` to `set/PassiveMode` enables active mode immediately.
The RC timer is disabled and the AC can be controlled by MHI-AC-Ctrl again.
The AC does not power off when active mode is enabled.

# Advanced settings

## Topic and payload text ([MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h))
All topic and payload text is included in defines, e.g. change
```cpp
#define PAYLOAD_POWER_ON "On"
```
to
```cpp
#define PAYLOAD_POWER_ON "on"
```
if your framework prefers lower case. These topics and payloads are used for MQTT topics and payloads.

## Operating data ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
Currently the following operating data in double quotes are supported
```cpp
  { 0xc0, 0x02},  //  1 "MODE"
  { 0xc0, 0x05},  //  2 "SET-TEMP" [°C]
  { 0xc0, 0x80},  //  3 "RETURN-AIR" [°C]
  { 0xc0, 0x81},  //  5 "THI-R1" [°C]
  { 0x40, 0x81},  //  6 "THI-R2" [°C]
  { 0xc0, 0x87},  //  7 "THI-R3" [°C]
  { 0xc0, 0x1f},  //  8 "IU-FANSPEED"
  { 0xc0, 0x1e},  // 12 "TOTAL-IU-RUN" [h]
  { 0x40, 0x80},  // 21 "OUTDOOR" [°C]
  { 0x40, 0x82},  // 22 "THO-R1" [°C]
  { 0x40, 0x11},  // 24 "COMP" [Hz]
  { 0x40, 0x85},  // 27 "TD" [°C]
  { 0x40, 0x90},  // 29 "CT" [A]
  { 0x40, 0xb1},  // 32 "TDSH" [°C]
  { 0x40, 0x7c},  // 33 "PROTECTION-No"
  { 0x40, 0x1f},  // 34 "OU-FANSPEED"
  { 0x40, 0x0c},  // 36 "DEFROST"
  { 0x40, 0x1e},  // 37 "TOTAL-COMP-RUN" [h]
  { 0x40, 0x13},  // 38 "OU-EEV" [Puls]
  { 0xc0, 0x94},  //    "energy-used" [kWh]
```

Note 1: If you are not interested in these operating modes (e.g. to reduce the MQTT load) you can comment out the according lines. But at least 1 line has to stay.
For `THI-R2`, `THO-R1` and `TDSH` the formula for calculation is not known yet.
You can find some hints related to the meaning of the operating data [here](https://www.hrponline.co.uk/media/pdf/41/42/ed/Beijer-Ref-Service-Support-Handbook-19cWKESQUhzVIy5.pdf#page=7). Addtional opdata information is available [here](https://github.com/absalom-muc/MHI-AC-Trace/blob/main/SPI.md#operation-data-details).

Note 2: The energy-used is the energy in kWh counting from power on the AC. If you power off the AC, the value (in kWh) will keep the last value. When you power on the AC again, it will start from 0 again.

Hint: The error operating data is usually a sub-set of the operating data above. If user requests error operating data, all available error operating data is provided independent from the list above.

## Access Speed ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
Default the above operating data is requested within 400 frames. Because 20 frames takes 1 second, the update interval for all operating data will be  400/20 = 20 seconds. If you disable some operating data (above), the update interval will stay 20 seconds.
With the following parameter you can change this interval of 20 seconds.
```cpp
#define NoFramesPerOpDataCycle 400             // number of frames used for a OpData request cycle; will be 20s (20 frames are 1s)
```
Some operating data (32 -38) will take some time at the AC for processing. So don't decrease this value too much.

Changes to Power, Mode, Tsetpoint, Fan and Vanes wll be written to the AC right away. Above parameter will not influence this.

## Jitter internal temperature sensor ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
If the AC internal temperature sensor is used, the received `Troom` can changes (+/- 0.25 degrees) sometimes several times in a second. This will give a burst of MQTT `Troom` messages.
To avoid this behaviour, the changed received `Troom` will only be published after at least 5 seconds. This is ONLY the case when the AC internal temperature sensor is used.
With the following parameter you can change this minimum interval of 5 seconds.
```cpp
#define minTimeInternalTroom 5000              // minimal time in ms used for Troom internal sensor changes for publishing to avoid jitter
```

This jitter can also be avoided by using the `TROOM_FILTER_LIMIT` as descibed above. But this filter is also used if the temperature is provided by an external temperature sensor or a connected DS18B20. With above it will be also possible to see smaller changes.

## Not switching off AC when MQTT connections fails ([support.h](src/support.h))
In the original firmware the module stops communicating with the AC when the MQTT connection gets disconnected. After 120 sec the AC will power off because of [this](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/Troubleshooting.md#fire-ac-switches-power-off-sometimes).
In this fork communication with the AC continues by default (also the status page and LED keep working without a broker). To get the original behaviour back:
```cpp
#define CONTINUE_WITHOUT_MQTT 0
```

Warning: be aware that there might be some safety implication and that the deactivation of this feature is on your own risk.
The AC now keeps running and no control is possible anymore when MQTT is disconnected. Also of course no MQTT topics are updated anymore. Of course control is still possible with the remote control.

## SPI pins
The AC is connected to GPIO 14 (SCK), 13 (MOSI) and 12 (MISO), defined in [MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h). They can be overridden per board with build flags. The `lolin_s3_mini` environment in [platformio.ini](platformio.ini) uses the GPIOs at the same header positions of the MHI-AC-Ctrl PCB (see [Hardware.md](Hardware.md#using-a-wemos-lolin-s3-mini-esp32-s3)):
```ini
build_flags =
    -DSCK_PIN=12
    -DMOSI_PIN=11
    -DMISO_PIN=13
    -DONE_WIRE_BUS=35
```

## Status LED ([status_led.h](src/status_led.h))
LOLIN S3 mini (RGB LED, GPIO47) | D1 mini (blue LED, GPIO2) | Meaning
--------------------------------|---------------------------|--------
yellow | fast blink | connecting to WiFi
blue, blinking | slow blink | setup access point is open
red | steady on | WiFi OK, but no valid frame from the AC for 5 s
orange | double blink | AC OK, but MQTT broker not reachable
dim green | short flash every 3 s | everything OK

```cpp
#define STATUS_LED 1                                  // 0 disables the LED
#define STATUS_LED_PIN RGB_BUILTIN                    // or LED_BUILTIN on boards without RGB LED
#define STATUS_LED_COLOR_ORDER LED_COLOR_ORDER_GRB    // RGB LED only; use LED_COLOR_ORDER_RGB if red and green are swapped
```
Build with `-DSTATUS_LED_DEMO` to cycle through all states (4 s each) for checking the colors.

## MHI-AC-Ctrl partitioning
MHI-AC-Ctrl-core implements the core functions (SPI read/write, communication with the wrapper).
MQTT (incl. Home Assistant discovery), OTA and DS18x20 stuff is located in `support.h` and `support.cpp`, WiFi setup, login and the persisted settings (MQTT, hostname, device password) in `wifi_config.h` and `wifi_config.cpp`, the AC status page in `web_status.h` and `web_status.cpp`, the status LED in `status_led.h` and `status_led.cpp`. The styling of the web pages is in `web_theme.h`.
`main.cpp` and `MHI-AC-Ctrl.h` contain the wrapper for `MHI-AC-Ctrl-core.cpp` and `support.cpp`.

### `MHI-AC-Ctrl-core.h` and `MHI-AC-Ctrl-core.cpp`
Usually it should be not touched, only configured via [MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h).
AC status information change will trigger the callback function `cbiStatusFunction` located in [main.cpp](src/main.cpp)
It is controlled via the functions:
```cpp
void init();                          // initialization called once after boot
void reset_old_values();              // resets the 'old' variables ensuring that all status information are resend
int loop(uint max_time_ms);           // receive / transmit a frame of 20 bytes
void set_power(boolean power);        // power on/off the AC
void set_mode(ACMode mode);           // change AC mode (e.g. heat, dry, cool etc.)
void set_tsetpoint(uint tsetpoint);   // set the target temperature of the AC)
void set_fan(uint fan);               // set the requested fan speed
void set_vanes(uint vanes);           // set the vanes horizontal position (or swing)
void set_troom(byte temperature);     // set the room temperature used by AC
void set_passive_mode(bool mode);     // enable or disable passive mode
void request_ErrOpData();             // request that the AC provides the error data
```
The following sections describe the usage of these functions.

#### `void init()`
Configures the input /output state of the SPI pins. Resets old values.

### `reset_old_values()`
This should be called if you want to ensure that the receiver of the status data has the latest data. E.g. in case of a MQTT broker disconnect it should be called.

### `int loop(uint max_time_ms)`
For receiving / transmitting a frame of 20 bytes.
The input parameter is the maximum time which should be consumed by the loop function. Use a value > T<sub>Frame</sub> + T<sub>FramePause</sub> to ensure there is sufficient time to receive a frame.

This is a blocking function which takes - dependent on the AC model - about 10 ... 50ms. Inside the loop function no delay() or yield() call is used.
The following return values are supported:

return value|meaning
------------|-------------
err_msg_valid_frame     |a valid frame was received in the given time
err_msg_invalid_signature | a frame with invalid signature bytes was received
err_msg_invalid_checksum | a frame with an invalid checksum was received
err_msg_timeout_SCK_low | the specified time max_time_ms has been exceeded because SCK is const low and not toggling
err_msg_timeout_SCK_high | the specified time max_time_ms has been exceeded because SCK is const high and not toggling

Note: The input parameters and return values could be changed in future.

### `set_*()`
Controls the AC.

### `request_ErrOpData()`
The error operating data will be read upon a request via this function.

### `support.*`
Provides the interface between MHI-AC-Ctrl and the user interfaces.
It contains helper functions for serving MQTT, OTA and the external temperature sensor DS18x20.

### `wifi_config.*`
WiFi provisioning with [WiFiManager](https://github.com/tzapu/WiFiManager): setup access point, settings page (also on the home network), reconnect handling and the MQTT settings stored in flash (EEPROM area).

### `MHI-AC-Ctrl.h` and `main.cpp`
This is the wrapper for MHI-AC-Ctrl-core and support.
It provides beside the standard `setup()` and `loop()` functions the following two functions.

#### `void cbiStatusFunction(ACStatus status, int value)`
This is a member of the class `StatusHandler : public CallbackInterface_Status`. It is a callback function called by MHI-AC-Ctrl-core in case of AC status changes.

#### `void MQTT_subscribe_callback(char* topic, byte* payload, unsigned int length)`
This function is called for incoming MQTT messages, the message is analyzed and translated to function calls.
From systematic point of view this function should actually be located in [support.h](src/support.h) but in order to keep it simple it resides in [main.cpp](src/main.cpp).

# Integration examples
You find here some examples for integration of MHI-AC-Ctrl
- [Node-Red / Google Assistant](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/60)
- [openHAB](https://community.openhab.org/t/control-mhi-aircon-by-mqtt/104972)
- [IoT MQTT Panel](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/59)
- [Home Assistant](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/23)
- [Home Assistant with ESPHome](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome)
- [Tasmota](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/13#issuecomment-630425714)
- [ioBroker](https://forum.iobroker.net/topic/17041/anfrage-airconwithme-intesishome-klimasteuerung-adapter/14)
- [FHEM](https://forum.fhem.de/index.php/topic,88841.0/all.html)
- [WiFi SSID, hostname and MQTT server dynamic](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/69) (built into this fork, see [WiFi](#wifi-setup-access-point))
- [Web page with MQTT](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/141)
- [Display with AC controller](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/173)
