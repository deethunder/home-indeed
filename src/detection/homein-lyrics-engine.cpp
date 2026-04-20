#include "homein-lyrics-engine.hpp"
#include <thread>
#include <obs-module.h>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QEventLoop>

HomeInLyricsEngine::HomeInLyricsEngine() : QObject(nullptr) {}

HomeInLyricsEngine::~HomeInLyricsEngine() {}

bool HomeInLyricsEngine::Initialize(const std::string& db_path) {
    return local_db.Open(db_path);
}

void HomeInLyricsEngine::Search(const std::string& query, bool allow_web, SearchCallback callback) {
    auto local_results = local_db.Search(query);
    if (!local_results.empty()) {
        callback(local_results);
        return;
    }

    if (allow_web) {
        // Run network query on a separate thread to avoid blocking STT loop
        std::thread([this, query, callback]() {
            FetchFromLRCLIB(query, callback);
        }).detach();
    } else {
        callback({});
    }
}

void HomeInLyricsEngine::FetchFromLRCLIB(const std::string& query, SearchCallback callback) {
    QUrl url("https://lrclib.net/api/search");
    QUrlQuery q;
    q.addQueryItem("q", QString::fromStdString(query));
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "HomeIndeed/1.0 (OBS Plugin)");

    // Since we are in a worker thread, we use a local event loop to wait for response
    QEventLoop loop;
    QNetworkReply* reply = network_manager.get(request);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    std::vector<SongLyric> results;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray arr = doc.array();

        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject obj = arr[i].toObject();
            SongLyric s;
            s.id = 0;
            s.title = obj["name"].toString().toStdString();
            s.artist = obj["artistName"].toString().toStdString();
            
            // Prefer plain lyrics, fallback to synced
            if (obj.contains("plainLyrics")) {
                s.content = obj["plainLyrics"].toString().toStdString();
            } else if (obj.contains("syncedLyrics")) {
                s.content = obj["syncedLyrics"].toString().toStdString();
            }
            
            s.source = "LRCLIB";
            if (!s.content.empty()) {
                results.push_back(s);
                local_db.AddSong(s.title, s.artist, s.content, s.source);
            }
            if (i >= 5) break; // Limit to top 5 online results
        }
    } else {
        blog(LOG_ERROR, "LRCLIB query failed: %s", reply->errorString().toStdString().c_str());
    }

    reply->deleteLater();
    callback(results);
}
