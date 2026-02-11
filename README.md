# LaCrosse to MQTT gateway

This is a gateway to receive temperature and humidity from (868 MHz)
 * LaCrosse IT+ - temperature + humidity sensors
 * WH1080 - Weatherstations
 * TX35-IT - temperature + humidity sensors
 * TX38-IT - indoor temperature sensors
 * WH1080 - Weatherstations
 * WS1600 - Weatherstations
 * WT440XH - temperature + humidity sensors
 * TX22-IT - Weatherstations
 * W136 - Weatherstations
 * EMT7110 - Energy Meter
 * WH24 - Weathersensor
 * WH25 - Pressure Sensor

and publish them to a MQTT broker.

The code was originally inspired by [LaCrosseITPlusReader](https://github.com/rinie/LaCrosseITPlusReader) but the original code has been completely reworked since then.
It is designed to run on a "TTGO LORA" board which has a SX1276 RF chip and a SSD1306 OLED on board.

At first installation just connect to the WiFi: Lacrosse2mqttAP  and connect the device to your network with the wizard.

The web page is showing the received sensors with their values, the configuration page allows to specify a name / label for every sensor ID.
To clear a label for a sensor, just enter an empty label.

## MQTT publishing of values
On the config page, you can enter the hostname / IP of your MQTT broker. The topics published are:

   * `lacrosse/climate/<LABEL>/temp` temperate
   * `lacrosse/climate/<LABEL>/humi` humidity (if available)
   or
   * `lacrosse/id_<ID>/temp`, `lacrosse/id_<ID>/humi` the same but per ID. Note that the ID may change after a battery change! Labels can be rearranged after a battery change for stable naming.
   * `lacrosse/id_<ID>/state` additional flags "low_batt", "init" (for new battery state), "RSSI" (signal), "baud" (data rate) as JSON string

## First upload
 * Open Chrome or any chromium based browser.
 * Download newest firmware (lacrose2mqtt.YYYY.XX.X.bin) at the [release page](https://github.com/steigerbalett/lacrosse2mqtt/releases).
 * Connect the board with your computer over USB.
 * Open [ESP Web Tools](https://espressif.github.io/esp-launchpad/)
 * Click "connetct" and chose the right COM-Port.
 * Upload the bin file (follow the instuctions).

## Firmware update
Use the buildin Online Firmware Updater
Or:
Download newest firmware (lacrose2mqtt.YYYY.XX.X.bin) at the [release page](https://github.com/steigerbalett/lacrosse2mqtt/releases).
The software update can be uploaded via the "Update software" link from the configuration page

## Reset WiFi
Long press (5s) the PRG button

## Toggle Display
Short press the PRG button

## Debugging
More information about the current state is printed to the serial console, configured at 115200 baud.

## Dependencies / credits
The following libraries are needed for building (could all be installed via arduino lib manager, github url only for reference):

   * [LittleFS_esp32](https://github.com/lorol/LITTLEFS)
   * [PubSubClient](http://pubsubclient.knolleary.net/)
   * [ArduinoJson](https://arduinojson.org/?utm_source=meta&utm_medium=library.properties)
   * [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
   * [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)
   * [WiFiManager](https://github.com/tzapu/WiFiManager)

   * [Heltec Boards](https://resource.heltec.cn/download/package_heltec_esp32_index.json)

## Nice2have
 * Add FHEM connector

## Know problems


## Source/Idea
This project uses code and protocol descriptions derived from
[LaCrosseITPlusReader10](https://github.com/rinie/LaCrosseITPlusReader10),
which itself is based on the LaCrosse IT+ Reader contributed to the
[FHEM](https://fhem.de/) project. See `LICENSES.md` for details.