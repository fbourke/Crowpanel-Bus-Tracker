#include <Arduino.h>    
#include <SPI.h>         
#include "EPD.h"         
#include <WiFi.h>        
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <secrets.h>
#include "ota.h"

// Bring in the Adafruit Graphics Engine and fonts
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// --- Configuration ---
// These pull securely from the secrets.h tab!
const char* ssids[]     = WIFI_SSIDS; 
const char* passwords[] = WIFI_PASSWORDS;       

// Automatically counts how many networks you put in the list!
const int numNetworks   = sizeof(ssids) / sizeof(ssids[0]); 

// --- Over-the-air flashing ---
// Defaults keep an older secrets.h compiling; set both there instead.
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "bustracker"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

// Seconds spent doing nothing but listening for uploads on every boot. This is
// the escape hatch: a build that wedges loop() can still be replaced by power
// cycling the sign and uploading during this window.
const uint32_t otaBootWindowMs = 20000;

// Defined here rather than in ota.cpp: the IDE always recompiles the sketch, so
// the stamp on screen always matches the build that is actually running.
const char* OTA_FIRMWARE_VERSION = __DATE__ " " __TIME__;

const uint32_t refreshIntervalMs = 60000;
const uint32_t wifiGraceMs       = 180000;  // how long a dropout may last before rebooting

// URLs (You can inject your secret API key right into the string if you ever get a real one!)
const char* stop12780Url = "https://api.pugetsound.onebusaway.org/api/where/arrivals-and-departures-for-stop/1_12780.json?key=" OBA_API_KEY;
const char* stop27350Url = "https://api.pugetsound.onebusaway.org/api/where/arrivals-and-departures-for-stop/1_27350.json?key=" OBA_API_KEY;

// Other Stop info:
// 95_4 - WSDot Bremerton/Seattle Ferry
// 20_230 - Kitsap Fast Ferry

GFXcanvas1 canvas(800, 272);

// --- Custom Data Structure ---
struct BusData {
  bool active; 
  String route;
  String headsign;
  String time;
};

// Global Variables
String lastUpdated = "Updating...";
String currentWeather = "Loading..."; 
BusData dataRoute4[2]; 
BusData dataRoute27[2];
String bootIp = "";
uint32_t lastRefresh = 0;
uint32_t wifiDownSince = 0;

// --- Helper Function: Format Time ---
String formatTime(long long epochMillis) {
  time_t t = epochMillis / 1000;
  struct tm *timeinfo = localtime(&t);
  char buffer[12];
  strftime(buffer, sizeof(buffer), "%I:%M %p", timeinfo); 
  
  // Strip leading zero from hours (e.g., "04:30" -> "4:30")
  String timeStr = String(buffer);
  if (timeStr.startsWith("0")) {
    timeStr.remove(0, 1);
  }
  return timeStr;
}

// --- Data Fetching Function (Upgraded with Future Filtering!) ---
// --- Data Fetching Function (Upgraded with Ghost Bus Filtering!) ---
void getNextTwoBuses(const char* url, String targetRoute, BusData predictions[2]) {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(secureClient, url);
  http.addHeader("Connection", "close"); 
  
  int httpResponseCode = http.GET();

  if (httpResponseCode < 0) {
    Serial.println("Connection dropped! Rebuilding socket...");
    http.end(); 
    http.begin(secureClient, url);
    http.addHeader("Connection", "close");
    httpResponseCode = http.GET(); 
  }
  
  if (httpResponseCode == 429) {
    Serial.println("Rate Limited (429)! Preserving old data on screen.");
    http.end();
    return; 
  }


  predictions[0].active = false;
  predictions[1].active = false;

  if (httpResponseCode == 200) {
    String payload = http.getString();
    
    JsonDocument doc; 
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      predictions[0].time = "Parse Error";
      return;
    }

    long long currentTimeMillis = doc["currentTime"].as<long long>();
    lastUpdated = formatTime(currentTimeMillis);

    JsonArray arrivals = doc["data"]["entry"]["arrivalsAndDepartures"];
    
    int foundCount = 0; 
    String firstTripId = ""; 
    
    for (int i = 0; i < arrivals.size() && foundCount < 2; i++) {
      String route = arrivals[i]["routeShortName"].as<String>();
      String tripId = arrivals[i]["tripId"].as<String>(); // <-- NEW: Extract the Trip ID
      
      long long predictedTime = arrivals[i]["predictedArrivalTime"].as<long long>();
      if (predictedTime == 0) {
        predictedTime = arrivals[i]["scheduledArrivalTime"].as<long long>();
      }

      // Check: Does it match the route? Is it in the future?
      if (route == targetRoute && predictedTime >= currentTimeMillis) {
        
        // If we already have 1 bus, check if this new one is just a duplicate trip
        if (foundCount == 1 && tripId == firstTripId) {
          continue; // It's a ghost! Skip it and keep searching the loop.
        }

        // If this is the very first bus we found, memorize its Trip ID
        if (foundCount == 0) {
          firstTripId = tripId; 
        }

        predictions[foundCount].active = true;
        predictions[foundCount].route = route;
        predictions[foundCount].headsign = arrivals[i]["tripHeadsign"].as<String>();
        predictions[foundCount].time = formatTime(predictedTime);
        foundCount++; 
      }
    }
    
    if (foundCount == 0) {
      predictions[0].time = "No upcoming buses";
    }
    
  } else {
    predictions[0].time = "Network Error " + String(httpResponseCode);
  }
  http.end();
}

// --- UI Helper: Draw Stop Column ---
void drawStopColumn(int startX, int startY, String title, BusData buses[2]) {
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(startX, startY); 
  canvas.print(title);
  
  if (!buses[0].active) {
    canvas.setFont(&FreeSans9pt7b);
    canvas.setCursor(startX + 10, startY + 30); 
    canvas.print(buses[0].time);
    return;
  }

  canvas.setFont(&FreeSansBold18pt7b);
  canvas.setTextSize(2);
  canvas.setCursor(startX + 10, startY + 65); 
  canvas.print(buses[0].time);
  canvas.setTextSize(1);

  canvas.setFont(&FreeSans9pt7b);
  canvas.setCursor(startX, startY + 95); 
  
  String description = "Route " + buses[0].route + " to " + buses[0].headsign;
  if (description.length() > 40) {
    description = description.substring(0, 37) + "..."; 
  }
  canvas.print(description);

  if (buses[1].active) {
    canvas.setCursor(startX, startY + 135); 
    canvas.print("Next: " + buses[1].time + "  (Route " + buses[1].route + ")");
  }
}
// --- Helper Function: Decode Weather Codes ---
String decodeWeather(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2) return "Partly Cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 67) return "Rain";
  if (code >= 71 && code <= 86) return "Snow";
  if (code >= 95) return "Storm";
  return "Cloudy";
}

// --- Data Fetching Function: Weather ---
void getWeather() {
  HTTPClient http;
  
  // Seattle coordinates (Lat: 47.6062, Lon: -122.3321)
  String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=47.6062&longitude=-122.3321&current_weather=true&temperature_unit=fahrenheit";
  
  http.setTimeout(10000); 
  http.begin(weatherUrl);
  
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String payload = http.getString();
    JsonDocument doc; 
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      float temp = doc["current_weather"]["temperature"];
      int code = doc["current_weather"]["weathercode"];
      
      // Formats into something like: "54 F | Partly Cloudy"
      currentWeather = String(temp, 0) + " F | " + decodeWeather(code);
    }
  } else {
    currentWeather = "Weather Error";
  }
  http.end();
}

// --- Screen Drawing Function ---
void updateScreen() {
  EPD_GPIOInit();
  EPD_FastMode1Init();
  
  canvas.fillScreen(1); 
  canvas.setTextColor(0); 
  
  canvas.setFont(&FreeSansBold18pt7b);
  
  // The 5.79" panel is two halves with a physical seam at x=400, and the
  // header lands with DOW|NTOWN straddling it, so the gap eats into the N.
  // Print it as two runs: the left one is right-aligned to end exactly at
  // the seam, and the right one starts a space-width further over, which
  // puts the slack inside the seam where it reads as even spacing.
  const int seamX   = 400;
  const int seamGap = 10;  // one space at this size; widen if still tight

  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds("NEXT BUS TO DOW", 0, 40, &x1, &y1, &w, &h);
  canvas.setCursor(seamX - w, 40);
  canvas.print("NEXT BUS TO DOW");

  canvas.setCursor(seamX + seamGap, 40);
  canvas.print("NTOWN");
  
  // --- Weather (Top Right) ---
  canvas.setFont(&FreeSans9pt7b);
  canvas.setCursor(600, 30); 
  canvas.print(currentWeather);

  drawStopColumn(10, 100, "Route 4 (Stop 12780):", dataRoute4);
  drawStopColumn(420, 100, "Route 27 (Stop 27350):", dataRoute27);

  canvas.setFont(&FreeSans9pt7b);
  canvas.setCursor(600, 262); 
  canvas.print("Updated: " + lastUpdated);

  // Built-in 5x7 font: the stamp is a diagnostic, not content, so it takes
  // under half the width FreeSans9pt did. Its cursor y is the glyph top,
  // not the baseline, unlike the custom fonts above.
  canvas.setFont(NULL);
  canvas.setCursor(10, 255);
  canvas.print("Build: " + String(OTA_FIRMWARE_VERSION));

  EPD_Display(canvas.getBuffer());
  EPD_FastUpdate();
  EPD_DeepSleep();
}

// --- On-Screen Debugging Helper ---
void debugToScreen(String line1, String line2) {
  EPD_GPIOInit();
  EPD_FastMode1Init();
  canvas.fillScreen(1); 
  canvas.setTextColor(0); 
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(10, 50);
  canvas.print(line1);
  canvas.setCursor(10, 90);
  canvas.print(line2);

  // These are the screens you read when Wi-Fi is down and updateScreen()
  // never runs, so the build stamp has to be here too.
  canvas.setFont(NULL);
  canvas.setCursor(10, 255);
  canvas.print("Build: " + String(OTA_FIRMWARE_VERSION));

  EPD_Display(canvas.getBuffer());
  EPD_FastUpdate();
  EPD_DeepSleep(); 
}

// --- Wi-Fi diagnostics ---
// The core knows exactly why a join failed, but only log_w()s it, which Core
// Debug Level "None" discards. Capture it so the screen can say what happened.
volatile uint8_t lastDisconnectReason = 0;

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  lastDisconnectReason = info.wifi_sta_disconnected.reason;
}

// On failure, list both what secrets.h asks for and what the radio actually sees.
// Without the second list, an SSID present under a slightly different name and a
// network that is 5/6GHz-only look identical: both just read "absent".
void wifiFailureScreen(String why) {
  // A scan cannot start while the station is still trying to associate, and
  // setAutoReconnect keeps it trying indefinitely - so stop that first, or
  // esp_wifi_scan_start() is refused and scanNetworks() returns -2.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  delay(200);

  int found = WiFi.scanNetworks();
  if (found < 0) {  // radio was still settling; one retry is enough in practice
    delay(500);
    found = WiFi.scanNetworks();
  }

  String seen = found < 0 ? "scan FAILED, code " + String(found)
                          : String(found) + " found, 5/6GHz invisible here";

  Serial.println("Wi-Fi failed: " + why);
  Serial.println("Seen on 2.4GHz: " + seen);
  for (int j = 0; j < found; j++) {
    Serial.println("  " + WiFi.SSID(j) + "  " + String(WiFi.RSSI(j)) + "dBm");
  }

  EPD_GPIOInit();
  EPD_FastMode1Init();
  canvas.fillScreen(1);
  canvas.setTextColor(0);

  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(10, 30);
  canvas.print("Wi-Fi FAILED: " + why);

  canvas.setFont(NULL);
  int y = 55;
  canvas.setCursor(10, y);
  canvas.print("Wanted (secrets.h):");
  for (int i = 0; i < numNetworks; i++) {
    y += 10;
    canvas.setCursor(20, y);
    canvas.print(ssids[i]);
  }

  y += 18;
  canvas.setCursor(10, y);
  canvas.print("Seen on 2.4GHz: " + seen);
  for (int j = 0; j < found && y < 235; j++) {
    y += 10;
    canvas.setCursor(20, y);
    canvas.print(WiFi.SSID(j) + "  " + String(WiFi.RSSI(j)) + "dBm");
  }
  WiFi.scanDelete();

  canvas.setCursor(10, 255);
  canvas.print("Build: " + String(OTA_FIRMWARE_VERSION));

  EPD_Display(canvas.getBuffer());
  EPD_FastUpdate();
  EPD_DeepSleep();
}

// --- Wi-Fi: try each network in secrets.h until one answers ---
bool connectWifi(bool showProgress) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds seconds of latency to OTA discovery

  for (int i = 0; i < numNetworks; i++) {
    if (showProgress) debugToScreen("Connecting to WiFi:", String(ssids[i]));

    // esp_wifi_set_config() is refused while the station is still
    // connecting (ESP_ERR_WIFI_STATE), which silently left every attempt
    // after the first running on network #1's config - so the fallback
    // SSIDs were never actually tried. Drop the old association first.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, true);  // erase the stored config too
    delay(300);

    WiFi.begin(ssids[i], passwords[i]);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setAutoReconnect(true);  // safe now: we hold an association
      return true;
    }
  }
  return false;
}

// --- Main Setup ---
void setup() {
  Serial.begin(115200);
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  
  canvas.setRotation(2); 

  EPD_GPIOInit();
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();

  debugToScreen("Booting up...", "Wiping old WiFi data...");
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect(true); 
  delay(1000);

  if (connectWifi(true)) {
    otaBegin(OTA_HOSTNAME, OTA_PASSWORD, debugToScreen);
    bootIp = WiFi.localIP().toString();
    otaBootWindow(otaBootWindowMs);
  } else {
    wifiFailureScreen(lastDisconnectReason
                          ? String(WiFi.disconnectReasonName((wifi_err_reason_t)lastDisconnectReason))
                          : String("no AP found"));
  }

  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
}

// --- Main Loop ---
// Never blocks for long: otaHandle() has to run between every slow step, or an
// upload started mid-refresh has nobody listening for it.
void loop() {
  otaHandle();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDownSince == 0) {
      wifiDownSince = millis();
      Serial.println("WiFi dropped. Retrying...");
    }
    if (millis() - wifiDownSince > wifiGraceMs) {
      debugToScreen("Wi-Fi lost.", "Rebooting to reconnect...");
      ESP.restart();
    }
    if (!connectWifi(false)) return;

    wifiDownSince = 0;
    // The OTA listeners are bound to the old address, so a new lease needs a reboot.
    if (WiFi.localIP().toString() != bootIp) ESP.restart();
    Serial.println("WiFi reconnected.");
  }

  if (lastRefresh != 0 && millis() - lastRefresh < refreshIntervalMs) {
    delay(10);
    return;
  }
  lastRefresh = millis();

  Serial.println("Fetching Weather...");
  getWeather();
  otaHandle();

  Serial.println("Fetching Route 4 at Stop 12780...");
  getNextTwoBuses(stop12780Url, "4", dataRoute4); 
  otaHandle();

  Serial.println("Fetching Route 27 at Stop 27350...");
  getNextTwoBuses(stop27350Url, "27", dataRoute27);
  otaHandle();

  Serial.println("Updating Display...");
  updateScreen();
}
