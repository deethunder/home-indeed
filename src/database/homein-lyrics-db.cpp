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
        blog(LOG_ERROR, "HomeIndeed: Failed to open Lyrics database: %s", sqlite3_errmsg(db));
        db = nullptr;
        return false;
    }

    ValidateDatabase();
    return true;
}

void HomeInLyricsDB::ValidateDatabase() {
    if (!db) return;
    
    blog(LOG_INFO, "=== HomeIndeed Lyrics DB Diagnostic ===");
    
    // Check if lyrics table exists and count rows
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM lyrics", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        blog(LOG_INFO, "HomeIndeed: Lyrics table has %d songs cached.", count);
    } else {
        blog(LOG_ERROR, "HomeIndeed: Lyrics table MISSING or inaccessible: %s", sqlite3_errmsg(db));
    }
    
    blog(LOG_INFO, "=== End Diagnostic ===");
}

void HomeInLyricsDB::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool HomeInLyricsDB::AddSong(const std::string& title, const std::string& artist, const std::string& content, const std::string& source) {
    if (!db) return false;

    const char* sql = "INSERT OR REPLACE INTO lyrics (title, artist, content, source) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, source.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool HomeInLyricsDB::FindSong(const std::string& title, const std::string& artist, SongLyric& out_song) {
    if (!db) return false;

    const char* sql = "SELECT id, title, artist, content, source FROM lyrics WHERE title = ? AND artist = ? LIMIT 1";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_song.id = sqlite3_column_int(stmt, 0);
        out_song.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        out_song.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        out_song.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out_song.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}

std::vector<SongLyric> HomeInLyricsDB::Search(const std::string& query, int limit) {
    std::vector<SongLyric> results;
    if (!db) return results;

    const char* sql = 
        "SELECT id, title, artist, content, source "
        "FROM lyrics_fts "
        "JOIN lyrics ON lyrics_fts.rowid = lyrics.id "
        "WHERE lyrics_fts MATCH ? "
        "LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SongLyric s;
        s.id = sqlite3_column_int(stmt, 0);
        s.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        s.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        s.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        results.push_back(s);
    }

    sqlite3_finalize(stmt);
    
    // Fallback: LIKE search if FTS found nothing
    if (results.empty()) {
        const char* like_sql = 
            "SELECT id, title, artist, content, source "
            "FROM lyrics "
            "WHERE title LIKE ? OR content LIKE ? OR artist LIKE ? "
            "LIMIT ?";

        if (sqlite3_prepare_v2(db, like_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string pattern = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, limit);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                SongLyric s;
                s.id = sqlite3_column_int(stmt, 0);
                s.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                s.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                s.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                s.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                results.push_back(s);
            }
            sqlite3_finalize(stmt);
        }
    }

    return results;
}

void HomeInLyricsDB::RebuildFTS() {
    if (!db) return;
    
    // Delete all existing FTS entries and re-populate from the lyrics table
    sqlite3_exec(db, "DELETE FROM lyrics_fts;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT INTO lyrics_fts(rowid, title, content) SELECT id, title, content FROM lyrics;", nullptr, nullptr, nullptr);
    blog(LOG_INFO, "HomeIndeed: FTS index rebuilt for lyrics database");
}
