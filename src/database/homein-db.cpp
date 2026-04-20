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
        blog(LOG_ERROR, "Failed to open Bible database: %s", sqlite3_errmsg(db));
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

    // 1. Try to match Book Name first (e.g. "gen" -> Genesis 1:1)
    const char* book_sql = 
        "SELECT v.text, b.name, t.abbreviation, v.chapter, v.verse "
        "FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "JOIN translations t ON v.translation_id = t.id "
        "WHERE (b.name LIKE ? OR b.abbreviation LIKE ?) AND v.chapter = 1 AND v.verse = 1 "
        "LIMIT 1";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, book_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            BibleVerse v;
            v.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            v.book_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            v.translation_abbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            v.chapter = sqlite3_column_int(stmt, 3);
            v.verse = sqlite3_column_int(stmt, 4);
            results.push_back(v);
        }
        sqlite3_finalize(stmt);
    }

    // 2. Optimized FTS5 Search for full text
    const char* fts_sql = 
        "SELECT v.text, b.name, t.abbreviation, v.chapter, v.verse "
        "FROM verses v "
        "JOIN books b ON v.book_id = b.id "
        "JOIN translations t ON v.translation_id = t.id "
        "WHERE verses MATCH ? "
        "LIMIT ?";

    if (sqlite3_prepare_v2(db, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        // If query has multiple words, wrap in quotes for phrase search
        std::string formatted_query = query;
        if (query.find(' ') != std::string::npos && query.front() != '"') {
            formatted_query = "\"" + query + "\"";
        }

        sqlite3_bind_text(stmt, 1, formatted_query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit - (int)results.size());

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BibleVerse v;
            v.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            v.book_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            v.translation_abbr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            v.chapter = sqlite3_column_int(stmt, 3);
            v.verse = sqlite3_column_int(stmt, 4);
            
            // Avoid duplicates if book match already found it
            bool duplicate = false;
            for (const auto& r : results) if (r.book_name == v.book_name && r.chapter == v.chapter && r.verse == v.verse) { duplicate = true; break; }
            if (!duplicate) results.push_back(v);
        }
        sqlite3_finalize(stmt);
    }

    // 3. Last Resort: LIKE search for partial word matches
    if (results.empty()) {
        const char* fallback_sql = 
            "SELECT v.text, b.name, t.abbreviation, v.chapter, v.verse "
            "FROM verses v "
            "JOIN books b ON v.book_id = b.id "
            "JOIN translations t ON v.translation_id = t.id "
            "WHERE v.text LIKE ? "
            "LIMIT ?";
        
        if (sqlite3_prepare_v2(db, fallback_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string pattern = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
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
        }
    }

    return results;
}

std::vector<std::string> HomeInDB::GetTranslations() {
    std::vector<std::string> results;
    if (!db) return results;

    const char* sql = "SELECT abbreviation FROM translations ORDER BY abbreviation ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}
