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
    // FIX #6: Removed FetchFromLRCLIB(). The old implementation called
    // QNetworkAccessManager::get() from a detached std::thread, which is
    // undefined behaviour (QNAM is not thread-safe). It also used QEventLoop
    // inside that worker thread which deadlocks because there is no Qt event
    // loop running there. Replaced with a proper signal/slot async pattern
    // that stays on the Qt main thread throughout.
    void FetchFromLRCLIB(const std::string& query, SearchCallback callback);

    HomeInLyricsDB local_db;
    QNetworkAccessManager network_manager;
};
