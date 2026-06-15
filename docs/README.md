# Sapiens Note — documentation

**A minimalist, distraction-free AI voice-note app** for a 1.54" e-ink voice
recorder. Record with one tap; play it back, transcribe it, and read the full
transcript — all locally.

For the product overview, screenshots, and build/run instructions see the
repository [README](../README.md) and [BUILD.md](../BUILD.md). This folder
documents how the app is built.

## What's here

- **The device firmware** — a working ESP-IDF build for the ESP32-S3 e-paper board
  that reuses the same core + renderer (audio, SD-backed notes, RTC, Wi-Fi captive
  portal, an on-device web dashboard, deep sleep, and a USB disk mode).
- **Two desktop simulators** that share one C++ core and behave identically: a
  1-bit `Canvas` build (`sapiens-sim`, SDL2) and a real-widget LVGL 9 build
  (`sapiens-sim-lvgl`).
- **A shared C++ core** (domain models, app state machine + navigation, audio,
  the offline sync client, and the storage/audio/clock HAL).
- **SQLite storage** at `~/.sapiensnote-sim/sapiensnote.db`, shared by everything.
- **A local web dashboard** (`sapiens-web`) to play, download, and transcribe
  recordings via a pluggable engine (whisper.cpp or OpenAI).

## Documentation index

- [ARCHITECTURE.md](ARCHITECTURE.md) — how the pieces fit (clients + core + web).
- [SHARED_CORE.md](SHARED_CORE.md) — the C++ core + HAL shared by every client.
- [SIMULATOR_SPEC.md](SIMULATOR_SPEC.md) — the desktop simulators + web dashboard.
- [UI_UX.md](UI_UX.md) — screens and interaction flows.
- [HARDWARE.md](HARDWARE.md) — the e-ink device: confirmed pin map + peripherals.
- [../firmware/README.md](../firmware/README.md) — the ESP-IDF firmware: container
  build + host flash, the HAL layout, and on-device behaviour.
- [GLOSSARY.md](GLOSSARY.md) — shared vocabulary.
- [ADR/](ADR/) — the Architecture Decision Records behind the design.

## Design principles

1. **Offline first** — capture never depends on the network.
2. **One experience** — every client is a thin shell over one shared C++ core.
3. **Personal** — a single user; no accounts, teams, or multi-tenancy.
4. **Pluggable AI** — transcription runs locally (whisper.cpp) or in the cloud
   (OpenAI), behind one interface.
