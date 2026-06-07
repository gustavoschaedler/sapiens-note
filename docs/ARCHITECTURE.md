# ARCHITECTURE

Sapiens Note is an **offline-first**, personal voice-note app. You capture ideas
by voice; an AI engine turns them into text you can read and search. It runs as
two desktop **simulators** of a 1.54" e-ink voice recorder — both built from one
shared C++ core — plus a local **web dashboard** that does the transcription.

It is intentionally **personal, not enterprise**: a single user, all data local,
no accounts or multi-tenancy.

---

## Components

```text
  ┌───────────────────┐    ┌─────────────────────┐
  │ Simulator (1-bit) │    │ Simulator (LVGL 9)  │
  │ sapiens-sim       │    │ sapiens-sim-lvgl    │
  │ SDL2 + Canvas     │    │ SDL2 + LVGL widgets │
  └───────────────────┘    └─────────────────────┘
            └─────────────┬───────────┘
┌─────────────────────────┴─────────────────────────┐
│                  Shared C++ core                  │
│ domain · state machine · navigation · audio · HAL │
└───────────────────────────────────────────────────┘
                          │
                          ▼
SQLite   ~/.sapiensnote-sim/sapiensnote.db  +  audio/*.wav
                          ▲
                          │  same DB
       ┌──────────────────┴─────────────────┐
       │ Web dashboard                      │
       │ sapiens-web                        │
       │ cpp-httplib + whisper.cpp / OpenAI │
       └────────────────────────────────────┘
```

### The simulators — the app you use
Two desktop builds of the same 200×200 monochrome e-ink UI, sharing all logic:

- **`sapiens-sim`** — a hand-rolled 1-bit `Canvas` renderer with baked
  anti-aliased fonts, drawn to an SDL2 window.
- **`sapiens-sim-lvgl`** — the same screens built with real **LVGL 9** widgets.

Both capture from the host mic, play back through the host speakers, and persist
to the shared SQLite DB. See [SIMULATOR_SPEC.md](SIMULATOR_SPEC.md) and the screen
flows in [UI_UX.md](UI_UX.md). They emulate the device in [HARDWARE.md](HARDWARE.md).

### The shared C++ core
The single most important element: each client is a thin shell around one core —
domain models (`Note`, `Tag`, `AudioClip`), the app state machine and navigation,
note/audio services, the offline sync client, and a **HAL** (Hardware Abstraction
Layer) for storage, audio, clock, and logging. The simulators provide an
SDL/desktop HAL; only HAL code is platform-specific, so the two clients behave
identically by construction. See [SHARED_CORE.md](SHARED_CORE.md) and
[ADR-0002](ADR/ADR-0002%20%E2%80%94%20Shared%20UI).

### The web dashboard
`sapiens-web` (built on cpp-httplib) reads the **same** SQLite DB + audio
directory and serves a local, e-ink-styled page where you can play each
recording, download the `.wav`, **transcribe / re-transcribe** it, and read or
download the transcript. Transcription runs through a pluggable engine —
**whisper.cpp** (local) or the **OpenAI** API — and the result is written back
through the core, so it immediately shows up in the simulators. See
[ADR-0003](ADR/ADR-0003%20%E2%80%94%20Pluggable%20Transcription%20Engine).

---

## Data flow

### Capture (offline, always works)
```text
Tap record → host mic → 16 kHz mono WAV on disk
          → create Note (local SQLite) → shows in the list
```

### Transcription (off-device, on demand)
```text
Open sapiens-web → tap Transcribe on a recording
   → engine (whisper.cpp / OpenAI) returns text
   → written back via the core (apply_transcription)
   → the note shows the full transcript in the simulators
```
An e-ink MCU can't run speech recognition itself, so transcription is
**off-device** — here, the local web dashboard does it
([ADR-0004](ADR/ADR-0004%20%E2%80%94%20Off-Device%20Transcription)).

### Shared storage
The simulators and the web dashboard all open the same WAL-mode SQLite database,
so a transcription (or delete) made in one shows up in the others within a second
(the simulators poll and refresh).

---

## Technology

| Layer | Technology |
|---|---|
| Simulators | C++23, SDL2; one build adds LVGL 9 |
| Shared core | C++23 (portable; platform access only via the HAL) |
| Storage | SQLite (WAL) |
| Web dashboard | C++23, cpp-httplib |
| Transcription | whisper.cpp (local) or OpenAI Whisper API — pluggable |

---

## Principles
- **Offline first** — capture never needs the network
  ([ADR-0001](ADR/ADR-0001%20%E2%80%94%20Offline%20First)).
- **One experience** — every client from one core.
- **Personal** — single user, all data local.
- **Pluggable AI** — local or cloud transcription, swappable.
