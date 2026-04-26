#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

struct SongLyric {
    int id;
    std::string title;
    std::string artist;
    std::string content;
    std::string source;
};

/**
 * @brief Logic for managing the local worship lyrics cache.
 */
class HomeInLyricsDB {
public:
    HomeInLyricsDB();
    ~HomeInLyricsDB();

    bool Open(const std::string& db_path);
    void Close();

    // FIX #8: Added IsOpen() so callers can guard against an uninitialised DB.
    bool IsOpen() const { return db != nullptr; }

    bool AddSong(const std::string& title, const std::string& artist,
                 const std::string& content, const std::string& source);

    std::vector<SongLyric> Search(const std::string& query, int limit = 10);
    std::vector<SongLyric> GetLibrary(int limit = 100);

    bool FindSong(const std::string& title, const std::string& artist,
                  SongLyric& out_song);

    void RebuildFTS();
    void ValidateDatabase();

private:
    // FIX #7: Creates the lyrics + lyrics_fts tables if they don't exist.
    // Previously the DB file existed but was completely empty — every
    // AddSong(), Search(), and RebuildFTS() call silently failed.
    void EnsureSchema();

    sqlite3* db = nullptr;
};
