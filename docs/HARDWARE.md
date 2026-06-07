# HARDWARE

Target board: **Waveshare ESP32-S3 1.54" e-Paper Development Board (touch variant)**.
Product: <https://www.waveshare.com/esp32-s3-epaper-1.54.htm> ·
Wiki: <https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54>

This is the single hardware target for v1 firmware. The simulator emulates this
board's constraints (see [SIMULATOR_SPEC.md](SIMULATOR_SPEC.md)).

---

## Board specification

| Subsystem | Detail |
|---|---|
| MCU | ESP32-S3 (dual-core Xtensa LX7 @ 240 MHz) |
| Flash | 8 MB (V2) / 4 MB (V1) |
| PSRAM | 8 MB (V2) / 2 MB (V1) |
| Display | 1.54" e-paper, **200 × 200 px, monochrome (black/white only)** |
| Touch | FT6336 capacitive touch (I²C) |
| Audio codec | **ES8311** — onboard microphone + speaker (low-power, voice-grade) |
| Speaker out | MX1.25 2-pin speaker header |
| RTC | PCF85063 |
| Sensor | SHTC3 temperature + humidity |
| Storage | microSD (TF) slot, **FAT32** |
| Power | Li-battery (MX1.25 2-pin header) + onboard charge management |
| USB | USB-C (flashing + serial log) |
| Wireless | Wi-Fi 2.4 GHz 802.11 b/g/n, Bluetooth 5 (LE) |
| Buttons | PWR + BOOT (configurable) |
| Exposed | USB, UART, I²C, GPIO headers |

> **Design constraints that drive the whole UI:** the panel is **200×200 and
> 1-bit monochrome** with slow refresh. No grayscale, no color, no animation.
> Plan every screen for ~40k pixels of black-or-white and partial-refresh timing.

---

## Memory budget (V2: 8 MB flash / 8 MB PSRAM)

| Area | Budget (planned) | Notes |
|---|---|---|
| Application (firmware) | ~2–3 MB flash | ESP-IDF + LVGL + app |
| LVGL frame buffer | ~5 KB RAM | 200×200 × 1-bit = 5,000 bytes |
| Audio buffers | PSRAM | ring buffer for I²S capture/playback |
| Local DB + audio | microSD | SQLite + FLAC/WAV files (not flash) |
| OTA / recovery | reserved flash partition | optional |

Audio and notes live on the **microSD card**, never on internal flash, so storage
is bounded by the card, not the 8 MB.

---

## Pin map

> **Status: to be confirmed against the Waveshare schematic and demo code.** The
> wiki product page does not publish a GPIO table; the authoritative source is the
> board schematic and the vendor ESP-IDF examples (look for `pin_config.h` /
> `bsp` headers in the Waveshare demo). Fill the exact GPIO numbers below from
> those before writing the HAL. Interfaces and controllers are known; pin numbers
> are placeholders (`GPIO_xx`).

| Signal group | Controller | Interface | Pins (confirm) |
|---|---|---|---|
| e-Paper | SSD-class EPD | SPI | MOSI `GPIO_xx`, SCK `GPIO_xx`, CS `GPIO_xx`, DC `GPIO_xx`, RST `GPIO_xx`, BUSY `GPIO_xx` |
| Touch | FT6336 | I²C (+INT) | SDA `GPIO_xx`, SCL `GPIO_xx`, INT `GPIO_xx`, RST `GPIO_xx` |
| Audio control | ES8311 | I²C | shares the touch/sensor I²C bus (confirm address) |
| Audio data | ES8311 | I²S | MCLK `GPIO_xx`, BCLK `GPIO_xx`, LRCK/WS `GPIO_xx`, DOUT `GPIO_xx`, DIN `GPIO_xx` |
| Speaker amp | PA enable | GPIO | `GPIO_xx` (confirm if present) |
| microSD | TF slot | SPI or SDMMC | confirm mode + pins |
| RTC | PCF85063 | I²C (+INT) | shared I²C; INT `GPIO_xx` |
| Sensor | SHTC3 | I²C | shared I²C (addr 0x70) |
| Buttons | PWR, BOOT | GPIO | PWR `GPIO_xx`, BOOT `GPIO0` |
| Battery | voltage divider | ADC | `GPIO_xx` (confirm ADC channel) |

The I²C bus is shared by FT6336, ES8311 (control), PCF85063, and SHTC3 — verify
there are no address collisions when bringing up the HAL.

---

## Power & sleep

- Primary power-saving strategy: **deep sleep** between interactions; e-paper
  holds its image with zero power, so the screen stays readable while asleep.
- Wake sources: touch INT, PWR button, RTC alarm.
- Battery monitored via ADC; the UI shows the level + a low-battery warning.
- Target autonomy: days of light use on a ~500 mAh cell (record a few notes/day,
  mostly asleep).

---

## Audio capture format

- **16 kHz, mono** PCM (matches Whisper's native rate, so the transcription engine
  never resamples — [ADR-0004](ADR/ADR-0004%20%E2%80%94%20Off-Device%20Transcription)).
- On-card encoding: **FLAC** preferred (lossless, ~50% smaller than WAV), **WAV**
  fallback if FLAC encode is too costly on-device. Decide during M1.3 by measuring
  CPU headroom.

---

## Development frameworks

- **ESP-IDF** (primary; required for the LVGL + I²S + EPD stack used here).
- Arduino IDE is supported by the board but not used by this project.

See the Waveshare ESP-IDF setup guide:
<https://docs.waveshare.com/ESP32-S3-ePaper-1.54/Development-Environment-Setup-ESP-IDF>
