#include "homein-lyrics-engine.hpp"
#include <obs-module.h>
#include "network/homein-http.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

HomeInLyricsEngine::HomeInLyricsEngine() : QObject(nullptr) {}
HomeInLyricsEngine::~HomeInLyricsEngine() {}

bool HomeInLyricsEngine::Initialize(const std::string& db_path) {
    return local_db.Open(db_path);
}

void HomeInLyricsEngine::Search(const std::string& query,
                                bool allow_web,
                                SearchCallback callback) {
    auto local = local_db.Search(query);
    if (!local.empty()) {
        callback(local);
        return;
    }
    if (allow_web) {
        FetchFromLRCLIB(query, callback);
    } else {
        callback({});
    }
}

void HomeInLyricsEngine::FetchFromLRCLIB(const std::string& query,
                                          SearchCallback callback) {
    blog(LOG_INFO, "HomeIndeed: Fetching lyrics from LRCLIB for: %s (WinHTTP)", query.c_str());
    
    QtConcurrent::run([this, query, callback]() {
        std::string encoded = query;
        // Simple encoding for query
        std::replace(encoded.begin(), encoded.end(), ' ', '+');
        std::string url = "https://lrclib.net/api/search?q=" + encoded;

        std::string response = HomeIn::HttpClient::Get(url);
        std::vector<SongLyric> results;

        if (!response.empty()) {
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(response));
            QJsonArray arr = doc.array();
            blog(LOG_INFO, "HomeIndeed: LRCLIB returned %lld bytes, %d matches found", 
                 (long long)response.size(), (int)arr.size());

            for (int i = 0; i < std::min((int)arr.size(), 15); ++i) {
                QJsonObject obj = arr[i].toObject();
                SongLyric s;
                s.title   = obj["name"].toString().toStdString();
                s.artist  = obj["artistName"].toString().toStdString();
                s.content = obj.contains("plainLyrics") && !obj["plainLyrics"].toString().isEmpty()
                    ? obj["plainLyrics"].toString().toStdString()
                    : obj["syncedLyrics"].toString().toStdString();
                s.source  = "LRCLIB";
                if (!s.content.empty()) {
                    results.push_back(s);
                    local_db.AddSong(s.title, s.artist, s.content, s.source);
                }
            }
            if (!results.empty()) local_db.RebuildFTS();
        } else {
            blog(LOG_ERROR, "HomeIndeed: LRCLIB query failed (WinHTTP) or empty response");
        }

        // Return to main thread via callback
        QMetaObject::invokeMethod(this, [callback, results]() {
            callback(results);
        }, Qt::QueuedConnection);
    });
}
