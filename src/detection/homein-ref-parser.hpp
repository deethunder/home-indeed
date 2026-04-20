#pragma once

#include <string>
#include <vector>
#include <regex>

/**
 * @brief Represents a detected reference to a Bible passage.
 */
struct BibleRef {
    std::string book;
    int chapter = 0;
    int verse_start = 0;
    int verse_end = 0;
    float confidence = 0.0f;
    std::string original_text;
};

/**
 * @brief Logic for identifying Bible references within raw text.
 */
class HomeInRefParser {
public:
    HomeInRefParser();
    ~HomeInRefParser();

    /**
     * @brief Parses a string and returns all detected Bible references.
     * Uses context tracking to infer missing books/chapters in conversational speech.
     */
    std::vector<BibleRef> Parse(const std::string& text);

    void ClearContext() { last_book = ""; last_chapter = 0; }

private:
    // Regex patterns for various styles: "John 3:16", "1 John 1:9", "Genesis 1:1-5"
    std::regex standard_ref_regex;
    std::regex conversational_verse_regex;

    // Intelligent Context Tracking
    std::string last_book;
    int last_chapter = 0;
};
