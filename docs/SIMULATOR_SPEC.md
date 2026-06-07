# SIMULATOR_SPEC

The SapiensNote desktop simulator. Runs the **exact same UI and logic as the device**
by linking the shared C++ core ([SHARED_CORE.md](SHARED_CORE.md)) and providing a
desktop HAL built on **SDL2 + LVGL 9**. It is both the primary development surface
and the way to use SapiensNote without buying hardware.

Targets: **macOS 14+, Ubuntu 24.04+, Windows 11+**.

> Goal: the simulator is a faithful stand-in for the device, not a separate app.
> Any divergence is a bug.

---

## Responsibilities

The simulator supplies the **desktop HAL** and a `main()` entry point; everything
else (screens, ViewModels, services, sync client) comes from the shared core. It
implements the same HAL interfaces as the firmware.

| Interface | Desktop implementation |
|---|---|
| `IDisplay` | SDL renderer drawing a 200×200 1-bit framebuffer, upscaled to a window |
| `IInput` | SDL mouse/touch → tap/swipe/long-press; keyboard → buttons |
| `IAudio` | SDL audio: capture from host mic, play to host speakers (16 kHz mono) |
| `IStorage` | local filesystem + SQLite (mirrors the device layout) |
| `IClock` | `std::chrono` |
| `IPower` | stub: faked battery level + no-op sleep |
| `ILog` | stdout |

---

## e-ink emulation

The simulator renders the 1-bit framebuffer and emulates e-paper behavior so
timing/appearance match the device:

| Mode | Refresh timing | Use |
|---|---|---|
| **Development** | instant (60 FPS) | fast iteration |
| **Fast e-ink** | ~250 ms | quick realism |
| **Real device** | 800–1200 ms + ghosting | match the panel |

Ghosting is simulated by blending the previous frame under the new one on partial
refreshes, cleared on full refresh — mirroring the firmware's refresh policy.

The 200×200 surface can be upscaled (×2/×3/×4) for visibility on a desktop without
changing the logical resolution or touch coordinates.

---

## Storage layout (host)

Mirrors the device so behavior is identical:

```text
~/.sapiensnote-sim/
  sapiensnote.db            # SQLite (WAL: + -wal / -shm sidecars)
  audio/rec-*.wav           # 16 kHz mono recordings
  name_format.txt           # selected note-title format
  scroll_speed.txt          # transcript auto-scroll speed
```
The same directory is read by the web dashboard (`sapiens-web`), so transcripts
written there appear in the simulators.

---

## Input mapping

| Desktop | Maps to |
|---|---|
| Left click | tap |
| Click + drag | swipe / scroll |
| Right click or `Esc` | back |
| `R` | quick-record (PWR long-press) |
| `S` | sleep/wake (PWR short) |
| `+` / `-` | upscale factor |
| `1` / `2` / `3` | e-ink refresh mode |

---

## Developer tools (optional overlays)

Toggleable panels useful while building (not shown on the device):
- **Event viewer** — input + app events.
- **State viewer** — current app state machine + ViewModel state.
- **Sync inspector** — queued local changes and the last merge result.
- **Audio inspector** — input level, file written, encode timing.

---

## Crash safety

- SQLite in WAL mode; autosave note metadata on change.
- Same `recording.tmp` recovery flow as firmware on next launch.

---

## Build

CMake project producing a native binary per platform. Links `core`, LVGL, SQLite,
SDL2, libcurl. See the suggested layout in [SHARED_CORE.md](SHARED_CORE.md).

```text
cmake -B build -S . && cmake --build build      # produces ./build/sapiensnote-sim
```
