#include "homein-importer.hpp"
#include <QFile>
#include <QDir>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <algorithm>
#include <obs-module.h>

HomeInImporter::HomeInImporter(HomeInLyricsDB& db) : lyrics_db(db) {}

int HomeInImporter::ImportFromFolder(const QString& folder_path) {
    QDir dir(folder_path);
    QStringList filters;
    filters << "*.xml";
    QStringList files = dir.entryList(filters, QDir::Files);
    
    int count = 0;
    for (const QString& filename : files) {
        if (ImportFile(dir.absoluteFilePath(filename)) > 0) {
            count++;
        }
    }
    return count;
}

int HomeInImporter::ImportFromEW7(const QString& db_path) {
    if (db_path.isEmpty()) return 0;

    sqlite3* ew_db;
    if (sqlite3_open(db_path.toStdString().c_str(), &ew_db) != SQLITE_OK) {
        blog(LOG_ERROR, "HomeIndeed: Could not open EW database: %s", db_path.toStdString().c_str());
        return 0;
    }

    // Step 1: Discover all tables in the EW database
    std::vector<std::string> tables;
    sqlite3_stmt* tbl_stmt;
    if (sqlite3_prepare_v2(ew_db, "SELECT name FROM sqlite_master WHERE type='table'", -1, &tbl_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(tbl_stmt) == SQLITE_ROW) {
            tables.push_back(reinterpret_cast<const char*>(sqlite3_column_text(tbl_stmt, 0)));
        }
        sqlite3_finalize(tbl_stmt);
    }
    blog(LOG_INFO, "HomeIndeed: EW database has %d tables", (int)tables.size());
    for (const auto& t : tables) {
        blog(LOG_INFO, "  Table: %s", t.c_str());
    }

    // Step 2: Find the songs table (could be "song", "songs", "Song", etc.)
    std::string song_table;
    for (const auto& t : tables) {
        std::string lower = t;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "song" || lower == "songs") {
            song_table = t;
            break;
        }
    }

    if (song_table.empty()) {
        blog(LOG_ERROR, "HomeIndeed: No 'song' or 'songs' table found in EW database");
        // Try the first non-system table as a guess
        for (const auto& t : tables) {
            if (t.find("sqlite") == std::string::npos) {
                song_table = t;
                blog(LOG_INFO, "HomeIndeed: Trying table '%s' as fallback", t.c_str());
                break;
            }
        }
    }

    if (song_table.empty()) {
        sqlite3_close(ew_db);
        return 0;
    }

    // Step 3: Get column names from the table
    std::vector<std::string> columns;
    std::string pragma = "PRAGMA table_info(" + song_table + ")";
    sqlite3_stmt* col_stmt;
    if (sqlite3_prepare_v2(ew_db, pragma.c_str(), -1, &col_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(col_stmt) == SQLITE_ROW) {
            columns.push_back(reinterpret_cast<const char*>(sqlite3_column_text(col_stmt, 1)));
        }
        sqlite3_finalize(col_stmt);
    }
    blog(LOG_INFO, "HomeIndeed: Table '%s' has %d columns", song_table.c_str(), (int)columns.size());
    for (const auto& col : columns) {
        blog(LOG_INFO, "  Column: %s", col.c_str());
    }

    // Step 4: Map columns (case-insensitive)
    std::string title_col, lyrics_col, author_col;
    bool high_quality_lyrics = false;
    for (const auto& col : columns) {
        std::string lower = col;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "title" || lower == "name" || lower == "songtitle") title_col = col;
        if (lower == "lyrics" || lower == "words" || lower == "content" || lower == "text" || lower == "body" || lower == "words_rtf") {
            lyrics_col = col;
            high_quality_lyrics = true;
        }
        if (lower == "author" || lower == "artist" || lower == "copyright" || lower == "writer") author_col = col;
    }
    
    // If no high quality lyrics column found in 'song', we WILL check 'revision' table later
    if (!high_quality_lyrics) {
        for (const auto& col : columns) {
            std::string lower = col;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "description" || lower == "revision") {
                lyrics_col = col;
                blog(LOG_INFO, "HomeIndeed: Found '%s' column, but will prefer 'revision' table if available", col.c_str());
                break;
            }
        }
    }

    // Step 5: Check 'revision' table first for EW7 if available, 
    // unless we found a high-quality lyrics column in the main 'song' table.
    if (!high_quality_lyrics || title_col.empty()) {
        blog(LOG_INFO, "HomeIndeed: High-quality lyrics not in 'song' table, checking 'revision' table...");
        std::string rev_table;
        for (const auto& t : tables) {
            std::string lower = t;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "revision" || lower == "revisions") {
                rev_table = t;
                break;
            }
        }

        if (!rev_table.empty()) {
            std::vector<std::string> rev_columns;
            std::string rev_pragma = "PRAGMA table_info(\"" + rev_table + "\")";
            if (sqlite3_prepare_v2(ew_db, rev_pragma.c_str(), -1, &col_stmt, nullptr) == SQLITE_OK) {
                blog(LOG_INFO, "HomeIndeed: Table '%s' has columns:", rev_table.c_str());
                while (sqlite3_step(col_stmt) == SQLITE_ROW) {
                    const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(col_stmt, 1));
                    rev_columns.push_back(col_name);
                    blog(LOG_INFO, "  Column: %s", col_name);
                }
                sqlite3_finalize(col_stmt);
            }

            std::string rev_lyrics_col;
            for (const auto& col : rev_columns) {
                std::string lower = col;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "words" || lower == "body" || lower == "lyrics" || lower == "content" || lower == "words_rtf") {
                    rev_lyrics_col = col;
                    break;
                }
            }

            if (!rev_lyrics_col.empty()) {
                blog(LOG_INFO, "HomeIndeed: Found lyrics in '%s' table, column '%s'", rev_table.c_str(), rev_lyrics_col.c_str());
                
                // Join song and revision on song_uid, taking the latest revision
                std::string query = "SELECT s.\"" + title_col + "\", r.\"" + rev_lyrics_col + "\"";
                if (!author_col.empty()) query += ", s.\"" + author_col + "\"";
                query += " FROM \"" + song_table + "\" s JOIN \"" + rev_table + "\" r ON s.song_uid = r.song_uid";
                query += " WHERE r.rowid = (SELECT MAX(rowid) FROM \"" + rev_table + "\" WHERE song_uid = s.song_uid)";
                
                return ExecuteImportQuery(ew_db, query, !author_col.empty());
            }
        }

        // If we get here, we haven't returned yet, meaning we didn't find high quality lyrics in 'song'
        // or a working 'revision' table. Let's dump some data to the log for debugging.
        blog(LOG_INFO, "HomeIndeed: No obvious lyrics found. Dumping first 5 rows of 'song' table for inspection:");
        sqlite3_stmt* diag_stmt;
        if (sqlite3_prepare_v2(ew_db, "SELECT * FROM \"song\" LIMIT 5", -1, &diag_stmt, nullptr) == SQLITE_OK) {
            int col_count = sqlite3_column_count(diag_stmt);
            int row_idx = 0;
            while (sqlite3_step(diag_stmt) == SQLITE_ROW) {
                row_idx++;
                for (int i = 0; i < col_count; ++i) {
                    const char* col_name = sqlite3_column_name(diag_stmt, i);
                    const char* val = (const char*)sqlite3_column_text(diag_stmt, i);
                    if (val && strlen(val) > 0) {
                        std::string snippet = std::string(val).substr(0, 100);
                        blog(LOG_INFO, "  [ROW %d] Col '%s': \"%s...\"", row_idx, col_name, snippet.c_str());
                    }
                }
            }
            sqlite3_finalize(diag_stmt);
        }

        blog(LOG_ERROR, "HomeIndeed: Could not find title or lyrics column in EW database");
        sqlite3_close(ew_db);
        return 0;
    }

    // Standard single-table import (if we found a high quality column in 'song')
    blog(LOG_INFO, "HomeIndeed: Finalizing mapping - Title: '%s', Lyrics: '%s'", 
         title_col.c_str(), lyrics_col.c_str());
    
    std::string query = "SELECT \"" + title_col + "\", \"" + lyrics_col + "\"";
    if (!author_col.empty()) query += ", \"" + author_col + "\"";
    query += " FROM \"" + song_table + "\"";
    
    return ExecuteImportQuery(ew_db, query, !author_col.empty());
}

int HomeInImporter::ExecuteImportQuery(sqlite3* ew_db, const std::string& query, bool has_author) {
    blog(LOG_INFO, "HomeIndeed: Running import query: %s", query.c_str());

    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(ew_db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* raw_title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* raw_lyrics = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* raw_author = has_author ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";

            if (!raw_title || !raw_lyrics) continue;

            QString title = QString::fromUtf8(raw_title);
            QString lyrics = StripRTF(QString::fromUtf8(raw_lyrics));
            QString author = raw_author ? QString::fromUtf8(raw_author) : "";

            if (!lyrics.isEmpty()) {
                lyrics_db.AddSong(title.toStdString(), author.toStdString(), lyrics.toStdString(), "EasyWorship");
                count++;
            }
        }
        sqlite3_finalize(stmt);
    } else {
        blog(LOG_ERROR, "HomeIndeed: EW import query failed: %s", sqlite3_errmsg(ew_db));
    }

    sqlite3_close(ew_db);

    if (count > 0) {
        lyrics_db.RebuildFTS();
    }

    blog(LOG_INFO, "HomeIndeed: Imported %d songs from EasyWorship", count);
    return count;
}

QString HomeInImporter::StripRTF(const QString& rtf) {
    if (!rtf.contains("{\\rtf")) return rtf;

    QString out = rtf;

    // 1. Remove RTF header groups: {\fonttbl ...}, {\colortbl ...}, etc.
    out.remove(QRegularExpression("\\{\\\\[^\\{\\}]*\\}"));

    // 2. Replace \par and \line with actual newlines BEFORE stripping words
    out.replace(QRegularExpression("\\\\par[d]?\\b\\s*",
        QRegularExpression::CaseInsensitiveOption), "\n");
    out.replace(QRegularExpression("\\\\line\\b\\s*",
        QRegularExpression::CaseInsensitiveOption), "\n");

    // 3. Remove ALL RTF control words (case-insensitive, optional numeric param)
    out.remove(QRegularExpression("\\\\[a-zA-Z]+[-]?[0-9]*\\s?"));

    // 4. Remove remaining braces
    out.remove('{');
    out.remove('}');

    // 5. Collapse multiple blank lines and trim
    out.replace(QRegularExpression("\\n{3,}"), "\n\n");
    out = out.trimmed();
    return out;
}

int HomeInImporter::ImportFile(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;

    QXmlStreamReader xml(&file);
    std::string title, artist, content;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == "title") {
                title = xml.readElementText().toStdString();
            } else if (xml.name() == "author") {
                artist = xml.readElementText().toStdString();
            } else if (xml.name() == "lines") {
                content += StripFormatting(xml.readElementText(QXmlStreamReader::IncludeChildElements)) + "\n";
            }
        }
    }

    if (xml.hasError()) {
        blog(LOG_ERROR, "XML Error: %s", xml.errorString().toStdString().c_str());
        return 0;
    }

    if (!title.empty() && !content.empty()) {
        if (lyrics_db.AddSong(title, artist, content, "EasyWorship Import")) {
            return 1;
        }
    }

    return 0;
}

std::string HomeInImporter::StripFormatting(const QString& text) {
    // 1. Remove all XML tags (e.g., <tag>, <tag/>)
    QString clean = text;
    clean.replace(QRegularExpression("<[^>]*>"), "");
    
    // 2. Normalize whitespace
    clean = clean.trimmed();
    
    return clean.toStdString();
}
