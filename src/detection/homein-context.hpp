#pragma once
#include <string>
#include <mutex>
#include <chrono>

/**
 * @class SermonContext
 * @brief Remembers the last spoken Bible book and chapter to resolve implied references.
 */
class SermonContext {
public:
    void Update(const std::string& book, int chapter) {
        std::lock_guard<std::mutex> lock(mtx);
        last_book = book;
        last_chapter = chapter;
        last_update = std::chrono::steady_clock::now();
    }

    bool GetCurrent(std::string& out_book, int& out_chapter) {
        std::lock_guard<std::mutex> lock(mtx);
        
        // Context expires after 10 minutes of silence
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::minutes>(now - last_update).count();
        
        if (last_book.empty() || diff > 10) return false;
        
        out_book = last_book;
        out_chapter = last_chapter;
        return true;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        last_book.clear();
        last_chapter = 0;
    }

private:
    std::string last_book;
    int last_chapter = 0;
    std::chrono::steady_clock::time_point last_update;
    std::mutex mtx;
};
