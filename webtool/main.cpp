// sapiens-web — a local e-ink-styled dashboard to manage the simulator's
// recordings: list them, play in the browser, download the WAV, transcribe /
// re-transcribe per audio, and view/download the transcript. It reads/writes the
// SAME SQLite DB + audio dir the simulators use (~/.sapiensnote-sim), writing
// results back through the shared core (NoteService::apply_transcription), so
// transcripts also show up in the simulator.
//
//   sapiens-web [--engine whisper|openai] [--port N] [--db PATH] [--audio-dir DIR]
//            [--once]   # transcribe all pending notes then exit (headless)
//
// Engines (see webtool/Transcriber.h):
//   whisper : local whisper.cpp  (SAPIENS_WHISPER_BIN, SAPIENS_WHISPER_MODEL)
//   openai  : OpenAI Whisper API (OPENAI_API_KEY)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "httplib.h"

#include "Logo.h"
#include "Transcriber.h"
#include "page.h"
#include "sapiens/Format.h"
#include "sapiens/Id.h"
#include "sapiens/NoteService.h"
#include "sapiens/SqliteNoteRepository.h"
#include "sapiens/Types.h"
#include "sapiens/Version.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

namespace {

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? std::string(h) : ".";
}

std::string basename_of(const std::string& path) {
    auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// Minimal JSON string escaping.
std::string jesc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

const char* status_str(NoteStatus s) {
    return s == NoteStatus::Transcribed ? "transcribed" : "pending";
}

} // namespace

int main(int argc, char** argv) {
    std::string db = home_dir() + "/.sapiensnote-sim/sapiensnote.db";
    std::string audio_dir = home_dir() + "/.sapiensnote-sim/audio";
    std::string engine = web::getenv_str("SAPIENS_TRANSCRIBER", "whisper");
    int port = 8080;
    bool once = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--db") db = next();
        else if (a == "--audio-dir") audio_dir = next();
        else if (a == "--engine") engine = next();
        else if (a == "--port") port = std::atoi(next());
        else if (a == "--once") once = true;
        else if (a == "--help" || a == "-h") {
            std::printf("usage: sapiens-web [--engine whisper|openai] [--port N] "
                        "[--db PATH] [--audio-dir DIR] [--once]\n");
            return 0;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(audio_dir, ec);

    host::SystemClock clock;
    host::ConsoleLog log{true};
    IdGenerator ids{clock};
    SqliteNoteRepository repo{db};
    NoteService notes{repo, ids, clock, log};

    std::unique_ptr<web::ITranscriber> engine_ptr;
    try {
        engine_ptr = web::make_transcriber(engine);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "engine error: %s\n", e.what());
        return 1;
    }

    std::mutex db_mtx; // guard the single SQLite connection across http threads

    // Resolve a note's WAV: prefer the stored audio_key, else <audio-dir>/<base>.
    auto resolve_wav = [&](const Note& n) -> std::string {
        if (n.audio_key.empty()) return "";
        if (std::filesystem::exists(n.audio_key)) return n.audio_key;
        std::string alt = audio_dir + "/" + basename_of(n.audio_key);
        return std::filesystem::exists(alt) ? alt : n.audio_key;
    };

    // Transcribe one note id (no lock held during the slow engine call).
    auto transcribe_note = [&](const std::string& id) -> std::string {
        Note n;
        {
            std::lock_guard<std::mutex> lk(db_mtx);
            auto opt = notes.get_note(id);
            if (!opt) throw std::runtime_error("note not found");
            n = *opt;
        }
        std::string wav = resolve_wav(n);
        if (wav.empty() || !std::filesystem::exists(wav))
            throw std::runtime_error("audio file not found for note " + id);
        web::TranscriptionResult r = engine_ptr->transcribe(wav); // slow, unlocked
        {
            std::lock_guard<std::mutex> lk(db_mtx);
            notes.apply_transcription(id, r.text, /*summary*/ "", /*key_points*/ {},
                                      r.language);
        }
        return r.text;
    };

    if (once) { // headless: transcribe every pending note, then exit
        int done = 0;
        std::vector<Note> list;
        { std::lock_guard<std::mutex> lk(db_mtx); list = notes.list_notes(); }
        for (const auto& n : list) {
            if (n.audio_key.empty() || n.status == NoteStatus::Transcribed) continue;
            try {
                log.info("transcribing " + n.id);
                transcribe_note(n.id);
                ++done;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "  failed %s: %s\n", n.id.c_str(), e.what());
            }
        }
        std::printf("transcribed %d note(s)\n", done);
        return 0;
    }

    httplib::Server svr;

    // Serve the WAVs with a length-aware content provider so cpp-httplib emits
    // Accept-Ranges + handles Range requests (206). Without this the browser's
    // <audio> can't seek and shows 0:00 duration / stalls after a few seconds.
    svr.Get(R"(/media/([^/]+))",
            [&](const httplib::Request& req, httplib::Response& res) {
                std::string name = req.matches[1];
                if (name.find("..") != std::string::npos) { res.status = 400; return; }
                std::string path = audio_dir + "/" + name;
                std::error_code fe;
                auto sz = std::filesystem::file_size(path, fe);
                if (fe) { res.status = 404; res.set_content("not found", "text/plain"); return; }
                auto fp = std::make_shared<std::ifstream>(path, std::ios::binary);
                if (!*fp) { res.status = 404; return; }
                res.set_header("Accept-Ranges", "bytes");
                res.set_content_provider(
                    static_cast<size_t>(sz), "audio/wav",
                    [fp](size_t offset, size_t length, httplib::DataSink& sink) {
                        fp->seekg(static_cast<std::streamoff>(offset));
                        char buf[16384];
                        size_t remaining = length;
                        while (remaining > 0) {
                            size_t want = std::min(remaining, sizeof(buf));
                            fp->read(buf, static_cast<std::streamsize>(want));
                            std::streamsize got = fp->gcount();
                            if (got <= 0) return false;
                            if (!sink.write(buf, static_cast<size_t>(got))) return false;
                            remaining -= static_cast<size_t>(got);
                        }
                        return true;
                    });
            });

    // Build the page once with the logo data URI spliced in (instead of on every
    // request). The engine/version are dynamic, so they stay in /api/info.
    static const std::string index_html = [] {
        std::string h = web::kIndexHtml;
        const std::string tok = "{{LOGO}}";
        if (auto p = h.find(tok); p != std::string::npos)
            h.replace(p, tok.size(), web::kLogoDataUri);
        return h;
    }();
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(index_html, "text/html; charset=utf-8");
    });

    svr.Get("/api/info", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"engine\":\"" + jesc(engine) +
                            "\",\"version\":\"" + jesc(SAPIENS_VERSION) + "\"}",
                        "application/json");
    });

    svr.Get("/api/notes", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(db_mtx);
        auto list = notes.list_notes();
        std::string j = "[";
        int idx = 0;
        for (const auto& n : list) {
            std::string title = n.title.empty() ? "Untitled" : n.title;
            if (idx) j += ",";
            j += "{\"id\":\"" + jesc(n.id) + "\",";
            j += "\"idx\":" + std::to_string(++idx) + ",";
            j += "\"title\":\"" + jesc(title) + "\",";
            j += "\"status\":\"" + std::string(status_str(n.status)) + "\",";
            j += "\"audio\":\"" + jesc(basename_of(n.audio_key)) + "\",";
            std::uintmax_t sz = 0;
            if (!n.audio_key.empty()) {
                std::error_code se;
                auto s = std::filesystem::file_size(resolve_wav(n), se);
                if (!se) sz = s;
            }
            j += "\"size\":" + std::to_string(static_cast<unsigned long long>(sz)) + ",";
            j += "\"has_transcript\":" + std::string(n.transcript.empty() ? "false" : "true") + ",";
            j += "\"language\":\"" + jesc(n.language) + "\"}";
        }
        j += "]";
        res.set_content(j, "application/json");
    });

    svr.Get(R"(/api/transcript/([^/]+))",
            [&](const httplib::Request& req, httplib::Response& res) {
                std::string id = req.matches[1];
                std::optional<Note> n;
                { std::lock_guard<std::mutex> lk(db_mtx); n = notes.get_note(id); }
                if (!n) { res.status = 404; res.set_content("not found", "text/plain"); return; }
                if (req.has_param("download")) {
                    std::string fn = (n->title.empty() ? "transcript" : n->title) + ".txt";
                    res.set_header("Content-Disposition",
                                   "attachment; filename=\"" + fn + "\"");
                }
                res.set_content(n->transcript, "text/plain; charset=utf-8");
            });

    svr.Delete(R"(/api/notes/([^/]+))",
               [&](const httplib::Request& req, httplib::Response& res) {
                   std::string id = req.matches[1];
                   std::lock_guard<std::mutex> lk(db_mtx);
                   if (!notes.soft_delete(id)) {
                       res.status = 404;
                       res.set_content("not found", "text/plain");
                       return;
                   }
                   res.set_content("{\"ok\":true}", "application/json");
               });

    svr.Post(R"(/api/transcribe/([^/]+))",
             [&](const httplib::Request& req, httplib::Response& res) {
                 std::string id = req.matches[1];
                 try {
                     std::string text = transcribe_note(id);
                     res.set_content("{\"ok\":true,\"transcript\":\"" + jesc(text) + "\"}",
                                     "application/json");
                 } catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(e.what(), "text/plain; charset=utf-8");
                 }
             });

    std::printf("sapiens-web %s on http://127.0.0.1:%d  (engine: %s)\n",
                SAPIENS_BUILD_VERSION, port, engine.c_str());
    std::printf("  db:    %s\n  audio: %s\n", db.c_str(), audio_dir.c_str());
    if (!svr.listen("127.0.0.1", port)) {
        std::fprintf(stderr, "failed to bind 127.0.0.1:%d\n", port);
        return 1;
    }
    return 0;
}
