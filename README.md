# MHI-AC-Ctrl
Reads and writes data (e.g. power, mode, fan status etc.) from/to a Mitsubishi Heavy Industries (MHI) air conditioner (AC) via SPI controlled by MQTT. The AC is the SPI master and the ESP8266 / ESP32 is the SPI slave.

<img src="/images/IoT-MQTT-Panel.jpg" width=200 align="right" />

# About this fork
This is a fork of [absalom-muc/MHI-AC-Ctrl](https://github.com/absalom-muc/MHI-AC-Ctrl). Compared to the original it adds:
- **PlatformIO build** (plain C++ instead of an Arduino sketch) with a VS Code workspace
- **ESP32-S3 support** (WeMos LOLIN S3 mini) next to the original ESP8266 (WeMos D1 mini)
- **WiFi setup via a captive portal**: no WiFi credentials in the code; the device opens a setup access point and serves a settings page (WiFi, MQTT broker, hostname, password) with a dark theme
- **Secured settings**: login on the home network, per-device password for the setup AP, settings page and OTA, no firmware upload via the web page
- **AC status page** in the browser, works without MQTT
- **Home Assistant MQTT discovery**: the AC appears as a climate entity automatically
- **Robust reconnects**: the saved WiFi is retried while offline, the setup access point re-opens after 5 minutes offline, and an unreachable MQTT broker never drops the WiFi
- **Status LED** showing WiFi / MQTT / AC state at a glance
- **Changed defaults**: communication with the AC continues without MQTT (`CONTINUE_WITHOUT_MQTT`), mode `Off` switches the AC off (`POWERON_WHEN_CHANGING_MODE`)

The AC protocol handling (`MHI-AC-Ctrl-core`) and the MQTT topics are unchanged (except the removed `APs` topic; the topic root is now the hostname set on the settings page), so existing integrations keep working.
See [Version.md](Version.md) for the detailed list of changes.

# Attention:
:warning: You have to open the indoor unit to have access to the SPI. Opening of the indoor unit should be done by 
a qualified professional because faulty handling may cause leakage of water, electric shock or fire! :warning: 

# Prerequisites:
For use of the program you have to connect your ESP8266 (I use a LOLIN(WEMOS) D1 R2 & mini with 80 MHz) or an ESP32-S3 (WeMos LOLIN S3 mini) via a
cable connector to your air conditioner. This has to be a split device (separated indoor and outdoor unit).
I assume that all AC units of the type "SRK xx ZS-S" / "SRC xx ZS-S" are supported. I use the indoor unit SRK 35 ZS-S and the outdoor unit SRC 35 ZS-S. Users reported that additionally the following models are supported:

- [SRF xx ZJX-S1](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issue-632187165)
- [SRF xx ZMX-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-704789297)
- [SRF xx ZMXA-S](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/91)
- SRF xx ZF-W
- [SRF xx ZS-W](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/216#issue-3234769567) works with frame size 20
- [SRK xx ZJ-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/10)
- [SRK xx ZM-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/9)
- [SRK xx ZJX-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-646469621)
- [SRK xx ZJX-S1](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-646968940)
- [SRK xx ZR-WF](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/214)
- [SRK xx ZRA-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-730628655) / DXK xx ZRA-W
- [SRK xx ZSA-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-891649495) / DXK xx ZSA-W
- [SRK63ZSPR-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/149)
- SRK xx ZSXA-W/A
- [SRK xx ZSX-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/6#issuecomment-582242372)
- [SRK xx ZSX-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-643748095)
- [SRK xx ZS-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/121)
- [SRK xx ZS-WF](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/121#issuecomment-1938350129)
- [SRC 35Z SA-W]
- [DXK xx ZMA-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/201)
- [SRK 2824 T (Japanese domestic model)](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/211)

Unsupported models:
- [SRK71ZEA-S1](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/143)
- [SRK35ZC-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/154)
- [SRK xx ZSPR-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/200) (except SRK63ZSPR-S)

 
If you find out that also other models are supported that are not listed here, please give feedback so that I can expand the list. In general, please recheck if your AC has a CNS connector before you spend time and money to build the hardware.

# Installing:

## Hardware:
The ESP8266 is powered from the AC via DC-DC (12V -> 5V) converter. 
The ESP8266 SPI signals SCL (SPI clock), MOSI (Master Out Slave In) and MISO (Master In Slave Out) are connected via a voltage level shifter 5V <-> 3.3V with the AC. Direct connection of the signals without a level shifter could damage your ESP8266!
More details are described in [Hardware.md](Hardware.md).

## Software:
The project is built with [PlatformIO](https://platformio.org/), either from the command line or with the PlatformIO IDE extension for VS Code (open `MHI-AC-Ctrl.code-workspace`, the extension is recommended automatically).
Libraries are downloaded automatically:
 - MQTT client library [PubSubClient3](https://github.com/hmueller01/pubsubclient3)
   - :warning: Don't use PubSubClient v2.8.0, it is broken due to issue [#747](https://github.com/knolleary/pubsubclient/issues/747) :warning:
 - [WiFiManager](https://github.com/tzapu/WiFiManager) for the WiFi setup / settings page
 - [ArduinoOTA](https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA) (part of the ESP8266 / ESP32 core)
 - [OneWire](https://www.pjrc.com/teensy/td_libs_OneWire.html) and [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) (only used with an external DS18x20 temperature sensor)

### Build and flash
Install PlatformIO (`pip install platformio`), then in the project directory:

Board | Build environment | Notes
------|-------------------|------
WeMos D1 mini (ESP8266) | `d1_mini` | 160 MHz (better stability), USB upload
WeMos D1 mini, over the air | `d1_mini_ota` | set `upload_port` in `platformio.ini` to the device name / IP and `upload_flags = --auth=<device password>` (see `secrets.ini.example`)
WeMos LOLIN S3 mini (ESP32-S3) | `lolin_s3_mini` | native USB; press RST after uploading if the board does not start

```
pio run -e lolin_s3_mini              # build
pio run -e lolin_s3_mini -t upload    # build and flash via USB
pio device monitor                    # serial log, 115200 baud
pio run                               # build all environments
```
(Use `python -m platformio` instead of `pio` if the `pio` command is not on your PATH.)

Optional build-time settings (MQTT defaults, setup AP password, OTA password, pins, ...) go into `secrets.ini`: copy [secrets.ini.example](secrets.ini.example) to `secrets.ini`, it is ignored by git.
The SPI pins (SCK / MOSI / MISO) are GPIO 14 / 13 / 12 on the D1 mini and GPIO 12 / 11 / 13 on the LOLIN S3 mini, i.e. the same header positions on the MHI-AC-Ctrl PCB (see [Hardware.md](Hardware.md#using-a-wemos-lolin-s3-mini-esp32-s3)). They can be changed per board in `platformio.ini`.
:warning: On the LOLIN S3 mini, don't connect USB while the board is powered by the AC (no protection diode on VBUS).

### First start: WiFi and MQTT setup
1. **Device password**: on first boot the device generates a random password and prints it in the serial log at every boot (`Device password (...)`). Alternatively set your own with `DEVICE_PASSWORD` in `secrets.ini`. It is used for the setup access point, the settings login and OTA.
2. The device opens the WiFi access point **`MHI-AC-Ctrl-Setup`**; connect with a phone or laptop using the device password. The setup page opens automatically (otherwise browse to http://192.168.4.1/).
3. **Configure WiFi**: select your network, enter the WiFi password and optionally the MQTT broker, then **Save**.
4. Once connected, the settings page is available on the home network at `http://<device IP>/` (IP in the serial log and on your router). Log in with user **`admin`** and the device password. **AC status** shows the AC values, **Setup** changes the MQTT broker, hostname and device password.

Settings are stored in flash and survive reboots and firmware updates. When the WiFi is offline for 5 minutes, the setup access point opens again.

### Home Assistant
With an MQTT broker connected to Home Assistant, the device announces itself via MQTT discovery: a climate entity (mode incl. off, target temperature, room temperature, fan, vanes) plus outdoor temperature, energy, error code and WiFi signal sensors. No YAML needed. Disable with `-DHA_DISCOVERY=0`.
Details: [SW-Configuration.md](SW-Configuration.md#wifi-setup-access-point).

### Status LED
LOLIN S3 mini (RGB LED) | D1 mini (blue LED) | Meaning
------------------------|--------------------|--------
yellow | fast blink | connecting to WiFi
blue, blinking | slow blink | setup access point is open
red | steady on | WiFi OK, but no data from the AC (not connected or wiring problem)
orange | double blink | AC OK, but MQTT broker not reachable
dim green | short flash every 3 s | everything OK

The configuration options are described in [SW-Configuration.md](SW-Configuration.md).

In a previous version (see [here](https://github.com/absalom-muc/MHI-AC-SPY)) I used the Hardware-SPI of the ESP8266. But since the SPI documentation of ESP8266 is poor, I decided to switch to a Software based SPI.
This Software based SPI is reliable and the performance of the ESP8266 is sufficient for this use case.
In case of problems please check the [Troubleshooting guide](Troubleshooting.md).

# Enhancement
If you are interested to have a deeper look on the SPI protocol or want to trace the SPI signals, please check [MHI-AC-Trace](https://github.com/absalom-muc/MHI-AC-Trace). But this is not needed for the standard user of MHI-AC-Ctrl.

# License
This project is licensed under the MIT License - see the LICENSE.md file for details

# Acknowledgments
The coding of the [SPI protocol](https://github.com/absalom-muc/MHI-AC-Trace/blob/main/SPI.md) of the AC is a nightmare. Without [rjdekker's MHI2MQTT](https://github.com/rjdekker/MHI2MQTT) I had no chance to understand the protocol! Unfortunately rjdekker is no longer active on GitHub. He used an Arduino plus an ESP8266 for his project.
Also thank you very much on the authors and contributors of [MQTT client](https://github.com/knolleary/pubsubclient), [WiFiManager](https://github.com/tzapu/WiFiManager), [ArduinoOTA](https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA), [OneWire](https://www.pjrc.com/teensy/td_libs_OneWire.html) and [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) libraries.

Last but not least thank you for the implementation of MHI-AC-Ctrl in [different environments](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/SW-Configuration.md#integration-examples) (FHEM, Tasmota, Home Assistant etc.)
