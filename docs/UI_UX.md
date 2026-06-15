# UI_UX

Screens and interaction flows, shared by the device and the simulators
([SHARED_CORE.md](SHARED_CORE.md)). Designed for a **200×200, 1-bit monochrome
e-ink panel** with capacitive touch **and two physical buttons** (top = BOOT,
bottom = PWR).

---

## Design constraints

- **Monochrome only.** Black/white, borders, and bold type for hierarchy — no
  colour or grayscale.
- **Small canvas.** 200×200 px. One primary action per screen; large touch targets
  (≥ 44 px).
- **Slow refresh.** Partial refresh for updates; a full refresh (black↔white flash)
  on screen changes and occasionally on animated screens to clear ghosting.
- **Glanceable.** Used in seconds, then pocketed.

---

## Navigation — touch + two buttons

Every screen can be driven by **touch** or by the **two physical buttons**:

| Button | 1 click | 2 clicks | Hold |
|---|---|---|---|
| **Top** (BOOT) | focus next item | focus previous item | **start recording (~3 s)** |
| **Bottom** (PWR) | **select** focused item | **back** | — |

A **focus ring** highlights the current item for button users; touch taps work in
parallel. There are **no on-screen Back buttons** — back is the bottom button
(double-click). The shared header (brand + battery %) is the same on every screen.

```text
Splash → Home ─┬─ Notes → Note list ─REC/hold─► Recording ─► (note created)
               │                  └─ tap row ─► Note detail ─► Player
               ├─ Sync (Wi-Fi / web dashboard)
               ├─ Tags
               └─ Settings
```

---

## Screens

### Splash
Animated logo writing in letter by letter, ~4 s, then auto-advances to Home.

### Home (menu)
2×2 grid of large buttons: **Notes**, **Sync**, **Tags**, **Settings**.

### Note list
- Header: **Sapiens Note** + battery %. Three large rows/page, newest first; long
  titles **marquee-scroll**. Per-row markers: ● pending transcript, ◆ favorite,
  ▶ has audio.
- Bottom: large **REC** + page ▲/▼ when paged.
- **Footer (centered):** total notes + page (e.g. `12 notes · page 1/4`).
- Empty state: "No notes yet — hold the top button to record."

### Recording
- A blinking **● REC** pill, a big **mm:ss** timer, the hint "Tap STOP to finish",
  and a large **STOP** button. Audio streams to the SD continuously.

### Note detail
- Title (marquee). With a transcript: the full text fills the screen and
  **auto-scrolls** at the configured speed (tap to pause/resume). Without one:
  "Pending transcription — appears after you run it from the web dashboard."
- Bottom: **Play** and **Delete** fill the width (no Back).

### Player
- Title, state, progress bar, elapsed/total, and large **Play/Pause** + **Stop**.

### Sync (Wi-Fi / web dashboard)
- Explains that **Connect** brings up Wi-Fi to open the web dashboard and sync the
  clock. Connecting shows a captive-portal AP (`SapiensNote-XXXX`), then "Connected
  — open http://<ip>". The dashboard serves recordings + on-device transcription.

### Tags
- List of tags (or "No tags yet").

### Settings (touch steppers; also editable in the dashboard)
- **Text scroll** — transcript auto-scroll speed.
- **Name format** — date-time format for new note titles.
- **Sleep** — deep-sleep idle timeout (`Off` = never).
- (Hold-to-record seconds and the **Disk mode** toggle live in the dashboard.)

### Disk mode (USB drive)
When the Settings **Disk mode** toggle is on, the device reboots into a USB
mass-storage drive (the SD with `.wav` + `.txt`). A full-screen page says "hold the
top button to exit".

---

## States surfaced in the UI

- **Note processing:** pending → transcribed (the ● clears once a transcript
  exists; transcription runs on-device via the dashboard or off-device).
- **Battery:** percentage in every header; low battery → red LED blinks (when wired).
- **Recording:** an unmistakable full-screen state.
- **Scroll:** running / paused on the transcript.

On the desktop simulators, mouse click = tap; the device firmware adds the
two-button model and a focus cursor.
