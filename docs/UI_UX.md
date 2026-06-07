# UI_UX

Screens and interaction flows, shared by the device and the simulators
([SHARED_CORE.md](SHARED_CORE.md)). Designed for a **200×200, 1-bit monochrome
e-ink panel** with capacitive touch and two physical buttons (PWR, BOOT).

---

## Design constraints

- **Monochrome only.** Use black/white, borders, and bold type for hierarchy —
  never color or grayscale shading.
- **Small canvas.** 200×200 px. One primary action per screen; large touch targets
  (≥ 44 px). Short labels.
- **Slow refresh.** Prefer partial refresh; avoid frequent full repaints
  (ghosting). The on-tap **invert** flash mimics the e-ink redraw and gives
  immediate touch feedback.
- **Glanceable.** The device is meant to be used in seconds, then pocketed.

---

## Navigation map

```text
┌────────────────┐
│     Splash     │   animated logo (~4 s)
└────────────────┘
         │
         ▼
┌────────────────┐
│   Home  menu   │   2×2 menu
└────────────────┘
         │   Sync · Tags · Settings open their own screens
         ▼
┌────────────────┐        ┌────────────────┐
│   Note List    │──REC──►│   Recording    │
└────────────────┘        └────────────────┘
         │
         ▼
┌────────────────┐
│  Note Detail   │   full-screen transcript · tap to pause
└────────────────┘
         │
         ▼  tap Play
┌────────────────┐
│     Player     │
└────────────────┘
```

Every screen below the Home menu shows a **Menu**/back control under the battery
to return one level up.

---

## Screens

### 1. Splash
Animated logo — the brand name writes in letter by letter, then blinks — for about
four seconds, then auto-advances to the Home menu.

### 2. Home (menu)
A 2×2 grid of large 3-D buttons: **Notes**, **Sync**, **Tags**, **Settings**.
Tapping a button flashes an e-ink colour invert, then opens that screen.

### 3. Note List
- Scrollable, paginated list of notes (newest first). Each row shows a sequence
  number, the title (marquee-scrolled if long), a ● dot if it has audio not yet
  transcribed, and a ★ if favorite.
- Top: battery icon with the percentage below it; a **Menu** button returns to Home.
- Bottom: large **REC** button, plus page up/down controls when there is more than
  one page.
- Empty state: "No notes yet — tap REC to capture one."

### 4. Recording
- Big timer (mm:ss), a live input-level meter, and a large **STOP** button.
- Visual + audible cue on start/stop.
- Audio is written to disk continuously (crash-safe).
- Stop → create the Note locally → return to the Note List with the new note on
  top, marked "pending" (no transcript yet).

### 5. Note Detail
- Title (marquee-scrolled if long).
- When a transcript exists, the full text fills the screen and **auto-scrolls
  infinitely** at the configured scroll speed (it exits the top and re-enters from
  the bottom, no bounce). **Tap the text to pause/resume** the scroll.
- When no transcript exists yet, a "Pending transcription — appears after you run
  it through the web dashboard" placeholder is shown.
- Controls: **Play** audio, **Delete** (with a confirm dialog), and back to the list.

### 6. Player
- Title, playback state, a progress bar, and play/pause/stop controls with
  elapsed/total time. Back returns to the note.

### 7. Sync
- A simple status screen reachable from the Home menu, with a back control. The
  shared **sync client** keeps the local DB consistent across the two simulators
  (last-writer-wins); see [SHARED_CORE.md](SHARED_CORE.md).

### 8. Tags
- List of existing tags (or "No tags yet"), with a back control. Tags label notes
  for organization/filtering on the Note List.

### 9. Settings
- **Scroll speed** — stepper controlling the transcript auto-scroll speed.
- **Name format** — stepper choosing how new notes are titled.
- Back control returns to Home.

---

## Input model

| Gesture / button | Action |
|---|---|
| Tap | activate control / open item (flashes invert) |
| Tap transcript | pause / resume auto-scroll |
| Swipe up/down | scroll lists |
| PWR short | wake / sleep |
| PWR long | quick-record from anywhere |
| BOOT | reserved (developer / firmware) |

On the simulators, mouse click = tap and keyboard shortcuts mirror the buttons
(documented in [SIMULATOR_SPEC.md](SIMULATOR_SPEC.md)).

---

## States surfaced in the UI

- **Note processing**: pending → transcribed (the ● dot clears when the transcript
  is written back from the web dashboard).
- **Battery**: level + low-battery warning.
- **Recording**: a clearly distinct full-screen state so it's never ambiguous
  whether the mic is live.
- **Scroll**: running / paused on the Note Detail transcript.
