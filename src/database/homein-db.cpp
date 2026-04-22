#include "homein-db.hpp"
#include <obs-module.h>
#include <sstream>

static const char* BIBLE_BOOK_NAMES[] = {
    "Genesis", "Exodus", "Leviticus", "Numbers", "Deuteronomy",
    "Joshua", "Judges", "Ruth", "1 Samuel", "2 Samuel",
    "1 Kings", "2 Kings", "1 Chronicles", "2 Chronicles", "Ezra",
    "Nehemiah", "Esther", "Job", "Psalms", "Proverbs",
    "Ecclesiastes", "Song of Solomon", "Isaiah", "Jeremiah", "Lamentations",
    "Ezekiel", "Daniel", "Hosea", "Joel", "Amos",
    "Obadiah", "Jonah", "Micah", "Nahum", "Habakkuk",
    "Zephaniah", "Haggai", "Zechariah", "Malachi",
    "Matthew", "Mark", "Luke", "John", "Acts",
    "Romans", "1 Corinthians", "2 Corinthians", "Galatians", "Ephesians",
    "Philippians", "Colossians", "1 Thessalonians", "2 Thessalonians",
    "1 Timothy", "2 Timothy", "Titus", "Philemon", "Hebrews",
    "James", "1 Peter", "2 Peter", "1 John", "2 John",
    "3 John", "Jude", "Revelation"
};

static const char* BIBLE_BOOK_ABBRS[] = {
    "Gen", "Exo", "Lev", "Num", "Deut", "Josh", "Judg", "Ruth",
    "1Sam", "2Sam", "1Kgs", "2Kgs", "1Chr", "2Chr", "Ezra", "Neh",
    "Est", "Job", "Psa", "Prov", "Eccl", "Song", "Isa", "Jer",
    "Lam", "Ezek", "Dan", "Hos", "Joel", "Amos", "Obad", "Jonah",
    "Mic", "Nah", "Hab", "Zeph", "Hag", "Zech", "Mal",
    "Matt", "Mark", "Luke", "John", "Acts", "Rom", "1Cor", "2Cor",
    "Gal", "Eph", "Phil", "Col", "1Thess", "2Thess", "1Tim", "2Tim",
    "Titus", "Phlm", "Heb", "Jas", "1Pet", "2Pet", "1John", "2John",
    "3John", "Jude", "Rev"
};

HomeInDB::HomeInDB() {}
HomeInDB::~HomeInDB() { Close(); }

bool HomeInDB::Open(const std::string& db_path) {
    if (db) Close();

    blog(LOG_INFO, "HomeIndeed: Opening Bible DB at: %s", db_path.c_str());
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        blog(LOG_ERROR, "HomeIndeed: Failed to open Bible database: %s",
             sqlite3_errmsg(db));
        db = nullptr;
        return false;
    }

    ValidateDatabase();
    RunMigration();
    return true;
}

void HomeInDB::ValidateDatabase() {
    if (!db) return;
    blog(LOG_INFO, "=== HomeIndeed Bible DB Diagnostic ===");

    const char* sql_tables = "SELECT name FROM sqlite_master WHERE type='table';";
    sqlite3_stmt* stmt;
    std::vector<std::string> tables;
    if (sqlite3_prepare_v2(db, sql_tables, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            tables.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    std::stringstream ss;
    ss << "Tables found: ";
    for (const auto& t : tables) ss << t << " ";
    blog(LOG_INFO, "HomeIndeed: %s", ss.str().c_str());

    auto get_count = [&](const char* table) -> int {
        int count = 0;
        char query[128];
        snprintf(query, sizeof(query), "SELECT COUNT(*) FROM %s", table);
        if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return count;
    };

    for (const auto& t : tables)
        blog(LOG_INFO, "HomeIndeed: Table '%s' has %d rows", t.c_str(), get_count(t.c_str()));

    if (std::find(tables.begin(), tables.end(), "translations") != tables.end()) {
        const char* sql_trans = "SELECT id, abbreviation FROM translations LIMIT 5;";
        if (sqlite3_prepare_v2(db, sql_trans, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                blog(LOG_INFO, "HomeIndeed: Translation: ID=%d, Abbr=%s",
                     sqlite3_column_int(stmt, 0),
                     reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            }
        }
        sqlite3_finalize(stmt);
    }
    blog(LOG_INFO, "=== End Diagnostic ===");
}

void HomeInDB::RunMigration() {
    if (!db) return;

    sqlite3_stmt* check;
    if (sqlite3_prepare_v2(db, "SELECT name FROM books WHERE id=1", -1, &check,
                            nullptr) == SQLITE_OK) {
        if (sqlite3_step(check) == SQLITE_ROW) {
            const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(check, 0));
            std::string name = raw ? raw : "";
            sqlite3_finalize(check);

            if (name.find("Book ") == 0 || name.empty()) {
                blog(LOG_INFO, "HomeIndeed: Migrating Bible book names...");
                sqlite3_stmt* update;
                if (sqlite3_prepare_v2(db,
                        "UPDATE books SET name=?, abbreviation=? WHERE id=?",
                        -1, &update, nullptr) == SQLITE_OK) {
                    for (int i = 0; i < 66; i++) {
                        sqlite3_bind_text(update, 1, BIBLE_BOOK_NAMES[i], -1, SQLITE_STATIC);
                        sqlite3_bind_text(update, 2, BIBLE_BOOK_ABBRS[i],  -1, SQLITE_STATIC);
                        sqlite3_bind_int(update, 3, i + 1);
                        sqlite3_step(update);
                        sqlite3_reset(update);
                    }
                    sqlite3_finalize(update);
                    blog(LOG_INFO, "HomeIndeed: Bible migration complete.");
                }
            }
        } else {
            sqlite3_finalize(check);
        }
    }
}

void HomeInDB::Close() {
    if (db) { sqlite3_close(db); db = nullptr; }
}

bool HomeInDB::GetVerse(const std::string& book_name, int chapter, int verse,
                         const std::string& translation_abbr, BibleVerse& out_verse) {
    if (!db) return false;

    // FIX #1: translation_abbr is now the raw abbreviation (e.g. "KJV"),
    // matched with exact equality, not LIKE with a full display string.
    // The old code received "King James Version (KJV)" from currentText()
    // and tried to LIKE-match it against "KJV" in the DB — always failed.
    const char* query =
        "SELECT v.text, b.name, t.abbreviation, v.translation_id, v.book_id "
        "FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "JOIN translations t ON v.translation_id = t.id "
        "WHERE (b.name LIKE ? OR b.abbreviation LIKE ?) "
        "AND CAST(v.chapter AS INTEGER) = ? "
        "AND CAST(v.verse AS INTEGER) = ? "
        "AND t.abbreviation = ? "
        "LIMIT 1";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    std::string book_pattern = "%" + book_name + "%";

    sqlite3_bind_text(stmt, 1, book_pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, book_pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, chapter);
    sqlite3_bind_int(stmt, 4, verse);
    // FIX #1: Exact match on abbreviation — no wildcards
    sqlite3_bind_text(stmt, 5, translation_abbr.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* bname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* tabbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        out_verse.text             = text  ? text  : "";
        out_verse.book_name        = bname ? bname : "";
        out_verse.translation_abbr = tabbr ? tabbr : "";
        out_verse.translation_id   = sqlite3_column_int(stmt, 3);
        out_verse.book_id          = sqlite3_column_int(stmt, 4);
        out_verse.chapter          = chapter;
        out_verse.verse            = verse;
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}

int HomeInDB::GetChapterCount(const std::string& book_name) {
    if (!db) return 0;

    const char* sql =
        "SELECT MAX(CAST(v.chapter AS INTEGER)) FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "WHERE b.name LIKE ? OR b.abbreviation LIKE ?";

    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + book_name + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

std::vector<BibleVerse> HomeInDB::SearchVerses(const std::string& query, int limit) {
    std::vector<BibleVerse> results;
    if (!db) return results;

    // FIX #10: Use FTS5 MATCH instead of LIKE.
    // The verses table IS a virtual FTS5 table. Using LIKE on it:
    //   (a) bypasses the index entirely — full scan of 155k rows
    //   (b) doesn't work correctly because text lives in verses_content
    // MATCH is fast (uses the FTS index) and returns results ranked by relevance.
    const char* sql =
        "SELECT v.text, b.name, t.abbreviation, "
        "CAST(v.chapter AS INTEGER), CAST(v.verse AS INTEGER) "
        "FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "JOIN translations t ON v.translation_id = t.id "
        "WHERE verses MATCH ? "
        "ORDER BY rank "
        "LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BibleVerse v;
            const char* text  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* bname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* tabbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            v.text             = text  ? text  : "";
            v.book_name        = bname ? bname : "";
            v.translation_abbr = tabbr ? tabbr : "";
            v.chapter          = sqlite3_column_int(stmt, 3);
            v.verse            = sqlite3_column_int(stmt, 4);
            results.push_back(v);
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

std::vector<std::string> HomeInDB::GetTranslations() {
    std::vector<std::string> results;
    if (!db) return results;

    const char* sql = "SELECT abbreviation FROM translations ORDER BY id ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* abbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (abbr) results.push_back(abbr);
        }
        sqlite3_finalize(stmt);
    }
    return results;
}
