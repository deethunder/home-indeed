#include "homein-lyrics-engine.hpp"
#include <obs-module.h>
#include "network/homein-http.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <regex>

static void DeduplicateResults(std::vector<SongLyric>& results);

HomeInLyricsEngine::HomeInLyricsEngine() : QObject(nullptr) {}
HomeInLyricsEngine::~HomeInLyricsEngine() {}

bool HomeInLyricsEngine::Initialize(const std::string& db_path) {
    return local_db.Open(db_path);
}

void HomeInLyricsEngine::Search(const std::string& query, bool allow_web, SearchCallback callback) {
    auto local = local_db.Search(query);
    if (!local.empty()) {
        DeduplicateResults(local); // Scub any duplicates already trapped in the database!
        callback(local);
        return;
    }
    if (allow_web) {
        FetchFromWeb(query, callback);
    } else {
        callback({});
    }
}

// Custom C++ HTML Stripper
static std::string StripHTML(const std::string& html) {
    std::string out = html;
    out = std::regex_replace(out, std::regex("<br\\s*/?>", std::regex_constants::icase), "\n");
    out = std::regex_replace(out, std::regex("<[^>]*>"), "");
    out = std::regex_replace(out, std::regex("&#x27;"), "'");
    out = std::regex_replace(out, std::regex("&quot;"), "\"");
    out = std::regex_replace(out, std::regex("&amp;"), "&");
    return out;
}

// --- NEW: Bulletproof Case-Insensitive Deduplication ---
static std::string CleanString(const std::string& str) {
    std::string out;
    for (char c : str) {
        if (std::isalnum(c)) out += std::tolower(c);
    }
    return out;
}

static void DeduplicateResults(std::vector<SongLyric>& results) {
    std::sort(results.begin(), results.end(), [](const SongLyric& a, const SongLyric& b) {
        std::string ta = CleanString(a.title);
        std::string tb = CleanString(b.title);
        if (ta != tb) return ta < tb;
        return CleanString(a.artist) < CleanString(b.artist);
    });

    auto it = std::unique(results.begin(), results.end(),
        [](const SongLyric& a, const SongLyric& b) {
            return CleanString(a.title) == CleanString(b.title) && 
                   CleanString(a.artist) == CleanString(b.artist);
        });
    results.erase(it, results.end());
}
// --------------------------------------------------------

void HomeInLyricsEngine::FetchFromWeb(const std::string& query, SearchCallback callback) {
    blog(LOG_INFO, "HomeIndeed: Scraping Web for: %s", query.c_str());
    
    QtConcurrent::run([this, query, callback]() {
        std::vector<SongLyric> results;
        std::string safe_query = query;
        std::replace(safe_query.begin(), safe_query.end(), ' ', '+');

        // --- PHASE 1: GENIUS SCRAPER ---
        std::string genius_api = "https://genius.com/api/search/multi?per_page=3&q=" + safe_query;
        std::string response = HomeIn::HttpClient::Get(genius_api);
        
        if (!response.empty()) {
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(response));
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray sections = doc.object()["response"].toObject()["sections"].toArray();
                if (!sections.isEmpty()) {
                    QJsonArray hits = sections[0].toObject()["hits"].toArray();
                    
                    for (int i = 0; i < hits.size() && i < 3; ++i) { // Grab top 3 matches
                        QJsonObject resultObj = hits[i].toObject()["result"].toObject();
                        std::string title = resultObj["title"].toString().toStdString();
                        std::string artist = resultObj["primary_artist"].toObject()["name"].toString().toStdString();
                        std::string song_url = resultObj["url"].toString().toStdString();

                        // Rip the actual webpage HTML
                        std::string page_html = HomeIn::HttpClient::Get(song_url);
                        if (!page_html.empty()) {
                            // Target the Genius React CSS containers
                            std::regex lyrics_regex(R"(<div data-lyrics-container="true"[^>]*>(.*?)</div>)");
                            auto words_begin = std::sregex_iterator(page_html.begin(), page_html.end(), lyrics_regex);
                            auto words_end = std::sregex_iterator();

                            std::string full_lyrics;
                            for (std::sregex_iterator it = words_begin; it != words_end; ++it) {
                                std::smatch match = *it;
                                full_lyrics += StripHTML(match[1].str()) + "\n\n";
                            }

                            if (!full_lyrics.empty()) {
                                // Clean up trailing space
                                full_lyrics.erase(full_lyrics.find_last_not_of(" \n\r\t") + 1);

                                SongLyric s;
                                s.title = title;
                                s.artist = artist;
                                s.content = full_lyrics;
                                s.source = "Genius";
                                results.push_back(s);

                                // Save to local SQLite immediately for instant future loading
                                if (IsDBOpen()) local_db.AddSong(s.title, s.artist, s.content, s.source);
                            }
                        }
                    }
                }
            }
        }

        // --- PHASE 2: LRCLIB FALLBACK ---
        // If Genius failed or found nothing, fallback to the LRCLIB API
        if (results.empty()) {
            blog(LOG_INFO, "HomeIndeed: Genius returned empty. Falling back to LRCLIB.");
            std::string lrclib_url = "https://lrclib.net/api/search?q=" + safe_query;
            std::string lrc_resp = HomeIn::HttpClient::Get(lrclib_url);
            
            if (!lrc_resp.empty()) {
                QJsonDocument lrc_doc = QJsonDocument::fromJson(QByteArray::fromStdString(lrc_resp));
                if (!lrc_doc.isNull() && lrc_doc.isArray()) {
                    QJsonArray arr = lrc_doc.array();
                    for (int i = 0; i < arr.size(); ++i) {
                        QJsonObject obj = arr[i].toObject();
                        QString plainLyrics = obj["plainLyrics"].toString();
                        if (!plainLyrics.trimmed().isEmpty()) {
                            SongLyric s;
                            s.title = obj["trackName"].toString().toStdString();
                            s.artist = obj["artistName"].toString().toStdString();
                            s.content = plainLyrics.toStdString();
                            s.source = "LRCLIB";
                            results.push_back(s);
                            
                            if (IsDBOpen()) local_db.AddSong(s.title, s.artist, s.content, s.source);
                        }
                    }
                }
            }
        }
// --- PHASE 3: DEDUPLICATION & RETURN ---
        DeduplicateResults(results);

        QMetaObject::invokeMethod(this, [callback, results]() {
            callback(results);
        }, Qt::QueuedConnection);
    });
}