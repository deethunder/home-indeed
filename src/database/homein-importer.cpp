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
    for (const auto& col : columns) {
        std::string lower = col;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "title" || lower == "name" || lower == "songtitle") title_col = col;
        if (lower == "lyrics" || lower == "words" || lower == "content" || lower == "text") lyrics_col = col;
        if (lower == "author" || lower == "artist" || lower == "copyright" || lower == "writer") author_col = col;
    }

    if (title_col.empty() || lyrics_col.empty()) {
        blog(LOG_ERROR, "HomeIndeed: Could not find title or lyrics column in EW database");
        sqlite3_close(ew_db);
        return 0;
    }

    // Step 5: Import songs
    std::string query = "SELECT \"" + title_col + "\", \"" + lyrics_col + "\"";
    if (!author_col.empty()) query += ", \"" + author_col + "\"";
    query += " FROM \"" + song_table + "\"";

    blog(LOG_INFO, "HomeIndeed: Running import query: %s", query.c_str());

    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(ew_db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* raw_title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* raw_lyrics = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* raw_author = !author_col.empty() ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";

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

    // Step 6: Rebuild FTS index in our lyrics DB
    if (count > 0) {
        lyrics_db.RebuildFTS();
    }

    blog(LOG_INFO, "HomeIndeed: Imported %d songs from EasyWorship", count);
    return count;
}

QString HomeInImporter::StripRTF(const QString& rtf) {
    if (!rtf.contains("{\\rtf1")) return rtf; // Not RTF

    QString out = rtf;
    // 1. Remove control words (e.g. \fonttbl, \colortbl)
    // Simple regex for RTF tags: anything starting with \ and ended by a space or another \ or { or }
    static QRegularExpression re("\\\\[a-z0-9]+ ?");
    out.remove(re);

    // 2. Remove group braces
    out.remove("{");
    out.remove("}");

    // 3. Clean up whitespace and newlines
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
