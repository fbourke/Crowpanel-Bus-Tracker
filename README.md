# Crowpanel-Bus-Tracker
Wireless display showing bus arrivals or departures with the [Elecrow ESP32 5.79" e-ink Crowpanel](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html). Data is pulled from OneBusAway API. The script also pulls weather from the open-meteo API. Once the board is programmed, it should automatically connect to wifi each time it's powered up, so it can be left on a wall and powered with just a wall wart.

To get the script working, you'll need to add your own `secrets.h` file according to `secrets_template.h` containing your wifi password and OneBusAway API key. 

## Updating over Wi-Fi

The sign accepts firmware uploads over the network, so it doesn't have to come off
the wall to be reprogrammed. After one initial upload over USB, new builds can go
up either through the Arduino IDE's network port or by dragging a `.bin` onto a web
page it serves. See [OTA.md](OTA.md) for the workflow and the recovery path for a
bad build.

This can be uploaded to the Crowpanel via the Arduino IDE. Necessary dependencies need to be installed:
```
Using library SPI at version 3.3.7 
Using library WiFi at version 3.3.7 
Using library Network at version 3.3.7 
Using library HTTPClient at version 3.3.7 
Using library NetworkClientSecure at version 3.3.7 
Using library ArduinoJson at version 7.4.3 
Using library Adafruit_GFX_Library at version 1.12.5 
Using library Adafruit_BusIO at version 1.17.4 
Using library Wire at version 3.3.7
```
The over-the-air update path additionally uses `ArduinoOTA`, `WebServer`, `Update`
and `ESPmDNS`. All four ship with the ESP32 core, so there is nothing extra to
install from Library Manager:
```
Using library ArduinoOTA at version 3.3.7
Using library WebServer at version 3.3.7
Using library Update at version 3.3.7
Using library ESPmDNS at version 3.3.7
```

The board needs to be configured per the following screenshot:

![Board Configuration Screenshot from Arduino IDE v1.8.19](/Board_Config_Screenshot.png)
