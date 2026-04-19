#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

/**
 * @brief Represents a single Bible verse.
 */
struct BibleVerse {
    int translation_id;
    int book_id;
    int chapter;
    int verse;
    std::string text;
    std::string book_name;
    std::string translation_abbr;
};

/**
 * @class HomeInDB
 * @brief Thread-safe interface for the Bible SQLite FTS5 database.
 * 
 * Handles multi-translation lookup, fuzzy quote matching, and optimized
 * Full-Text Search (FTS5) for instant verse detection across multiple languages.
 */
class HomeInDB {
public:
    HomeInDB();
    ~HomeInDB();

    /**
     * @brief Opens the SQLite database from the filesystem.
     * @param db_path Absolute path to the .db file.
     * @return True if opened successfully, false otherwise.
     */
    bool Open(const std::string& db_path);

    /**
     * @brief Closes the database.
     */
    void Close();

    /**
     * @brief Retrieves a specific verse from a translation.
     */
    bool GetVerse(const std::string& book_name, int chapter, int verse, const std::string& translation_abbr, BibleVerse& out_verse);

    /**
     * @brief Fuzzy search for a verse by text snippet.
     */
    std::vector<BibleVerse> SearchVerses(const std::string& query, int limit = 5);

private:
    sqlite3* db = nullptr;
};
