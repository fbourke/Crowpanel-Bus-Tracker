# Crowpanel-Bus-Tracker
Wireless display showing bus arrivals or departures with the Elecrow ESP32 4.7" e-ink Crowpanel. Data is pulled from OneBusAway API. The script also pulls weather from the open-meteo API. 

To get the script working, you'll need to add your own `secrets.h` file according to `secrets_template.h` containing your wifi password and OneBusAway API key. 

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

The board needs to be configured per the following screenshot:

![Board Configuration Screenshot from Arduino IDE v1.8.19](/Board_Config_Screenshot.png)
