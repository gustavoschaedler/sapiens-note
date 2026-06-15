# HARDWARE

Target board: an **ESP32-S3 + 1.54" 200×200 e-paper** device (the "Sapiens Note"
board), the same hardware as the **Pala Note** reference (board id
`S3_ePaper_1_54`). The pin map and drivers below are **confirmed against that
reference firmware** and verified on the physical device — they are no longer
placeholders.

The desktop simulator emulates this board's constraints (see
[SIMULATOR_SPEC.md](SIMULATOR_SPEC.md)).

---

## Board specification

| Subsystem | Detail |
|---|---|
| MCU | ESP32-S3 (dual-core Xtensa LX7) |
| Flash | 8 MB (octal) |
| PSRAM | 8 MB (octal, AP, 3V) |
| Display | 1.54" e-paper **SSD1681-class, 200 × 200, 1-bit monochrome** |
| Touch | FT6336 capacitive (I²C @ 0x38) |
| Audio codec | **ES8311** (I²C @ 0x18) — mic + speaker, 16 kHz mono |
| RTC | PCF85063 (I²C @ 0x51) |
| Sensor | SHTC3 temp/humidity (I²C @ 0x70) |
| Storage | microSD, **SDMMC 1-bit**, FAT32 (LFN enabled) |
| Power | Li-battery + charge management; VBAT power-latch (GPIO17) |
| USB | USB-C (native USB-Serial-JTAG: flash + log; or TinyUSB MSC in disk mode) |
| Wireless | Wi-Fi 2.4 GHz b/g/n |
| Buttons | two: top = BOOT (GPIO0), bottom = PWR (GPIO18) |
| Status LEDs | green + red (**GPIOs not documented — TBD**, see below) |

> **Design constraint that drives the whole UI:** the panel is **200×200, 1-bit**
> with slow refresh. No grayscale/colour/smooth animation. A full refresh flashes
> (black↔white) to clear ghosting; partial refresh is faster but accumulates
> ghosting, so a full refresh is done on every screen change and occasionally on
> animated screens.

---

## Pin map (confirmed)

| Signal group | Controller | Interface | Pins |
|---|---|---|---|
| e-Paper | SSD1681 | SPI2 | MOSI 13, SCK 12, CS 11, DC 10, RST 9, BUSY 8 |
| Touch | FT6336 (0x38) | I²C | shares SDA 47 / SCL 48 |
| Audio control | ES8311 (0x18) | I²C | shares SDA 47 / SCL 48 |
| Audio data | ES8311 | I²S | MCLK 14, BCLK 15, WS 38, DOUT 45, DIN 16; PA enable 46 |
| microSD | TF slot | **SDMMC 1-bit** | CLK 39, CMD 41, D0 40 |
| RTC | PCF85063 (0x51) | I²C | shares SDA 47 / SCL 48 |
| Sensor | SHTC3 (0x70) | I²C | shares SDA 47 / SCL 48 |
| Buttons | BOOT (top), PWR (bottom) | GPIO | BOOT 0, PWR 18 (active-low) |
| Battery | voltage divider (2:1) | ADC | GPIO4 = ADC1 channel 3 |
| Power rails | EPD / audio / VBAT latch | GPIO | EPD_EN 6 (active-low), AUDIO_EN 42 (active-low), VBAT latch 17 (active-high) |
| Status LEDs | green / red | GPIO | **unknown — not in the reference docs; set in `firmware/main/hal/Pins.h`** |

The **single I²C bus** (port 0, SDA 47 / SCL 48) is shared by FT6336 (0x38),
ES8311 (0x18), PCF85063 (0x51) and SHTC3 (0x70) — no address collisions. The
firmware creates the bus once (`EspInput`) and the codec stack reuses it.

> **LEDs:** the reference `config.h` and the Wokwi `diagram.json` (board
> `esp32-s3-devkitc-1`, whose onboard RGB is GPIO48 = our I²C SCL) define **no
> indicator-LED pin**. The firmware's LED logic (green on USB, red ≤10 % battery)
> is implemented but disabled until `LED_GREEN` / `LED_RED` / `LED_ACTIVE_HIGH`
> are filled in (in `Pins.h`) from the real schematic.

---

## Power & sleep

- **Deep sleep** after a configurable idle (default 15 s; 0 = never; set in
  Settings). The e-paper holds its image with no power, so the last screen stays
  readable while asleep. A "Sleeping" page is shown first.
- **Wake:** either nav button (ext1 wake, ANY_LOW on GPIO0/GPIO18). Wake is a full
  reboot — notes (SD) and the RTC time persist.
- **Never sleeps while a USB host is connected** (`usb_serial_jtag_is_connected`)
  or on the Sync screen / while recording / playing.
- **VBAT latch (GPIO17)** held high at boot to keep the board powered on battery.
- **Battery:** read on ADC1_CH3 (GPIO4) through a 2:1 divider, mapped to %
  (≈3.30 V→0 %, ≈4.05 V→100 %; the ADC reads a full cell low, so 100 % is anchored
  at ~4.05 V). Shown in every header; the red LED blinks ≤10 %.

---

## Audio

- **16 kHz, mono, 16-bit WAV.** The ES8311 runs I²S stereo 16 kHz; capture takes
  the left channel to mono, playback duplicates mono to stereo.
- Files: `/sdcard/sapiensnote/audio/rec-YYYYMMDD-HHMMSS[-name].wav`. The date-time
  comes from the RTC; an optional `-name` is added when renamed in the dashboard.
- Transcripts (when produced) are written next to the WAV as `….txt`.

---

## USB modes

- **Normal:** native **USB-Serial-JTAG** — flashing + serial log.
- **Disk mode** (Settings toggle): on boot the device starts **TinyUSB MSC**
  (`esp_tinyusb`) and exposes the SD card as a USB drive (recordings + `.txt`).
  While in disk mode the console and the app are off; **hold the top button 3 s to
  exit** (clears the flag + reboots). Recovery: hold BOOT (GPIO0) at power-on for
  download mode.

---

## Build / flash

The firmware builds inside the **espressif/idf:release-v5.5** container and is
flashed from the **host** (macOS Docker can't pass the USB port through). See
[../firmware/README.md](../firmware/README.md) for the exact commands. Always flash
with `--chip esp32s3`.
