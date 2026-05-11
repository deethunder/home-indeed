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

#include "homein-context.hpp"
#include <memory>

/**
 * @brief Logic for identifying Bible references within raw text.
 * Implements a 3-layer detection strategy: Regex -> Contextual -> Fuzzy.
 */
class HomeInRefParser {
public:
    HomeInRefParser();
    ~HomeInRefParser();

    std::vector<BibleRef> Parse(const std::string& text);
    void SetContext(std::shared_ptr<SermonContext> ctx) { context = ctx; }

private:
    std::regex standard_ref_regex;
    std::regex verse_only_regex;
    std::shared_ptr<SermonContext> context;
};
