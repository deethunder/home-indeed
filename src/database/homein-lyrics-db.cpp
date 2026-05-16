#include "homein-lyrics-db.hpp"
#include <obs-module.h>

HomeInLyricsDB::HomeInLyricsDB() {}

HomeInLyricsDB::~HomeInLyricsDB() {
    Close();
}

bool HomeInLyricsDB::Open(const std::string& db_path) {
    if (db) Close();

    blog(LOG_INFO, "HomeIndeed: Opening Lyrics DB at: %s", db_path.c_str());
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        blog(LOG_ERROR, "HomeIndeed: Failed to open Lyrics database: %s",
             sqlite3_errmsg(db));
        db = nullptr;
        return false;
    }

    // FIX #7: Create tables before ValidateDatabase tries to COUNT(*) them.
    EnsureSchema();
    ValidateDatabase();
    return true;
}

// FIX #7: This was the root cause of ALL lyrics failures.
// The homein-lyrics.db file existed on disk but contained zero tables.
// Every call to AddSong / Search / RebuildFTS returned early or failed silently.
void HomeInLyricsDB::EnsureSchema() {
    if (!db) return;

    const char* sql =
        "CREATE TABLE IF NOT EXISTS lyrics ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT NOT NULL, artist TEXT,"
        "  content TEXT NOT NULL, source TEXT,"
        "  last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP);"
        "CREATE VIRTUAL TABLE IF NOT EXISTS lyrics_fts USING fts5("
        "  title, artist, content,"
        "  content='lyrics', content_rowid='id');"
        "CREATE TRIGGER IF NOT EXISTS lyrics_ai AFTER INSERT ON lyrics BEGIN"
        "  INSERT INTO lyrics_fts(rowid,title,artist,content)"
        "  VALUES(new.id,new.title,new.artist,new.content); END;"
        "CREATE TRIGGER IF NOT EXISTS lyrics_ad AFTER DELETE ON lyrics BEGIN"
        "  INSERT INTO lyrics_fts(lyrics_fts,rowid,title,artist,content)"
        "  VALUES('delete',old.id,old.title,old.artist,old.content); END;"
        "CREATE TRIGGER IF NOT EXISTS lyrics_au AFTER UPDATE ON lyrics BEGIN"
        "  INSERT INTO lyrics_fts(lyrics_fts,rowid,title,artist,content)"
        "  VALUES('delete',old.id,old.title,old.artist,old.content);"
        "  INSERT INTO lyrics_fts(rowid,title,artist,content)"
        "  VALUES(new.id,new.title,new.artist,new.content); END;";

    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        blog(LOG_ERROR, "HomeIndeed: Failed to create lyrics schema: %s", err);
        sqlite3_free(err);
    } else {
        blog(LOG_INFO, "HomeIndeed: Lyrics schema ready.");
    }
}

void HomeInLyricsDB::ValidateDatabase() {
    if (!db) return;

    blog(LOG_INFO, "=== HomeIndeed Lyrics DB Diagnostic ===");

    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM lyrics", -1, &stmt,
                            nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        blog(LOG_INFO, "HomeIndeed: Lyrics table has %d songs cached.", count);
    } else {
        blog(LOG_ERROR, "HomeIndeed: Lyrics table inaccessible: %s",
             sqlite3_errmsg(db));
    }

    blog(LOG_INFO, "=== End Diagnostic ===");
}

void HomeInLyricsDB::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool HomeInLyricsDB::AddSong(const std::string& title, const std::string& artist,
                              const std::string& content, const std::string& source) {
    if (!db) return false;

    // FIX: Check if it already exists to prevent SQLite from cloning duplicates!
    SongLyric existing;
    if (FindSong(title, artist, existing)) {
        return true; 
    }

    // INSERT OR REPLACE triggers DELETE then INSERT, which keeps FTS in sync
    const char* sql =
        "INSERT INTO lyrics (title, artist, content, source) "
        "VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, title.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, source.c_str(),  -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool HomeInLyricsDB::FindSong(const std::string& title, const std::string& artist,
                               SongLyric& out_song) {
    if (!db) return false;

    const char* sql =
        "SELECT id, title, artist, content, source FROM lyrics "
        "WHERE title = ? AND artist = ? LIMIT 1";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, title.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_song.id      = sqlite3_column_int(stmt, 0);
        out_song.title   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        out_song.artist  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        out_song.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out_song.source  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}

std::vector<SongLyric> HomeInLyricsDB::Search(const std::string& query, int limit) {
    std::vector<SongLyric> results;
    if (!db) return results;

    // Primary: FTS5 MATCH (fast, relevance-ranked)
    const char* fts_sql =
        "SELECT l.id, l.title, l.artist, l.content, l.source "
        "FROM lyrics_fts "
        "JOIN lyrics l ON lyrics_fts.rowid = l.id "
        "WHERE lyrics_fts MATCH ? "
        "ORDER BY rank "
        "LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        // IMPROVED SEARCH: Use prefix matching (query*) to find words more easily.
        std::string fts_query = query + "*";
        sqlite3_bind_text(stmt, 1, fts_query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SongLyric s;
            s.id      = sqlite3_column_int(stmt, 0);
            s.title   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            s.artist  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            s.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            s.source  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            results.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    // Fallback: LIKE search if FTS found nothing (handles partial word matches)
    if (results.empty()) {
        const char* like_sql =
            "SELECT id, title, artist, content, source FROM lyrics "
            "WHERE title LIKE ? OR content LIKE ? OR artist LIKE ? "
            "LIMIT ?";

        if (sqlite3_prepare_v2(db, like_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string pat = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, pat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, limit);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                SongLyric s;
                s.id      = sqlite3_column_int(stmt, 0);
                s.title   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                s.artist  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                s.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                s.source  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                results.push_back(s);
            }
            sqlite3_finalize(stmt);
        }
    }

    return results;
}

std::vector<SongLyric> HomeInLyricsDB::GetLibrary(int limit) {
    std::vector<SongLyric> results;
    if (!db) return results;

    // OPTIMIZATION: Only fetch metadata for the browse list. 
    // Content is fetched individually in FindSong() or OnSongSelected.
    const char* sql = "SELECT id, title, artist, source FROM lyrics ORDER BY title LIMIT ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SongLyric s;
            s.id      = sqlite3_column_int(stmt, 0);
            s.title   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* art = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            s.artist  = art ? art : "";
            s.source  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            results.push_back(s);
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

void HomeInLyricsDB::RebuildFTS() {
    if (!db) return;
    // 'rebuild' re-reads all content from the backing lyrics table automatically
    // No need to list columns — safe and always in sync
    char* err = nullptr;
    sqlite3_exec(db,
        "INSERT INTO lyrics_fts(lyrics_fts) VALUES('rebuild');",
        nullptr, nullptr, &err);
    if (err) {
        blog(LOG_ERROR, "HomeIndeed: FTS rebuild failed: %s", err);
        sqlite3_free(err);
    } else {
        blog(LOG_INFO, "HomeIndeed: FTS index rebuilt successfully");
    }
}
