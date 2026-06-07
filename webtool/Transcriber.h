#pragma once
// Pluggable transcription providers for the sapiens-web dashboard.
//   - WhisperCppTranscriber: shells out to a local whisper.cpp CLI (offline).
//   - OpenAiTranscriber:     POSTs the WAV to the OpenAI Whisper API (cloud).
//   - FakeTranscriber:       canned text, for tests.
// All take a WAV path and return plain transcript text. Summary is out of scope
// for now (the caller passes an empty summary to NoteService::apply_transcription).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h> // getpid

namespace sapiens::web {

struct TranscriptionResult {
    std::string text;
    std::string language; // best-effort; may be empty/"auto"
};

struct ITranscriber {
    virtual ~ITranscriber() = default;
    virtual TranscriptionResult transcribe(const std::string& wav_path) = 0;
};

inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

inline std::string getenv_str(const char* k, const std::string& def = "") {
    const char* v = std::getenv(k);
    return v ? std::string(v) : def;
}

// POSIX single-quote shell escaping for a path passed to std::system.
inline std::string shq(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

// Local whisper.cpp CLI. Needs SAPIENS_WHISPER_MODEL (path to a ggml model);
// the binary defaults to `whisper-cli` (override with SAPIENS_WHISPER_BIN).
class WhisperCppTranscriber : public ITranscriber {
public:
    WhisperCppTranscriber()
        : bin_(getenv_str("SAPIENS_WHISPER_BIN", "whisper-cli")),
          model_(getenv_str("SAPIENS_WHISPER_MODEL")) {}

    TranscriptionResult transcribe(const std::string& wav) override {
        if (model_.empty())
            throw std::runtime_error(
                "SAPIENS_WHISPER_MODEL not set (path to a ggml-*.bin model)");
        static std::atomic<unsigned> seq{0};
        std::string base = "/tmp/sapiens-whisper-" + std::to_string(::getpid()) + "-" +
                           std::to_string(seq.fetch_add(1));
        std::string txt = base + ".txt";
        std::string cmd = shq(bin_) + " -m " + shq(model_) + " -f " + shq(wav) +
                          " -otxt -nt -l auto -of " + shq(base) + " >/dev/null 2>&1";
        int rc = std::system(cmd.c_str());
        if (rc != 0)
            throw std::runtime_error(
                "whisper failed (rc=" + std::to_string(rc) +
                "); check SAPIENS_WHISPER_BIN / SAPIENS_WHISPER_MODEL and the WAV path");
        std::ifstream f(txt);
        if (!f) throw std::runtime_error("whisper produced no output (" + txt + ")");
        std::stringstream ss;
        ss << f.rdbuf();
        std::remove(txt.c_str());
        return {trim(ss.str()), "auto"};
    }

private:
    std::string bin_, model_;
};

#ifdef SAPIENS_HAVE_CURL
} // namespace sapiens::web
#include <curl/curl.h>
namespace sapiens::web {

// OpenAI Whisper API. Needs OPENAI_API_KEY. response_format=text -> the HTTP
// body is the transcript itself, so no JSON parsing is required.
class OpenAiTranscriber : public ITranscriber {
public:
    OpenAiTranscriber()
        : key_(getenv_str("OPENAI_API_KEY")),
          model_(getenv_str("SAPIENS_OPENAI_MODEL", "whisper-1")) {}

    TranscriptionResult transcribe(const std::string& wav) override {
        if (key_.empty()) throw std::runtime_error("OPENAI_API_KEY not set");
        CURL* c = curl_easy_init();
        if (!c) throw std::runtime_error("curl init failed");

        std::string body;
        curl_mime* mime = curl_mime_init(c);
        curl_mimepart* p = curl_mime_addpart(mime);
        curl_mime_name(p, "file");
        curl_mime_filedata(p, wav.c_str());
        p = curl_mime_addpart(mime);
        curl_mime_name(p, "model");
        curl_mime_data(p, model_.c_str(), CURL_ZERO_TERMINATED);
        p = curl_mime_addpart(mime);
        curl_mime_name(p, "response_format");
        curl_mime_data(p, "text", CURL_ZERO_TERMINATED);

        std::string auth = "Authorization: Bearer " + key_;
        curl_slist* hdr = curl_slist_append(nullptr, auth.c_str());
        curl_easy_setopt(c, CURLOPT_URL,
                         "https://api.openai.com/v1/audio/transcriptions");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, &write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);

        CURLcode rc = curl_easy_perform(c);
        long http = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
        curl_mime_free(mime);
        curl_slist_free_all(hdr);
        curl_easy_cleanup(c);

        if (rc != CURLE_OK)
            throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(rc));
        if (http >= 300)
            throw std::runtime_error("OpenAI HTTP " + std::to_string(http) + ": " + body);
        return {trim(body), "auto"};
    }

private:
    static size_t write_cb(char* ptr, size_t sz, size_t n, void* ud) {
        static_cast<std::string*>(ud)->append(ptr, sz * n);
        return sz * n;
    }
    std::string key_, model_;
};
#endif // SAPIENS_HAVE_CURL

// Canned text — used by tests so the scan/apply loop runs without an engine.
class FakeTranscriber : public ITranscriber {
public:
    explicit FakeTranscriber(std::string text) : text_(std::move(text)) {}
    TranscriptionResult transcribe(const std::string&) override {
        if (const char* d = std::getenv("SAPIENS_FAKE_DELAY_MS")) { // dev: simulate work
            int ms = std::atoi(d);
            if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        return {text_, "en"};
    }

private:
    std::string text_;
};

inline std::unique_ptr<ITranscriber> make_transcriber(const std::string& engine) {
    if (engine == "fake") // demo/offline: canned text, no real engine needed
        return std::make_unique<FakeTranscriber>(
            "[demo transcript] This is placeholder text produced by the 'fake' "
            "engine so the dashboard round-trip can be tested without installing "
            "whisper.cpp or setting an API key.");
    if (engine == "openai") {
#ifdef SAPIENS_HAVE_CURL
        return std::make_unique<OpenAiTranscriber>();
#else
        throw std::runtime_error(
            "engine 'openai' unavailable: sapiens-web was built without libcurl");
#endif
    }
    if (engine == "whisper" || engine.empty())
        return std::make_unique<WhisperCppTranscriber>();
    throw std::runtime_error("unknown engine: " + engine + " (use whisper|openai)");
}

} // namespace sapiens::web
