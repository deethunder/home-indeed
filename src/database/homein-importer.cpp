#include "homein-importer.hpp"
#include <QFile>
#include <QDir>
#include <QXmlStreamReader>
#include <QRegularExpression>
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
        return 0;
    }

    const char* query = "SELECT title, lyrics, author FROM song;";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(ew_db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QString title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            QString rtf_lyrics = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            QString author = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));

            QString clean_lyrics = StripRTF(rtf_lyrics);
            if (!clean_lyrics.isEmpty()) {
                lyrics_db.AddSong(title.toStdString(), author.toStdString(), clean_lyrics.toStdString(), "EasyWorship");
                count++;
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(ew_db);
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
