#pragma once

#include <string>
#include <vector>
#include <functional>
#include <QObject>
#include <QNetworkAccessManager>
// FIX #16: Corrected relative include path (was "database/..." from detection/ dir)
#include "../database/homein-lyrics-db.hpp"

/**
 * @brief Orchestrates lyrics lookup and caching.
 * Links local search with the LRCLIB API.
 */
class HomeInLyricsEngine : public QObject {
    Q_OBJECT

public:
    HomeInLyricsEngine();
    ~HomeInLyricsEngine();

    using SearchCallback = std::function<void(const std::vector<SongLyric>& results)>;

    bool Initialize(const std::string& db_path);
    HomeInLyricsDB& GetDB() { return local_db; }

    // FIX #8: Exposes the DB open state so callers can guard imports.
    bool IsDBOpen() const { return local_db.IsOpen(); }

    /**
     * @brief Searches locally first, then hits LRCLIB if allowed.
     * Fully Qt-thread-safe — no std::thread used.
     */
    void Search(const std::string& query, bool allow_web, SearchCallback callback);
    void GetLocalLibrary(SearchCallback callback);

private:
    // Scrapes Genius HTML first, falls back to LRCLIB if not found
    void FetchFromWeb(const std::string& query, SearchCallback callback);

    HomeInLyricsDB local_db;
    QNetworkAccessManager network_manager;
};
