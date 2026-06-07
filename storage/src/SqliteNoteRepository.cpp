#include "sapiens/SqliteNoteRepository.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace sapiens {
namespace {

constexpr char kUnitSep = '\x1f'; // joins key_points without colliding with text

std::string join_points(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += kUnitSep;
        s += v[i];
    }
    return s;
}

std::vector<std::string> split_points(const std::string& s) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::string cur;
    for (char c : s) {
        if (c == kUnitSep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// RAII for a prepared statement.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("prepare failed: ") +
                                     sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(st_); }
    sqlite3_stmt* get() { return st_; }

    void text(int i, const std::string& v) {
        sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    void i64(int i, std::int64_t v) { sqlite3_bind_int64(st_, i, v); }
    void null(int i) { sqlite3_bind_null(st_, i); }

private:
    sqlite3_stmt* st_{nullptr};
};

std::string col_text(sqlite3_stmt* s, int i) {
    const unsigned char* t = sqlite3_column_text(s, i);
    return t ? reinterpret_cast<const char*>(t) : "";
}

// Column list shared by get() and list() so a row maps the same way in one place.
constexpr const char* kNoteCols =
    "id,title,transcript,summary,key_points,language,audio_key,status,favorite,"
    "version,created_at,updated_at,deleted_at,audio_secs";

// Map a notes row (selected as kNoteCols) into a Note (without tags).
Note hydrate(sqlite3_stmt* r) {
    Note n;
    n.id = col_text(r, 0);
    n.title = col_text(r, 1);
    n.transcript = col_text(r, 2);
    n.summary = col_text(r, 3);
    n.key_points = split_points(col_text(r, 4));
    n.language = col_text(r, 5);
    n.audio_key = col_text(r, 6);
    n.status = static_cast<NoteStatus>(sqlite3_column_int(r, 7));
    n.favorite = sqlite3_column_int(r, 8) != 0;
    n.version = sqlite3_column_int64(r, 9);
    n.created_at = sqlite3_column_int64(r, 10);
    n.updated_at = sqlite3_column_int64(r, 11);
    if (sqlite3_column_type(r, 12) != SQLITE_NULL)
        n.deleted_at = sqlite3_column_int64(r, 12);
    n.audio_secs = sqlite3_column_int(r, 13);
    return n;
}

} // namespace

SqliteNoteRepository::SqliteNoteRepository(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "open failed";
        sqlite3_close(db_);
        throw std::runtime_error("sqlite open: " + msg);
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    // Let concurrent writers (e.g. the simulator + the sapiens-web dashboard
    // sharing this DB) retry briefly instead of failing with SQLITE_BUSY.
    exec("PRAGMA busy_timeout=3000;");
    exec(R"(
        CREATE TABLE IF NOT EXISTS notes(
            id TEXT PRIMARY KEY, title TEXT, transcript TEXT, summary TEXT,
            key_points TEXT, language TEXT, audio_key TEXT, audio_secs INTEGER NOT NULL DEFAULT 0, status INTEGER NOT NULL,
            favorite INTEGER NOT NULL, version INTEGER NOT NULL,
            created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
            deleted_at INTEGER);
        CREATE TABLE IF NOT EXISTS tags(
            id TEXT PRIMARY KEY, name TEXT NOT NULL, color TEXT);
        CREATE TABLE IF NOT EXISTS note_tags(
            note_id TEXT NOT NULL, tag_id TEXT NOT NULL,
            PRIMARY KEY(note_id, tag_id));
        CREATE INDEX IF NOT EXISTS idx_notes_updated ON notes(updated_at);
    )");

    // Lightweight migration: add columns introduced after a DB was first created.
    // ADD COLUMN is a no-op error ("duplicate column") on up-to-date DBs, ignored.
    auto add_col = [&](const char* sql) {
        char* e = nullptr;
        sqlite3_exec(db_, sql, nullptr, nullptr, &e);
        if (e) sqlite3_free(e); // ignore "duplicate column" on up-to-date DBs
    };
    add_col("ALTER TABLE notes ADD COLUMN audio_key TEXT;");
    add_col("ALTER TABLE notes ADD COLUMN audio_secs INTEGER NOT NULL DEFAULT 0;");
}

SqliteNoteRepository::~SqliteNoteRepository() { sqlite3_close(db_); }

void SqliteNoteRepository::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string m = err ? err : "exec failed";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec: " + m);
    }
}

void SqliteNoteRepository::upsert(const Note& n) {
    exec("BEGIN;");
    try {
        {
            Stmt s(db_,
                   "INSERT INTO notes(id,title,transcript,summary,key_points,"
                   "language,audio_key,status,favorite,version,created_at,"
                   "updated_at,deleted_at,audio_secs) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                   "ON CONFLICT(id) DO UPDATE SET title=excluded.title,"
                   "transcript=excluded.transcript,summary=excluded.summary,"
                   "key_points=excluded.key_points,language=excluded.language,"
                   "audio_key=excluded.audio_key,status=excluded.status,"
                   "favorite=excluded.favorite,version=excluded.version,"
                   "updated_at=excluded.updated_at,deleted_at=excluded.deleted_at,"
                   "audio_secs=excluded.audio_secs;");
            s.text(1, n.id);
            s.text(2, n.title);
            s.text(3, n.transcript);
            s.text(4, n.summary);
            s.text(5, join_points(n.key_points));
            s.text(6, n.language);
            s.text(7, n.audio_key);
            s.i64(8, static_cast<std::int64_t>(n.status));
            s.i64(9, n.favorite ? 1 : 0);
            s.i64(10, n.version);
            s.i64(11, n.created_at);
            s.i64(12, n.updated_at);
            if (n.deleted_at) s.i64(13, *n.deleted_at); else s.null(13);
            s.i64(14, n.audio_secs);
            if (sqlite3_step(s.get()) != SQLITE_DONE)
                throw std::runtime_error(sqlite3_errmsg(db_));
        }
        {
            Stmt d(db_, "DELETE FROM note_tags WHERE note_id=?;");
            d.text(1, n.id);
            sqlite3_step(d.get());
        }
        for (const auto& tid : n.tag_ids) {
            Stmt t(db_, "INSERT OR IGNORE INTO note_tags(note_id,tag_id) "
                        "VALUES(?,?);");
            t.text(1, n.id);
            t.text(2, tid);
            sqlite3_step(t.get());
        }
        exec("COMMIT;");
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

void SqliteNoteRepository::load_tag_ids(Note& n) const {
    Stmt s(db_, "SELECT tag_id FROM note_tags WHERE note_id=? ORDER BY tag_id;");
    s.text(1, n.id);
    while (sqlite3_step(s.get()) == SQLITE_ROW)
        n.tag_ids.push_back(col_text(s.get(), 0));
}

std::optional<Note> SqliteNoteRepository::get(const std::string& id) const {
    Stmt s(db_, (std::string("SELECT ") + kNoteCols + " FROM notes WHERE id=?;").c_str());
    s.text(1, id);
    if (sqlite3_step(s.get()) != SQLITE_ROW) return std::nullopt;
    Note n = hydrate(s.get());
    load_tag_ids(n);
    return n;
}

std::vector<Note> SqliteNoteRepository::list(const NoteFilter& f) const {
    // Single query selecting all columns + the filters (no per-id round-trip).
    std::string sql = std::string("SELECT ") + kNoteCols + " FROM notes WHERE 1=1";
    if (!f.include_deleted) sql += " AND deleted_at IS NULL";
    if (f.favorite) sql += " AND favorite=?";
    if (f.updated_after) sql += " AND updated_at>?";
    if (f.tag_id)
        sql += " AND id IN (SELECT note_id FROM note_tags WHERE tag_id=?)";
    sql += " ORDER BY created_at DESC;";

    Stmt s(db_, sql.c_str());
    int idx = 1;
    if (f.favorite) s.i64(idx++, *f.favorite ? 1 : 0);
    if (f.updated_after) s.i64(idx++, *f.updated_after);
    if (f.tag_id) s.text(idx++, *f.tag_id);

    std::vector<Note> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        Note n = hydrate(s.get());
        load_tag_ids(n);
        out.push_back(std::move(n));
    }
    return out;
}

void SqliteNoteRepository::upsert_tag(const Tag& t) {
    Stmt s(db_, "INSERT INTO tags(id,name,color) VALUES(?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name,"
                "color=excluded.color;");
    s.text(1, t.id);
    s.text(2, t.name);
    s.text(3, t.color);
    if (sqlite3_step(s.get()) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
}

std::vector<Tag> SqliteNoteRepository::tags() const {
    Stmt s(db_, "SELECT id,name,color FROM tags ORDER BY name;");
    std::vector<Tag> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        Tag t;
        t.id = col_text(s.get(), 0);
        t.name = col_text(s.get(), 1);
        t.color = col_text(s.get(), 2);
        out.push_back(t);
    }
    return out;
}

} // namespace sapiens
