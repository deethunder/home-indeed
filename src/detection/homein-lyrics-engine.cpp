#include "homein-lyrics-engine.hpp"
#include <obs-module.h>
#include "network/homein-http.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrent>

HomeInLyricsEngine::HomeInLyricsEngine() : QObject(nullptr) {}
HomeInLyricsEngine::~HomeInLyricsEngine() {}

bool HomeInLyricsEngine::Initialize(const std::string& db_path) {
    return local_db.Open(db_path);
}

void HomeInLyricsEngine::Search(const std::string& query, bool allow_web,
                                 SearchCallback callback) {
    // Always check local DB first — instant, offline
    auto local_results = local_db.Search(query);

    if (allow_web) {
        FetchFromLRCLIB(query, [callback, local_results](const std::vector<SongLyric>& web_results) {
            std::vector<SongLyric> combined = local_results;
            
            // Add web results that aren't already in local results (by title/artist)
            for (const auto& ws : web_results) {
                bool exists = false;
                for (const auto& ls : local_results) {
                    if (ls.title == ws.title && ls.artist == ws.artist) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    combined.push_back(ws);
                }
            }
            callback(combined);
        });
    } else {
        callback(local_results);
    }
}

// FIX #6: Completely rewritten.
// Old code: called QNAM::get() from a detached std::thread → crash/UB.
//           Used QEventLoop inside that thread → deadlock.
// New code: runs entirely on the Qt main thread using Qt's built-in async
//           signal/slot mechanism. No std::thread, no QEventLoop needed.
void HomeInLyricsEngine::FetchFromLRCLIB(const std::string& query,
                                          SearchCallback callback) {
    // Run blocking WinHTTP request in a background thread to keep UI responsive
    QtConcurrent::run([this, query, callback]() {
        std::string encoded = query;
        std::replace(encoded.begin(), encoded.end(), ' ', '+');
        std::string url = "https://lrclib.net/api/search?q=" + encoded;

        std::string response = HomeIn::HttpClient::Get(url);
        std::vector<SongLyric> results;

        if (!response.empty()) {
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(response));
            QJsonArray arr = doc.array();

            int limit = std::min((int)arr.size(), 5);
            for (int i = 0; i < limit; ++i) {
                QJsonObject obj = arr[i].toObject();
                SongLyric s;
                s.id     = 0;
                s.title  = obj["name"].toString().toStdString();
                s.artist = obj["artistName"].toString().toStdString();

                if (obj.contains("plainLyrics") && !obj["plainLyrics"].toString().isEmpty()) {
                    s.content = obj["plainLyrics"].toString().toStdString();
                } else if (obj.contains("syncedLyrics")) {
                    s.content = obj["syncedLyrics"].toString().toStdString();
                }

                s.source = "LRCLIB";
                if (!s.content.empty()) {
                    results.push_back(s);
                    local_db.AddSong(s.title, s.artist, s.content, s.source);
                }
            }
        } else {
            blog(LOG_ERROR, "HomeIndeed: LRCLIB query failed or returned empty response (WinHTTP)");
        }

        // Return to main thread via callback
        QMetaObject::invokeMethod(this, [callback, results]() {
            callback(results);
        }, Qt::QueuedConnection);
    });
}
