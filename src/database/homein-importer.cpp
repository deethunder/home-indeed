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
