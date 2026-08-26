// --- secrets_template.h ---
// Instructions: Rename this file to "secrets.h" and put your real Wi-Fi info below!

#define WIFI_SSIDS {"Network_1", "Network_2", "Network_3"}
#define WIFI_PASSWORDS {"Password_1", "Password_2", "Password_3"}

#define OBA_API_KEY "TEST"

// --- Over-the-air flashing ---
// The sign appears in the Arduino IDE as OTA_HOSTNAME and at http://OTA_HOSTNAME.local
// Pick a real OTA_PASSWORD: it is the only thing stopping anyone on your Wi-Fi
// from reflashing the sign. Leave it as "" to disable the check entirely.
#define OTA_HOSTNAME "bustracker"
#define OTA_PASSWORD "ChangeThisPassword"
