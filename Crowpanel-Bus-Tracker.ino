#include <Arduino.h>    
#include <SPI.h>         
#include "EPD.h"         
#include <WiFi.h>        
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <secrets.h>

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

// URLs (You can inject your secret API key right into the string if you ever get a real one!)
const char* stop400Url = "https://api.pugetsound.onebusaway.org/api/where/arrivals-and-departures-for-stop/1_400.json?key=" OBA_API_KEY;
const char* stop590Url = "https://api.pugetsound.onebusaway.org/api/where/arrivals-and-departures-for-stop/1_590.json?key=" OBA_API_KEY;

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
BusData data400[2]; 
BusData data590[2];

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
  
  String leftHeader = "NEXT BUS";
  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds(leftHeader, 0, 40, &x1, &y1, &w, &h);
  canvas.setCursor(380 - w, 40); 
  canvas.print(leftHeader);
  
  canvas.setCursor(420, 40); 
  canvas.print("HOME");
  
  // --- Weather (Top Right) ---
  canvas.setFont(&FreeSans9pt7b);
  canvas.setCursor(600, 30); 
  canvas.print(currentWeather);

  drawStopColumn(10, 100, "Route 4 (Stop 400):", data400);
  drawStopColumn(420, 100, "Route 27 (Stop 590):", data590);

  canvas.setFont(&FreeSans9pt7b);
  canvas.setCursor(600, 262); 
  canvas.print("Updated: " + lastUpdated);

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
  EPD_Display(canvas.getBuffer());
  EPD_FastUpdate();
  EPD_DeepSleep(); 
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
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect(true); 
  delay(1000);

  bool isConnected = false;

  for (int i = 0; i < numNetworks; i++) {
    debugToScreen("Connecting to WiFi:", String(ssids[i]));
    
    WiFi.begin(ssids[i], passwords[i]);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;
      break; 
    }
  }
  
  if (isConnected) {
    debugToScreen("Wi-Fi Connected!", "IP: " + WiFi.localIP().toString());
    delay(4000); 
  } else {
    debugToScreen("Wi-Fi FAILED.", "Tried all networks.");
  }

  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
}

// --- Main Loop ---
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Fetching Weather...");
    getWeather();
    
    Serial.println("Fetching Route 4 at Stop 400...");
    getNextTwoBuses(stop400Url, "4", data400); 
    
    Serial.println("Fetching Route 27 at Stop 590...");
    getNextTwoBuses(stop590Url, "27", data590);

    Serial.println("Updating Display...");
    updateScreen();
  } else {
    Serial.println("WiFi dropped. Rebooting to find a network...");
    ESP.restart(); 
  }
  
  delay(60000); 
}
