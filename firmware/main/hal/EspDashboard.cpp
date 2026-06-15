#include "EspDashboard.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"

#include "EspStorage.h"
#include "DeviceSettings.h"
#include "page.h" // webtool: sapiens::web::kIndexHtml
#include "Logo.h" // webtool: sapiens::web::kLogoDataUri

#if __has_include("sapiens/Version.h")
#include "sapiens/Version.h"
#endif
#ifndef SAPIENS_VERSION
#define SAPIENS_VERSION "fw"
#endif

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.web";
httpd_handle_t s_srv = nullptr;
sapiens::NoteService* g_notes = nullptr;

std::string jesc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}
std::string basename_of(const std::string& path) {
    auto p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}
// Companion transcript path (…/rec-….wav -> …/rec-….txt) so disk mode / a pulled
// SD card shows the transcription next to the recording.
std::string txt_path(const std::string& wav) {
    auto p = wav.rfind(".wav");
    return (p == std::string::npos) ? wav + ".txt" : wav.substr(0, p) + ".txt";
}
void write_text_file(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); }
}
void urldecode(const std::string& in, std::string& out) {
    out.clear();
    auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            out += (char)(hex(in[i + 1]) * 16 + hex(in[i + 2]));
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
}
// the path segment after `prefix`, query stripped + url-decoded
std::string uri_tail(httpd_req_t* req, const char* prefix) {
    std::string uri = req->uri;
    auto q = uri.find('?');
    if (q != std::string::npos) uri = uri.substr(0, q);
    std::string tail = uri.size() > std::strlen(prefix) ? uri.substr(std::strlen(prefix)) : "";
    std::string dec;
    urldecode(tail, dec);
    return dec;
}

// ---- transcription config (NVS "tx": url, key, model) ----------------------
constexpr char TX_NS[] = "tx";
std::string cfg_get(const char* k) {
    nvs_handle_t h;
    if (nvs_open(TX_NS, NVS_READONLY, &h) != ESP_OK) return "";
    char buf[300];
    size_t len = sizeof(buf);
    esp_err_t e = nvs_get_str(h, k, buf, &len);
    nvs_close(h);
    return e == ESP_OK ? std::string(buf) : std::string();
}
void cfg_set(const char* k, const std::string& v) {
    nvs_handle_t h;
    if (nvs_open(TX_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, k, v.c_str());
    nvs_commit(h);
    nvs_close(h);
}
bool tx_configured() { return !cfg_get("url").empty(); }

// POST the WAV to an OpenAI-compatible /audio/transcriptions endpoint (cloud or
// local) and return the transcript text. Streams the file so long clips fit.
bool transcribe_file(const std::string& path, std::string& out_text, std::string& err) {
    std::string url = cfg_get("url");
    std::string key = cfg_get("key");
    std::string model = cfg_get("model");
    if (model.empty()) model = "whisper-1";
    if (url.empty()) { err = "not configured"; return false; }

    struct stat st;
    if (stat(path.c_str(), &st) != 0) { err = "audio file missing"; return false; }
    long fsize = (long)st.st_size;

    const char* B = "----sapiensnote7e3fboundary";
    std::string pre = std::string("--") + B +
        "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + model + "\r\n--" + B +
        "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    std::string post = std::string("\r\n--") + B + "--\r\n";
    int content_len = (int)(pre.size() + fsize + post.size());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 60000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach; // https (OpenAI); ignored for http
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 4096;
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    std::string ctype = std::string("multipart/form-data; boundary=") + B;
    esp_http_client_set_header(cl, "Content-Type", ctype.c_str());
    if (!key.empty()) {
        std::string auth = "Bearer " + key;
        esp_http_client_set_header(cl, "Authorization", auth.c_str());
    }

    bool ok = false;
    if (esp_http_client_open(cl, content_len) != ESP_OK) {
        err = "connection failed";
    } else {
        esp_http_client_write(cl, pre.data(), pre.size());
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            char buf[4096];
            size_t r;
            while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0)
                esp_http_client_write(cl, buf, r);
            std::fclose(f);
        }
        esp_http_client_write(cl, post.data(), post.size());
        esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        std::string body;
        char rb[1024];
        int n;
        while ((n = esp_http_client_read(cl, rb, sizeof(rb))) > 0) body.append(rb, n);
        if (status < 200 || status >= 300) {
            err = "HTTP " + std::to_string(status) + ": " + body.substr(0, 180);
        } else if (cJSON* root = cJSON_Parse(body.c_str())) {
            cJSON* t = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(t) && t->valuestring) { out_text = t->valuestring; ok = true; }
            cJSON_Delete(root);
            if (!ok) err = "no 'text' field in response";
        } else {
            err = "bad response";
        }
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return ok;
}

// The SPA with the {{LOGO}} placeholder replaced by the embedded data URI (built
// once, since the logo is large).
const std::string& index_html() {
    static const std::string html = [] {
        std::string h = sapiens::web::kIndexHtml;
        const std::string tok = "{{LOGO}}";
        auto p = h.find(tok);
        if (p != std::string::npos) h.replace(p, tok.size(), sapiens::web::kLogoDataUri);
        return h;
    }();
    return html;
}

esp_err_t h_index(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const std::string& h = index_html();
    httpd_resp_send(req, h.c_str(), h.size());
    return ESP_OK;
}

esp_err_t h_info(httpd_req_t* req) {
    bool cfg = tx_configured();
    std::string j = std::string("{\"engine\":\"") + (cfg ? "remote" : "none") +
                    "\",\"configured\":" + (cfg ? "true" : "false") +
                    ",\"version\":\"" + SAPIENS_VERSION + "\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, j.c_str());
    return ESP_OK;
}

esp_err_t h_config_get(httpd_req_t* req) {
    namespace s = sapiens::fw::settings;
    std::string j = "{\"url\":\"" + jesc(cfg_get("url")) + "\",\"model\":\"" +
                    jesc(cfg_get("model")) + "\",\"has_key\":" +
                    (cfg_get("key").empty() ? "false" : "true") +
                    ",\"sleep_s\":" + std::to_string(s::sleep_secs()) +
                    ",\"hold_s\":" + std::to_string(s::hold_secs()) +
                    ",\"disk\":" + (s::disk_mode() ? "true" : "false") +
                    ",\"scroll\":" + std::to_string(s::scroll()) +
                    ",\"scroll_max\":" + std::to_string(s::scroll_max()) +
                    ",\"name_fmt\":" + std::to_string(s::name_fmt() + 1) +
                    ",\"name_count\":" + std::to_string(s::name_count());
    j += ",\"name_examples\":[";
    const auto& ex = s::name_examples();
    for (size_t i = 0; i < ex.size(); ++i) {
        if (i) j += ",";
        j += "\"" + jesc(ex[i]) + "\"";
    }
    j += "]}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, j.c_str());
    return ESP_OK;
}

esp_err_t h_config_post(httpd_req_t* req) {
    std::string body;
    body.resize(req->content_len < 1024 ? req->content_len : 1024);
    int r = 0, total = (int)body.size();
    while (r < total) {
        int k = httpd_req_recv(req, &body[r], total - r);
        if (k <= 0) break;
        r += k;
    }
    body.resize(r);
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_OK; }
    auto str = [&](const char* k) -> std::string {
        cJSON* it = cJSON_GetObjectItem(root, k);
        return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : "";
    };
    std::string url = str("url"), key = str("key"), model = str("model");
    if (url.empty()) url = "https://api.openai.com/v1/audio/transcriptions";
    if (model.empty()) model = "whisper-1";
    cfg_set("url", url);
    cfg_set("model", model);
    if (cJSON_GetObjectItem(root, "key")) cfg_set("key", key); // allow clearing
    // Device settings (optional fields).
    namespace s = sapiens::fw::settings;
    if (cJSON* j = cJSON_GetObjectItem(root, "sleep_s"); cJSON_IsNumber(j)) s::set_sleep(j->valueint);
    if (cJSON* j = cJSON_GetObjectItem(root, "hold_s"); cJSON_IsNumber(j)) s::set_hold(j->valueint);
    if (cJSON* j = cJSON_GetObjectItem(root, "scroll"); cJSON_IsNumber(j)) s::set_scroll(j->valueint);
    if (cJSON* j = cJSON_GetObjectItem(root, "name_fmt"); cJSON_IsNumber(j)) s::set_name_fmt(j->valueint - 1);
    if (cJSON* j = cJSON_GetObjectItem(root, "disk"); cJSON_IsBool(j)) s::set_disk(cJSON_IsTrue(j));
    cJSON_Delete(root);
    ESP_LOGI(TAG, "transcription config set: %s", url.c_str());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t h_config_delete(httpd_req_t* req) {
    nvs_handle_t h;
    if (nvs_open(TX_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "transcription config cleared");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t h_notes(httpd_req_t* req) {
    std::string j = "[";
    int idx = 0;
    if (g_notes) {
        for (const auto& n : g_notes->list_notes()) {
            std::string title = n.title.empty() ? "Untitled" : n.title;
            std::string base = basename_of(n.audio_key);
            long size = 0;
            if (!n.audio_key.empty()) {
                struct stat st;
                if (stat(n.audio_key.c_str(), &st) == 0) size = (long)st.st_size;
            }
            if (idx) j += ",";
            j += "{\"id\":\"" + jesc(n.id) + "\",";
            j += "\"idx\":" + std::to_string(++idx) + ",";
            j += "\"title\":\"" + jesc(title) + "\",";
            j += "\"status\":\"" +
                 std::string(n.status == sapiens::NoteStatus::Transcribed ? "transcribed" : "pending") + "\",";
            j += "\"audio\":\"" + jesc(base) + "\",";
            j += "\"size\":" + std::to_string(size) + ",";
            j += "\"has_transcript\":" + std::string(n.transcript.empty() ? "false" : "true") + ",";
            j += "\"language\":\"" + jesc(n.language) + "\"}";
        }
    }
    j += "]";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, j.c_str(), j.size());
    return ESP_OK;
}

esp_err_t h_media(httpd_req_t* req) {
    std::string file = uri_tail(req, "/media/");
    if (file.empty() || file.find("..") != std::string::npos) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
        return ESP_OK;
    }
    std::string path = std::string(EspStorage::kAudioDir) + "/" + file;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "audio/wav");
    char buf[2048];
    size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, r) != ESP_OK) break;
    }
    std::fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t h_transcript(httpd_req_t* req) {
    std::string id = uri_tail(req, "/api/transcript/");
    std::string text;
    if (g_notes) {
        auto n = g_notes->get_note(id);
        if (n) text = n->transcript;
    }
    // ?download=1 -> attachment
    char q[8] = {0};
    if (httpd_req_get_url_query_len(req) > 0) {
        char qs[64];
        if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK)
            httpd_query_key_value(qs, "download", q, sizeof(q));
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (q[0] == '1')
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=transcript.txt");
    httpd_resp_send(req, text.c_str(), text.size());
    return ESP_OK;
}

esp_err_t h_delete(httpd_req_t* req) {
    std::string id = uri_tail(req, "/api/notes/");
    bool ok = g_notes && g_notes->soft_delete(id);
    if (!ok) { httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found"); return ESP_OK; }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// Keep only filename-safe chars; collapse the rest to '_'; cap at 100.
std::string sanitize_name(const std::string& in) {
    std::string o;
    for (char c : in) {
        if (o.size() >= 100) break;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == ' ' || c == '_' || c == '-' || c == '.')
            o += c;
        else
            o += '_';
    }
    while (!o.empty() && (o.front() == ' ')) o.erase(o.begin());
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    return o;
}

// "rec-YYYYMMDD-HHMMSS" stem from a recording basename (drops any old name suffix).
std::string datetime_stem(const std::string& base) {
    std::string nm = base;
    auto dot = nm.rfind(".wav");
    if (dot != std::string::npos) nm = nm.substr(0, dot);
    // split on '-'
    std::vector<std::string> parts;
    size_t s = 0;
    for (size_t i = 0; i <= nm.size(); ++i) {
        if (i == nm.size() || nm[i] == '-') { parts.push_back(nm.substr(s, i - s)); s = i + 1; }
    }
    if (parts.size() >= 3 && parts[0] == "rec")
        return "rec-" + parts[1] + "-" + parts[2];
    return parts.empty() ? std::string("rec") : parts[0];
}

// POST /api/notes/<id>  {name:"..."}  -> rename the WAV + retitle the note.
esp_err_t h_rename(httpd_req_t* req) {
    std::string id = uri_tail(req, "/api/notes/");
    auto note = g_notes ? g_notes->get_note(id) : std::nullopt;
    if (!note || note->audio_key.empty()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "note not found");
        return ESP_OK;
    }
    std::string body;
    body.resize(req->content_len < 400 ? req->content_len : 400);
    int r = 0, total = (int)body.size();
    while (r < total) { int k = httpd_req_recv(req, &body[r], total - r); if (k <= 0) break; r += k; }
    body.resize(r);
    std::string name;
    if (cJSON* root = cJSON_Parse(body.c_str())) {
        cJSON* jn = cJSON_GetObjectItem(root, "name");
        if (cJSON_IsString(jn) && jn->valuestring) name = jn->valuestring;
        cJSON_Delete(root);
    }
    name = sanitize_name(name);

    std::string stem = datetime_stem(basename_of(note->audio_key));
    std::string newbase = stem + (name.empty() ? "" : ("-" + name)) + ".wav";
    std::string newpath = std::string(EspStorage::kAudioDir) + "/" + newbase;
    if (newpath != note->audio_key) {
        if (std::rename(note->audio_key.c_str(), newpath.c_str()) != 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rename failed");
            return ESP_OK;
        }
        g_notes->set_audio(id, newpath, note->audio_secs);
        std::rename(txt_path(note->audio_key).c_str(), txt_path(newpath).c_str()); // move .txt too
    }
    if (!name.empty()) g_notes->rename(id, name);
    ESP_LOGI(TAG, "renamed note %s -> %s", id.c_str(), newbase.c_str());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t h_transcribe(httpd_req_t* req) {
    if (!tx_configured()) {
        // Ask the dashboard to collect a service URL / API key, then retry.
        httpd_resp_set_status(req, "428 Precondition Required");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "No transcription service configured.");
        return ESP_OK;
    }
    std::string id = uri_tail(req, "/api/transcribe/");
    auto note = g_notes ? g_notes->get_note(id) : std::nullopt;
    if (!note || note->audio_key.empty()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "note/audio not found");
        return ESP_OK;
    }
    std::string text, err;
    ESP_LOGI(TAG, "transcribing %s ...", note->audio_key.c_str());
    if (!transcribe_file(note->audio_key, text, err)) {
        ESP_LOGW(TAG, "transcribe failed: %s", err.c_str());
        httpd_resp_set_status(req, "502 Bad Gateway");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, err.c_str());
        return ESP_OK;
    }
    g_notes->apply_transcription(id, text, "", {}, "");
    write_text_file(txt_path(note->audio_key), text); // .txt next to the .wav
    std::string j = "{\"ok\":true,\"transcript\":\"" + jesc(text) + "\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, j.c_str());
    return ESP_OK;
}
} // namespace

void EspDashboard::start(sapiens::NoteService& notes) {
    if (s_srv) return;
    g_notes = &notes;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 14;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 24576; // headroom for the TLS handshake during transcription
    if (httpd_start(&s_srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "dashboard httpd start failed");
        s_srv = nullptr;
        return;
    }
    auto reg = [&](const char* uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t*)) {
        httpd_uri_t u = { uri, m, h, nullptr };
        httpd_register_uri_handler(s_srv, &u);
    };
    reg("/api/info", HTTP_GET, h_info);
    reg("/api/config", HTTP_GET, h_config_get);
    reg("/api/config", HTTP_POST, h_config_post);
    reg("/api/config", HTTP_DELETE, h_config_delete);
    reg("/api/notes", HTTP_GET, h_notes);
    reg("/api/transcript/*", HTTP_GET, h_transcript);
    reg("/media/*", HTTP_GET, h_media);
    reg("/api/notes/*", HTTP_DELETE, h_delete);
    reg("/api/notes/*", HTTP_POST, h_rename);
    reg("/api/transcribe/*", HTTP_POST, h_transcribe);
    reg("/*", HTTP_GET, h_index); // catch-all -> the SPA (must be registered last)
    ESP_LOGI(TAG, "dashboard up on :80");
}

void EspDashboard::stop() {
    if (s_srv) { httpd_stop(s_srv); s_srv = nullptr; }
    g_notes = nullptr;
}

bool EspDashboard::running() const { return s_srv != nullptr; }

} // namespace sapiens::fw
