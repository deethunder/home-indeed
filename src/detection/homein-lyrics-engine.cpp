#include "homein-lyrics-engine.hpp"
#include <obs-module.h>
#include <thread>

HomeInLyricsEngine::HomeInLyricsEngine() {}

HomeInLyricsEngine::~HomeInLyricsEngine() {}

bool HomeInLyricsEngine::Initialize(const std::string& db_path) {
    return local_db.Open(db_path);
}

void HomeInLyricsEngine::Search(const std::string& query, bool allow_web, SearchCallback callback) {
    // 1. Search Local DB First
    auto local_results = local_db.Search(query);
    
    if (!local_results.empty()) {
        callback(local_results);
        return;
    }

    // 2. Search Web if allowed and no local results
    if (allow_web) {
        std::thread([this, query, callback]() {
            FetchFromLRCLIB(query, callback);
        }).detach();
    } else {
        callback({});
    }
}

void HomeInLyricsEngine::FetchFromLRCLIB(const std::string& query, SearchCallback callback) {
    // In a final production build, we would use libcurl here. 
    // For the current implementation step, we'll implement a clean skeleton 
    // that uses a placeholder result to demonstrate the UI-Engine connection.
    
    blog(LOG_INFO, "Simulating LRCLIB search for: %s", query.c_str());
    
    // Simulating a network delay
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    
    std::vector<SongLyric> results;
    // Example result for testing
    if (query.find("Amazing Grace") != std::string::npos) {
        SongLyric s;
        s.id = 0;
        s.title = "Amazing Grace";
        s.artist = "Chris Tomlin Version";
        s.content = "Amazing grace how sweet the sound\nThat saved a wretch like me\nI once was lost but now am found\nWas blind but now I see";
        s.source = "LRCLIB";
        results.push_back(s);
        
        // Auto-cache to local DB
        local_db.AddSong(s.title, s.artist, s.content, s.source);
    }
    
    callback(results);
}
