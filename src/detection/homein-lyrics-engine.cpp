#include "homein-lyrics-engine.hpp"
#include <obs-module.h>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

HomeInLyricsEngine::HomeInLyricsEngine() : QObject(nullptr) {}
HomeInLyricsEngine::~HomeInLyricsEngine() {}

bool HomeInLyricsEngine::Initialize(const std::string& db_path) {
    return local_db.Open(db_path);
}

void HomeInLyricsEngine::Search(const std::string& query, bool allow_web,
                                 SearchCallback callback) {
    // Always check local DB first — instant, offline, no threading needed
    auto local_results = local_db.Search(query);
    if (!local_results.empty()) {
        callback(local_results);
        return;
    }

    if (allow_web) {
        FetchFromLRCLIB(query, callback);
    } else {
        callback({});
    }
}

// FIX #6: Completely rewritten.
// Old code: called QNAM::get() from a detached std::thread → crash/UB.
//           Used QEventLoop inside that thread → deadlock.
// New code: runs entirely on the Qt main thread using Qt's built-in async
//           signal/slot mechanism. No std::thread, no QEventLoop needed.
void HomeInLyricsEngine::FetchFromLRCLIB(const std::string& query,
                                          SearchCallback callback) {
    QUrl url("https://lrclib.net/api/search");
    QUrlQuery q;
    q.addQueryItem("q", QString::fromStdString(query));
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "HomeIndeed/1.0 (OBS Plugin)");

    // network_manager lives on the Qt main thread; get() is safe here.
    QNetworkReply* reply = network_manager.get(request);

    // Lambda fires on the Qt main thread when the reply arrives — no blocking.
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback]() {
        std::vector<SongLyric> results;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray arr = doc.array();

            int limit = std::min((int)arr.size(), 5);
            for (int i = 0; i < limit; ++i) {
                QJsonObject obj = arr[i].toObject();
                SongLyric s;
                s.id     = 0;
                s.title  = obj["name"].toString().toStdString();
                s.artist = obj["artistName"].toString().toStdString();

                if (obj.contains("plainLyrics") &&
                    !obj["plainLyrics"].toString().isEmpty()) {
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

            if (!results.empty()) {
                // Triggers are now keeping FTS in sync automatically via
                // EnsureSchema() triggers — RebuildFTS still available for
                // bulk imports but not needed for single inserts.
                blog(LOG_INFO,
                     "HomeIndeed: Cached %d songs from LRCLIB for offline use",
                     (int)results.size());
            }
        } else {
            blog(LOG_ERROR, "HomeIndeed: LRCLIB query failed: %s",
                 reply->errorString().toStdString().c_str());
        }

        reply->deleteLater();
        callback(results);
    });
}
