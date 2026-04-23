#pragma once

#include <string>
#include <vector>
#include <QString>
#include "homein-lyrics-db.hpp"

/**
 * @class HomeInImporter
 * @brief Handles importing lyrics from external formats (EasyWorship/OpenLyrics XML).
 */
class HomeInImporter {
public:
    HomeInImporter(HomeInLyricsDB& db);

    /**
     * @brief Parses a directory of XML files or a single file.
     * @return Number of songs successfully imported.
     */
    int ImportFromFolder(const QString& folder_path);
    int ImportFile(const QString& file_path);
    /**
     * @brief Imports songs directly from an EasyWorship 6/7 SQLite database.
     * @return Number of songs imported.
     */
    int ImportFromEW7(const QString& db_path);

private:
    int ExecuteImportQuery(struct sqlite3* ew_db, const std::string& query, bool has_author);
    std::string StripFormatting(const QString& text);
    /**
     * @brief Strips EasyWorship RTF formatting to return plain text.
     */
    QString StripRTF(const QString& rtf);
    HomeInLyricsDB& lyrics_db;
};
