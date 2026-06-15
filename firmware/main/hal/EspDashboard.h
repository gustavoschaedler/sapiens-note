#pragma once
// On-demand web dashboard served on the device (Phase B). Reuses the sapiens-web
// SPA (webtool/page.h) and exposes a compatible API backed by the SD-card notes:
//   GET /                     -> the dashboard page
//   GET /api/info             -> {engine, version}
//   GET /api/notes            -> JSON list of notes
//   GET /media/<file>         -> stream a recording (WAV) from the SD card
//   GET /api/transcript/<id>  -> transcript text (?download=1 forces a .txt)
//   DELETE /api/notes/<id>    -> soft-delete a note
//   POST /api/transcribe/<id> -> 501 (transcription is off-device)
#include "sapiens/NoteService.h"

namespace sapiens::fw {

class EspDashboard {
public:
    void start(sapiens::NoteService& notes); // start the HTTP server (port 80)
    void stop();
    bool running() const;
};

} // namespace sapiens::fw
