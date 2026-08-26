#ifndef _OTA_H_
#define _OTA_H_

#include <Arduino.h>

// Shows a two-line OTA status message. debugToScreen() in the sketch matches this.
typedef void (*OtaStatusFn)(String line1, String line2);

extern const char* OTA_FIRMWARE_VERSION;

// Starts the Arduino IDE network port (espota, UDP/TCP 3232) and the browser
// uploader on port 80. Call once, after Wi-Fi is up. An empty password leaves
// both paths open to anyone on the LAN.
void otaBegin(const char* hostname, const char* password, OtaStatusFn statusFn);

// Services both upload paths. Anything that blocks longer than a few seconds
// locks OTA out for that whole time, so call this between slow steps.
void otaHandle();

// Services OTA and nothing else for ms milliseconds. Running this at boot means
// a build that hangs or crashes in loop() can still be replaced after a power
// cycle, so it is the one guaranteed way back in.
void otaBootWindow(uint32_t ms);

// Address to hand to the IDE or a browser, e.g. "bustracker.local (192.168.1.42)".
String otaAddress();

#endif
