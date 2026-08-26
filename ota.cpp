#include "ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

static WebServer server(80);
static OtaStatusFn showStatus = nullptr;
static String otaHostname;
static String otaPassword;
static bool began = false;
static bool uploadAuthorized = false;
static bool uploadStarted = false;

// e-paper refreshes take a second or two, so only the milestones get one.
static void status(String line1, String line2) {
  Serial.println(line1 + " " + line2);
  if (showStatus) showStatus(line1, line2);
}

String otaAddress() {
  return otaHostname + ".local (" + WiFi.localIP().toString() + ")";
}

static const char kUploadPage[] PROGMEM = R"HTML(<!doctype html>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bus Tracker OTA</title>
<style>body{font:16px system-ui,sans-serif;margin:2rem;max-width:30rem}progress{width:100%}</style>
<h1>Upload firmware</h1>
<p>In the Arduino IDE use Sketch &rarr; Export compiled Binary, then pick the
<code>.ino.bin</code> it drops next to the sketch.</p>
<form id="f"><input type="file" name="firmware" accept=".bin" required> <button>Flash</button></form>
<progress id="p" value="0" max="100" hidden></progress>
<p id="s"></p>
<script>
const f=document.getElementById('f'),p=document.getElementById('p'),s=document.getElementById('s');
f.onsubmit=e=>{e.preventDefault();p.hidden=false;s.textContent='Uploading...';
const x=new XMLHttpRequest();
x.upload.onprogress=v=>{p.value=v.loaded/v.total*100;s.textContent=Math.round(p.value)+'%'};
x.onload=()=>{s.textContent=x.responseText};
x.onerror=()=>{s.textContent='Upload failed - is the sign still on the network?'};
x.open('POST','/update');x.send(new FormData(f))};
</script>
)HTML";

static bool requireAuth() {
  if (otaPassword.length() == 0) return true;
  if (server.authenticate("admin", otaPassword.c_str())) return true;
  server.requestAuthentication();
  return false;
}

static void handleRoot() {
  if (!requireAuth()) return;
  String body = F("<!doctype html><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<title>Bus Tracker</title><style>body{font:16px system-ui,sans-serif;margin:2rem}</style>"
                  "<h1>Bus Tracker</h1><ul>");
  body += "<li>Firmware: " + String(OTA_FIRMWARE_VERSION) + "</li>";
  body += "<li>Address: " + otaAddress() + "</li>";
  body += "<li>Wi-Fi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)</li>";
  body += "<li>Uptime: " + String(millis() / 1000) + " s</li>";
  body += "<li>Free heap: " + String(ESP.getFreeHeap()) + " bytes</li>";
  body += F("</ul><p><a href=\"/update\">Upload firmware</a></p>");
  server.send(200, "text/html", body);
}

static void handleUpdatePage() {
  if (!requireAuth()) return;
  server.send_P(200, "text/html", kUploadPage);
}

static void handleUpdateResult() {
  if (!requireAuth()) return;
  if (!uploadStarted) {
    server.send(400, "text/plain", "No firmware file in the request.");
    return;
  }
  uploadStarted = false;
  if (Update.hasError()) {
    server.send(500, "text/plain", "Update failed: " + String(Update.errorString()));
    return;
  }
  server.send(200, "text/plain", "Update OK, rebooting into the new build.");
  status("OTA complete", "Rebooting...");
  server.client().stop();
  delay(500);
  ESP.restart();
}

static void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadAuthorized = requireAuth();
    uploadStarted = uploadAuthorized;
    if (!uploadAuthorized) return;
    status("OTA: browser upload", upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (!uploadAuthorized) {
    return;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("OTA: browser upload aborted");
  }
}

void otaBegin(const char* hostname, const char* password, OtaStatusFn statusFn) {
  showStatus = statusFn;
  otaHostname = hostname;
  otaPassword = password;

  if (otaPassword.length() == 0) {
    Serial.println("OTA: no password set - anyone on this network can reflash the sign");
  }

  ArduinoOTA.setHostname(otaHostname.c_str());
  if (otaPassword.length() > 0) ArduinoOTA.setPassword(otaPassword.c_str());

  ArduinoOTA.onStart([]() {
    status("OTA update starting", ArduinoOTA.getCommand() == U_FLASH ? "Sketch" : "Filesystem");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned int lastDecile = 999;  // Serial only; a refresh here would stall the stream
    unsigned int decile = total ? (done * 10 / total) : 0;
    if (decile != lastDecile) {
      lastDecile = decile;
      Serial.printf("OTA: %u%%\n", decile * 10);
    }
  });
  ArduinoOTA.onEnd([]() { status("OTA complete", "Rebooting..."); });
  ArduinoOTA.onError([](ota_error_t error) { status("OTA failed", "Error code " + String((int)error)); });

  ArduinoOTA.begin();  // also brings up mDNS as <hostname>.local

  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  began = true;
  Serial.println("OTA ready at " + otaAddress());
}

void otaHandle() {
  if (!began) return;
  ArduinoOTA.handle();
  server.handleClient();
}

void otaBootWindow(uint32_t ms) {
  if (!began) return;
  status("OTA window open", otaAddress());
  uint32_t start = millis();
  while (millis() - start < ms) {
    otaHandle();  // blocks here for the whole transfer once an upload starts
    delay(10);
  }
}
