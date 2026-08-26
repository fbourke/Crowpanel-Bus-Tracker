# Flashing the bus tracker over Wi-Fi

The sign now accepts firmware uploads over the network, so it can stay screwed to
the wall. Two upload paths are always listening: the Arduino IDE's network port
and a plain web page.

## One-time setup

1. Set `OTA_HOSTNAME` and `OTA_PASSWORD` in `secrets.h`. **Change the password** —
   it is the only thing stopping anyone on your Wi-Fi from reflashing the sign.
2. Leave the board settings exactly as they are. `8M with spiffs (3MB APP/1.5MB
   SPIFFS)` already reserves two 3 MB app slots (`app0`/`app1`), which is what OTA
   needs. Do not switch to a "Huge APP" scheme — those have only one slot and OTA
   will refuse to start.
3. Flash **once over USB**, as usual. OTA can only work once firmware containing
   the OTA code is actually running on the board.

From then on, uploads can go over Wi-Fi.

## Path A — Arduino IDE network port

After the USB flash, `Tools -> Port` grows a *Network ports* section containing
`bustracker at 192.168.x.x`. Pick it and hit Upload; the IDE prompts for the OTA
password. Nothing else changes.

If the network port never appears, it is mDNS discovery failing, not the sign —
use path B or C.

## Path B — Browser

Go to `http://bustracker.local/` (or `http://<the IP on the screen>/`). Log in as
user `admin` with your OTA password. The status page shows the running firmware
build, the Wi-Fi it is on, and uptime; `/update` takes a firmware file.

To get that file: `Sketch -> Export compiled Binary`, then upload the
`Crowpanel-Bus-Tracker.ino.bin` it drops next to the sketch. Ignore any
`.bootloader.bin` or `.partitions.bin` — OTA only replaces the app.

This path works from a phone.

## Path C — espota.py

Useful when mDNS is broken (common on Windows without Bonjour):

```
python espota.py -i 192.168.1.42 -p 3232 -a YOUR_OTA_PASSWORD -r \
  -f Crowpanel-Bus-Tracker.ino.bin
```

`espota.py` ships with the ESP32 core, under
`packages/esp32/hardware/esp32/<version>/tools/`.

## The recovery window

Every boot, after Wi-Fi connects, the sign spends 20 seconds doing nothing but
listening for uploads, showing its hostname and IP on the display. This is the
escape hatch: if you upload a build that crashes or wedges in `loop()`, power
cycle the sign and upload a good build during that window. Without it a bad
build means getting the USB cable back out.

`otaBootWindowMs` in the sketch controls the length. Don't set it to 0.

## Things worth knowing

- **The build stamp**: the bottom-left of every screen reads e.g.
  `Aug 25 2026 17:52:03`. That is the compile time of the running build, so it
  is what tells you whether an upload actually took effect. It is on the status
  screens as well as the bus screen, so it stays readable when Wi-Fi is down and
  `updateScreen()` never runs.
- **"Wi-Fi FAILED" is not a sign of a failed upload.** That message predates OTA
  and appears in every build. Check the build stamp, not the message, to tell
  builds apart.
- **When a join fails**, the screen names the reason the core reported
  (`AUTH_FAIL`, `NO_AP_FOUND`, `HANDSHAKE_TIMEOUT`, ...), then lists the SSIDs
  from `secrets.h` next to every network the radio actually sees with its signal
  strength. For even more detail set Core Debug Level to "Warn" in the IDE: the
  core logs every disconnect reason itself, and the default "None" discards them.
- **A negative scan count** means the scan itself never ran, not that nothing is
  out there: `-1` is still running, `-2` is `WIFI_SCAN_FAILED`. The usual cause
  is asking for a scan while the station is mid-association, which
  `esp_wifi_scan_start()` refuses.
- **The radio is 2.4GHz only.** The ESP32-S3 has no 5GHz or 6GHz support, so a
  network on those bands is not weak from the sign's point of view - it does not
  exist. If your SSID is missing from the "seen" list while your phone is happily
  connected to it, check which band the phone is on. A Wi-Fi 6E router may be
  putting that SSID on 6GHz only, and the 2.4GHz radio needs enabling or the
  2.4GHz SSID may have a different name to add to `secrets.h`.
- **WPA3.** Once the SSID does appear in the scan but the join still fails with
  `AUTH_FAIL` or `HANDSHAKE_TIMEOUT`, suspect a WPA3-only AP. The ESP32-S3
  supports WPA3-SAE, but WPA2/WPA3 transitional mode is the reliable setting.
- **Upload timing**: the sign is unreachable for a few seconds at a time while it
  fetches bus data or refreshes the e-paper. `loop()` calls `otaHandle()` between
  every slow step, and the upload tools retry, so this is usually invisible — but
  if an upload fails to connect, just run it again.
- **Wi-Fi dropouts** no longer reboot the sign instantly. It retries for three
  minutes (`wifiGraceMs`) before rebooting, and reboots immediately if it comes
  back on a different IP, since the OTA listeners are bound to the old address.
- **Progress during an upload** goes to Serial only. Refreshing the e-paper
  mid-transfer would stall the stream, so the display only changes at the start
  and end of an update.
