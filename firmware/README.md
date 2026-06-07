# SapiensNote firmware (ESP-IDF) — scaffold

Target: **Waveshare ESP32-S3 1.54" e-Paper (touch)** — see [../docs/HARDWARE.md](../docs/HARDWARE.md)
and [../docs/FIRMWARE_SPEC.md](../docs/FIRMWARE_SPEC.md).

This is the firmware shell. It **reuses the shared C++ core** (`../core`) and the
**portable renderer** (`../simulator/src`: `Canvas`, `Font`, `Renderer`) — exactly
the same code the simulator runs — and provides an **ESP-IDF HAL** implementation
(`main/hal`). Only the HAL differs from the desktop simulator
([../docs/SHARED_CORE.md](../docs/SHARED_CORE.md), [ADR-0002](../docs/ADR/ADR-0002%20%E2%80%94%20Shared%20UI)).

> ⚠️ **Status: scaffold, not yet buildable end-to-end.** It compiles against
> ESP-IDF, *not* a host compiler, so it is not built by the top-level CMake here.
> Several board specifics are marked `TODO(board)` and must be filled from the
> Waveshare schematic / demo (`pin_config.h`): the EPD controller command
> sequence, the exact **GPIO pin map**, and the ES8311 register init. The pin map
> placeholders mirror [../docs/HARDWARE.md](../docs/HARDWARE.md).

## Build (requires ESP-IDF v5.x installed)

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig        # confirm PSRAM (octal), flash 8MB
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

## Layout

```
firmware/
  CMakeLists.txt              ESP-IDF top-level project
  sdkconfig.defaults         PSRAM / flash / FreeRTOS defaults
  partitions.csv             factory app + SD-backed storage notes
  components/
    sapienscore/CMakeLists.txt  wraps ../../core as an IDF component
  main/
    CMakeLists.txt
    main.cpp                 app_main: init HAL, wire core, run UI loop
    hal/                     ESP-IDF HAL implementations
      EspClock.h  EspLog.h  EspPower.h
      EspStorage.h/.cpp      mount microSD (FAT32)
      EspDisplay.h/.cpp      push the 1-bit framebuffer to the e-paper panel
      EspInput.h/.cpp        FT6336 capacitive touch -> InputEvent
      EspAudio.h/.cpp        ES8311 over I2S, 16 kHz mono capture/playback
```

## What works conceptually vs TODO

- ✅ Reuses `sapiens::` core (notes, services, app state machine) and `sapiens::sim`
  `Renderer`/`Canvas` to draw the 200×200, 1-bit UI.
- ✅ HAL interface boundary is the same eight interfaces as the simulator.
- ⬜ `TODO(board)`: real GPIO numbers, EPD init/refresh waveform, FT6336 I²C
  address + read sequence, ES8311 register configuration.
- ⬜ Local persistence: scaffold uses the in-memory repository; swap to SQLite on
  the SD card (esp-idf SQLite component) once storage is wired.
- ⬜ Wi-Fi + sync client (`sapiens::SyncClient`) against the Phase-2 server.
