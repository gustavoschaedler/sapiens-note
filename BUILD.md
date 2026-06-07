# Building SapiensNote

A shared C++ core drives the desktop simulators, the web dashboard, and the
firmware — no server, no cloud account. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/SHARED_CORE.md](docs/SHARED_CORE.md).

## Requirements

- CMake ≥ 3.20
- A C++23 compiler (Apple Clang 16+/Clang 17+/GCC 13+)
- **SDL2** — windowed simulator + audio (`brew install sdl2` / `apt install libsdl2-dev`)
- **sqlite3** — local persistence (system library; present on macOS/most Linux)
- *(optional)* network access to fetch **LVGL** when `-DSAPIENS_WITH_LVGL=ON`

Without SDL2/sqlite3 the core + tests + CLI still build.

## Versioning

The build version is the single source of truth in the top-level `CMakeLists.txt`:

```cmake
project(SapiensNote LANGUAGES CXX VERSION 0.1.0)   # ← bump here
```

At configure time CMake generates `<build>/generated/sapiens/Version.h` with
`SAPIENS_VERSION`, `SAPIENS_GIT_HASH` (short git hash, or `nogit`),
`SAPIENS_BUILD_DATE`, and the combined `SAPIENS_BUILD_VERSION`
(`0.1.0+abc1234 (2026-06-07)`). The version is shown on the simulators' splash
screen, in the web dashboard header, and in `GET /api/info`. To cut a new build,
bump the `VERSION` and re-configure (`cmake -B build -S .`).

## Configure & build

```bash
cmake -B build -S .
cmake --build build -j
```

Targets:

| Target | Path | What it is |
|---|---|---|
| `sapienscore` | `build/core/libsapienscore.a` | shared core: domain, services, app state, **sync client** |
| `sapiensstore` | `build/storage/libsapiensstore.a` | **SQLite** NoteRepository |
| `sapiens_tests` | `build/tests/sapiens_tests` | core unit tests (52 checks) |
| `sapiens_sqlite_tests` | `build/tests/sapiens_sqlite_tests` | SQLite repo tests (19 checks) |
| `sapiens_sync_tests` | `build/tests/sapiens_sync_tests` | sync push/pull/LWW/tombstone (11 checks) |
| `sapiens_core_extra_tests` | `build/tests/sapiens_core_extra_tests` | extra core/domain coverage (128 checks) |
| `sapiens_app_tests` | `build/tests/sapiens_app_tests` | AppController state/navigation (81 checks) |
| `sapiens_render_tests` | `build/tests/sapiens_render_tests` | Canvas/Renderer drawing (57 checks) |
| `sapiens-cli` | `build/cli/sapiens-cli` | headless core demo |
| `sapiens-sim` | `build/simulator/sapiens-sim` | SDL2 simulator (hand renderer, SQLite, real mic) |

All six suites (**348 checks**) run via `ctest --test-dir build`.

## Run

```bash
ctest --test-dir build --output-on-failure   # all test suites
./build/cli/sapiens-cli                          # headless core demo
./build/simulator/sapiens-sim                    # simulator window (persists to ~/.sapiensnote-sim)
./build/simulator/sapiens-sim --headless         # logic smoke test, no window
./build/simulator/sapiens-sim --shot ui.ppm      # render the UI to an image
```

### Simulator controls
Click = tap · **R** = record/stop · **P** = open the player for the open note ·
**Space** = play/pause · **S** = settings · **Esc/B** = back · **Q** = quit.

The window opens at the panel's **real size (200×200, 1:1)** so the anti-aliased
font is crisp (no stretching). Use `--scale N` to enlarge (N>1 scales with linear
filtering, so it gets softer); for a large *and* crisp view use the LVGL simulator
(vector fonts, `--scale` renders at native resolution).

Open a note that has a recording (▶ marker in the list) → tap **> Play** to enter the
**player** (play/pause, stop, progress bar, elapsed/total time). A note can be removed
with **Delete → confirm**. Recordings are saved to `~/.sapiensnote-sim/audio/` as
`rec-YYYYMMDD-HHMMSS-mmm.wav`; in the list a recorded note shows its **date-time name
followed by the recording duration** (e.g. `Jun 05, 21:29  0:01`).

**List:** shows **5 notes per page** with a per-row counter (`0001`…), a header line
with the **total count and page** (`8 notes  1/2`), and **Up / Dn** buttons to
paginate. Recording shows a large **STOP** button (size of the REC box).

**Scroll speed:** Settings has a **slider** (slow→fast) controlling the marquee
speed (persisted in `~/.sapiensnote-sim/scroll_speed.txt`).

**Name format:** Settings → *Next format* cycles through **5 date-time formats**
(persisted in `~/.sapiensnote-sim/name_format.txt`, shared by both simulators):
`2026-06-05 21:10` · `05/06/2026 21:10` · `Jun 05, 21:10` · `2026-06-05 21:10:08` ·
`21:10 - 05 Jun 2026`.

Long note names **scroll** (marquee) when they don't fit. Render any screen to an
image with `--shot file.ppm [--screen list|detail|player|settings|confirm|recording]`.

### UI font
The UI uses a **rounded, anti-aliased** font (Arial Rounded Bold), baked from a TTF
into `simulator/src/GeneratedFont.h` by `tools/genfont.cpp` — 8-bit coverage so the
simulator can alpha-blend smooth edges (the small e-ink buffer is then **bilinearly**
upscaled to the window). The firmware thresholds the same buffer to 1-bit. Runtime
needs no TTF. Regenerate from any typeface:
```bash
git clone --depth 1 https://github.com/nothings/stb /tmp/stb
c++ -std=c++17 -I/tmp/stb tools/genfont.cpp -o /tmp/genfont
/tmp/genfont "/path/to/Rounded.ttf" simulator/src/GeneratedFont.h 15
cmake --build build --target sapiens-sim
```

The windowed simulator records from your microphone via SDL (16 kHz mono WAV under
`~/.sapiensnote-sim/audio`); if no mic is available it falls back to a silent clip so
the flow still works. Notes persist in `~/.sapiensnote-sim/sapiensnote.db` (SQLite).

## Build with Docker (no local setup)

Don't want to install a compiler, SDL2, or sqlite3? Build inside a container. The
test suite runs during the image build, so a successful build already proves it works.

```bash
docker build -t sapiensnote .                      # builds everything + runs tests
docker run --rm sapiensnote                         # re-run tests + headless smoke
```

Render the UI to an image on your host (no display needed):

```bash
mkdir -p out
docker run --rm -v "$PWD/out:/out" sapiensnote \
    ./build/simulator/sapiens-sim --shot /out/ui.ppm --scale 4
# open out/ui.ppm  (macOS: `sips -s format png out/ui.ppm --out out/ui.png && open out/ui.png`)
```

Or via compose: `docker compose run --rm tests` / `docker compose run --rm shot`.

### Interactive window from Docker
The simulator is a GUI app. To show the window from the Linux container you need an
X server on the host:

- **macOS:** install XQuartz (`brew install --cask xquartz`), launch it, enable
  *Preferences → Security → Allow connections from network clients*, then
  `xhost + 127.0.0.1` and `docker compose run --rm sim-x11`.
- **Linux:** `xhost +local: && docker run --rm -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix sapiensnote ./build/simulator/sapiens-sim`.

There is **no microphone inside the container**, so recording falls back to a
silent clip. **For the real interactive experience (window + mic), run the native
build** (`./build/simulator/sapiens-sim`) — that's the recommended way to actually use
the simulator.

## Optional: LVGL simulator

A second simulator built with real LVGL 9 widgets, made to **mirror** the main
simulator and the device: same screens (list / detail / player / recording /
settings / delete-confirm), monochrome look, the **same rounded font**, and **real
audio** (record + playback via the shared `SdlAudio`). It renders at **native
resolution** (`--scale N`, default 2 → crisp vector fonts, no jagged upscaling) and
shares the core/data. Fetches LVGL on first configure:

```bash
cmake -B build-lvgl -S . -DSAPIENS_WITH_LVGL=ON
cmake --build build-lvgl --target sapiens-sim-lvgl -j
./build-lvgl/simulator_lvgl/sapiens-sim-lvgl                 # window (record + play)
./build-lvgl/simulator_lvgl/sapiens-sim-lvgl --scale 3       # bigger, still crisp
./build-lvgl/simulator_lvgl/sapiens-sim-lvgl --shot ui.ppm --screen player
./build-lvgl/simulator_lvgl/sapiens-sim-lvgl --headless      # off-screen (CI)
```
The LVGL sim uses LVGL's **built-in Montserrat** bitmap fonts (stable, no runtime
rasterizer). The runtime TTF rasterizer (`tiny_ttf`/stb) proved unstable on x86_64
/ Rosetta builds (assert / SIGILL), so it was removed. It starts **empty** — only
your real recordings appear (no demo notes). For a crisp **rounded** font, the main
simulator (`sapiens-sim`) uses a baked rounded font with no runtime rasterizer.

## Optional: web dashboard (transcribe + download)

`sapiens-web` is a small local web app (single C++ binary, cpp-httplib) that reads
the **same** `~/.sapiensnote-sim` DB + audio the simulators use, and lets you manage
each recording from the browser: **play in-page, download the `.wav`, transcribe /
re-transcribe, and view/download the transcript** (`.txt`). The UI mirrors the
device's **e-ink** look (black & white, rounded buttons). Results are written back
through the shared core (`apply_transcription`), so transcripts also appear in the
simulator (on opening the note / **Sync now**).

```bash
cmake -B build -S . -DSAPIENS_WITH_WEB=ON     # fetches cpp-httplib (one header)
cmake --build build --target sapiens-web -j
./build/webtool/sapiens-web                    # http://127.0.0.1:8080 (engine: whisper)
```

Transcription engine is **pluggable** (transcript only for now):

| Engine | Flag | Needs |
|---|---|---|
| **whisper.cpp** (local, offline) | `--engine whisper` *(default)* | `SAPIENS_WHISPER_MODEL=/path/ggml-base.bin` (and `whisper-cli` on `PATH`, or `SAPIENS_WHISPER_BIN`) |
| **OpenAI Whisper API** (cloud) | `--engine openai` | `OPENAI_API_KEY` (built only if libcurl is found) |
| **fake** (demo, no engine) | `--engine fake` | nothing — writes placeholder text to test the round-trip |

```bash
# local, offline
SAPIENS_WHISPER_MODEL=~/models/ggml-base.bin ./build/webtool/sapiens-web
# cloud
OPENAI_API_KEY=sk-… ./build/webtool/sapiens-web --engine openai
# headless: transcribe every pending recording, then exit (no server)
./build/webtool/sapiens-web --engine fake --once
```

Flags: `--port N` (default 8080, binds `127.0.0.1` only), `--db PATH`,
`--audio-dir DIR`, `--once`. Run it natively on the host (it shares the WAL SQLite
DB with the simulator; `busy_timeout` lets both write safely).

## Firmware (ESP-IDF) — scaffold

`firmware/` is a real ESP-IDF project that reuses the same `core` and the same
`Canvas`/`Renderer`, providing an ESP-IDF HAL. It builds with `idf.py` (not this
CMake) and has `TODO(board)` markers for the GPIO map / EPD / ES8311 specifics.
See [firmware/README.md](firmware/README.md).

## Project layout

```
core/         shared C++ library (no platform deps) + host/ HAL impls + SyncClient
  include/sapiens/   Types, Hal, Id, NoteRepository, NoteService, AudioService,
                  AppController, Sync, host/Host.h
storage/      SqliteNoteRepository (libsapiensstore)
tests/        CTest suites: core, sqlite, sync
cli/          headless demo
simulator/    SDL2 desktop app (DesktopHAL + ink-level Canvas + baked rounded font + Renderer + SdlAudio)
simulator_lvgl/  optional LVGL 9 UI that mirrors the simulator (Montserrat fonts, opt-in)
webtool/      optional e-ink web dashboard 'sapiens-web' (cpp-httplib): play/download/
              transcribe recordings; pluggable whisper.cpp / OpenAI engines (opt-in)
tools/        genfont.cpp (bakes a TTF -> GeneratedFont.h)
firmware/     ESP-IDF project (scaffold): reuses core + renderer, ESP-IDF HAL
docs/         full specification set
```

## Status

Verified on host (build + tests):
- core domain/services/app-state, in-memory + **SQLite** repositories
- **sync client** (delta, last-writer-wins, tombstones) via a fake server
- SDL2 simulator (UI render + interaction); LVGL simulator (init + UI from core)

Needs hardware/toolchain to verify:
- **firmware** (requires ESP-IDF; pin map + EPD/ES8311 bring-up are `TODO(board)`)
- real microphone capture quality; LVGL on the actual e-paper panel

## Hardware bring-up

The desktop simulators, web dashboard, and core are complete and verified on the
host. To run on the physical board, the firmware scaffold still needs:

- the `TODO(board)` GPIO map / EPD / ES8311 details filled from the Waveshare
  schematic and vendor demo (see [firmware/README.md](firmware/README.md));
- the firmware's in-memory repo swapped for SQLite-on-SD.
