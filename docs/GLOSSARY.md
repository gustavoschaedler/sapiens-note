# GLOSSARY

Shared vocabulary for Sapiens Note. Keep these terms consistent across all docs.

| Term | Meaning |
|---|---|
| **Device** | The 1.54" e-ink voice recorder — an ESP32-S3 + 200×200 e-paper board (`S3_ePaper_1_54`, SSD1681; same hardware as Pala Note). Runs the working ESP-IDF firmware. See [HARDWARE.md](HARDWARE.md) + [../firmware/README.md](../firmware/README.md). |
| **Firmware** | The ESP-IDF build that runs on the device — reuses the shared core + the portable 1-bit renderer through an ESP-IDF HAL (display, touch + buttons, audio, SD, RTC, Wi-Fi, power). |
| **Simulator** | The desktop app (macOS/Linux/Windows) running the UI/logic via SDL2. Two builds: a 1-bit `Canvas` one and an LVGL 9 one. |
| **Client** | Anything that links the shared core (the firmware or either simulator). |
| **Shared core** | The portable C++ library every client links (domain, app state, navigation, audio, sync client). See [SHARED_CORE.md](SHARED_CORE.md). |
| **HAL** | Hardware Abstraction Layer — the interfaces (storage, audio, clock, log) the core calls; each client supplies an implementation. |
| **Web dashboard** | `sapiens-web` — a local web page that plays, downloads, and transcribes recordings from the same SQLite DB. |
| **Capture** | Recording a voice note. |
| **Note** | The primary entity: title, optional transcript, tags, timestamps, linked audio. |
| **Audio clip** | The recorded audio file linked to a note (16 kHz mono WAV). |
| **Transcript** | Text produced from a recording by the transcription engine. |
| **Provider / engine** | A swappable transcription backend — whisper.cpp (local) or OpenAI (cloud). See [ADR-0003](ADR/ADR-0003%20%E2%80%94%20Pluggable%20Transcription%20Engine). |
| **Tag** | A user label attached to notes for organization/filtering. |
| **Sync client** | The core's transport-agnostic, offline-first delta-merge component (last-writer-wins). Used today to keep the shared local DB consistent across clients. |
| **Tombstone** | A soft-deleted record (kept so other clients learn about the deletion). |
| **User** | The single owner of all data; everything is local. |
