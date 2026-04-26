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

    // --- THE NUCLEAR NEIGHBORHOOD SCAN ---
    // Instead of guessing one file, we scan the entire folder for ANY .db file 
    // that contains the "{\rtf" signature.
    QFileInfo picked_file(db_path);
    QDir db_dir = picked_file.absoluteDir();
    QStringList db_files = db_dir.entryList({"*.db"}, QDir::Files);
    
    blog(LOG_INFO, "HomeIndeed: Starting Neighborhood Scan in %s...", db_dir.absolutePath().toStdString().c_str());

    int total_imported = 0;
    for (const QString& filename : db_files) {
        QString full_path = db_dir.absoluteFilePath(filename);
        blog(LOG_INFO, "HomeIndeed: Checking neighbor file: %s", filename.toStdString().c_str());

        sqlite3* temp_db;
        if (sqlite3_open_v2(full_path.toStdString().c_str(), &temp_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            continue;
        }

        // Search every table in this database for RTF
        char** tables; int rows, cols;
        if (sqlite3_get_table(temp_db, "SELECT name FROM sqlite_master WHERE type='table'", &tables, &rows, &cols, nullptr) == SQLITE_OK) {
            for (int i = 1; i <= rows; ++i) {
                std::string table_name = tables[i];
                if (table_name.find("sqlite") != std::string::npos) continue;

                // Scan first 100 rows of this table for RTF
                std::string scan_query = "SELECT * FROM \"" + table_name + "\" LIMIT 100";
                sqlite3_stmt* scan_stmt;
                if (sqlite3_prepare_v2(temp_db, scan_query.c_str(), -1, &scan_stmt, nullptr) == SQLITE_OK) {
                    int col_count = sqlite3_column_count(scan_stmt);
                    std::string lyrics_col, title_col;
                    
                    while (sqlite3_step(scan_stmt) == SQLITE_ROW) {
                        for (int c = 0; c < col_count; ++c) {
                            const char* col_name = sqlite3_column_name(scan_stmt, c);
                            const unsigned char* blob = (const unsigned char*)sqlite3_column_blob(scan_stmt, c);
                            int bytes = sqlite3_column_bytes(scan_stmt, c);
                            
                            if (blob && bytes > 5) {
                                for (int b = 0; b < bytes - 5; ++b) {
                                    if (blob[b] == '{' && blob[b+1] == '\\' && blob[b+2] == 'r' && blob[b+3] == 't' && blob[b+4] == 'f') {
                                        lyrics_col = col_name;
                                        break;
                                    }
                                }
                            }
                            
                            std::string lower_col = col_name;
                            std::transform(lower_col.begin(), lower_col.end(), lower_col.begin(), ::tolower);
                            if (lower_col == "title" || lower_col == "name") title_col = col_name;
                        }
                        if (!lyrics_col.empty()) break;
                    }
                    sqlite3_finalize(scan_stmt);

                    if (!lyrics_col.empty()) {
                        blog(LOG_INFO, "HomeIndeed: FOUND LYRICS in file '%s', table '%s', column '%s'!", 
                             filename.toStdString().c_str(), table_name.c_str(), lyrics_col.c_str());
                        
                        std::string import_query;
                        if (!title_col.empty()) {
                            import_query = "SELECT \"" + title_col + "\", \"" + lyrics_col + "\" FROM \"" + table_name + "\"";
                            total_imported += ExecuteImportQuery(temp_db, import_query, false);
                        } else {
                            // TRIAL AND ERROR JOINING
                            blog(LOG_INFO, "HomeIndeed: Table has no titles. Starting Key Master discovery...");
                            std::string attach_cmd = "ATTACH DATABASE '" + QDir::toNativeSeparators(db_path).toStdString() + "' AS main_db";
                            sqlite3_exec(temp_db, attach_cmd.c_str(), nullptr, nullptr, nullptr);

                            std::vector<std::string> source_keys = {"song_uid", "song_item_uid", "rowid", "song_id"};
                            std::vector<std::string> target_keys = {"song_id", "parent_uid", "song_uid", "song_item_uid"};
                            
                            std::string best_source, best_target;
                            for (const auto& s_key : source_keys) {
                                for (const auto& t_key : target_keys) {
                                    std::string check_query = "SELECT COUNT(*) FROM main_db.song m JOIN \"" + table_name + "\" t ON m.\"" + s_key + "\" = t.\"" + t_key + "\"";
                                    sqlite3_stmt* check_stmt;
                                    if (sqlite3_prepare_v2(temp_db, check_query.c_str(), -1, &check_stmt, nullptr) == SQLITE_OK) {
                                        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                                            int count = sqlite3_column_int(check_stmt, 0);
                                            if (count > 0) {
                                                blog(LOG_INFO, "HomeIndeed: KEY FOUND! Bridging '%s' -> '%s' (%d matches)", s_key.c_str(), t_key.c_str(), count);
                                                best_source = s_key; best_target = t_key;
                                                sqlite3_finalize(check_stmt);
                                                goto key_found;
                                            }
                                        }
                                        sqlite3_finalize(check_stmt);
                                    }
                                }
                            }

                            key_found:
                            if (!best_source.empty()) {
                                import_query = "SELECT m.title, t.\"" + lyrics_col + "\" FROM main_db.song m JOIN \"" + table_name + "\" t ON m.\"" + best_source + "\" = t.\"" + best_target + "\"";
                                total_imported += ExecuteImportQuery(temp_db, import_query, false);
                            } else {
                                blog(LOG_WARNING, "HomeIndeed: Could not find a valid ID bridge for file '%s'", filename.toStdString().c_str());
                            }
                        }
                    }
                }
            }
            sqlite3_free_table(tables);
        }
        sqlite3_close(temp_db);
        if (total_imported > 0) break;
    }

    sqlite3_close(ew_db);
    if (total_imported > 0) {
        blog(LOG_INFO, "HomeIndeed: Success! Imported %d songs via Neighborhood Scan.", total_imported);
        return total_imported;
    }

    blog(LOG_ERROR, "HomeIndeed: Neighborhood Scan failed. No lyrics found in any .db file in that folder.");
    return 0;
}

int HomeInImporter::ExecuteImportQuery(sqlite3* ew_db, const std::string& query, bool has_author) {
    blog(LOG_INFO, "HomeIndeed: Running full-table import (filtering in C++): %s", query.c_str());

    sqlite3_stmt* stmt;
    int count = 0;
    int total_rows = 0;
    if (sqlite3_prepare_v2(ew_db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            total_rows++;
            const char* raw_title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            
            // Read lyrics as BLOB to handle binary RTF
            const unsigned char* blob = (const unsigned char*)sqlite3_column_blob(stmt, 1);
            int bytes = sqlite3_column_bytes(stmt, 1);
            
            if (!raw_title || !blob || bytes < 5) continue;

            // Manual check for RTF signature in binary blob
            bool is_rtf = false;
            for (int i = 0; i < bytes - 5; ++i) {
                if (blob[i] == '{' && blob[i+1] == '\\' && blob[i+2] == 'r' && blob[i+3] == 't' && blob[i+4] == 'f') {
                    is_rtf = true;
                    break;
                }
            }
            if (!is_rtf) continue;

            QString title = QString::fromUtf8(raw_title);
            // Convert blob to string for the RTF parser
            QString rtf_content = QString::fromUtf8((const char*)blob, bytes);
            QString lyrics = StripRTF(rtf_content);
            
            if (lyrics.isEmpty()) continue;

            QString author = "";
            if (has_author) {
                const char* raw_author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                if (raw_author) author = QString::fromUtf8(raw_author);
            }

            if (lyrics_db.AddSong(title.toStdString(), author.toStdString(), lyrics.toStdString(), "EasyWorship")) {
                count++;
            }
        }
        sqlite3_finalize(stmt);
    } else {
        blog(LOG_ERROR, "HomeIndeed: SQL prepare failed: %s", sqlite3_errmsg(ew_db));
    }

    if (count > 0) {
        lyrics_db.RebuildFTS();
    }

    blog(LOG_INFO, "HomeIndeed: Query complete. Found %d rows total, %d songs matched RTF signature and were imported.", total_rows, count);
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
