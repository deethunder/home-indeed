#pragma once

#include <string>
#include <vector>
#include <functional>
#include <QObject>
#include <QNetworkAccessManager>
#include "../database/homein-lyrics-db.hpp"

/**
 * @brief Orchestrates lyrics lookup and caching.
 * Links local search with the LRCLIB API.
 */
class HomeInLyricsEngine : public QObject {
    Q_OBJECT

public:
    HomeInLyricsEngine();
    using SearchCallback = std::function<void(const std::vector<SongLyric>& results)>;

    ~HomeInLyricsEngine();

    bool Initialize(const std::string& db_path);
    HomeInLyricsDB& GetDB() { return local_db; }
    bool IsDBOpen() const { return local_db.IsOpen(); }

    /**
     * @brief Searches for lyrics. First checks local DB, then hits web API if allowed.
     */
    void Search(const std::string& query, bool allow_web, SearchCallback callback);

private:

    HomeInLyricsDB local_db;
    QNetworkAccessManager network_manager;
};
