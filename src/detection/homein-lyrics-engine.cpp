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
        QUrl url("https://lrclib.net/api/search");
        QUrlQuery q;
        q.addQueryItem("q", QString::fromStdString(query));
        url.setQuery(q);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "HomeIndeed/1.0");

        QNetworkReply* reply = network_manager.get(request);
        // Use a lambda connection — fires on Qt main thread automatically
        connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
            std::vector<SongLyric> results;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonArray arr = doc.array();
                for (int i = 0; i < std::min((int)arr.size(), 5); ++i) {
                    QJsonObject obj = arr[i].toObject();
                    SongLyric s;
                    s.title = obj["name"].toString().toStdString();
                    s.artist = obj["artistName"].toString().toStdString();
                    s.content = obj.contains("plainLyrics")
                        ? obj["plainLyrics"].toString().toStdString()
                        : obj["syncedLyrics"].toString().toStdString();
                    s.source = "LRCLIB";
                    if (!s.content.empty()) {
                        results.push_back(s);
                        local_db.AddSong(s.title, s.artist, s.content, s.source);
                    }
                }
                if (!results.empty()) local_db.RebuildFTS();
            } else {
                blog(LOG_ERROR, "LRCLIB failed: %s", reply->errorString().toStdString().c_str());
            }
            reply->deleteLater();
            callback(results);
        });
    } else {
        callback({});
    }
}
