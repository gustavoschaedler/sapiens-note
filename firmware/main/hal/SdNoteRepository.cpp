#include "SdNoteRepository.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.notesdb";

cJSON* str_array(const std::vector<std::string>& v) {
    cJSON* a = cJSON_CreateArray();
    for (const auto& s : v) cJSON_AddItemToArray(a, cJSON_CreateString(s.c_str()));
    return a;
}
void read_str_array(cJSON* arr, std::vector<std::string>& out) {
    if (!cJSON_IsArray(arr)) return;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; ++i) {
        cJSON* it = cJSON_GetArrayItem(arr, i);
        if (cJSON_IsString(it) && it->valuestring) out.emplace_back(it->valuestring);
    }
}
const char* sget(cJSON* o, const char* k) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : "";
}
double nget(cJSON* o, const char* k, double dflt = 0) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(it) ? it->valuedouble : dflt;
}

cJSON* note_to_json(const sapiens::Note& n) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", n.id.c_str());
    cJSON_AddStringToObject(o, "title", n.title.c_str());
    cJSON_AddStringToObject(o, "transcript", n.transcript.c_str());
    cJSON_AddStringToObject(o, "summary", n.summary.c_str());
    cJSON_AddItemToObject(o, "key_points", str_array(n.key_points));
    cJSON_AddStringToObject(o, "language", n.language.c_str());
    cJSON_AddStringToObject(o, "audio_key", n.audio_key.c_str());
    cJSON_AddNumberToObject(o, "audio_secs", n.audio_secs);
    cJSON_AddNumberToObject(o, "status", (int)n.status);
    cJSON_AddBoolToObject(o, "favorite", n.favorite);
    cJSON_AddItemToObject(o, "tag_ids", str_array(n.tag_ids));
    cJSON_AddNumberToObject(o, "version", (double)n.version);
    cJSON_AddNumberToObject(o, "created_at", (double)n.created_at);
    cJSON_AddNumberToObject(o, "updated_at", (double)n.updated_at);
    if (n.deleted_at) cJSON_AddNumberToObject(o, "deleted_at", (double)*n.deleted_at);
    else cJSON_AddNullToObject(o, "deleted_at");
    return o;
}

sapiens::Note note_from_json(cJSON* o) {
    sapiens::Note n;
    n.id = sget(o, "id");
    n.title = sget(o, "title");
    n.transcript = sget(o, "transcript");
    n.summary = sget(o, "summary");
    read_str_array(cJSON_GetObjectItem(o, "key_points"), n.key_points);
    n.language = sget(o, "language");
    n.audio_key = sget(o, "audio_key");
    n.audio_secs = (int)nget(o, "audio_secs");
    n.status = (sapiens::NoteStatus)(int)nget(o, "status");
    cJSON* fav = cJSON_GetObjectItem(o, "favorite");
    n.favorite = cJSON_IsBool(fav) ? cJSON_IsTrue(fav) : false;
    read_str_array(cJSON_GetObjectItem(o, "tag_ids"), n.tag_ids);
    n.version = (std::int64_t)nget(o, "version", 1);
    n.created_at = (sapiens::Millis)nget(o, "created_at");
    n.updated_at = (sapiens::Millis)nget(o, "updated_at");
    cJSON* del = cJSON_GetObjectItem(o, "deleted_at");
    if (cJSON_IsNumber(del)) n.deleted_at = (sapiens::Millis)del->valuedouble;
    return n;
}
} // namespace

SdNoteRepository::SdNoteRepository(std::string path) : path_(std::move(path)) {
    load();
}

void SdNoteRepository::load() {
    std::FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) {
        ESP_LOGI(TAG, "no index at %s (fresh start)", path_.c_str());
        return;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> buf(sz > 0 ? sz + 1 : 1, 0);
    if (sz > 0) std::fread(buf.data(), 1, sz, f);
    std::fclose(f);

    cJSON* root = cJSON_Parse(buf.data());
    if (!root) {
        ESP_LOGW(TAG, "index parse failed; starting empty");
        return;
    }
    cJSON* notes = cJSON_GetObjectItem(root, "notes");
    if (cJSON_IsArray(notes)) {
        int n = cJSON_GetArraySize(notes);
        for (int i = 0; i < n; ++i) {
            sapiens::Note note = note_from_json(cJSON_GetArrayItem(notes, i));
            if (!note.id.empty()) notes_[note.id] = note;
        }
    }
    cJSON* tags = cJSON_GetObjectItem(root, "tags");
    if (cJSON_IsArray(tags)) {
        int n = cJSON_GetArraySize(tags);
        for (int i = 0; i < n; ++i) {
            cJSON* t = cJSON_GetArrayItem(tags, i);
            sapiens::Tag tag;
            tag.id = sget(t, "id");
            tag.name = sget(t, "name");
            tag.color = sget(t, "color");
            if (!tag.id.empty()) tags_[tag.id] = tag;
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "loaded %d note(s), %d tag(s) from %s",
             (int)notes_.size(), (int)tags_.size(), path_.c_str());
}

void SdNoteRepository::save() const {
    cJSON* root = cJSON_CreateObject();
    cJSON* notes = cJSON_AddArrayToObject(root, "notes");
    for (const auto& [id, n] : notes_) cJSON_AddItemToArray(notes, note_to_json(n));
    cJSON* tags = cJSON_AddArrayToObject(root, "tags");
    for (const auto& [id, t] : tags_) {
        cJSON* to = cJSON_CreateObject();
        cJSON_AddStringToObject(to, "id", t.id.c_str());
        cJSON_AddStringToObject(to, "name", t.name.c_str());
        cJSON_AddStringToObject(to, "color", t.color.c_str());
        cJSON_AddItemToArray(tags, to);
    }
    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return;

    // Write to a temp file then rename, so a crash mid-write can't corrupt the DB.
    std::string tmp = path_ + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f) {
        std::fwrite(txt, 1, std::strlen(txt), f);
        std::fclose(f);
        std::remove(path_.c_str());
        if (std::rename(tmp.c_str(), path_.c_str()) != 0)
            ESP_LOGW(TAG, "rename %s -> %s failed", tmp.c_str(), path_.c_str());
    } else {
        ESP_LOGE(TAG, "cannot write %s", tmp.c_str());
    }
    std::free(txt);
}

void SdNoteRepository::upsert(const sapiens::Note& note) {
    notes_[note.id] = note;
    save();
}

std::optional<sapiens::Note> SdNoteRepository::get(const std::string& id) const {
    auto it = notes_.find(id);
    if (it == notes_.end()) return std::nullopt;
    return it->second;
}

std::vector<sapiens::Note> SdNoteRepository::list(const sapiens::NoteFilter& f) const {
    std::vector<sapiens::Note> out;
    for (const auto& [id, n] : notes_) {
        if (n.is_deleted() && !f.include_deleted) continue;
        if (f.favorite && n.favorite != *f.favorite) continue;
        if (f.updated_after && n.updated_at <= *f.updated_after) continue;
        if (f.tag_id) {
            const auto& t = n.tag_ids;
            if (std::find(t.begin(), t.end(), *f.tag_id) == t.end()) continue;
        }
        out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const sapiens::Note& a, const sapiens::Note& b) {
        return a.created_at > b.created_at; // newest first
    });
    return out;
}

void SdNoteRepository::upsert_tag(const sapiens::Tag& tag) {
    tags_[tag.id] = tag;
    save();
}

std::vector<sapiens::Tag> SdNoteRepository::tags() const {
    std::vector<sapiens::Tag> out;
    out.reserve(tags_.size());
    for (const auto& [id, t] : tags_) out.push_back(t);
    std::sort(out.begin(), out.end(),
              [](const sapiens::Tag& a, const sapiens::Tag& b) { return a.name < b.name; });
    return out;
}

} // namespace sapiens::fw
