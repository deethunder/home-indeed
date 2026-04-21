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
    bool IsOpen() const { return db != nullptr; }

    /**
     * @brief Adds a song to the local cache.
     */
    bool AddSong(const std::string& title, const std::string& artist, const std::string& content, const std::string& source);

    /**
     * @brief Searches for a song in the local cache.
     */
    std::vector<SongLyric> Search(const std::string& query, int limit = 10);

    /**
     * @brief Finds a song by title and artist.
     */
    bool FindSong(const std::string& title, const std::string& artist, SongLyric& out_song);

    /**
     * @brief Rebuilds the FTS5 index after batch imports.
     */
    void RebuildFTS();

    /**
     * @brief Performs a startup check and logs the status of the lyrics database.
     */
    void ValidateDatabase();

private:
    void EnsureSchema();
    sqlite3* db = nullptr;
};
