#include "homein-db.hpp"
#include <obs-module.h>

HomeInDB::HomeInDB() {}

HomeInDB::~HomeInDB() {
    Close();
}

bool HomeInDB::Open(const std::string& db_path) {
    if (db) Close();
    
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        obs_log(LOG_ERROR, "Failed to open Bible database: %s", sqlite3_errmsg(db));
        db = nullptr;
        return false;
    }
    return true;
}

void HomeInDB::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool HomeInDB::GetVerse(const std::string& book_name, int chapter, int verse, const std::string& translation_abbr, BibleVerse& out_verse) {
    if (!db) return false;

    const char* query = 
        "SELECT v.text, b.name, t.abbreviation, v.translation_id, v.book_id "
        "FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "JOIN translations t ON v.translation_id = t.id "
        "WHERE b.name LIKE ? AND v.chapter = ? AND v.verse = ? AND t.abbreviation = ? "
        "LIMIT 1";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string book_pattern = "%" + book_name + "%";
    sqlite3_bind_text(stmt, 1, book_pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chapter);
    sqlite3_bind_int(stmt, 3, verse);
    sqlite3_bind_text(stmt, 4, translation_abbr.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_verse.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        out_verse.book_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        out_verse.translation_abbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        out_verse.translation_id = sqlite3_column_int(stmt, 3);
        out_verse.book_id = sqlite3_column_int(stmt, 4);
        out_verse.chapter = chapter;
        out_verse.verse = verse;
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}

std::vector<BibleVerse> HomeInDB::SearchVerses(const std::string& query, int limit) {
    std::vector<BibleVerse> results;
    if (!db) return results;

    const char* sql = 
        "SELECT v.text, b.name, t.abbreviation, v.chapter, v.verse "
        "FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "JOIN translations t ON v.translation_id = t.id "
        "WHERE verses MATCH ? "
        "LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BibleVerse v;
        v.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        v.book_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        v.translation_abbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        v.chapter = sqlite3_column_int(stmt, 3);
        v.verse = sqlite3_column_int(stmt, 4);
        results.push_back(v);
    }

    sqlite3_finalize(stmt);
    return results;
}
