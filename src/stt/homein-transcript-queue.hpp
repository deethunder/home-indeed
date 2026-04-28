#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

/**
 * @class TranscriptQueue
 * @brief Thread-safe queue for passing transcriptions from STT to Detection.
 */
class TranscriptQueue {
public:
    void Push(const std::string& text) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(text);
        cv.notify_one();
    }

    bool Pop(std::string& out_text, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mtx);
        if (cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue.empty(); })) {
            out_text = queue.front();
            queue.pop();
            return true;
        }
        return false;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        while (!queue.empty()) queue.pop();
    }

private:
    std::queue<std::string> queue;
    std::mutex mtx;
    std::condition_variable cv;
};
