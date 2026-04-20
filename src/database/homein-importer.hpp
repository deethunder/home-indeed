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

private:
    std::string StripFormatting(const QString& text);
    HomeInLyricsDB& lyrics_db;
};
