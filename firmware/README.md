# SapiensNote firmware (ESP-IDF)

Target: **ESP32-S3 + 1.54" 200×200 e-paper** (the Sapiens Note board / Pala Note
`S3_ePaper_1_54`) — see [../docs/HARDWARE.md](../docs/HARDWARE.md).

The firmware **reuses the shared C++ core** (`../core`) and the **portable
renderer** (`../simulator/src`: `Canvas`, `Font`, `Renderer`) — the same code the
desktop simulator runs — and provides an **ESP-IDF HAL** (`main/hal`). It is
**working end-to-end** on hardware: e-paper UI, touch + 2-button navigation, audio
record/playback, SD-backed note persistence, real-time clock, Wi-Fi captive-portal
provisioning, an on-device web dashboard with on-device transcription, and a USB
mass-storage "disk mode".

## Build (container) + flash (host)

The build runs in the ESP-IDF container; flashing is done from the **host**
(macOS Docker has no USB pass-through). Always target the **ESP32-S3**.

```bash
# build (from the repo root)
docker run --rm -v "$PWD":/project -w /project/firmware espressif/idf:release-v5.5 \
  bash -c "idf.py -B build-docker build"

# flash from the host (pick the S3's port; never the other board)
cd firmware/build-docker
esptool.py --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin \
  0x10000 sapiensnote.bin
```

Notes:
- If the SD-port is missing, the device is asleep on battery — **press a button**
  to wake it, then flash. It never sleeps while a USB host is attached.
- First build fetches the managed component **`esp_tinyusb`** (needs network once).
- `build-docker/`, `managed_components/`, `.cache/` and the generated
  `main/hal/BuildClock.h` are gitignored.

## Layout

```
firmware/
  CMakeLists.txt              ESP-IDF project
  sdkconfig.defaults         target/PSRAM/flash, FATFS LFN, TinyUSB MSC, …
  partitions.csv             factory app (2 MB) + storage
  main/idf_component.yml     managed deps (espressif/esp_tinyusb)
  components/
    sapienscore/             wraps ../../core as an IDF component
    audiocodec/              vendored esp_codec_dev + ES8311 + audio_bsp (from Pala Note)
  main/
    main.cpp                 app_main: HAL init, core wiring, UI loop, disk mode
    hal/
      Pins.h                 confirmed GPIO map (+ LED pins TBD)
      EspDisplay.*           SSD1681 e-paper driver wrapper
      epaper_driver_bsp.*    SSD1681 driver (ported from Pala Note)
      board_power_bsp.*      EPD/audio/VBAT power rails
      EspInput.*             FT6336 touch + 2-button nav (5 ms task)
      EspAudio.*             ES8311/I²S record→WAV / WAV→playback
      EspStorage.*           microSD (SDMMC 1-bit) mount
      SdNoteRepository.*     notes persisted to /sdcard/.../notes.json
      EspClock.*             PCF85063 RTC -> system clock (+ NTP write-back)
      EspPower.*             battery ADC + deep sleep (ext1 button wake)
      EspNet.*               Wi-Fi: captive portal provisioning + STA + SNTP
      EspDashboard.*         on-device web dashboard + API + transcription
      DeviceSettings.*       NVS settings (sleep/hold/scroll/name/disk)
      EspLed.*               status LEDs (green=USB, red=low batt; pins TBD)
```

## Behaviour

- **Display:** the shared `Renderer` draws the 200×200 1-bit UI; `EspDisplay` pushes
  it to the SSD1681 (partial refresh, full refresh on screen change / periodically).
- **Navigation:** capacitive touch (FT6336) **and** two physical buttons —
  **top:** 1 click = next, 2 = previous, **hold = record**; **bottom:** 1 = select,
  2 = back. A focus ring shows the cursor. No on-screen Back buttons.
- **Audio:** record to `rec-YYYYMMDD-HHMMSS[-name].wav` on the SD; play back through
  the speaker; the player shows progress.
- **Notes** persist in `notes.json` on the SD (survive reboot); orphan WAVs are
  reconciled into notes at boot.
- **Clock:** PCF85063 RTC read at boot; Wi-Fi → SNTP writes the real time back.
- **Wi-Fi / dashboard** (Sync → Connect): captive-portal provisioning (SoftAP
  `SapiensNote-XXXX`), then serves the **sapiens-web** dashboard on :80 (list /
  play / download `.wav`/`.txt` / rename / delete) and **on-device transcription**
  via an OpenAI-compatible endpoint (key/URL set in the dashboard Settings).
- **Settings** (NVS, editable on-device and in the dashboard): text-scroll speed,
  name format, sleep timeout, hold-to-record seconds, disk-mode toggle.
- **Power:** deep sleep after the configured idle (button-wake); never on USB.
- **Disk mode:** Settings toggle → reboots into TinyUSB MSC, exposing the SD as a
  USB drive (recordings + `.txt`). Hold the top button 3 s to exit.

## Maintaining

- RQ-style note: **RQ workers / the firmware don't hot-reload** — re-flash after
  edits. See the repo `CLAUDE.md` for the broader stack rules.
- The app binary is ~85 % of the 2 MB factory partition (Wi-Fi + TLS + TinyUSB);
  if it grows past that, enlarge the app partition in `partitions.csv`.
