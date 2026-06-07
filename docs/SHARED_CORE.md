# SHARED_CORE

The shared C++ core is what makes the device and the simulator behave identically.
Both clients are thin shells that link this core and provide a **HAL**
implementation. This document defines the boundary. Backs
[ADR-0002](ADR/ADR-0002%20%E2%80%94%20Shared%20UI).

> **Rule:** if code is not platform-specific, it lives in the shared core. Only HAL
> implementations differ between firmware and simulator. Target ≥ 80% shared code.

---

## Layering

```text
┌─────────────────────────────────────────┐
│               SHARED CORE               │
│                                         │
│ UI (LVGL screens + widgets)             │
│ ViewModels (MVVM presentation state)    │
│ App state machine + navigation          │
│ Domain (Note, Tag, AudioClip, Settings) │
│ Services (NoteService, AudioService,    │
│           SyncClient, SettingsService)  │
│ Repositories (over Storage HAL)         │
│                                         │
│ ── HAL interfaces (pure virtual) ──     │
│ IDisplay · IInput · IAudio · IStorage · │
│ IClock · IPower · ILog                  │
└─────────┬────────────────────┬──────────┘
          │                    │
          ▼                    ▼
  ┌──────────────┐   ┌──────────────────┐
  │ Firmware HAL │   │ Simulator HAL    │
  │ (ESP-IDF)    │   │ (SDL2 + desktop) │
  └──────────────┘   └──────────────────┘
```

MVVM lives **in the core**, not in the simulator only — this resolves the previous
spec's firmware-vs-simulator architecture mismatch. Both clients get the same
ViewModels and the same LVGL screens.

---

## What is shared

- **UI**: every LVGL screen and widget (see [UI_UX.md](UI_UX.md)).
- **ViewModels**: presentation state + commands bound to the UI.
- **App state machine**: splash → home → list → recording → playback → settings.
- **Navigation**: the screen graph and transitions.
- **Domain models**: `Note`, `Tag`, `AudioClip`, `Settings`.
- **Services**: note CRUD, audio record/playback orchestration, the sync client
  (local last-writer-wins merge), settings.
- **Local persistence**: SQLite schema + repositories (queried via the Storage HAL).

## What is NOT shared (HAL only)

Hardware access, OS APIs, and the event loop entry point.

---

## HAL interfaces

Pure-virtual C++ interfaces. Firmware and simulator each provide one implementation.

| Interface | Responsibility | Firmware impl | Simulator impl |
|---|---|---|---|
| `IDisplay` | flush a 200×200 1-bit framebuffer; request full/partial refresh | EPD panel driver | SDL renderer (emulated e-ink) |
| `IInput` | deliver touch/tap/swipe/long-press + button events | FT6336 + GPIO buttons | SDL mouse/touch + keyboard |
| `IAudio` | start/stop capture (16 kHz mono), play back, level metering | ES8311 over I²S | SDL audio (host mic/speaker) |
| `IStorage` | open SQLite, read/write files (audio blobs) | microSD (FAT32) | local filesystem |
| `IClock` | wall-clock time, monotonic time, sleep/timers | PCF85063 RTC + system | std::chrono |
| `IPower` | battery level, deep sleep, wake reasons | ADC + esp_sleep | no-op / faked battery |
| `ILog` | structured logging | ESP_LOG / UART | stdout |

### Design notes
- **`IDisplay`** works in 1-bit. The core never assumes color or grayscale.
  Refresh hints (`Full` vs `Partial`) let the simulator emulate ghosting/timing
  and let the firmware minimize flashing.
- **`IAudio`** capture format is fixed at 16 kHz mono ([ADR-0004](ADR/ADR-0004%20%E2%80%94%20Off-Device%20Transcription)).
- **`IPower`** on the simulator is a stub so power-related UI can still be exercised.

---

## Threading model

- One **UI thread** runs the LVGL loop and ViewModels.
- Blocking work (audio encode, file IO, network) runs off the UI thread and posts
  results back via the app's event queue. On firmware these are FreeRTOS tasks; on
  the simulator they are `std::thread`s. The core exposes a small task/dispatch
  abstraction so service code is identical on both.

---

## Build layout (suggested)

```text
/core         # shared library (no platform headers)
  /domain  /services  /repositories  /ui  /viewmodels  /sync
  /hal        # interface headers only (IDisplay.h, IAudio.h, …)
/firmware     # ESP-IDF app: HAL impl + main
/simulator    # SDL2 app: HAL impl + main
/third_party  # LVGL, SQLite, etc.
```

`core` must compile for both the Xtensa (ESP-IDF) and host (desktop) toolchains
with no `#ifdef PLATFORM` outside the HAL implementations.

---

## Why this matters

The previous specs *claimed* shared UI but described two different architectures.
Pinning MVVM and all services into one library — with hardware reached only through
eight small interfaces — is what actually delivers "the simulator is the device".
