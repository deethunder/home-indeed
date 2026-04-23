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
     * @brief Retrieves an entire chapter of verses from a translation.
     */
    std::vector<BibleVerse> GetChapterVerses(const std::string& book_name, int chapter, const std::string& translation_abbr);

    /**
     * @brief Returns the number of chapters in a book.
     */
    int GetChapterCount(const std::string& book_name);

    /**
     * @brief Fuzzy search for a verse by text snippet.
     */
    std::vector<BibleVerse> SearchVerses(const std::string& query, int limit = 5);

    /**
     * @brief Returns list of available translations in the DB (e.g. "KJV", "NIV").
     */
    std::vector<std::string> GetTranslations();

    /**
     * @brief Returns list of all Bible books in the DB.
     */
    std::vector<std::string> GetAllBooks();

    /**
     * @brief Performs a startup check and logs the status of the database tables.
     */
    void ValidateDatabase();

    /**
     * @brief Auto-migration: rename "Book 1" to "Genesis" etc.
     */
    void RunMigration();

private:
    sqlite3* db = nullptr;
};
