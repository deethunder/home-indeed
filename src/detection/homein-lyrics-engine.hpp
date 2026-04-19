#pragma once

#include <string>
#include <vector>
#include <functional>
#include "homein-lyrics-db.hpp"

/**
 * @brief Orchestrates lyrics lookup and caching.
 * Links local search with the LRCLIB API.
 */
class HomeInLyricsEngine {
public:
    using SearchCallback = std::function<void(const std::vector<SongLyric>& results)>;

    HomeInLyricsEngine();
    ~HomeInLyricsEngine();

    bool Initialize(const std::string& db_path);

    /**
     * @brief Searches for lyrics. First checks local DB, then hits web API if allowed.
     */
    void Search(const std::string& query, bool allow_web, SearchCallback callback);

private:
    void FetchFromLRCLIB(const std::string& query, SearchCallback callback);

    HomeInLyricsDB local_db;
};
